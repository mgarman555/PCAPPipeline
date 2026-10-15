<#
.SYNOPSIS
  Rode receiver bring-up probe for docs/devices/rode.md (handoff §4.2(c)).

.DESCRIPTION
  Windows PowerShell 5.1 compatible. Observation only: records one short test file, never
  changes Windows audio settings.

    1. Finds the receiver: USB VID 0x19F7 (RODE) in PnP, and the matching dshow audio input
       (-Device overrides the match, e.g. to dry-run on another input).
    2. Reports the PnP names (model), Windows mic privacy for desktop apps, and the endpoint's
       Windows default format (the rate Unreal captures at before its fixed 44.1 kHz resample).
    3. Lists the dshow formats the input offers.
    4. Records -Seconds at 48 kHz / 24-bit to WAV. SPEAK OR TAP THE MIC while it runs.
    5. ffprobe + volumedetect: rate, channels, duration, mean/max level, silence / clipping flags.
    6. Writes summary-<stamp>.md to paste into the fact sheet.

  Output: <repo>\Saved\DeviceProbes\rode\ (gitignored).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File Tools\probes\rode_probe.ps1
  powershell -ExecutionPolicy Bypass -File Tools\probes\rode_probe.ps1 -Device "Analogue 1 + 2 (Focusrite USB Audio)"
#>
param(
  [string]$Device,
  [int]$Seconds = 10,
  [string]$FFmpegDir,
  [string]$OutDir
)

