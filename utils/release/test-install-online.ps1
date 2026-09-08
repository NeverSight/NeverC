# Exercise the public one-line installer against real GitHub releases on Windows.
[CmdletBinding()]
param(
    [string] $Repository = $env:GITHUB_REPOSITORY,
    [string] $Commit = $env:GITHUB_SHA
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$PSNativeCommandUseErrorActionPreference = $false
if ($env:OS -ne 'Windows_NT') { throw 'Online installer checks require native Windows.' }
if ($Repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or
    $Commit -cnotmatch '^[0-9a-fA-F]{40}$') {
    throw 'Provide a GitHub Repository and full Commit SHA to test the exact public installer.'
}

function Assert-True($Condition, [string] $Message) {
    if (-not $Condition) { throw "ASSERTION FAILED: $Message" }
}

function Get-PeMachine([string] $Path) {
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try {
        Assert-True ($reader.ReadUInt16() -eq 0x5a4d) "Missing DOS header: $Path"
        [void] $reader.BaseStream.Seek(0x3c, [IO.SeekOrigin]::Begin)
        $offset = $reader.ReadInt32()
        [void] $reader.BaseStream.Seek($offset, [IO.SeekOrigin]::Begin)
        Assert-True ($reader.ReadUInt32() -eq 0x00004550) "Missing PE header: $Path"
        return $reader.ReadUInt16()
    }
    finally { $reader.Dispose() }
}

$installerPath = Join-Path $PSScriptRoot '../../install.ps1'
$expectedSource = [IO.File]::ReadAllText($installerPath).Replace("`r`n", "`n")
$installerUri = "https://raw.githubusercontent.com/$Repository/$Commit/install.ps1"
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('neverc-online-' + [guid]::NewGuid().ToString('N'))
$installDir = Join-Path $testRoot 'installed compiler [network]'
$binDir = Join-Path $installDir 'bin'
$expectedExe = Join-Path $binDir 'neverc.exe'
$savedEnvironment = @{}
foreach ($name in @('PATH', 'NEVERC_VERSION', 'NEVERC_INSTALL_DIR', 'NEVERC_NO_MODIFY_PATH')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$userPathExisted = $false
$userPath = $null
$userKind = [Microsoft.Win32.RegistryValueKind]::ExpandString
$key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment')
try {
    if ($key -and $key.GetValueNames() -contains 'Path') {
        $userPathExisted = $true
        $userPath = $key.GetValue('Path', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $userKind = $key.GetValueKind('Path')
    }
}
finally { if ($key) { $key.Dispose() } }
$previousTls = [Net.ServicePointManager]::SecurityProtocol

try {
    [void] [IO.Directory]::CreateDirectory($testRoot)
    $architectures = @(Get-CimInstance -ClassName Win32_Processor |
        Select-Object -ExpandProperty Architecture -Unique)
    $expectedArchitecture = switch ($env:RUNNER_ARCH) {
        'X64' { 9 }
        'ARM64' { 12 }
        default { throw 'Online checks require GitHub RUNNER_ARCH to be X64 or ARM64.' }
    }
    Assert-True ($architectures.Count -eq 1 -and $architectures[0] -eq $expectedArchitecture) 'CIM architecture differs from the GitHub runner architecture'
    $expectedMachine = if ($env:RUNNER_ARCH -eq 'ARM64') { 0xaa64 } else { 0x8664 }
    Write-Host "Testing $installerUri on $($PSVersionTable.PSEdition) $($PSVersionTable.PSVersion), GitHub runner $env:RUNNER_ARCH, native architecture $($architectures[0])"
    $env:NEVERC_VERSION = $null
    $env:NEVERC_NO_MODIFY_PATH = $null
    $env:NEVERC_INSTALL_DIR = $installDir
    [Net.ServicePointManager]::SecurityProtocol = $previousTls -bor [Net.SecurityProtocolType]::Tls12

    # Do not mock any installer command. Verify the public commit's source before
    # executing the same irm | iex pipeline users run (checkout may use CRLF).
    irm -UseBasicParsing -Uri $installerUri | ForEach-Object {
        Assert-True ($_ -is [string]) 'Public installer response was not text'
        Assert-True ($_.Replace("`r`n", "`n") -ceq $expectedSource) 'Public installer differs from the checked-out commit'
        $_
    } | iex

    Assert-True (Test-Path -LiteralPath $expectedExe -PathType Leaf) 'Installer did not create neverc.exe'
    Assert-True ((Get-PeMachine $expectedExe) -eq $expectedMachine) 'Installer selected a compiler for the wrong native architecture'
    $command = Get-Command neverc -CommandType Application -ErrorAction Stop
    Assert-True ($command.Source -ieq $expectedExe) 'Current session resolved a different neverc.exe'
    $expectedProcessPath = $binDir
    if (-not [string]::IsNullOrEmpty($savedEnvironment.PATH)) { $expectedProcessPath += ';' + $savedEnvironment.PATH }
    Assert-True ($env:PATH -ceq $expectedProcessPath) 'Current session PATH was not prepended or existing entries changed'
    $expectedUserPath = $binDir
    if (-not [string]::IsNullOrEmpty($userPath)) { $expectedUserPath += ';' + $userPath }
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment')
    try {
        Assert-True ($null -ne $key) 'Persistent user environment is missing'
        $actualUserPath = $key.GetValue('Path', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        Assert-True ($actualUserPath -ceq $expectedUserPath) 'Persistent raw user PATH was not prepended or existing entries changed'
        Assert-True ($key.GetValueKind('Path') -eq $userKind) 'Persistent user PATH registry kind changed'
    }
    finally { if ($key) { $key.Dispose() } }
    neverc --version
    Assert-True ($LASTEXITCODE -eq 0) 'Installed compiler --version failed'
    Write-Host 'PASS: public latest release download, checksum, extraction, native binary and session/user PATH'

    $sourcePath = Join-Path $testRoot 'smoke.c'
    $outputPath = Join-Path $testRoot 'smoke.exe'
    [IO.File]::WriteAllText($sourcePath, "int main(void) { return 37; }`n")
    neverc $sourcePath -o $outputPath
    Assert-True ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $outputPath -PathType Leaf)) 'Installed compiler could not build a C executable'
    Assert-True ((Get-PeMachine $outputPath) -eq $expectedMachine) 'Compiled executable has the wrong native architecture'
    & $outputPath
    Assert-True ($LASTEXITCODE -eq 37) 'Compiled C executable did not return the expected exit code 37'
    Write-Host 'PASS: installed compiler builds and runs a native C executable'

    # Child processes normally inherit this session's PATH. Reconstruct it solely
    # from persistent machine/user values to prove a fresh shell can find NeverC.
    $env:PATH = @(
        [Environment]::GetEnvironmentVariable('Path', 'Machine')
        [Environment]::GetEnvironmentVariable('Path', 'User')
    ) -join ';'
    $childPath = Join-Path $testRoot 'check-fresh-shell.ps1'
    [IO.File]::WriteAllText($childPath, @'
param([string] $ExpectedExe)
$ErrorActionPreference = 'Stop'
$command = Get-Command neverc -CommandType Application -ErrorAction Stop
if ($command.Source -ine $ExpectedExe) { throw 'Fresh shell resolved a different neverc.exe.' }
neverc --version
if ($LASTEXITCODE -ne 0) { throw 'Fresh shell could not run neverc --version.' }
Write-Host 'PASS: fresh PowerShell resolves and runs NeverC using persistent PATH'
'@)
    $shellName = if ($PSVersionTable.PSEdition -eq 'Desktop') { 'powershell.exe' } else { 'pwsh.exe' }
    & (Join-Path $PSHOME $shellName) -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $childPath $expectedExe
    Assert-True ($LASTEXITCODE -eq 0) 'Fresh PowerShell persistent PATH check failed'
    Write-Host 'All real-network Windows installer checks passed.'
}
finally {
    try {
        $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')
        try {
            if ($userPathExisted) { $key.SetValue('Path', $userPath, $userKind) }
            else { $key.DeleteValue('Path', $false) }
        }
        finally { $key.Dispose() }
    }
    finally {
        foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
        [Net.ServicePointManager]::SecurityProtocol = $previousTls
        if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
    }
}
