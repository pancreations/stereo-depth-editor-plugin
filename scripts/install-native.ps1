param([string]$PluginPath = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $PluginPath) { $PluginPath = Join-Path $projectRoot 'build\native\premiere\Release\StereoDepth.prm' }
if (-not (Test-Path -LiteralPath $PluginPath -PathType Leaf)) {
    throw 'StereoDepth.prm does not exist. Build with both Adobe SDK paths first.'
}
if (Get-Process 'Adobe Premiere Pro','Adobe Media Encoder' -ErrorAction SilentlyContinue) {
    throw 'Close Premiere and Media Encoder before installing the effect.'
}
$destination = Join-Path $env:ProgramFiles 'Adobe\Common\Plug-ins\7.0\MediaCore\StereoDepth'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$target = Join-Path $destination 'StereoDepth.prm'
if (Test-Path -LiteralPath $target) {
    Copy-Item -LiteralPath $target -Destination "$target.previous" -Force
}
Copy-Item -LiteralPath (Resolve-Path -LiteralPath $PluginPath).Path -Destination $target -Force
$nativeHash = (Get-FileHash -LiteralPath $target).Hash
if ($nativeHash -ne (Get-FileHash -LiteralPath $PluginPath).Hash) { throw 'Effect installation hash mismatch.' }
@{version='0.3.1'; component='2D to 3D'; sha256=$nativeHash; installed_at=(Get-Date).ToString('o')} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $projectRoot 'artifacts\native-install.json')
$previewPath = Join-Path $projectRoot 'build\native\preview\Release\StereoDepthPreview.prm'
if (Test-Path -LiteralPath $previewPath) { & (Join-Path $PSScriptRoot 'install-preview.ps1') }
Write-Host "Installed $target. Restart Premiere, then find StereoDepth > 2D to 3D in Effects."
