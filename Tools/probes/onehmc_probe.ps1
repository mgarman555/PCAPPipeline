<#
.SYNOPSIS
  OneHMC (Soma recorder) discovery and read-only probe. Observation tool for docs/devices/onehmc.md.

.DESCRIPTION
  Windows PowerShell 5.1 compatible (Python is not installed on the P620).
  This script NEVER starts a recording, changes a setting, or touches firmware. It only:
    Discover : ping-sweeps the local IPv4 /24 of the chosen adapter, probes TCP 80/8080/9000 on
               responders and ARP neighbours, prints HTTP Server header + <title>, and runs a
               legacy-unicast mDNS browse (_services._dns-sd._udp, _http._tcp, _workstation._tcp,
               _device-info._tcp) to list *.local names (looking for soma-xxxx.local).
    Inspect  : for -Target <host or IP>, GETs / on :9000, :8080 and :80, saves the HTML and every
               referenced .js/.css into the output folder, and greps the JS for URL-like strings
               (endpoint candidates) and keywords (firmware, version, stereo, mono, record, ...).
    Download : times a single GET of -Url into the output folder and reports MB, seconds, MB/s.
               (Use only on a file URL already observed in the GUI's own network requests.)

  Output goes under <repo>\Saved\DeviceProbes\onehmc\ (gitignored). Never into the tracked tree.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File Tools\probes\onehmc_probe.ps1 -Mode Discover
  powershell -ExecutionPolicy Bypass -File Tools\probes\onehmc_probe.ps1 -Mode Inspect -Target soma-1234.local
  powershell -ExecutionPolicy Bypass -File Tools\probes\onehmc_probe.ps1 -Mode Download -Url http://<ip>:9000/<observed path>
#>
param(
  [ValidateSet('Discover','Inspect','Download')] [string]$Mode = 'Discover',
  [string]$Target,
  [string]$Url,
  [string]$InterfaceAlias,
  [string]$OutDir
)

$ErrorActionPreference = 'Continue'
if (-not $OutDir) {
  # Tools\probes -> repo root (works from the main checkout; from a worktree, pass -OutDir explicitly
  # pointing at the MAIN repo's Saved\DeviceProbes\onehmc).
  $repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
  $OutDir = Join-Path $repo 'Saved\DeviceProbes\onehmc'
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
function Log($m) { $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m; Write-Output $line; Add-Content -Path (Join-Path $OutDir "probe-$stamp.log") -Value $line }

function Test-Port([string]$h, [int]$p, [int]$ms = 600) {
  $c = New-Object Net.Sockets.TcpClient
  try { $a = $c.BeginConnect($h, $p, $null, $null); return ($a.AsyncWaitHandle.WaitOne($ms) -and $c.Connected) } catch { return $false } finally { $c.Close() }
}

function Get-HttpSummary([string]$u) {
  $hdr = & curl.exe -s -m 5 -I $u 2>$null
  $body = (& curl.exe -s -m 5 $u 2>$null) -join "`n"
  $server = ($hdr | Where-Object { $_ -match '^(Server|HTTP/)' }) -join ' | '
  $title = if ($body -match '<title>(.*?)</title>') { $Matches[1] } else { '' }
  return "$server | title='$title' | bytes=$($body.Length)"
}

function Invoke-MdnsBrowse([int]$listenMs = 3000) {
  # Legacy unicast query (source port != 5353): RFC 6762 s6.7 says responders reply by unicast to us,
  # so this does not need to bind 5353 (which the Windows DNS client already holds).
  $services = '_services._dns-sd._udp.local','_http._tcp.local','_workstation._tcp.local','_device-info._tcp.local','_soma._tcp.local'
  $udp = New-Object Net.Sockets.UdpClient(0)
  $udp.Client.ReceiveTimeout = 500
  $ep = New-Object Net.IPEndPoint([Net.IPAddress]::Parse('224.0.0.251'), 5353)
  foreach ($svc in $services) {
    $q = New-Object System.Collections.Generic.List[byte]
    $q.AddRange([byte[]](0,0, 0,0, 0,1, 0,0, 0,0, 0,0))
    foreach ($label in $svc.Split('.')) { $b = [Text.Encoding]::ASCII.GetBytes($label); $q.Add([byte]$b.Length); $q.AddRange($b) }
    $q.AddRange([byte[]](0, 0,12, 0,1))   # QTYPE PTR, QCLASS IN
    [void]$udp.Send($q.ToArray(), $q.Count, $ep)
  }
  $seen = @{}
  $deadline = (Get-Date).AddMilliseconds($listenMs)
  while ((Get-Date) -lt $deadline) {
    try {
      $from = New-Object Net.IPEndPoint([Net.IPAddress]::Any, 0)
      $data = $udp.Receive([ref]$from)
      # Crude but sufficient for discovery: pull printable label runs out of the packet.
      $txt = -join ($data | ForEach-Object { if ($_ -ge 32 -and $_ -lt 127) { [char]$_ } else { '|' } })
      $names = [regex]::Matches($txt, '[A-Za-z0-9_\-\. ]{3,}') | ForEach-Object { $_.Value.Trim() } | Where-Object { $_ } | Select-Object -Unique
      $key = "$($from.Address) $($names -join ',')"
      if (-not $seen.ContainsKey($key)) { $seen[$key] = 1; Log "mDNS reply from $($from.Address): $($names -join ' ; ')" }
    } catch { }
  }
  $udp.Close()
  if ($seen.Count -eq 0) { Log 'mDNS: no replies' }
}

switch ($Mode) {
  'Discover' {
    $ip = Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254*' -and $_.InterfaceAlias -notlike '*Tailscale*' -and $_.PrefixLength -eq 24 }
    if ($InterfaceAlias) { $ip = $ip | Where-Object InterfaceAlias -eq $InterfaceAlias }
    foreach ($a in $ip) {
      $prefix = ($a.IPAddress -split '\.')[0..2] -join '.'
      Log "Sweeping $prefix.0/24 on $($a.InterfaceAlias) (self $($a.IPAddress))"
      $pings = 1..254 | ForEach-Object { $p = New-Object Net.NetworkInformation.Ping; [pscustomobject]@{ ip = "$prefix.$_"; t = $p.SendPingAsync("$prefix.$_", 800) } }
      [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($pings | ForEach-Object { $_.t }))
      $up = @($pings | Where-Object { $_.t.Result.Status -eq 'Success' } | ForEach-Object ip)
      $arp = @(Get-NetNeighbor -AddressFamily IPv4 -InterfaceAlias $a.InterfaceAlias -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -like "$prefix.*" -and $_.State -notin 'Unreachable','Permanent','Incomplete' -and $_.LinkLayerAddress -and $_.LinkLayerAddress -ne '00-00-00-00-00-00' })
      Log "Ping responders: $($up -join ', ')"
      foreach ($n in $arp) { Log "ARP $($n.IPAddress) $($n.LinkLayerAddress) $($n.State)" }
      $hosts = @($up + ($arp | ForEach-Object IPAddress)) | Where-Object { $_ -ne $a.IPAddress } | Sort-Object -Unique
      foreach ($h in $hosts) {
        foreach ($port in 80, 8080, 9000) {
          if (Test-Port $h $port) { Log "OPEN ${h}:$port  $(Get-HttpSummary "http://${h}:$port/")" }
        }
      }
    }
    foreach ($n in 'soma.local','soma','onehmc.local','synapse.local') {
      try { $r = Resolve-DnsName $n -QuickTimeout -ErrorAction Stop | Where-Object IPAddress; Log "resolve $n -> $($r.IPAddress -join ',')" } catch { Log "resolve $n -> fail ($($_.Exception.Message))" }
    }
    Invoke-MdnsBrowse
  }
  'Inspect' {
    if (-not $Target) { throw '-Target required' }
    $web = Join-Path $OutDir "web-$stamp"; New-Item -ItemType Directory -Force -Path $web | Out-Null
    foreach ($port in 9000, 8080, 80) {
      $base = "http://${Target}:$port"
      if (-not (Test-Port $Target $port 1500)) { Log "$base closed"; continue }
      Log "$base open: $(Get-HttpSummary "$base/")"
      $html = Join-Path $web "index-$port.html"
      & curl.exe -s -m 10 -D (Join-Path $web "index-$port.headers.txt") -o $html "$base/"
      $refs = [regex]::Matches((Get-Content $html -Raw), '(?:src|href)\s*=\s*"([^"]+\.(?:js|css|json|webmanifest)(?:\?[^"]*)?)"') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique
      foreach ($r in $refs) {
        if ($r -match '^https?://') { Log "external ref (not fetched): $r"; continue }
        $leaf = ($r -replace '[\\/:*?"<>|]', '_')
        & curl.exe -s -m 30 -o (Join-Path $web "$port-$leaf") "$base/$($r.TrimStart('/'))"
        Log "fetched $base/$($r.TrimStart('/'))"
      }
    }
    Get-ChildItem $web -Filter '*.js' | ForEach-Object {
      $js = Get-Content $_.FullName -Raw
      $urls = [regex]::Matches($js, '["''`](/[A-Za-z0-9_\-/\.\?=&{}$]{2,80}|wss?://[^"''`]{3,80}|https?://[^"''`]{3,80})["''`]') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
      Log "== $($_.Name): $($urls.Count) URL-like strings"
      $urls | ForEach-Object { Log "  url: $_" }
      foreach ($kw in 'firmware','version','stereo','mono','record','battery','storage','temp','download','preview','mjpeg','rtsp','webrtc','websocket','take','ethernet') {
        $m = [regex]::Matches($js, ".{0,50}$kw.{0,50}", 'IgnoreCase') | Select-Object -First 5
        foreach ($x in $m) { Log "  [$kw] $($x.Value)" }
      }
    }
    Log "Saved GUI files under $web"
  }
  'Download' {
    if (-not $Url) { throw '-Url required' }
    $leaf = ([uri]$Url).Segments[-1]; if (-not $leaf -or $leaf -eq '/') { $leaf = "download-$stamp.bin" }
    $dest = Join-Path $OutDir $leaf
    $sw = [Diagnostics.Stopwatch]::StartNew()
    & curl.exe -s -m 3600 -o $dest -w "http=%{http_code} bytes=%{size_download} speed_Bps=%{speed_download}`n" $Url
    $sw.Stop()
    $mb = (Get-Item $dest).Length / 1MB
    Log ("Downloaded {0:N1} MB in {1:N1} s = {2:N2} MB/s -> {3}" -f $mb, $sw.Elapsed.TotalSeconds, ($mb / [math]::Max($sw.Elapsed.TotalSeconds, 0.001)), $dest)
    Log "Next: ffprobe -v error -show_format -show_streams `"$dest`""
  }
}
