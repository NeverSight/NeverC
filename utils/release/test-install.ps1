# Offline installer regressions. Add -Archive PATH on Windows to smoke-test a built release ZIP.
[CmdletBinding()]
param([string] $Archive)
$ErrorActionPreference = 'Stop'

$installerPath = Join-Path $PSScriptRoot '../../install.ps1'
$source = Get-Content -LiteralPath $installerPath -Raw
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('neverc-installer-tests-' + [guid]::NewGuid().ToString('N'))
$script:testProfile = Join-Path $testRoot 'profile'
$savedEnvironment = @{}
foreach ($name in @('OS', 'PATH', 'NEVERC_VERSION', 'NEVERC_INSTALL_DIR', 'NEVERC_NO_MODIFY_PATH')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$nativeWindows = $env:OS -eq 'Windows_NT'
$nativeRegistryPath = 'Software\NeverCInstallerTests-' + [guid]::NewGuid().ToString('N')
$registryBoundary = "[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')"
$profileBoundary = "[Environment]::GetFolderPath('UserProfile')"
$notifyBoundary = '[NeverCInstallerEnvironment]::Refresh()'
foreach ($boundary in @($registryBoundary, $profileBoundary, $notifyBoundary)) {
    if (-not $source.Contains($boundary)) { throw "Installer Windows boundary changed: $boundary" }
}
# Only Windows registry, profile, and notification boundaries are substituted. The
# complete installer still executes, including real extraction, hashing and copying.
$fixtureSource = $source.Replace($registryBoundary, '(Get-TestUserEnvironmentKey)').
    Replace($profileBoundary, '$script:testProfile').Replace($notifyBoundary, '$script:state.Notifications++')
$script:state = $null
$script:passed = 0

function Assert-True($Condition, [string] $Message) {
    if (-not $Condition) { throw "ASSERTION FAILED: $Message" }
}

function Get-TestUserEnvironmentKey {
    $key = [pscustomobject] @{}
    $key | Add-Member ScriptMethod GetValue { param($Name, $Default, $Options) $script:state.UserPath }
    $key | Add-Member ScriptMethod GetValueNames { @('Path') }
    $key | Add-Member ScriptMethod GetValueKind { param($Name) $script:state.UserKind }
    $key | Add-Member ScriptMethod SetValue {
        param($Name, $Value, $Kind)
        $script:state.UserPath = $Value
        $script:state.UserKind = $Kind
        $script:state.PathWrites++
    }
    $key | Add-Member ScriptMethod Dispose { }
    return $key
}

function Get-CimInstance {
    param([string] $ClassName)
    Assert-True ($ClassName -eq 'Win32_Processor') 'Unexpected CIM query'
    if ($script:state.Native) { CimCmdlets\Get-CimInstance -ClassName $ClassName }
    else { [pscustomobject] @{ Architecture = $script:state.Architecture } }
}

function Invoke-RestMethod {
    param([switch] $UseBasicParsing, $Headers, [string] $Uri)
    [void] $script:state.Urls.Add($Uri)
    if ($script:state.ApiFailure) { throw 'Fixture API network failure' }
    Assert-True ($Uri -eq 'https://api.github.com/repos/NeverSight/NeverC/releases?per_page=100&page=1') 'Unexpected API URL'
    return ,$script:state.Releases
}

function Invoke-WebRequest {
    param([switch] $UseBasicParsing, $Headers, [string] $Uri, [string] $OutFile)
    [void] $script:state.Urls.Add($Uri)
    [void] $script:state.TempDirs.Add([IO.Path]::GetDirectoryName($OutFile))
    $name = ($Uri -split '/')[-1]
    if ($script:state.FailDownload -eq $name -or $script:state.FailDownload -eq 'all') { throw 'Fixture download network failure' }
    if ($name -eq 'SHA256SUMS') {
        $digest = (Get-FileHash -LiteralPath $script:state.ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        $assetName = "windows-$($script:state.AssetArch)-neverc-release.zip"
        $manifest = "$digest  $assetName`n"
        switch ($script:state.ChecksumMode) {
            'mismatch' { $manifest = ('0' * 64) + "  $assetName`n" }
            'missing' { $manifest = "$digest  wrong-$assetName`n" }
            'duplicate' { $manifest += $manifest }
            'binary' { $manifest = "$digest *$assetName`r`n" }
        }
        [IO.File]::WriteAllText($OutFile, $manifest)
    }
    else {
        Assert-True ($name -eq "windows-$($script:state.AssetArch)-neverc-release.zip") 'Incorrect Windows architecture archive selected'
        [IO.File]::Copy($script:state.ArchivePath, $OutFile)
    }
}

function New-FixtureArchive([string] $Name, [string] $ExecutableMode = 'valid') {
    $directory = Join-Path $testRoot $Name
    [void] [IO.Directory]::CreateDirectory((Join-Path $directory 'install/bin'))
    [void] [IO.Directory]::CreateDirectory((Join-Path $directory 'install/lib/clang/include'))
    [void] [IO.Directory]::CreateDirectory((Join-Path $directory 'install/share/neverc/sdk'))
    [IO.File]::WriteAllText((Join-Path $directory 'install/lib/clang/include/test.h'), 'bundled header')
    [IO.File]::WriteAllText((Join-Path $directory 'install/share/neverc/sdk/data'), 'bundled SDK')
    $exe = Join-Path $directory 'install/bin/neverc.exe'
    switch ($ExecutableMode) {
        'valid' { [IO.File]::WriteAllText($exe, 'fixture compiler') }
        'empty' { [IO.File]::WriteAllText($exe, '') }
        'directory' { [void] [IO.Directory]::CreateDirectory($exe) }
    }
    $zip = Join-Path $testRoot "$Name.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($directory, $zip)
    return $zip
}

function New-Release([string] $Tag, [string[]] $Assets, [bool] $Draft = $false, [bool] $Prerelease = $false) {
    [pscustomobject] @{ tag_name = $Tag; draft = $Draft; prerelease = $Prerelease; assets = @($Assets | ForEach-Object { @{ name = $_ } }) }
}

function Reset-Case([string] $Name) {
    $env:OS = 'Windows_NT'
    $env:PATH = 'C:\Windows\System32;C:\existing tools'
    $env:NEVERC_VERSION = $null
    $env:NEVERC_INSTALL_DIR = Join-Path $testRoot "$Name [prefix]"
    $env:NEVERC_NO_MODIFY_PATH = $null
    $script:state = @{
        Architecture = 9; AssetArch = 'x64'; Native = $false; ArchivePath = $script:validArchive
        ChecksumMode = 'valid'; FailDownload = ''; ApiFailure = $false
        Urls = [Collections.Generic.List[string]]::new(); TempDirs = [Collections.Generic.List[string]]::new()
        UserPath = '%USERPROFILE%\tools;C:\user tools'; UserKind = [Microsoft.Win32.RegistryValueKind]::ExpandString
        PathWrites = 0; Notifications = 0
        Releases = @(New-Release 'v3389.1.5' @('windows-x64-neverc-release.zip', 'windows-arm64-neverc-release.zip', 'SHA256SUMS'))
    }
}

function Invoke-Installer([hashtable] $Parameters = @{}, [switch] $Iex, [string] $Code = $fixtureSource) {
    $Version = 'caller version'
    $InstallDir = 'caller directory'
    $NoModifyPath = 'caller path preference'
    $ErrorActionPreference = 'Continue'
    $ProgressPreference = 'Continue'
    $beforeError = $ErrorActionPreference
    $beforeProgress = $ProgressPreference
    $beforeTls = [Net.ServicePointManager]::SecurityProtocol
    try {
        if ($Iex) { Invoke-Expression $Code }
        else { & ([scriptblock]::Create($Code)) @Parameters }
    }
    finally {
        Assert-True ($Version -ceq 'caller version' -and $InstallDir -ceq 'caller directory' -and
            $NoModifyPath -ceq 'caller path preference') 'Installer overwrote caller variables'
        Assert-True ($ErrorActionPreference -eq $beforeError) 'Installer leaked ErrorActionPreference'
        Assert-True ($ProgressPreference -eq $beforeProgress) 'Installer leaked ProgressPreference'
        Assert-True ([Net.ServicePointManager]::SecurityProtocol -eq $beforeTls) 'Installer leaked TLS preferences'
        Assert-True (-not (Test-Path Function:\Get-NeverCArchitecture)) 'Installer leaked helper functions'
        foreach ($directory in $script:state.TempDirs) {
            Assert-True (-not (Test-Path -LiteralPath $directory)) "Temporary directory was not cleaned: $directory"
        }
    }
}

function Assert-Failure([string] $Pattern, [hashtable] $Parameters = @{}) {
    $oldExe = Join-Path $env:NEVERC_INSTALL_DIR 'bin/neverc.exe'
    [void] [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($oldExe))
    [IO.File]::WriteAllText($oldExe, 'old compiler')
    $oldPath = $env:PATH
    $oldUserPath = $script:state.UserPath
    $caught = $null
    try { Invoke-Installer $Parameters } catch { $caught = $_ }
    Assert-True ($null -ne $caught) 'Expected installer failure'
    Assert-True ($caught.Exception.Message -match $Pattern) "Unexpected error: $caught"
    Assert-True ([IO.File]::ReadAllText($oldExe) -eq 'old compiler') 'Failure modified the old compiler'
    Assert-True (@(Get-ChildItem -LiteralPath $env:NEVERC_INSTALL_DIR -Recurse -File).Count -eq 1) 'Failure copied files into the prefix'
    Assert-True ($env:PATH -ceq $oldPath -and $script:state.UserPath -ceq $oldUserPath) 'Failure modified PATH'
}

function Pass([string] $Name) { $script:passed++; Write-Host "PASS: $Name" }

try {
    [void] [IO.Directory]::CreateDirectory($testRoot)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $script:validArchive = New-FixtureArchive 'valid'
    $missingArchive = New-FixtureArchive 'missing' 'missing'
    $emptyArchive = New-FixtureArchive 'empty' 'empty'
    $directoryArchive = New-FixtureArchive 'directory' 'directory'

    Reset-Case 'iex-latest'
    $asset = 'windows-x64-neverc-release.zip'
    $script:state.Releases = @(
        (New-Release 'gki-build-20260908' @($asset, 'SHA256SUMS'))
        (New-Release 'v9999.0.0-rc1' @($asset, 'SHA256SUMS'))
        (New-Release 'v9998.0.0' @($asset, 'SHA256SUMS') $true)
        (New-Release 'v9997.0.0' @($asset, 'SHA256SUMS') $false $true)
        (New-Release 'v9996.0.0' @($asset))
        (New-Release 'v9995.0.0' @('windows-arm64-neverc-release.zip', 'SHA256SUMS'))
        (New-Release 'v3389.1.5' @($asset, 'SHA256SUMS'))
    )
    Invoke-Installer -Iex
    Assert-True ($script:state.Urls -contains "https://github.com/NeverSight/NeverC/releases/download/v3389.1.5/$asset") 'Latest release filtering failed'
    foreach ($file in @('bin/neverc.exe', 'lib/clang/include/test.h', 'share/neverc/sdk/data')) {
        Assert-True (Test-Path -LiteralPath (Join-Path $env:NEVERC_INSTALL_DIR $file) -PathType Leaf) "Distribution file missing: $file"
    }
    $expectedBin = Join-Path $env:NEVERC_INSTALL_DIR 'bin'
    Assert-True ($env:PATH -ceq "$expectedBin;C:\Windows\System32;C:\existing tools") 'Current PATH entries lost'
    Assert-True ($script:state.UserPath -ceq "$expectedBin;%USERPROFILE%\tools;C:\user tools") 'User PATH entries or expansion lost'
    Invoke-Installer -Iex
    Assert-True ($script:state.PathWrites -eq 1) 'Repeat install rewrote user PATH'
    Assert-True ($script:state.Notifications -eq 1) 'PATH notification was missing or duplicated'
    Pass 'iex latest filtering, full distribution, literal brackets/spaces, idempotent PATH'

    Reset-Case 'arm64-env'
    $script:state.Architecture = 12; $script:state.AssetArch = 'arm64'
    $env:NEVERC_VERSION = '3389.1.4'; $env:NEVERC_NO_MODIFY_PATH = '1'
    $beforePath = $env:PATH
    Invoke-Installer -Iex
    Assert-True ($script:state.Urls.Count -eq 2 -and $script:state.Urls[0] -match '/v3389\.1\.4/windows-arm64-') 'Environment version/architecture not respected'
    Assert-True ($env:PATH -ceq $beforePath -and $script:state.PathWrites -eq 0) 'Environment PATH opt-out failed'
    Pass 'ARM64, environment defaults and both PATH opt-outs'

    Reset-Case 'parameter-overrides'
    $env:NEVERC_VERSION = 'invalid'; $env:NEVERC_NO_MODIFY_PATH = '1'
    $chosenDir = Join-Path $testRoot 'explicit [directory]'
    Invoke-Installer @{ Version = 'v3389.1.3'; InstallDir = $chosenDir; NoModifyPath = $false }
    Assert-True (Test-Path -LiteralPath (Join-Path $chosenDir 'bin/neverc.exe')) 'InstallDir parameter did not override environment'
    Assert-True ($script:state.Urls[0] -match '/v3389\.1\.3/') 'Version parameter did not override environment'
    Assert-True ($script:state.PathWrites -eq 1) 'Explicit false did not override environment PATH opt-out'
    Pass 'explicit parameter precedence'

    Reset-Case 'switch-forwarding'
    $env:NEVERC_VERSION = '3389.1.5'; $env:NEVERC_NO_MODIFY_PATH = '1'
    & ([scriptblock]::Create($fixtureSource)) -NoModifyPath:$false
    Assert-True ($script:state.PathWrites -eq 1) 'Explicit switch false was misbound without other parameters'
    Reset-Case 'bare-switch'
    $env:NEVERC_VERSION = '3389.1.5'
    & ([scriptblock]::Create($fixtureSource)) -NoModifyPath
    Assert-True ($script:state.PathWrites -eq 0) 'Bare switch was not forwarded'
    Pass 'explicit false and bare switches without positional parameters'

    Reset-Case 'default-profile'
    $env:NEVERC_INSTALL_DIR = $null
    Invoke-Installer @{ Version = '3389.1.5'; NoModifyPath = $true }
    Assert-True (Test-Path -LiteralPath (Join-Path $script:testProfile '.neverc/bin/neverc.exe')) 'Default prefix is not profile/.neverc'
    Assert-True ($script:state.PathWrites -eq 0) 'Parameter PATH opt-out failed'
    Pass 'default profile and parameter PATH opt-out'

    Reset-Case 'relative-path'
    Push-Location -LiteralPath $testRoot
    try { Invoke-Installer @{ Version = '3389.1.5'; InstallDir = 'relative [prefix]'; NoModifyPath = $true } }
    finally { Pop-Location }
    Assert-True (Test-Path -LiteralPath (Join-Path $testRoot 'relative [prefix]/bin/neverc.exe')) 'Relative prefix ignored PowerShell location'
    Pass 'relative PowerShell install path'

    Reset-Case 'path-duplicates'
    $bin = Join-Path $env:NEVERC_INSTALL_DIR 'bin'
    $env:PATH = "C:\existing;`"$($bin.ToUpperInvariant())\`";$bin"
    $script:state.UserPath = "$bin;C:\user;$bin"
    $script:state.UserKind = [Microsoft.Win32.RegistryValueKind]::String
    $script:state.ChecksumMode = 'binary'
    Invoke-Installer @{ Version = '3389.1.5' }
    Assert-True ($env:PATH -ceq "$bin;C:\existing" -and $script:state.UserPath -ceq "$bin;C:\user") 'PATH duplicate normalization failed'
    Assert-True ($script:state.UserKind -eq [Microsoft.Win32.RegistryValueKind]::String) 'Existing registry value kind changed'
    Pass 'PATH duplicate normalization and binary checksum format'

    foreach ($mode in @('mismatch', 'missing', 'duplicate')) {
        Reset-Case "checksum-$mode"; $script:state.ChecksumMode = $mode
        Assert-Failure 'checksum|SHA256SUMS'; Pass "checksum $mode preserves existing prefix"
    }
    foreach ($zip in @($missingArchive, $emptyArchive, $directoryArchive)) {
        Reset-Case ([IO.Path]::GetFileNameWithoutExtension($zip)); $script:state.ArchivePath = $zip
        Assert-Failure 'install/bin/neverc.exe'; Pass "invalid executable $([IO.Path]::GetFileName($zip))"
    }
    foreach ($download in @('windows-x64-neverc-release.zip', 'SHA256SUMS')) {
        Reset-Case 'download-failure'; $script:state.FailDownload = $download
        Assert-Failure 'Failed to download'; Pass "download failure $download"
    }
    Reset-Case 'api-failure'; $script:state.ApiFailure = $true
    Assert-Failure 'API network failure'; Pass 'release discovery network failure'
    Reset-Case 'no-release'; $script:state.Releases = @()
    Assert-Failure 'No stable NeverC release'; Pass 'missing compatible release'
    Reset-Case 'invalid-version'
    Assert-Failure 'expected vMAJOR.MINOR.PATCH' @{ Version = '../bad' }
    Assert-True ($script:state.Urls.Count -eq 0) 'Invalid version used network'; Pass 'invalid version rejected before network'
    Reset-Case 'unsupported-architecture'; $script:state.Architecture = 0
    Assert-Failure 'Unsupported Windows processor architecture'
    Assert-True ($script:state.Urls.Count -eq 0) 'Unsupported architecture used network'; Pass 'unsupported architecture rejected before network'
    Reset-Case 'unsupported-os'; $env:OS = 'Other'
    Assert-Failure 'requires Windows'
    Assert-True ($script:state.Urls.Count -eq 0) 'Unsupported OS used network'; Pass 'unsupported OS rejected before network'

    if ($nativeWindows) {
        # Exercise the native CIM provider and real registry semantics in a disposable key.
        foreach ($kind in @([Microsoft.Win32.RegistryValueKind]::ExpandString, [Microsoft.Win32.RegistryValueKind]::String)) {
            Reset-Case "native-$kind"; $script:state.Native = $true
            $nativeArch = @(CimCmdlets\Get-CimInstance -ClassName Win32_Processor | Select-Object -ExpandProperty Architecture -Unique)
            Assert-True ($nativeArch.Count -eq 1 -and $nativeArch[0] -in @(9, 12)) 'Native host architecture unsupported'
            $script:state.AssetArch = if ($nativeArch[0] -eq 12) { 'arm64' } else { 'x64' }
            $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($nativeRegistryPath)
            try { $key.SetValue('Path', '%USERPROFILE%\tools;C:\existing', $kind) } finally { $key.Dispose() }
            $nativeSource = $source.Replace($registryBoundary, "[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('$nativeRegistryPath')")
            Invoke-Installer @{ Version = '3389.1.5' } -Code $nativeSource
            $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($nativeRegistryPath)
            try {
                $raw = $key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
                Assert-True ($raw -ceq "$(Join-Path $env:NEVERC_INSTALL_DIR 'bin');%USERPROFILE%\tools;C:\existing") 'Native raw user PATH changed'
                Assert-True ($key.GetValueKind('Path') -eq $kind) 'Native registry value kind changed'
            }
            finally { $key.Dispose() }
            Pass "native processor detection and registry $kind"
        }
        if ($Archive) {
            Reset-Case 'built-archive'; $script:state.Native = $true
            $script:state.AssetArch = if ($nativeArch[0] -eq 12) { 'arm64' } else { 'x64' }
            $script:state.ArchivePath = (Get-Item -LiteralPath $Archive).FullName
            Invoke-Installer @{ Version = '3389.1.5'; NoModifyPath = $true }
            & (Join-Path $env:NEVERC_INSTALL_DIR 'bin/neverc.exe') --version
            Assert-True ($LASTEXITCODE -eq 0) 'Installed built compiler --version failed'
            Pass 'built release archive installs and neverc.exe --version succeeds'
        }
    }
    else {
        if ($Archive) { throw '-Archive requires a native Windows host to execute neverc.exe.' }
        Write-Host 'SKIP: native Windows CIM, registry and executable checks (run by Windows CI).'
    }
    Write-Host "All $script:passed installer checks passed. No public network was used."
}
finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    if ($nativeWindows) { [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($nativeRegistryPath, $false) }
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
