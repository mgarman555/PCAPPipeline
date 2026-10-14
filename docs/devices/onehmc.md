# OneHMC (Soma recorder + Synapse sync box): device fact sheet

**Status: BLOCKED: device not on network.** No OneHMC unit was reachable from the P620 on 2026-10-14. Nothing about the device's API, firmware, files or throughput has been observed yet. Every device fact in the handoff spec (§4.2(a)) is still **owner-reported, unverified**.

- **Date of observations:** 2026-10-14
- **Observer:** OneHMC device agent (Claude Code), P620 `mgarman-threadripper`
- **Spec this checks:** `docs/handoffs/HOME_REBUILD_HANDOFF.md` §4.2(a), §4.5, §8
- **Probe script:** `Tools/probes/onehmc_probe.ps1` (PowerShell 5.1; Python is not installed). Read-only: it never starts a recording, changes a setting or touches firmware.
- **Raw logs:** `<repo>\Saved\DeviceProbes\onehmc\probe-20261014-*.log` (gitignored, not committed)

---

## 1. What was observed (2026-10-14)

### 1.1 The P620's network position

| Fact | Command | Result |
|---|---|---|
| P620 has one LAN interface, wired | `Get-NetAdapter \| ? Status -eq Up` | `Ethernet` = Marvell AQtion 10Gbit, linked at 1 Gbps. Plus `Tailscale` (tunnel, ignored). |
| P620 address | `Get-NetIPAddress -AddressFamily IPv4` | A private /24 address on `Ethernet` (home LAN details redacted from this public doc) |
| No Wi-Fi on the P620 | `netsh wlan show interfaces` | `There is no wireless interface on the system.` So the P620 cannot join a Soma-hosted Wi-Fi access point directly. |
| Ethernet profile is **Public** | `Get-NetConnectionProfile` | `Ethernet  Public`. Matters for inbound discovery traffic (see 1.3). |

### 1.2 Subnet sweep: no Soma found

Command (from the probe script, Discover mode):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\probes\onehmc_probe.ps1 -Mode Discover
```

It pings every address in the P620's /24, adds ARP neighbours, and TCP-probes ports 80, 8080, 9000 on each.

Result: **no host on the LAN has TCP 9000 open**, and every host with 80 or 8080 open identified as ordinary home network equipment (router, mesh node, NAS), not a Soma. One unidentified Raspberry Pi-class device answered ping with all common ports closed; it does not serve the Soma GUI (:9000 / :8080). The full per-host log is kept locally in `Saved\DeviceProbes\onehmc\` (gitignored), not in this public doc.

### 1.3 Name resolution: no Soma name

| Attempt | Command | Result |
|---|---|---|
| Guessed names | `Resolve-DnsName soma.local / soma / onehmc.local / synapse.local` | All `DNS name does not exist` |
| Windows mDNS works on this PC | `Resolve-DnsName <another Mac on the LAN>.local` | Resolves. **So if the Soma were on this LAN, `soma-xxxx.local` would resolve once the exact name is known.** |
| Raw DNS-SD browse | Probe script `Invoke-MdnsBrowse`: legacy-unicast PTR queries for `_services._dns-sd._udp`, `_http._tcp`, `_workstation._tcp`, `_device-info._tcp`, `_soma._tcp` to `224.0.0.251:5353` | `mDNS: no replies`. **Inconclusive**: most likely the Windows firewall (Public profile) drops unicast replies to the script's ephemeral port. Firewall not changed. A wildcard `soma-*` lookup is not possible with Windows' resolver. |

### 1.4 One unexplained neighbour

`Get-NetNeighbor` listed a stale entry for `192.168.8.1` (a GL.iNet travel-router vendor address, GL.iNet's default router IP), outside the home subnet; it did not answer ping. Not claimed to be related to the OneHMC. Madi has since confirmed the Soma runs **its own network** (§7), which a small travel router would explain; to check when the unit is on.

---
## 2. Still unknown (all of §4.2(a)'s list, plus discovery)

Nothing below has been observed. None of it may be used by adapter code until it is observed.

| # | Unknown | How it will be observed |
|---|---|---|
| U1 | Exact hostname (`soma-xxxx.local`) and IP | Read from the unit label/screen or the router's attached-device list (Madi logs in, not the agent). Then `Resolve-DnsName <name>`. |
| U2 | Network model: does the Soma join the house Wi-Fi, host its own AP, or sit behind a router (see 1.4)? Does the P620 need Wi-Fi it lacks? | Ask Madi; observe |
| U3 | **Firmware version, and where it is reported** (needed for the `>= 0.2.0` Record gate) | Inspect mode on :9000 and :8080, grep JS for `version`/`firmware`, open the GUI in the browser pane and read the version request in network requests |
| U4 | Mono/stereo: how it is switched, and how the active mode is reported | GUI + network requests while toggling (only toggle allowed besides record) |
| U5 | Preview stream format and URL (MJPEG? WebRTC? WebSocket? RTSP?) | Network requests in the browser pane; GUI JS |
| U6 | Record start/stop requests (method, path, body, response) | Network requests while pressing Record. **Only after U3 confirms >= 0.2.0.** |
| U7 | Take naming (device-chosen? settable?) | File list after a test take |
| U8 | File list and download endpoints | Network requests in the GUI's file/media page |
| U9 | Container, codec, resolution, fps, bit depth, one file per camera or one file for both | `ffprobe -v error -show_format -show_streams <file>` on a downloaded test take |
| U10 | Status fields: battery, storage free, temperature, recording state | GUI JS / status poll requests |
| U11 | Ethernet port on the Soma: yes or no | Look at the unit; GUI network settings page (read only) |
| U12 | Behaviour on Wi-Fi drop mid-take (keeps recording? file finalized?) | Later, after U6. Not in this pass. |
| U13 | What :8080 actually is (owner says firmware). Do not use it for anything except reading the version. | Inspect mode |
| U14 | Whether the GUI needs a login | Inspect mode. **If it does, stop and report to Madi.** |

### §4.5 measurements: not taken

| Measurement | Value | Why missing |
|---|---|---|
| File size per minute, stereo (default settings) | **not measured** | Device unreachable; firmware not read, so no internal recording is permitted anyway |
| File size per minute, mono (default settings) | **not measured** | Same |
| Download throughput (Wi-Fi; Ethernet if present) | **not measured** | Same |
| Storage removable / readable on the P620, and copy time | **not measured** | Same; needs physical inspection of the unit |

---

## 3. Contradictions with the handoff spec

None can be confirmed or ruled out, because the device was not observed. Two things to flag:

1. **§4.5 assumes the P620 can be on the Soma's network path.** The P620 has **no Wi-Fi interface** (observed). If the Soma only serves its GUI on its own Wi-Fi access point, the P620 cannot reach it without (a) the Soma joining the house network, (b) an Ethernet path, or (c) a USB Wi-Fi adapter. That would also put the background-offload option at risk.
2. **§4.2(a) names `soma-xxxx.local`.** No such name answered on 2026-10-14. Windows mDNS is confirmed working, so once the real name is known, a failed lookup will mean "unit off or on a different network", not "this PC can't do mDNS".

---

## 4. Measurement plan for when the unit is on the network

Prerequisites: unit powered (D-Tap), on a network the P620 can reach, Madi reports the hostname or IP. Output stays under the main repo's `Saved\DeviceProbes\onehmc\` (gitignored). Set `\$out = '<repo>\Saved\DeviceProbes\onehmc'`.

**Step 1: find it**
```powershell
Resolve-DnsName soma-XXXX.local        # exact name from Madi
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\probes\onehmc_probe.ps1 -Mode Discover -OutDir $out
```
Look for the new host with `OPEN <ip>:9000` and/or `:8080`.

**Step 2: read the GUI without touching anything**
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\probes\onehmc_probe.ps1 -Mode Inspect -Target <ip-or-name> -OutDir $out
curl.exe -s -i http://<ip>:9000/
curl.exe -s -i http://<ip>:8080/
```
This saves the HTML/JS/CSS from :9000, :8080 and :80, and logs URL-like strings and keyword hits (firmware, version, stereo, mono, record, battery, storage, temp, download, preview, mjpeg, rtsp, webrtc, websocket, take, ethernet). If any page asks for a login: **stop and report to Madi.**

