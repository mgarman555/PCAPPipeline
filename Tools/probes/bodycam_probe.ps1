<#
.SYNOPSIS
  Body camera bring-up probe for docs/devices/body-camera.md (handoff §4.2(b)).

.DESCRIPTION
  Windows PowerShell 5.1 compatible. Observation only: records short test clips, never changes
  camera or Windows settings.

    1. Lists dshow video devices and picks the camera (-Device, or the first real one that is not
       a virtual camera).
    2. Lists every mode the camera offers (ffmpeg -list_options).
    3. For each candidate (4K / 1440p / 1080p / 720p at 60 and 30 when offered), stream-copies
       -Seconds of video with no re-encode and counts REAL frames: packets under 100 bytes are the
       empty-frame padding some devices use to fake 60 fps (seen on the MS2109 at 1080p60).
       Also checks mean luma, so blank frames (privacy off, lens cap, no signal) are caught.
    4. Picks the mode: the highest resolution that really delivers >= 59 fps
       (handoff: 4K60 if real, else 1080p60 over 4K30).
    5. Runs the one-owner recipe on that mode: H.264 CRF 18 recording plus a 10 fps UDP MJPEG
       preview, read by a second ffmpeg, and reports recording fps and preview frames.
    6. Writes summary-<stamp>.md to paste into the fact sheet.

  Output: <repo>\Saved\DeviceProbes\body-camera\ (gitignored).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File Tools\probes\bodycam_probe.ps1
  powershell -ExecutionPolicy Bypass -File Tools\probes\bodycam_probe.ps1 -Device "Logitech BRIO" -Seconds 10
  powershell -ExecutionPolicy Bypass -File Tools\probes\bodycam_probe.ps1 -ListOnly
#>
param(
  [string]$Device,
  [int]$Seconds = 10,
  [switch]$ListOnly,
  [switch]$SkipRecipe,
  [string]$FFmpegDir,
  [string]$OutDir
)

