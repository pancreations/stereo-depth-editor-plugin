param([string]$AESdk = '', [string]$PremiereSdk = '', [switch]$Cuda)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $AESdk) {
    $candidate = Join-Path $projectRoot 'AfterEffectsSDK_26.5_win'
    if (Test-Path -LiteralPath (Join-Path $candidate 'Examples\Headers\AE_Effect.h')) { $AESdk = $candidate }
}
if (-not $PremiereSdk) {
    $candidate = Join-Path $projectRoot 'Premiere Pro 26.0 C++ SDK'
    if (Test-Path -LiteralPath (Join-Path $candidate 'Examples\Headers\PrSDKPixelFormat.h')) { $PremiereSdk = $candidate }
}
if ([bool]$AESdk -ne [bool]$PremiereSdk) { throw 'Both Adobe SDK roots are required for the native effect.' }
Push-Location $projectRoot
try {
    $cmakeArgs = @('-S', '.', '-B', 'build', '-G', 'Visual Studio 17 2022', '-A', 'x64')
    if ($AESdk -and $PremiereSdk) {
        $cmakeArgs += @('-DS3D_BUILD_PREMIERE=ON', "-DAE_SDK_ROOT=$AESdk", "-DPREMIERE_SDK_ROOT=$PremiereSdk")
    } else { $cmakeArgs += '-DS3D_BUILD_PREMIERE=OFF' }
    $cmakeArgs += "-DS3D_BUILD_CUDA=$($Cuda.IsPresent.ToString().ToUpperInvariant())"
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build build --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Core tests failed.' }
} finally { Pop-Location }
