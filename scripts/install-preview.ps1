param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $projectRoot 'build\native\preview\Release\StereoDepthPreview.prm'
$folder = Join-Path $env:ProgramFiles 'Adobe\Common\Plug-ins\7.0\MediaCore\StereoDepth'
$target = Join-Path $folder 'StereoDepthPreview.prm'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw 'Build StereoDepthPreview first.' }
# A first installation adds a new file and can be staged while Premiere is open.
# Updating an already-loaded module requires the host to close first.
if ((Test-Path -LiteralPath $target) -and (Get-Process 'Adobe Premiere Pro','Adobe Media Encoder' -ErrorAction SilentlyContinue)) {
    throw 'Close Premiere and Media Encoder before updating SBS Preview.'
}
New-Item -ItemType Directory -Force -Path $folder | Out-Null
if (Test-Path -LiteralPath $target) { Copy-Item -LiteralPath $target -Destination "$target.previous" -Force }
Copy-Item -LiteralPath $source -Destination $target -Force
$expected = (Get-FileHash -LiteralPath $source).Hash
$actual = (Get-FileHash -LiteralPath $target).Hash
if ($expected -ne $actual) { throw 'SBS Preview installation hash mismatch.' }
@{ version='0.3.1'; component='SBS Preview'; sha256=$actual; installed_at=(Get-Date).ToString('o') } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $projectRoot 'artifacts\preview-install.json')
Write-Host 'SBS Preview installed. Restart Premiere and enable StereoDepth SBS Preview in Playback preferences.'
