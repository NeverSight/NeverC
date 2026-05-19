# Install latest LLVM release (NSIS) on Windows CI.
# Usage: pwsh -File .github/scripts/install-llvm-windows.ps1
$ErrorActionPreference = 'Stop'

function Resolve-LlvmVersion {
    if ($env:LLVM_VER) {
        return $env:LLVM_VER
    }

    $headers = @{
        'Accept'        = 'application/vnd.github+json'
        'User-Agent'    = 'neverc-ci'
    }
    if ($env:GITHUB_TOKEN) {
        $headers['Authorization'] = "Bearer $($env:GITHUB_TOKEN)"
    }

    $releases = Invoke-RestMethod `
        -Uri 'https://api.github.com/repos/llvm/llvm-project/releases?per_page=40' `
        -Headers $headers

    $candidates = @()
    foreach ($release in $releases) {
        if ($release.prerelease) { continue }
        if ($release.tag_name -match '^llvmorg-(\d+\.\d+\.\d+)$') {
            $candidates += [PSCustomObject]@{
                Version = $Matches[1]
                Key     = ($Matches[1].Split('.') | ForEach-Object { [int]$_ })
            }
        }
    }
    if ($candidates.Count -eq 0) {
        throw 'no stable llvmorg release found'
    }
    $latest = $candidates | Sort-Object { $_.Key[0] }, { $_.Key[1] }, { $_.Key[2] } | Select-Object -Last 1
    $ver = $latest.Version
    Write-Host "Resolved LLVM_VER=$ver"
    $env:LLVM_VER = $ver
    if ($env:GITHUB_ENV) {
        Add-Content -Path $env:GITHUB_ENV -Value "LLVM_VER=$ver"
    }
    return $ver
}

function Get-LlvmWindowsAsset {
    param([string]$Version)
    $headers = @{ 'User-Agent' = 'neverc-ci' }
    if ($env:GITHUB_TOKEN) {
        $headers['Authorization'] = "Bearer $($env:GITHUB_TOKEN)"
    }
    $release = Invoke-RestMethod `
        -Uri "https://api.github.com/repos/llvm/llvm-project/releases/tags/llvmorg-$Version" `
        -Headers $headers
    $name = "LLVM-$Version-win64.exe"
    $asset = $release.assets | Where-Object { $_.name -eq $name } | Select-Object -First 1
    if (-not $asset) {
        throw "release asset not found: $name"
    }
    return $asset
}

$LLVM_VER = Resolve-LlvmVersion
$LLVM_ROOT = if ($env:LLVM_ROOT) { $env:LLVM_ROOT } else { 'C:\LLVM' }
$clang = Join-Path $LLVM_ROOT 'bin\clang.exe'

if (Test-Path $clang) {
    Write-Host "LLVM already present at $LLVM_ROOT"
} else {
    $asset = Get-LlvmWindowsAsset -Version $LLVM_VER
    $url = $asset.browser_download_url
    $expectedSize = [int64]$asset.size
    $installer = Join-Path $env:RUNNER_TEMP "llvm-$LLVM_VER-win64.exe"

    Write-Host "Downloading LLVM $LLVM_VER ($([math]::Round($expectedSize / 1MB)) MB)..."
    & curl.exe -fsSL --retry 5 --retry-delay 10 --retry-all-errors `
        --connect-timeout 30 --max-time 1800 `
        -o $installer `
        $url

    $size = (Get-Item -LiteralPath $installer).Length
    if ($size -lt [int64]($expectedSize * 0.99)) {
        throw "LLVM download incomplete: $size bytes (expected ~$expectedSize)"
    }

    if (Test-Path $LLVM_ROOT) {
        Remove-Item -LiteralPath $LLVM_ROOT -Recurse -Force
    }

    Write-Host "Running silent installer -> $LLVM_ROOT"
    $proc = Start-Process -FilePath $installer -ArgumentList '/S', "/D=$LLVM_ROOT" -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        throw "LLVM installer failed with exit code $($proc.ExitCode)"
    }

    if (-not (Test-Path $clang)) {
        throw "clang.exe not found at $clang after install"
    }
}

& $clang --version

$clangUnix = ($clang -replace '\\', '/')
$clangxxUnix = ($clangUnix -replace 'clang\.exe$', 'clang++.exe')

foreach ($line in @(
        "PGO_CLANG=$clangUnix",
        "PGO_CLANGXX=$clangxxUnix",
        "CC=$clangUnix",
        "CXX=$clangxxUnix",
        "LLVM_ROOT=$($LLVM_ROOT -replace '\\', '/')"
    )) {
    Add-Content -Path $env:GITHUB_ENV -Value $line
}
Add-Content -Path $env:GITHUB_PATH -Value (Join-Path $LLVM_ROOT 'bin')
