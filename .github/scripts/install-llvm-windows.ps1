# Install an LLVM release (NSIS) on Windows CI.
# Usage: pwsh -File .github/scripts/install-llvm-windows.ps1
#
# Optional environment contracts:
#   LLVM_VER        exact release to install (otherwise resolve latest)
#   LLVM_SHA256     expected installer digest
#   LLVM_TOOLS_ONLY expose LLVM_ROOT without selecting clang or changing PATH
$ErrorActionPreference = 'Stop'

function Resolve-LlvmVersion {
    if ($env:LLVM_VER) {
        return $env:LLVM_VER
    }

    # Avoid GitHub REST API rate limits; use releases/latest redirect.
    $latestUrl = (& curl.exe -fsSL -o $null -w '%{url_effective}' `
        'https://github.com/llvm/llvm-project/releases/latest').Trim()
    if ($latestUrl -notmatch '/llvmorg-(\d+\.\d+\.\d+)$') {
        throw "unexpected latest release URL: $latestUrl"
    }
    $ver = $Matches[1]
    Write-Host "Resolved LLVM_VER=$ver (from releases/latest redirect)"
    $env:LLVM_VER = $ver
    if ($env:GITHUB_ENV) {
        Add-Content -Path $env:GITHUB_ENV -Value "LLVM_VER=$ver"
    }
    return $ver
}

function Get-InstallerContentLength {
    param([string]$Url)
    $output = & curl.exe -fsSLI $Url 2>$null | Select-String -Pattern '^[Cc]ontent-[Ll]ength:\s*(\d+)$'
    if ($output -and $output.Matches[0].Groups[1].Success) {
        return [int64]$output.Matches[0].Groups[1].Value
    }
    return 0
}

$LLVM_VER = Resolve-LlvmVersion
$LLVM_ROOT = if ($env:LLVM_ROOT) { $env:LLVM_ROOT } else { 'C:\LLVM' }
$clang = Join-Path $LLVM_ROOT 'bin\clang.exe'

if (Test-Path $clang) {
    Write-Host "LLVM already present at $LLVM_ROOT"
} else {
    $url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VER}/LLVM-${LLVM_VER}-win64.exe"
    $expectedSize = Get-InstallerContentLength -Url $url
    $installer = Join-Path $env:RUNNER_TEMP "llvm-$LLVM_VER-win64.exe"

    if ($expectedSize -gt 0) {
        Write-Host "Downloading LLVM $LLVM_VER ($([math]::Round($expectedSize / 1MB)) MB)..."
    } else {
        Write-Host "Downloading LLVM $LLVM_VER..."
    }
    & curl.exe -fsSL --retry 5 --retry-delay 10 --retry-all-errors `
        --connect-timeout 30 --max-time 1800 `
        -o $installer `
        $url
    if ($LASTEXITCODE -ne 0) {
        throw "LLVM installer download failed with curl exit code $LASTEXITCODE"
    }

    $size = (Get-Item -LiteralPath $installer).Length
    if ($expectedSize -gt 0 -and $size -lt [int64]($expectedSize * 0.99)) {
        throw "LLVM download incomplete: $size bytes (expected ~$expectedSize)"
    }
    if ($env:LLVM_SHA256) {
        $expectedSha256 = $env:LLVM_SHA256.Trim().ToLowerInvariant()
        if ($expectedSha256 -notmatch '^[0-9a-f]{64}$') {
            throw "LLVM_SHA256 must be exactly 64 hexadecimal characters"
        }
        $actualSha256 = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualSha256 -ne $expectedSha256) {
            throw "LLVM installer SHA-256 mismatch: got $actualSha256, expected $expectedSha256"
        }
        Write-Host "Verified LLVM installer SHA-256: $actualSha256"
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
$toolsOnly = $env:LLVM_TOOLS_ONLY -in @('1', 'true', 'True', 'TRUE')

$environment = @("LLVM_ROOT=$($LLVM_ROOT -replace '\\', '/')")
if (-not $toolsOnly) {
    $environment += @(
        "PGO_CLANG=$clangUnix",
        "PGO_CLANGXX=$clangxxUnix",
        "CC=$clangUnix",
        "CXX=$clangxxUnix"
    )
    Add-Content -Path $env:GITHUB_PATH -Value (Join-Path $LLVM_ROOT 'bin')
}
foreach ($line in $environment) {
    Add-Content -Path $env:GITHUB_ENV -Value $line
}
