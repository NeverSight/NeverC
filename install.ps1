# NeverC installer (Windows PowerShell 5.1 or PowerShell 7).
# irm https://raw.githubusercontent.com/NeverSight/NeverC/HEAD/install.ps1 | iex
# File/scriptblock options: -Version vX.Y.Z -InstallDir PATH -NoModifyPath
# Environment defaults: NEVERC_VERSION, NEVERC_INSTALL_DIR, NEVERC_NO_MODIFY_PATH=1.
# Keep parameter binding, functions and preferences local when piped into iex.
& {
    # Forwarding unbound arguments preserves named parameters, but PowerShell
    # splits -Switch:$false into a switch token and a positional Boolean. Bind
    # that single switch explicitly; let PowerShell bind every other argument.
    $forwardArguments = @()
    $switchArguments = @{}
    for ($index = 0; $index -lt $args.Count; $index++) {
        if ($args[$index] -ieq '-NoModifyPath:' -and $index + 1 -lt $args.Count -and
            ($args[$index + 1] -is [bool] -or $args[$index + 1] -is [switch])) {
            $switchArguments.NoModifyPath = [bool] $args[++$index]
        }
        else { $forwardArguments += $args[$index] }
    }
    & {
    [CmdletBinding()]
    param(
        [string] $Version = $env:NEVERC_VERSION,
        [string] $InstallDir = $env:NEVERC_INSTALL_DIR,
        [switch] $NoModifyPath = ($env:NEVERC_NO_MODIFY_PATH -eq '1')
    )
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'

    function Get-NeverCArchitecture {
        if ($env:OS -ne 'Windows_NT') {
            throw 'NeverC install.ps1 requires Windows. Use install.sh on macOS or Linux.'
        }
        if (-not [Environment]::Is64BitOperatingSystem) {
            throw 'NeverC requires a 64-bit Windows operating system.'
        }
        # Query the processor, not the shell: x64 PowerShell can run on ARM64.
        $architectures = @(Get-CimInstance -ClassName Win32_Processor |
            Select-Object -ExpandProperty Architecture -Unique)
        if ($architectures.Count -ne 1) {
            throw 'Could not determine a single native Windows processor architecture.'
        }
        switch ([int] $architectures[0]) {
            9 { return 'x64' }
            12 { return 'arm64' }
            default { throw "Unsupported Windows processor architecture: $($architectures[0]) (requires x64 or ARM64)." }
        }
    }

    function Resolve-NeverCVersion([string] $Requested, [string] $Asset) {
        if (-not [string]::IsNullOrWhiteSpace($Requested)) {
            if ($Requested -cnotmatch '^v?[0-9]+\.[0-9]+\.[0-9]+$') {
                throw "Invalid version '$Requested': expected vMAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH."
            }
            return 'v' + $Requested.TrimStart('v')
        }

        for ($page = 1; ; $page++) {
            $response = Invoke-RestMethod -UseBasicParsing -Headers $headers -Uri (
                "https://api.github.com/repos/$repo/releases?per_page=100&page=$page")
            $releases = @($response)
            if ($releases.Count -eq 0) { break }
            foreach ($release in $releases) {
                if ($release.draft -or $release.prerelease -or
                    $release.tag_name -cnotmatch '^v[0-9]+\.[0-9]+\.[0-9]+$') { continue }
                $names = @($release.assets | ForEach-Object { $_.name })
                if ($names -ccontains $Asset -and $names -ccontains 'SHA256SUMS') {
                    return $release.tag_name
                }
            }
            if ($releases.Count -lt 100) { break }
        }
        throw "No stable NeverC release contains '$Asset' and SHA256SUMS. Set NEVERC_VERSION to a published compiler version."
    }

    function Get-NeverCPath([string] $Existing, [string] $BinDir) {
        $target = $BinDir.Replace('/', '\').TrimEnd('\')
        $entries = @()
        if (-not [string]::IsNullOrEmpty($Existing)) {
            $entries = @($Existing -split ';' | Where-Object {
                $expanded = [Environment]::ExpandEnvironmentVariables($_.Trim().Trim('"'))
                $candidate = $expanded.Replace('/', '\').TrimEnd('\')
                -not [string]::Equals($candidate, $target, [StringComparison]::OrdinalIgnoreCase)
            })
        }
        return (@($BinDir) + $entries) -join ';'
    }

    $repo = 'NeverSight/NeverC'
    $headers = @{ Accept = 'application/vnd.github+json'; 'User-Agent' = 'NeverC-installer' }
    $tempDir = $null
    $previousTls = [Net.ServicePointManager]::SecurityProtocol
    try {
        $arch = Get-NeverCArchitecture
        $asset = "windows-$arch-neverc-release.zip"
        [Net.ServicePointManager]::SecurityProtocol = $previousTls -bor [Net.SecurityProtocolType]::Tls12
        $tag = Resolve-NeverCVersion $Version $asset
        if ([string]::IsNullOrWhiteSpace($InstallDir)) {
            $profileDir = [Environment]::GetFolderPath('UserProfile')
            if ([string]::IsNullOrWhiteSpace($profileDir)) { throw 'Could not find the user profile directory.' }
            $InstallDir = Join-Path $profileDir '.neverc'
        }
        $InstallDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($InstallDir)
        Write-Host "Installing NeverC $tag for windows-$arch to $InstallDir"

        $tempDir = Join-Path ([IO.Path]::GetTempPath()) ('neverc-install-' + [guid]::NewGuid().ToString('N'))
        [void] [IO.Directory]::CreateDirectory($tempDir)
        $archivePath = Join-Path $tempDir $asset
        $manifestPath = Join-Path $tempDir 'SHA256SUMS'
        $baseUrl = "https://github.com/$repo/releases/download/$tag"
        try {
            Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri "$baseUrl/$asset" -OutFile $archivePath
            Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri "$baseUrl/SHA256SUMS" -OutFile $manifestPath
        }
        catch { throw "Failed to download NeverC $tag ($asset or SHA256SUMS): $($_.Exception.Message)" }

        $checksums = @(Get-Content -LiteralPath $manifestPath | ForEach-Object {
            if ($_ -cmatch '^([0-9a-fA-F]{64})\s+\*?(.+)$' -and $Matches[2] -ceq $asset) { $Matches[1] }
        })
        if ($checksums.Count -ne 1) { throw "SHA256SUMS must contain exactly one checksum for '$asset'." }
        $actual = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
        if ($actual -ine $checksums[0]) { throw "Checksum verification failed for '$asset'." }

        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $extractDir = Join-Path $tempDir 'extract'
        [IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $extractDir)
        $payloadDir = Join-Path $extractDir 'install'
        $executable = Join-Path $payloadDir 'bin/neverc.exe'
        if (-not (Test-Path -LiteralPath $executable -PathType Leaf) -or
            (Get-Item -LiteralPath $executable).Length -eq 0) {
            throw 'Release archive is missing a nonempty install/bin/neverc.exe.'
        }

        # Verify and extract first; failed downloads/checksums never touch an existing install.
        [void] [IO.Directory]::CreateDirectory($InstallDir)
        Get-ChildItem -LiteralPath $payloadDir -Force | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $InstallDir -Recurse -Force
        }
        $binDir = Join-Path $InstallDir 'bin'
        if (-not $NoModifyPath) {
            $userEnvironment = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')
            try {
                # Preserve %VARIABLE% entries and their registry expansion behavior.
                $userPath = $userEnvironment.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
                $kind = [Microsoft.Win32.RegistryValueKind]::ExpandString
                if ($userEnvironment.GetValueNames() -contains 'Path') { $kind = $userEnvironment.GetValueKind('Path') }
                $updatedUserPath = Get-NeverCPath $userPath $binDir
                if ($updatedUserPath -cne $userPath) { $userEnvironment.SetValue('Path', $updatedUserPath, $kind) }
            }
            finally { $userEnvironment.Dispose() }
            # Notify Windows applications that the persistent environment changed.
            if ($updatedUserPath -cne $userPath) {
                if (-not ('NeverCInstallerEnvironment' -as [type])) {
                    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NeverCInstallerEnvironment {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr SendMessageTimeout(IntPtr window, uint message,
        UIntPtr parameter, string value, uint flags, uint timeout, out UIntPtr result);
    public static void Refresh() {
        UIntPtr result;
        SendMessageTimeout(new IntPtr(0xffff), 0x001a, UIntPtr.Zero,
            "Environment", 2, 1000, out result);
    }
}
'@
                }
                [NeverCInstallerEnvironment]::Refresh()
            }
            $env:PATH = Get-NeverCPath $env:PATH $binDir
        }
        Write-Host "Installed NeverC $tag. Run: neverc --version"
        if ($NoModifyPath) { Write-Host "PATH was not modified. Executable: $(Join-Path $binDir 'neverc.exe')" }
        else { Write-Host 'PATH is ready in this session. New applications will inherit the updated user PATH.' }
    }
    finally {
        [Net.ServicePointManager]::SecurityProtocol = $previousTls
        if ($tempDir -and (Test-Path -LiteralPath $tempDir)) {
            Remove-Item -LiteralPath $tempDir -Recurse -Force
        }
    }
    } @forwardArguments @switchArguments
} @args