$ErrorActionPreference = 'Continue'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if (-not $OutDir) { $OutDir = Join-Path $repo 'Saved\DeviceProbes\body-camera' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$run = Join-Path $OutDir "run-$stamp"; New-Item -ItemType Directory -Force -Path $run | Out-Null
$logFile = Join-Path $run 'probe.log'
function Log($m) { $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Output $line; Add-Content -Path $logFile -Value $line }

# ffmpeg / ffprobe: -FFmpegDir, else PATH (CLAUDE.local.md lists the winget install).
if ($FFmpegDir) { $ff = Join-Path $FFmpegDir 'ffmpeg.exe'; $fp = Join-Path $FFmpegDir 'ffprobe.exe' }
else { $ff = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source; $fp = (Get-Command ffprobe -ErrorAction SilentlyContinue).Source }
if (-not $ff -or -not (Test-Path $ff)) { throw 'ffmpeg not found: pass -FFmpegDir' }
Log "ffmpeg: $ff"

# Run a native tool through cmd so stderr merges without PowerShell 5.1 NativeCommandError noise.
function Run([string]$exe, [string]$argLine) { & cmd /c "`"$exe`" $argLine 2>&1" }

# ── 1. Devices ─────────────────────────────────────────────────────────────────
$devOut = Run $ff '-hide_banner -list_devices true -f dshow -i dummy'
$videoDevs = @($devOut | ForEach-Object { if ($_ -match '"([^"]+)" \(video\)') { $Matches[1] } })
Log "dshow video devices: $($videoDevs -join ' | ')"
$virtual = 'OBS Virtual Camera','LSVCam','Camo','NVIDIA Broadcast','Snap Camera','XSplit','ManyCam'
if (-not $Device) {
  $Device = $videoDevs | Where-Object { $d = $_; -not ($virtual | Where-Object { $d -like "*$_*" }) } | Select-Object -First 1
}
if (-not $Device) { Log 'No real video device found. Plug the webcam straight into a USB 3 port and re-run.'; exit 2 }
Log "Camera under test: '$Device'"

$pnp = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -eq $Device }
foreach ($p in $pnp) { Log "PnP: $($p.FriendlyName) class=$($p.Class) id=$($p.InstanceId)" }
if ($pnp | Where-Object { $_.InstanceId -match 'VID_534D&PID_2109' }) {
  Log 'WARNING: this is the MacroSilicon MS2109 HDMI capture dongle, not a webcam (see body-camera.md §1).'
}

# ── 2. Modes ───────────────────────────────────────────────────────────────────
$optOut = Run $ff "-hide_banner -f dshow -list_options true -i video=`"$Device`""
$optOut | Set-Content (Join-Path $run 'list_options.txt')
$modes = foreach ($l in $optOut) {
  if ($l -match '(vcodec|pixel_format)=(\S+)\s+min s=(\d+)x(\d+) fps=([\d.]+) max s=(\d+)x(\d+) fps=([\d.]+)') {
    [pscustomobject]@{ Kind = $Matches[1]; Fmt = $Matches[2]; W = [int]$Matches[6]; H = [int]$Matches[7]
                       MinFps = [double]$Matches[5]; MaxFps = [double]$Matches[8] }
  }
}
$modes = @($modes | Sort-Object Kind, Fmt, W, H, MaxFps -Unique)
Log "Modes offered: $($modes.Count)"
foreach ($m in ($modes | Sort-Object { -$_.W * $_.H }, Kind)) { Log ("  {0}={1} {2}x{3} fps {4}-{5}" -f $m.Kind, $m.Fmt, $m.W, $m.H, $m.MinFps, $m.MaxFps) }
if ($ListOnly) { exit 0 }

# ── 3. Measure real delivered fps per candidate ────────────────────────────────
# Prefer compressed input (mjpeg, then h264) at each size: raw YUY2 rarely sustains 60 over USB.
function Input-Args($m) { if ($m.Kind -eq 'vcodec') { "-vcodec $($m.Fmt)" } else { "-pixel_format $($m.Fmt)" } }
$sizes = @(@(3840,2160), @(2560,1440), @(1920,1080), @(1280,720))
$cands = @()
foreach ($s in $sizes) {
  $atSize = $modes | Where-Object { $_.W -eq $s[0] -and $_.H -eq $s[1] }
  if (-not $atSize) { Log "  $($s[0])x$($s[1]): not offered"; continue }
  $best = $atSize | Sort-Object @{e={ switch ($_.Fmt) { 'mjpeg' {0} 'h264' {1} default {2} } }}, @{e={-$_.MaxFps}} | Select-Object -First 1
  foreach ($fps in 60, 30) {
    if ($best.MaxFps -ge ($fps - 0.5) -and $best.MinFps -le ($fps + 0.5)) { $cands += [pscustomobject]@{ Mode = $best; Fps = $fps } }
  }
}

$results = @()
foreach ($c in $cands) {
  $m = $c.Mode; $tag = "$($m.W)x$($m.H)p$($c.Fps)_$($m.Fmt)"
  $file = Join-Path $run "copy_$tag.mkv"
  Log "Measuring $tag for $Seconds s (stream copy, no re-encode)..."
  $recLog = Run $ff "-hide_banner -y -f dshow -rtbufsize 1024M $(Input-Args $m) -video_size $($m.W)x$($m.H) -framerate $($c.Fps) -i video=`"$Device`" -t $Seconds -c:v copy `"$file`""
  $recLog | Set-Content (Join-Path $run "copy_$tag.log")
  if (-not (Test-Path $file) -or (Get-Item $file).Length -lt 1024) { Log "  FAILED to record (see copy_$tag.log)"; continue }

  $pk = Run $fp "-v error -select_streams v:0 -show_entries packet=pts_time,size -of csv=p=0 `"$file`""
  $rows = @($pk | Where-Object { $_ -match '^[\d.]+,\d+$' } | ForEach-Object { $a = $_ -split ','; [pscustomobject]@{ t = [double]$a[0]; size = [int]$a[1] } })
  if ($rows.Count -lt 2) { Log '  no packets'; continue }
  $span = ($rows[-1].t - $rows[0].t)
  $real = @($rows | Where-Object { $_.size -ge 100 }).Count
  $empty = $rows.Count - $real
  $realFps = if ($span -gt 0) { ($real - 1) / $span } else { 0 }

  # Mean luma over the first ~2 s, to catch black / blank frames.
  $sig = Run $ff "-hide_banner -i `"$file`" -frames:v 120 -vf signalstats,metadata=print:key=lavfi.signalstats.YAVG -f null -"
  $yav = @($sig | ForEach-Object { if ($_ -match 'YAVG=([\d.]+)') { [double]$Matches[1] } })
  $luma = if ($yav.Count) { ($yav | Measure-Object -Average).Average } else { -1 }

  $r = [pscustomobject]@{ Tag = $tag; W = $m.W; H = $m.H; Asked = $c.Fps; Fmt = $m.Fmt; Packets = $rows.Count; Real = $real; Empty = $empty
                          Span = [math]::Round($span, 3); RealFps = [math]::Round($realFps, 2); Luma = [math]::Round($luma, 1) }
  $results += $r
  Log ("  {0}: {1} packets ({2} real, {3} empty) over {4}s = {5} real fps, mean luma {6}" -f $tag, $r.Packets, $r.Real, $r.Empty, $r.Span, $r.RealFps, $r.Luma)
  if ($luma -ge 0 -and $luma -lt 12) { Log '  WARNING: frames are black. Check lens cap / lighting / Windows Settings > Privacy > Camera (desktop apps).' }
}

# ── 4. Pick the mode ───────────────────────────────────────────────────────────
$pick = $results | Where-Object { $_.RealFps -ge 59 -and $_.Luma -ne -1 } | Sort-Object { -$_.W * $_.H } | Select-Object -First 1
if ($pick) { Log "RECOMMENDED: $($pick.W)x$($pick.H) @ 60 ($($pick.Fmt)), measured $($pick.RealFps) real fps" }
else {
  Log 'NO mode delivers a real 60 fps. Capture Setup must show amber (Epic target is 60 fps).'
  # Fallback: the largest mode that holds its own asked rate (no dropped frames), so the recipe still runs.
  $pick = $results | Where-Object { $_.RealFps -ge ($_.Asked * 0.98) -and $_.Luma -ne -1 } | Sort-Object { -$_.W * $_.H } | Select-Object -First 1
  if ($pick) { Log "FALLBACK: $($pick.W)x$($pick.H) @ $($pick.Asked) ($($pick.Fmt)), measured $($pick.RealFps) real fps (steady, below Epic's 60)" }
}
$pickFps = if ($pick) { $pick.Asked } else { 60 }

# ── 5. One-owner recipe on the chosen mode ─────────────────────────────────────
$recipe = $null
if ($pick -and -not $SkipRecipe) {
  $pm = $modes | Where-Object { $_.W -eq $pick.W -and $_.H -eq $pick.H -and $_.Fmt -eq $pick.Fmt } | Select-Object -First 1
  $recFile = Join-Path $run 'recipe_body.mp4'; $pvFile = Join-Path $run 'recipe_preview.mjpeg'
  $port = 5005
  $reader = Start-Process -FilePath $ff -PassThru -WindowStyle Hidden -RedirectStandardError (Join-Path $run 'recipe_reader.log') -ArgumentList @(
    '-hide_banner','-y','-f','mjpeg','-framerate','10','-i',"udp://127.0.0.1:${port}?fifo_size=1000000&overrun_nonfatal=1",'-t','8','-c','copy',"`"$pvFile`"")
  Start-Sleep -Milliseconds 500
  Log "Recipe: recording $($pick.W)x$($pick.H)@$pickFps libx264 CRF 18 + 10 fps UDP preview for $($Seconds + 2) s..."
  $recLog = Run $ff ("-hide_banner -y -f dshow -rtbufsize 1024M $(Input-Args $pm) -video_size $($pick.W)x$($pick.H) -framerate $pickFps -t $($Seconds + 2) -i video=`"$Device`" " +
    "-filter_complex `"[0:v]split=2[rec][pv];[pv]fps=10,scale=640:-2[pvo]`" " +
    "-map `"[rec]`" -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p -g $pickFps -flush_packets 1 -movflags +frag_keyframe+empty_moov+default_base_moof `"$recFile`" " +
    "-map `"[pvo]`" -c:v mjpeg -q:v 5 -f mjpeg `"udp://127.0.0.1:${port}?pkt_size=1316`"")
  $recLog | Set-Content (Join-Path $run 'recipe_record.log')
  if (-not $reader.WaitForExit(15000)) { $reader.Kill() }
  $probe = Run $fp "-v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames:format=duration -of default=nw=1 `"$recFile`""
  $frames = [int](($probe | Where-Object { $_ -match 'nb_read_frames=' }) -replace '\D','')
  $dur = [double](($probe | Where-Object { $_ -match 'duration=' }) -replace 'duration=','')
  $pvBytes = if (Test-Path $pvFile) { (Get-Item $pvFile).Length } else { 0 }
  # Frames the reader actually received, from its own progress line (the file may be unflushed if it was killed).
  $rl = Get-Content (Join-Path $run 'recipe_reader.log') -Raw -ErrorAction SilentlyContinue
  $pvFrames = if ($rl) { ([regex]::Matches($rl, 'frame=\s*(\d+)') | Select-Object -Last 1 | ForEach-Object { [int]$_.Groups[1].Value }) } else { 0 }
  $speed = ($recLog | Where-Object { $_ -match 'speed=' } | Select-Object -Last 1)
  $recFps = if ($dur -gt 0) { [math]::Round($frames / $dur, 2) } else { 0 }
  $recipe = [pscustomobject]@{ Frames = $frames; Duration = $dur; RecFps = $recFps; PreviewFrames = $pvFrames; Speed = $speed }
  Log "  recording: $frames frames / $dur s = $recFps fps; preview frames received: $pvFrames (~8 s at 10 fps = ~80 expected)"
  Log "  encoder: $speed"
  if ($recFps -lt ($pickFps - 1)) { Log "  WARNING: encoder did not hold $pickFps fps at this size. Try -preset ultrafast or h264_nvenc (note: nvenc has no true CRF)." }
}

# ── 6. Summary ─────────────────────────────────────────────────────────────────
$md = @("# Body camera probe $stamp", '', "Device: ``$Device``  ", "ffmpeg: ``$ff``", '',
        '| Mode | Asked fps | Packets | Real | Empty | Span s | Real fps | Mean luma |', '|---|---|---|---|---|---|---|---|')
$md += $results | ForEach-Object { "| $($_.W)x$($_.H) $($_.Fmt) | $($_.Asked) | $($_.Packets) | $($_.Real) | $($_.Empty) | $($_.Span) | **$($_.RealFps)** | $($_.Luma) |" }
$md += ''
$md += if ($pick -and $pickFps -ge 59) { "Recommended: **$($pick.W)x$($pick.H) @ 60 ($($pick.Fmt))**" } elseif ($pick) { "**No mode delivers a real 60 fps.** Fallback: **$($pick.W)x$($pick.H) @ $pickFps ($($pick.Fmt))**, steady." } else { '**No mode delivers a real 60 fps.**' }
if ($recipe) { $md += "Recipe (CRF 18 + UDP preview): $($recipe.Frames) frames / $($recipe.Duration) s = **$($recipe.RecFps) fps**, preview frames $($recipe.PreviewFrames). $($recipe.Speed)" }
$md += '', "Raw files and logs: ``$run``"
$md | Set-Content (Join-Path $OutDir "summary-$stamp.md") -Encoding UTF8
Log "Summary: $(Join-Path $OutDir "summary-$stamp.md")"