$ErrorActionPreference = 'Continue'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if (-not $OutDir) { $OutDir = Join-Path $repo 'Saved\DeviceProbes\rode' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logFile = Join-Path $OutDir "probe-$stamp.log"
function Log($m) { $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Output $line; Add-Content -Path $logFile -Value $line }

if ($FFmpegDir) { $ff = Join-Path $FFmpegDir 'ffmpeg.exe'; $fp = Join-Path $FFmpegDir 'ffprobe.exe' }
else { $ff = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source; $fp = (Get-Command ffprobe -ErrorAction SilentlyContinue).Source }
if (-not $ff -or -not (Test-Path $ff)) { throw 'ffmpeg not found: pass -FFmpegDir' }
function Run([string]$exe, [string]$argLine) { & cmd /c "`"$exe`" $argLine 2>&1" }

# ── 1. Find the receiver ───────────────────────────────────────────────────────
$rodePnp = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -match 'VID_19F7' })
if ($rodePnp.Count) { foreach ($p in $rodePnp) { Log "RODE PnP: '$($p.FriendlyName)' class=$($p.Class) status=$($p.Status) id=$($p.InstanceId)" } }
else { Log 'No USB VID_19F7 (RODE) device present. Check the cable is a DATA cable and the receiver is on.' }

$devOut = Run $ff '-hide_banner -list_devices true -f dshow -i dummy'
$audioDevs = @($devOut | ForEach-Object { if ($_ -match '"([^"]+)" \(audio\)') { $Matches[1] } })
Log "dshow audio inputs: $($audioDevs -join ' | ')"
if (-not $Device) { $Device = $audioDevs | Where-Object { $_ -match 'RODE|R.DE|Wireless' } | Select-Object -First 1 }
if (-not $Device) { Log 'No Rode audio input found. Pass -Device "<name>" from the list above to test another input.'; exit 2 }
Log "Input under test: '$Device'"

# ── 2. Privacy + Windows default format ────────────────────────────────────────
$cs = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone'
$priv = @{
  'HKLM microphone'        = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone' -ErrorAction SilentlyContinue).Value
  'HKCU microphone'        = (Get-ItemProperty $cs -ErrorAction SilentlyContinue).Value
  'HKCU desktop apps'      = (Get-ItemProperty "$cs\NonPackaged" -ErrorAction SilentlyContinue).Value }
foreach ($k in $priv.Keys) { Log "Privacy $k = $($priv[$k])" }
if ($priv.Values -contains 'Deny') { Log 'WARNING: microphone access is denied somewhere: recordings will be silent.' }

# Endpoint properties: {a45c254e-...},2 = endpoint name, {b3f8fa53-...},6 = interface (device) name,
# {f19f064d-...},0 = default format blob (8-byte header, then WAVEFORMATEX).
$defaultFmt = $null
$capRoot = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture'
foreach ($ep in Get-ChildItem $capRoot -ErrorAction SilentlyContinue) {
  $props = Get-ItemProperty (Join-Path $ep.PSPath 'Properties') -ErrorAction SilentlyContinue
  if (-not $props) { continue }
  $epName = $props.'{a45c254e-df1c-4efd-8020-67d146a850e0},2'
  $ifName = $props.'{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
  $full = "$epName ($ifName)"
  if ($full -ne $Device -and $Device -notlike "*$ifName*") { continue }
  $state = (Get-ItemProperty $ep.PSPath).DeviceState
  $blob = $props.'{f19f064d-082c-4e27-bc73-6882a1bb8e4c},0'
  if ($blob -and $blob.Length -ge 24) {
    $ch = [BitConverter]::ToUInt16($blob, 10); $sr = [BitConverter]::ToUInt32($blob, 12); $bits = [BitConverter]::ToUInt16($blob, 22)
    $defaultFmt = "$sr Hz, $bits-bit, $ch ch"
  }
  Log "Endpoint '$full' state=$state default format: $defaultFmt  key=$($ep.PSChildName)"
}
if (-not $defaultFmt) { Log 'Default format: not found in MMDevices (check Sound settings > device > Advanced).' }

# ── 3. dshow formats ───────────────────────────────────────────────────────────
$opt = Run $ff "-hide_banner -f dshow -list_options true -i audio=`"$Device`""
$opt | Set-Content (Join-Path $OutDir "list_options-$stamp.txt")
$opt | Where-Object { $_ -match 'ch=|channels|sample_rate|bits' } | ForEach-Object { Log "  $($_.Trim())" }

# ── 4. Record ──────────────────────────────────────────────────────────────────
$wav = Join-Path $OutDir "test_${Seconds}s_48k-$stamp.wav"
Log ">>> RECORDING $Seconds s now: speak or tap the transmitter mic <<<"
$rec = Run $ff "-hide_banner -y -f dshow -sample_rate 48000 -i audio=`"$Device`" -t $Seconds -c:a pcm_s24le `"$wav`""
$rec | Set-Content (Join-Path $OutDir "record-$stamp.log")
if (-not (Test-Path $wav)) { Log 'Recording FAILED (see record log).'; exit 3 }

# ── 5. Inspect ─────────────────────────────────────────────────────────────────
$st = Run $fp "-v error -show_entries stream=sample_rate,channels,bits_per_raw_sample,codec_name:format=duration -of default=nw=1 `"$wav`""
$info = @{}; foreach ($l in $st) { if ($l -match '^(\w+)=(.*)$') { $info[$Matches[1]] = $Matches[2] } }
Log "File: $($info.codec_name) $($info.sample_rate) Hz, $($info.channels) ch, $($info.duration) s"
$vd = Run $ff "-hide_banner -i `"$wav`" -af volumedetect -f null -"
$mean = ($vd | Where-Object { $_ -match 'mean_volume: (-?[\d.]+|-inf)' } | ForEach-Object { $Matches[1] }) | Select-Object -First 1
$max  = ($vd | Where-Object { $_ -match 'max_volume: (-?[\d.]+|-inf)' }  | ForEach-Object { $Matches[1] }) | Select-Object -First 1
Log "Level: mean $mean dB, max $max dB"
$verdict = 'OK'
if ($max -eq '-inf' -or [double]$max -lt -50) { $verdict = 'SILENT: no signal (transmitter unpaired/muted, wrong channel, or privacy).' }
elseif ([double]$max -ge -0.1) { $verdict = 'CLIPPING: lower the transmitter gain.' }
elseif ([double]$max -lt -30) { $verdict = 'LOW: signal present but quiet; check gain / mic placement.' }
Log "Verdict: $verdict"

# ── 6. Summary ─────────────────────────────────────────────────────────────────
@("# Rode probe $stamp", '',
  "RODE PnP: $(if ($rodePnp.Count) { ($rodePnp | ForEach-Object { "``$($_.FriendlyName)`` ($($_.InstanceId))" }) -join '; ' } else { 'none (VID_19F7 not present)' })  ",
  "Input: ``$Device``  ", "Windows default format: $defaultFmt  ",
  "Privacy: $(($priv.Keys | ForEach-Object { "$_=$($priv[$_])" }) -join ', ')  ",
  "Test file: $($info.sample_rate) Hz, $($info.channels) ch, $($info.duration) s, mean $mean dB, max $max dB  ",
  "Verdict: **$verdict**", '',
  'Unreal records this input at 44.1 kHz / 16-bit mono regardless (rode.md §4.3).',
  "Files: ``$OutDir``") | Set-Content (Join-Path $OutDir "summary-$stamp.md") -Encoding UTF8
Log "Summary: $(Join-Path $OutDir "summary-$stamp.md")"
