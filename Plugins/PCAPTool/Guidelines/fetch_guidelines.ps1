# Downloads Epic's MetaHuman capture-guideline reference images listed in manifest.json
# into Guidelines/images/<id>.<ext>. Run on the P620 (Windows PowerShell 5+ or pwsh):
#
#   powershell -ExecutionPolicy Bypass -File Plugins\PCAPTool\Guidelines\fetch_guidelines.ps1
#
# The images are Epic documentation content: images/ is gitignored and must never be
# committed (this repo is public). Re-run any time; existing files are skipped unless -Force.

param([switch]$Force)

$ErrorActionPreference = 'Stop'
$here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifest = Get-Content -Raw -Path (Join-Path $here 'manifest.json') | ConvertFrom-Json
$outDir   = Join-Path $here 'images'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$extFor = @{ 'image/png'='png'; 'image/jpeg'='jpg'; 'image/webp'='webp'; 'image/gif'='gif' }
$ok = 0; $skipped = 0; $failed = @()

foreach ($img in $manifest.images) {
    $existing = Get-ChildItem -Path $outDir -Filter "$($img.id).*" -ErrorAction SilentlyContinue
    if ($existing -and -not $Force) { $skipped++; continue }

    $tmp = Join-Path $outDir "$($img.id).download"
    try {
        $resp = Invoke-WebRequest -Uri $img.url -OutFile $tmp -PassThru -UseBasicParsing -TimeoutSec 60
        $ctype = ($resp.Headers['Content-Type'] -split ';')[0].Trim()
        $ext = $extFor[$ctype]; if (-not $ext) { $ext = 'png' }
        Move-Item -Force -Path $tmp -Destination (Join-Path $outDir "$($img.id).$ext")
        $ok++
    } catch {
        if (Test-Path $tmp) { Remove-Item $tmp -Force }
        $failed += $img.id
    }
}

Write-Host "Guidelines: $ok downloaded, $skipped already present, $($failed.Count) failed."
if ($failed.Count) { Write-Host "Failed: $($failed -join ', ')"; exit 1 }