**Step 3: browser pane, observation only.** Open `http://<ip>:9000/` in the built-in browser. Use `read_page` and `read_network_requests` to record the version/status poll, preview transport, file list, and (later) record requests. Do not submit forms, accept anything, change settings, or open the firmware updater on :8080 beyond reading the version.

**Step 4: firmware gate.** Write the exact firmware string and the request that returned it into §1. **If it is below 0.2.0 or cannot be read: STOP. No internal recording.** Steps 5 to 7 only run on a confirmed `>= 0.2.0`.

**Step 5: file size per minute (defaults, nothing else changed).**
- Stereo (the usual mode): record 30 s, timed with a stopwatch and from the file's own duration (`ffprobe -show_entries format=duration`). Then record 60 s. Size per minute = bytes / duration × 60, from both takes, and report both.
- Toggle to mono through the GUI and repeat the 30 s and 60 s takes. Then toggle **back to stereo** and confirm the GUI reports stereo.
- Compare against Epic's ~45 GB/min stereo figure in §4.5.

**Step 6: download throughput.** Use a file URL seen in the GUI's own network requests (U8), never a guessed one:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\probes\onehmc_probe.ps1 -Mode Download -Url "http://<ip>:9000/<observed path>" -OutDir $out
ffprobe -v error -show_format -show_streams "$out\<file>"
```
This logs MB, seconds and MB/s, over Wi-Fi and again over Ethernet if the Soma has a port. Offload ratio = (download seconds per minute of footage). Over 60 s means background offload cannot keep up (§4.5 then picks batch-at-breaks or media pull).

**Step 7: storage.** Inspect the unit physically for a card or drive slot (photo it). If removable, copy the same test take with a card reader and time it with `Measure-Command { Copy-Item ... }`. Compare against Step 6.

**Then:** fill in §1 and §2, and write PASS/FAIL per adapter method (connect, preview, start, stop, list, download) to `docs/devices/onehmc-verification.md` with firmware version, date, and exact command (§8).

---

## 5. Questions for Madi

1. What is the exact hostname or IP of the Soma (label, screen, or the router's attached-device list)? Was it powered on at about 16:00 on 2026-10-14?
2. Does the Soma join your house Wi-Fi, or does it make its own Wi-Fi network? The P620 has no Wi-Fi.
3. Does the Soma or the Synapse have an Ethernet port?
4. Does the kit include a GL.iNet travel router (see 1.4)?
5. Is the unidentified Raspberry Pi-class device on the LAN one of yours?
6. Has the 0.2.0 update been done yet?
