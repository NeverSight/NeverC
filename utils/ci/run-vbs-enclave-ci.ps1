[CmdletBinding()]
param(
  [ValidateSet('Certificate', 'Static', 'Runtime')]
  [string]$Phase = 'Static',
  [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
  [string]$BuildDirectory = 'build-vbs',
  [string]$ArtifactDirectory = 'artifacts\vbs-enclave',
  [string]$NeverCPath = '',
  [switch]$RequireRuntime
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Resolve-FullPath {
  param([Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$BasePath)
  if ([IO.Path]::IsPathRooted($PathValue)) {
    return [IO.Path]::GetFullPath($PathValue)
  }
  return [IO.Path]::GetFullPath((Join-Path $BasePath $PathValue))
}

function Require-File {
  param([Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$Description)
  if (-not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
    throw "$Description was not found at '$PathValue'"
  }
  return (Resolve-Path -LiteralPath $PathValue).Path
}

function Invoke-Logged {
  param([Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [switch]$AllowFailure)
  $rendered = @($FilePath) + $Arguments
  $commandLine = "COMMAND: $($rendered -join ' ')"
  $commandLine | Set-Content -LiteralPath $LogPath -Encoding utf8
  Write-Host $commandLine
  $output = & $FilePath @Arguments 2>&1
  $exitCode = $LASTEXITCODE
  $output | Tee-Object -FilePath $LogPath -Append | Out-Host
  if ($exitCode -ne 0 -and -not $AllowFailure) {
    throw "command failed with exit code $exitCode; see '$LogPath'"
  }
  return [int]$exitCode
}

function Invoke-TimedLogged {
  param([Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][ValidateRange(1, 3600)]
        [int]$TimeoutSeconds,
        [switch]$AllowFailure)
  $rendered = @($FilePath) + $Arguments
  $commandLine = "COMMAND: $($rendered -join ' ')"
  $commandLine | Set-Content -LiteralPath $LogPath -Encoding utf8
  Write-Host $commandLine

  $startInfo = [Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $FilePath
  $startInfo.UseShellExecute = $false
  $startInfo.CreateNoWindow = $true
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  foreach ($argument in $Arguments) {
    [void]$startInfo.ArgumentList.Add($argument)
  }

  $process = [Diagnostics.Process]::new()
  $process.StartInfo = $startInfo
  $timedOut = $false
  try {
    if (-not $process.Start()) {
      throw "failed to start '$FilePath'"
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) {
      try {
        $process.Kill($true)
      } catch {
        Write-Host "failed to kill timed-out process $($process.Id): $($_.Exception.Message)"
      }
      [void]$process.WaitForExit(5000)
    } else {
      $process.WaitForExit()
    }

    if ($process.HasExited) {
      $stdout = $stdoutTask.GetAwaiter().GetResult()
      $stderr = $stderrTask.GetAwaiter().GetResult()
      if ($stdout) {
        $stdout | Tee-Object -FilePath $LogPath -Append | Out-Host
      }
      if ($stderr) {
        $stderr | Tee-Object -FilePath $LogPath -Append | Out-Host
      }
    }

    if ($timedOut) {
      $timeoutLine = "PROCESS_TIMEOUT_SECONDS=$TimeoutSeconds"
      $timeoutLine | Tee-Object -FilePath $LogPath -Append | Out-Host
      if (-not $AllowFailure) {
        throw "command timed out after $TimeoutSeconds seconds; see '$LogPath'"
      }
      return 124
    }
    $exitCode = $process.ExitCode
  } finally {
    $process.Dispose()
  }

  if ($exitCode -ne 0 -and -not $AllowFailure) {
    throw "command failed with exit code $exitCode; see '$LogPath'"
  }
  return [int]$exitCode
}

function New-EphemeralVbsCertificate {
  param([Parameter(Mandatory = $true)][string]$HelperPath,
        [Parameter(Mandatory = $true)][string]$CertificatePath,
        [Parameter(Mandatory = $true)][string]$ThumbprintPath,
        [Parameter(Mandatory = $true)][string]$StageLogPath,
        [Parameter(Mandatory = $true)][string]$ProcessLogPath,
        [Parameter(Mandatory = $true)][string]$TrustLogPath)
  $pwsh = (Get-Command pwsh.exe -ErrorAction Stop).Source
  Invoke-TimedLogged $pwsh @(
    '-NoLogo', '-NoProfile', '-NonInteractive', '-File', $HelperPath,
    '-CertificatePath', $CertificatePath,
    '-ThumbprintPath', $ThumbprintPath,
    '-StageLogPath', $StageLogPath
  ) $ProcessLogPath -TimeoutSeconds 90 | Out-Null

  $thumbprint = (Get-Content -LiteralPath $ThumbprintPath -Raw).Trim()
  if ($thumbprint -notmatch '^[0-9A-Fa-f]{40}$') {
    throw "certificate helper returned an invalid thumbprint '$thumbprint'"
  }
  $certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$thumbprint" `
    -ErrorAction Stop
  if (-not $certificate.HasPrivateKey) {
    throw 'certificate helper installed a certificate without its private key'
  }
  # Root-store installation can trigger an OS security prompt even when
  # certutil is forced. The hosted runner is elevated and ephemeral, so trust
  # the self-signed test leaf in the machine-wide TrustedPeople store instead.
  'CERTIFICATE_STAGE=InstallTrustedPeople' |
    Tee-Object -FilePath $StageLogPath -Append | Out-Host
  $certutil = (Get-Command certutil.exe -ErrorAction Stop).Source
  Invoke-TimedLogged $certutil @(
    '-f', '-addstore', 'TrustedPeople', $CertificatePath
  ) $TrustLogPath -TimeoutSeconds 60 | Out-Null
  $trustedCertificate = Get-Item -LiteralPath `
    "Cert:\LocalMachine\TrustedPeople\$thumbprint" -ErrorAction Stop
  if ($trustedCertificate.Thumbprint -ne $thumbprint) {
    throw 'trusted-people certificate does not match the signing certificate'
  }
  'CERTIFICATE_STAGE=Complete' |
    Tee-Object -FilePath $StageLogPath -Append | Out-Host
  return $certificate
}

function Resolve-Toolchain {
  param([switch]$IncludeArm64)

  if (-not $env:VCToolsInstallDir) {
    throw 'VCToolsInstallDir is unset; run from an MSVC developer environment'
  }
  if (-not $env:WindowsSdkDir -or -not $env:WindowsSDKVersion) {
    throw 'WindowsSdkDir/WindowsSDKVersion are unset; run from an MSVC developer environment'
  }

  $vcRoot = $env:VCToolsInstallDir.TrimEnd('\', '/')
  $sdkRoot = $env:WindowsSdkDir.TrimEnd('\', '/')
  $sdkVersion = $env:WindowsSDKVersion.TrimEnd('\', '/')
  $vcBin = Join-Path $vcRoot 'bin\Hostx64\x64'
  $vcBinArm64 = Join-Path $vcRoot 'bin\Hostx64\arm64'
  $sdkLib = Join-Path $sdkRoot "Lib\$sdkVersion"
  $sdkBin = Join-Path $sdkRoot "Bin\$sdkVersion\x64"

  $paths = [ordered]@{
    cl = Require-File (Join-Path $vcBin 'cl.exe') 'MSVC compiler'
    link = Require-File (Join-Path $vcBin 'link.exe') 'MSVC linker'
    dumpbin = Require-File (Join-Path $vcBin 'dumpbin.exe') 'MSVC dumpbin'
    onecore = Require-File (Join-Path $sdkLib 'um\x64\onecore.lib') 'onecore.lib'
    veiid = Require-File (Join-Path $sdkBin 'veiid.exe') 'VEIID'
    signtool = Require-File (Join-Path $sdkBin 'signtool.exe') 'SignTool'
  }
  if ($IncludeArm64) {
    $paths['link_arm64'] = Require-File (Join-Path $vcBinArm64 'link.exe') 'MSVC ARM64 linker'
  }
  return $paths
}

function Resolve-BundledRuntime {
  param([Parameter(Mandatory = $true)][string]$NeverCCompiler)

  $installRoot = Split-Path (Split-Path $NeverCCompiler -Parent) -Parent
  $paths = [ordered]@{}
  foreach ($architecture in @('x64', 'arm64')) {
    $runtimeRoot = Join-Path $installRoot "runtime\windows\$architecture\msvc"
    $paths["${architecture}_enclave_libcmt"] = Require-File (Join-Path $runtimeRoot 'crt\lib\enclave\libcmt.lib') "bundled $architecture enclave libcmt.lib"
    $paths["${architecture}_enclave_libvcruntime"] = Require-File (Join-Path $runtimeRoot 'crt\lib\enclave\libvcruntime.lib') "bundled $architecture enclave libvcruntime.lib"
    $paths["${architecture}_enclave_ucrt"] = Require-File (Join-Path $runtimeRoot 'sdk\lib\ucrt_enclave\ucrt.lib') "bundled $architecture enclave ucrt.lib"
    $paths["${architecture}_vertdll"] = Require-File (Join-Path $runtimeRoot 'sdk\lib\um\vertdll.lib') "bundled $architecture vertdll.lib"
    $paths["${architecture}_bcrypt"] = Require-File (Join-Path $runtimeRoot 'sdk\lib\um\bcrypt.lib') "bundled $architecture bcrypt.lib"
  }
  return $paths
}

function ConvertTo-TraceComparableText {
  param([Parameter(Mandatory = $true)][string]$Text)

  $normalized = $Text -replace '[\\/]+', '/'
  return $normalized.ToLowerInvariant()
}

function Assert-BundledRuntimeTrace {
  param([Parameter(Mandatory = $true)][string]$LogPath,
        [Parameter(Mandatory = $true)][string]$Architecture,
        [Parameter(Mandatory = $true)]$BundledRuntime)

  $trace = ConvertTo-TraceComparableText (Get-Content -LiteralPath $LogPath -Raw)
  foreach ($assetName in @('enclave_libcmt', 'enclave_ucrt', 'vertdll')) {
    $asset = $BundledRuntime["${Architecture}_${assetName}"]
    $directory = ConvertTo-TraceComparableText (Split-Path $asset -Parent)
    if (-not $trace.Contains($directory)) {
      throw "NeverC trace did not select bundled $Architecture runtime directory '$directory'"
    }
  }
  foreach ($hostRoot in @($env:VCToolsInstallDir, $env:WindowsSdkDir)) {
    if ($hostRoot) {
      $normalizedHostRoot = ConvertTo-TraceComparableText ($hostRoot.TrimEnd('\', '/'))
      if ($trace.Contains($normalizedHostRoot)) {
        throw "NeverC trace leaked host runtime path '$normalizedHostRoot' into the default VBS link"
      }
    }
  }
}

function Write-Result {
  param([string]$Status, [string]$Stage, [int]$ErrorCode,
        [string]$Message, [string]$ResultPath)
  $result = [ordered]@{
    status = $Status
    stage = $Stage
    error = $ErrorCode
    message = $Message
    require_runtime = [bool]$RequireRuntime
  }
  $result | ConvertTo-Json | Set-Content -LiteralPath $ResultPath -Encoding utf8
  $summary = "### VBS enclave runtime: $Status`n`n- Stage: ``$Stage```n- Error: ``$ErrorCode```n- $Message`n"
  if ($env:GITHUB_STEP_SUMMARY) {
    $summary | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding utf8
  }
  Write-Host $summary
}

$repository = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$artifactRoot = Resolve-FullPath $ArtifactDirectory $repository
$logRoot = Join-Path $artifactRoot 'logs'
New-Item -ItemType Directory -Force -Path $artifactRoot, $logRoot | Out-Null
$fixtureRoot = Join-Path $repository 'tests\neverc\Inputs\VBSEnclave'
$verifier = Require-File (Join-Path $repository 'utils\ci\verify-vbs-enclave-pe.py') 'PE verifier'
$certificateHelper = Require-File `
  (Join-Path $repository 'utils\ci\new-vbs-enclave-test-certificate.ps1') `
  'VBS enclave test-certificate helper'

if ($Phase -eq 'Certificate') {
  $tools = Resolve-Toolchain
  $certificatePath = Join-Path $artifactRoot 'ephemeral-vbs-enclave-ci.cer'
  $thumbprintPath = Join-Path $artifactRoot 'certificate-thumbprint.txt'
  $certificate = New-EphemeralVbsCertificate `
    -HelperPath $certificateHelper `
    -CertificatePath $certificatePath `
    -ThumbprintPath $thumbprintPath `
    -StageLogPath (Join-Path $logRoot 'certificate-stages.log') `
    -ProcessLogPath (Join-Path $logRoot 'certificate-bootstrap.log') `
    -TrustLogPath (Join-Path $logRoot 'certificate-trust.log')

  $smokeImage = Join-Path $artifactRoot 'certificate-smoke.exe'
  Invoke-TimedLogged $tools.cl @(
    '/nologo', '/std:c++17', (Join-Path $fixtureRoot 'host.cpp'),
    "/Fe$smokeImage", '/link', '/INCREMENTAL:NO', $tools.onecore
  ) (Join-Path $logRoot 'certificate-build-host.log') -TimeoutSeconds 120 | Out-Null
  Invoke-TimedLogged $tools.signtool @(
    'sign', '/ph', '/fd', 'SHA256', '/sha1', $certificate.Thumbprint,
    $smokeImage
  ) (Join-Path $logRoot 'certificate-sign.log') -TimeoutSeconds 60 | Out-Null
  Invoke-TimedLogged $tools.signtool @(
    'verify', '/pa', '/v', $smokeImage
  ) (Join-Path $logRoot 'certificate-verify.log') -TimeoutSeconds 60 | Out-Null
  Write-Host 'CERTIFICATE PASS: non-interactive creation, trust, signing, and verification succeeded'
  exit 0
}

if ($Phase -eq 'Static') {
  $python = (Get-Command python.exe -ErrorAction Stop).Source
  Invoke-Logged $python @($verifier, 'self-test') (Join-Path $logRoot 'verifier-self-test.log') | Out-Null
  $tools = Resolve-Toolchain -IncludeArm64
  $tools.GetEnumerator() | ForEach-Object { Write-Host ("{0}: {1}" -f $_.Key, $_.Value) }
  $tools | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifactRoot 'tool-paths.json') -Encoding utf8
  $tools.GetEnumerator() | ForEach-Object {
    $item = Get-Item -LiteralPath $_.Value
    "{0}`t{1}`t{2}" -f $_.Key, $_.Value, $item.VersionInfo.FileVersion
  } | Set-Content -LiteralPath (Join-Path $artifactRoot 'tool-versions.txt') -Encoding utf8

  if (-not $NeverCPath) {
    $NeverCPath = Join-Path (Resolve-FullPath $BuildDirectory $repository) 'bin\neverc.exe'
  } else {
    $NeverCPath = Resolve-FullPath $NeverCPath $repository
  }
  $neverc = Require-File $NeverCPath 'NeverC compiler'
  $bundledRuntime = Resolve-BundledRuntime $neverc
  $bundledRuntime | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifactRoot 'bundled-runtime-paths.json') -Encoding utf8
  $bundledRuntime.GetEnumerator() | ForEach-Object {
    $item = Get-Item -LiteralPath $_.Value
    $hash = (Get-FileHash -LiteralPath $_.Value -Algorithm SHA256).Hash
    "{0}`t{1}`t{2}`t{3}" -f $_.Key, $_.Value, $item.Length, $hash
  } | Set-Content -LiteralPath (Join-Path $artifactRoot 'bundled-runtime-assets.txt') -Encoding utf8

  $objectRoot = Join-Path $artifactRoot 'objects'
  $msvcObjectRoot = Join-Path $objectRoot 'msvc'
  $nevercObjectRoot = Join-Path $objectRoot 'neverc'
  $nevercArm64ObjectRoot = Join-Path $objectRoot 'neverc-arm64'
  $unsignedRoot = Join-Path $artifactRoot 'unsigned'
  $runtimeRoot = Join-Path $artifactRoot 'runtime'
  $jsonRoot = Join-Path $artifactRoot 'json'
  $mapRoot = Join-Path $artifactRoot 'maps'
  New-Item -ItemType Directory -Force -Path $msvcObjectRoot, $nevercObjectRoot, $nevercArm64ObjectRoot, $unsignedRoot, $runtimeRoot, $jsonRoot, $mapRoot | Out-Null
  $mapPaths = [ordered]@{
    'msvc-msvc.dll' = Join-Path $mapRoot 'msvc-msvc.map'
    'neverc-msvc.dll' = Join-Path $mapRoot 'neverc-msvc.map'
    'neverc-neverc.dll' = Join-Path $mapRoot 'neverc-neverc.map'
    'neverc-msvc-arm64.dll' = Join-Path $mapRoot 'neverc-msvc-arm64.map'
    'neverc-neverc-arm64.dll' = Join-Path $mapRoot 'neverc-neverc-arm64.map'
  }

  $guardedSources = @('enclave.cpp', 'guarded.cpp')
  foreach ($sourceName in $guardedSources) {
    $source = Join-Path $fixtureRoot $sourceName
    $object = Join-Path $msvcObjectRoot ($sourceName + '.obj')
    Invoke-Logged $tools.cl @('/nologo', '/c', '/std:c++17', '/guard:cf', '/GS-', "/Fo$object", $source) (Join-Path $logRoot "cl-$sourceName.log") | Out-Null
    $nevercObject = Join-Path $nevercObjectRoot ($sourceName + '.obj')
    Invoke-Logged $neverc @('--target=x86_64-pc-windows-msvc', '-x', 'c', '-fno-lto', '-fno-builtin-mimalloc', '-c', '-fms-guard=cf', $source, '-o', $nevercObject) (Join-Path $logRoot "neverc-$sourceName.log") | Out-Null
    $nevercArm64Object = Join-Path $nevercArm64ObjectRoot ($sourceName + '.obj')
    Invoke-Logged $neverc @('--target=aarch64-pc-windows-msvc', '-x', 'c', '-fno-lto', '-fno-builtin-mimalloc', '-c', '-fms-guard=cf', $source, '-o', $nevercArm64Object) (Join-Path $logRoot "neverc-arm64-$sourceName.log") | Out-Null
  }
  $legacySource = Join-Path $fixtureRoot 'legacy.cpp'
  $legacyMsvc = Join-Path $msvcObjectRoot 'legacy.cpp.obj'
  Invoke-Logged $tools.cl @('/nologo', '/c', '/std:c++17', '/GS-', "/Fo$legacyMsvc", $legacySource) (Join-Path $logRoot 'cl-legacy.cpp.log') | Out-Null
  $legacyNeverC = Join-Path $nevercObjectRoot 'legacy.cpp.obj'
  Invoke-Logged $neverc @('--target=x86_64-pc-windows-msvc', '-x', 'c', '-fno-lto', '-fno-builtin-mimalloc', '-c', $legacySource, '-o', $legacyNeverC) (Join-Path $logRoot 'neverc-legacy.cpp.log') | Out-Null
  $legacyNeverCArm64 = Join-Path $nevercArm64ObjectRoot 'legacy.cpp.obj'
  Invoke-Logged $neverc @('--target=aarch64-pc-windows-msvc', '-x', 'c', '-fno-lto', '-fno-builtin-mimalloc', '-c', $legacySource, '-o', $legacyNeverCArm64) (Join-Path $logRoot 'neverc-arm64-legacy.cpp.log') | Out-Null

  $runtimeHost = Join-Path $artifactRoot 'vbs-enclave-host.exe'
  Invoke-Logged $tools.cl @('/nologo', '/std:c++17', (Join-Path $fixtureRoot 'host.cpp'), "/Fe$runtimeHost", '/link', '/INCREMENTAL:NO', $tools.onecore) (Join-Path $logRoot 'build-host.log') | Out-Null

  # Keep the Microsoft-link and integrated-link comparisons on the exact same
  # runtime bits. Host toolchain paths are still recorded above for diagnosis,
  # but must not change the reference image's object selection or CFG table.
  $x64Libraries = @(
    $bundledRuntime.x64_vertdll,
    $bundledRuntime.x64_bcrypt,
    $bundledRuntime.x64_enclave_libcmt,
    $bundledRuntime.x64_enclave_libvcruntime,
    $bundledRuntime.x64_enclave_ucrt
  )
  $msLinkFlags = @('/NOLOGO', '/DLL', '/INCREMENTAL:NO', '/NODEFAULTLIB',
                   '/ENCLAVE', '/INTEGRITYCHECK', '/GUARD:MIXED',
                   '/DYNAMICBASE', '/MACHINE:X64', '/WX:4229')
  $outputs = [ordered]@{
    'msvc-msvc.dll' = @(
      (Join-Path $msvcObjectRoot 'enclave.cpp.obj'),
      (Join-Path $msvcObjectRoot 'guarded.cpp.obj'), $legacyMsvc)
    'neverc-msvc.dll' = @(
      (Join-Path $nevercObjectRoot 'enclave.cpp.obj'),
      (Join-Path $nevercObjectRoot 'guarded.cpp.obj'), $legacyNeverC)
  }
  foreach ($entry in $outputs.GetEnumerator()) {
    $output = Join-Path $unsignedRoot $entry.Key
    $arguments = @($msLinkFlags) + @(
      "/OUT:$output", "/IMPLIB:$output.lib", "/MAP:$($mapPaths[$entry.Key])"
    ) + $entry.Value + $x64Libraries
    Invoke-Logged $tools.link $arguments (Join-Path $logRoot ("link-{0}.log" -f $entry.Key)) | Out-Null
  }

  # Use link.exe as the ARM64 reference linker over the exact NeverC objects
  # and bundled runtime bits consumed by the integrated linker below.
  $arm64Libraries = @(
    $bundledRuntime.arm64_vertdll,
    $bundledRuntime.arm64_bcrypt,
    $bundledRuntime.arm64_enclave_libcmt,
    $bundledRuntime.arm64_enclave_libvcruntime,
    $bundledRuntime.arm64_enclave_ucrt
  )
  $arm64Reference = Join-Path $unsignedRoot 'neverc-msvc-arm64.dll'
  $arm64ReferenceArguments = @(
    '/NOLOGO', '/DLL', '/INCREMENTAL:NO', '/NODEFAULTLIB', '/ENCLAVE',
    '/INTEGRITYCHECK', '/GUARD:MIXED', '/DYNAMICBASE', '/MACHINE:ARM64',
    '/WX:4229',
    "/OUT:$arm64Reference", "/IMPLIB:$arm64Reference.lib",
    "/MAP:$($mapPaths['neverc-msvc-arm64.dll'])",
    (Join-Path $nevercArm64ObjectRoot 'enclave.cpp.obj'),
    (Join-Path $nevercArm64ObjectRoot 'guarded.cpp.obj'),
    $legacyNeverCArm64
  ) + $arm64Libraries
  Invoke-Logged $tools.link_arm64 $arm64ReferenceArguments (Join-Path $logRoot 'link-neverc-msvc-arm64.dll.log') | Out-Null

  $bundledLibraries = @('-lvertdll', '-lbcrypt', '-llibcmt',
                        '-llibvcruntime', '-lucrt')
  $integrated = Join-Path $unsignedRoot 'neverc-neverc.dll'
  $candidateArguments = @(
    '--target=x86_64-pc-windows-msvc', '-fno-lto', '-shared', '-nostdlib',
    (Join-Path $nevercObjectRoot 'enclave.cpp.obj'),
    (Join-Path $nevercObjectRoot 'guarded.cpp.obj'), $legacyNeverC
  ) + $bundledLibraries + @(
    '-Xmslink', '/INCREMENTAL:NO', '-Xmslink', '/NODEFAULTLIB',
    '-Xmslink', '/ENCLAVE', '-Xmslink', '/INTEGRITYCHECK',
    '-Xmslink', '/GUARD:MIXED', '-Xmslink', '/DYNAMICBASE',
    '-Xmslink', '/MACHINE:X64',
    "-flinker-map=$($mapPaths['neverc-neverc.dll'])", '-o', $integrated
  )
  $candidateTrace = Join-Path $logRoot 'trace-neverc-neverc.dll.log'
  Invoke-Logged $neverc (@('-###') + $candidateArguments) $candidateTrace | Out-Null
  Assert-BundledRuntimeTrace $candidateTrace 'x64' $bundledRuntime
  Invoke-Logged $neverc $candidateArguments (Join-Path $logRoot 'link-neverc-neverc.dll.log') | Out-Null

  $integratedArm64 = Join-Path $unsignedRoot 'neverc-neverc-arm64.dll'
  $candidateArm64Arguments = @(
    '--target=aarch64-pc-windows-msvc', '-fno-lto', '-shared', '-nostdlib',
    (Join-Path $nevercArm64ObjectRoot 'enclave.cpp.obj'),
    (Join-Path $nevercArm64ObjectRoot 'guarded.cpp.obj'), $legacyNeverCArm64
  ) + $bundledLibraries + @(
    '-Xmslink', '/INCREMENTAL:NO', '-Xmslink', '/NODEFAULTLIB',
    '-Xmslink', '/ENCLAVE', '-Xmslink', '/INTEGRITYCHECK',
    '-Xmslink', '/GUARD:MIXED', '-Xmslink', '/DYNAMICBASE',
    '-Xmslink', '/MACHINE:ARM64',
    "-flinker-map=$($mapPaths['neverc-neverc-arm64.dll'])", '-o', $integratedArm64
  )
  $candidateArm64Trace = Join-Path $logRoot 'trace-neverc-neverc-arm64.dll.log'
  Invoke-Logged $neverc (@('-###') + $candidateArm64Arguments) $candidateArm64Trace | Out-Null
  Assert-BundledRuntimeTrace $candidateArm64Trace 'arm64' $bundledRuntime
  Invoke-Logged $neverc $candidateArm64Arguments (Join-Path $logRoot 'link-neverc-neverc-arm64.dll.log') | Out-Null

  $imageMachines = [ordered]@{
    'msvc-msvc.dll' = 'x86_64'
    'neverc-msvc.dll' = 'x86_64'
    'neverc-neverc.dll' = 'x86_64'
    'neverc-msvc-arm64.dll' = 'arm64'
    'neverc-neverc-arm64.dll' = 'arm64'
  }
  $imageLinkers = @{
    'msvc-msvc.dll' = 'microsoft'
    'neverc-msvc.dll' = 'microsoft'
    'neverc-neverc.dll' = 'integrated'
    'neverc-msvc-arm64.dll' = 'microsoft'
    'neverc-neverc-arm64.dll' = 'integrated'
  }
  foreach ($entry in $imageMachines.GetEnumerator()) {
    $name = $entry.Key
    $image = Join-Path $unsignedRoot $name
    Invoke-Logged $python @($verifier, 'inspect', $image, '--machine',
      $entry.Value, '--linker', $imageLinkers[$name],
      '--map', $mapPaths[$name],
      '--json', (Join-Path $jsonRoot "$name.json")) (Join-Path $logRoot "verify-$name.log") | Out-Null
    Invoke-Logged $tools.dumpbin @('/headers', '/loadconfig', $image) (Join-Path $artifactRoot "$name.dumpbin.txt") | Out-Null
  }
  Invoke-Logged $python @($verifier, 'compare',
    (Join-Path $unsignedRoot 'neverc-msvc.dll'),
    (Join-Path $unsignedRoot 'neverc-neverc.dll'),
    '--reference-map', $mapPaths['neverc-msvc.dll'],
    '--candidate-map', $mapPaths['neverc-neverc.dll']) (Join-Path $logRoot 'compare-neverc-linkers.log') | Out-Null
  Invoke-Logged $python @($verifier, 'compare',
    (Join-Path $unsignedRoot 'neverc-msvc-arm64.dll'),
    (Join-Path $unsignedRoot 'neverc-neverc-arm64.dll'),
    '--reference-map', $mapPaths['neverc-msvc-arm64.dll'],
    '--candidate-map', $mapPaths['neverc-neverc-arm64.dll']) (Join-Path $logRoot 'compare-neverc-linkers-arm64.log') | Out-Null

  # Static semantics are checked on the untouched images. Only then do we make
  # runtime copies and let VEIID mutate them. Signing happens last in Runtime.
  foreach ($name in @('msvc-msvc.dll', 'neverc-msvc.dll', 'neverc-neverc.dll',
                      'neverc-msvc-arm64.dll', 'neverc-neverc-arm64.dll')) {
    $runtimeImage = Join-Path $runtimeRoot $name
    Copy-Item -LiteralPath (Join-Path $unsignedRoot $name) -Destination $runtimeImage -Force
    Invoke-Logged $tools.veiid @($runtimeImage) (Join-Path $logRoot "veiid-$name.log") | Out-Null
  }
  Write-Host "STATIC PASS: x64 and ARM64 Microsoft-link semantics match the integrated linker, and VEIID runtime copies are ready in '$runtimeRoot'"
  exit 0
}

$resultPath = Join-Path $artifactRoot 'runtime-result.json'
try {
  $tools = Resolve-Toolchain
  $runtimeHost = Require-File (Join-Path $artifactRoot 'vbs-enclave-host.exe') 'runtime host'
  $runtimeRoot = Join-Path $artifactRoot 'runtime'
  $signedRoot = Join-Path $artifactRoot 'runtime-signed'
  New-Item -ItemType Directory -Force -Path $signedRoot | Out-Null

  $certificate = $null
  $certificateInMachineStore = $false
  if ($env:VBS_ENCLAVE_CERT_THUMBPRINT) {
    $certificate = Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\My |
      Where-Object Thumbprint -eq $env:VBS_ENCLAVE_CERT_THUMBPRINT |
      Select-Object -First 1
    if (-not $certificate) {
      throw "VBS_ENCLAVE_CERT_THUMBPRINT does not identify an installed certificate"
    }
    $certificateInMachineStore = $certificate.PSPath -like '*LocalMachine*'
  } elseif ($RequireRuntime) {
    throw 'required runtime needs a preinstalled VBS_ENCLAVE_CERT_THUMBPRINT certificate'
  } else {
    $certificatePath = Join-Path $artifactRoot 'ephemeral-vbs-enclave-ci.cer'
    $thumbprintPath = Join-Path $artifactRoot 'certificate-thumbprint.txt'
    $certificate = New-EphemeralVbsCertificate `
      -HelperPath $certificateHelper `
      -CertificatePath $certificatePath `
      -ThumbprintPath $thumbprintPath `
      -StageLogPath (Join-Path $logRoot 'certificate-stages.log') `
      -ProcessLogPath (Join-Path $logRoot 'certificate-bootstrap.log') `
      -TrustLogPath (Join-Path $logRoot 'certificate-trust.log')
  }

  foreach ($name in @('msvc-msvc.dll', 'neverc-msvc.dll', 'neverc-neverc.dll')) {
    $source = Require-File (Join-Path $runtimeRoot $name) "VEIID runtime image $name"
    $signed = Join-Path $signedRoot $name
    Copy-Item -LiteralPath $source -Destination $signed -Force
    $signArguments = @('sign', '/ph', '/fd', 'SHA256')
    if ($certificateInMachineStore) { $signArguments += '/sm' }
    $signArguments += @('/sha1', $certificate.Thumbprint, $signed)
    Invoke-TimedLogged $tools.signtool $signArguments `
      (Join-Path $logRoot "sign-$name.log") -TimeoutSeconds 60 | Out-Null
    Invoke-TimedLogged $tools.signtool @('verify', '/pa', '/v', $signed) `
      (Join-Path $logRoot "verify-signature-$name.log") `
      -TimeoutSeconds 60 | Out-Null
  }

  function Invoke-RuntimeImage {
    param([string]$Name)
    $image = Join-Path $signedRoot $Name
    $log = Join-Path $logRoot "runtime-$Name.log"
    $exitCode = Invoke-TimedLogged $runtimeHost @($image) $log `
      -TimeoutSeconds 180 -AllowFailure
    $stage = 'Complete'
    $errorCode = 0
    if ($exitCode -ne 0) {
      if ($exitCode -eq 124) {
        $stage = 'HostTimeout'
        $errorCode = 1460
      } else {
        $failureLine = Get-Content -LiteralPath $log |
          Where-Object { $_ -match 'VBS_STAGE=(\S+) STATUS=FAIL ERROR=(\d+)' } |
          Select-Object -Last 1
      }
      if ($exitCode -ne 124 -and $failureLine -and
          $failureLine -match 'VBS_STAGE=(\S+) STATUS=FAIL ERROR=(\d+)') {
        $stage = $Matches[1]
        $errorCode = [int]$Matches[2]
      } elseif ($exitCode -ne 124) {
        $stage = 'HostProcess'
        $errorCode = $exitCode
      }
    }
    return [pscustomobject]@{ Name = $Name; ExitCode = $exitCode; Stage = $stage; Error = $errorCode }
  }

  $reference = Invoke-RuntimeImage 'msvc-msvc.dll'
  if ($reference.ExitCode -ne 0) {
    $message = "Microsoft reference failed; runner lacks a usable VBS/test-signing environment"
    if ($RequireRuntime) {
      Write-Result 'FAIL' $reference.Stage $reference.Error $message $resultPath
      throw $message
    }
    Write-Result 'SKIP' $reference.Stage $reference.Error $message $resultPath
    exit 0
  }

  foreach ($candidateName in @('neverc-msvc.dll', 'neverc-neverc.dll')) {
    $candidate = Invoke-RuntimeImage $candidateName
    if ($candidate.ExitCode -ne 0) {
      $message = "$candidateName failed after the Microsoft reference passed"
      Write-Result 'FAIL' $candidate.Stage $candidate.Error $message $resultPath
      throw $message
    }
  }
  Write-Result 'PASS' 'Complete' 0 'Reference and both candidates loaded and initialized.' $resultPath
  exit 0
} catch {
  if (-not (Test-Path -LiteralPath $resultPath)) {
    Write-Result 'FAIL' 'Harness' 1 $_.Exception.Message $resultPath
  }
  throw
}
