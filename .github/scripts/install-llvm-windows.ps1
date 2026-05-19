# Install LLVM release build on Windows CI (NSIS installer).
# Usage: pwsh -File .github/scripts/install-llvm-windows.ps1
$ErrorActionPreference = 'Stop'

$LLVM_VER = if ($env:LLVM_VER) { $env:LLVM_VER } else { '20.1.8' }
$LLVM_ROOT = if ($env:LLVM_ROOT) { $env:LLVM_ROOT } else { 'C:\LLVM' }
# From https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8
$ExpectedSize = 385658654

$clang = Join-Path $LLVM_ROOT 'bin\clang.exe'
if (Test-Path $clang) {
    Write-Host "LLVM already present at $LLVM_ROOT"
} else {
    $url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VER/LLVM-$LLVM_VER-win64.exe"
    $installer = Join-Path $env:RUNNER_TEMP "llvm-$LLVM_VER-win64.exe"

    Write-Host "Downloading LLVM $LLVM_VER (~$([math]::Round($ExpectedSize / 1MB)) MB)..."
    # curl.exe ships with windows-latest; more reliable than Git Bash curl for large binaries.
    # ~386 MB; allow up to 30 min then fail instead of hanging the whole job.
    & curl.exe -fsSL --retry 5 --retry-delay 10 --retry-all-errors `
        --connect-timeout 30 --max-time 1800 `
        -o $installer `
        $url

    $size = (Get-Item -LiteralPath $installer).Length
    if ($size -lt [int64]($ExpectedSize * 0.99)) {
        throw "LLVM download incomplete: $size bytes (expected ~$ExpectedSize)"
    }

    if (Test-Path $LLVM_ROOT) {
        Remove-Item -LiteralPath $LLVM_ROOT -Recurse -Force
    }

    Write-Host "Running silent installer -> $LLVM_ROOT"
    # NSIS: /D= must be last; no quotes; no trailing backslash.
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
        "CXX=$clangxxUnix"
    )) {
    Add-Content -Path $env:GITHUB_ENV -Value $line
}
Add-Content -Path $env:GITHUB_PATH -Value (Join-Path $LLVM_ROOT 'bin')
