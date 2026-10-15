# OneHMC (Soma recorder + Synapse sync box): device fact sheet

**Status: BLOCKED: device not on network.** No OneHMC unit was reachable from the P620 on 2026-10-14. Nothing about the device's API, firmware, files or throughput has been observed yet. Every device fact in the handoff spec (§4.2(a)) is still **owner-reported, unverified**.

- **Date of observations:** 2026-10-14
- **Observer:** OneHMC device agent (Claude Code), P620 `mgarman-threadripper`
- **Spec this checks:** `docs/handoffs/HOME_REBUILD_HANDOFF.md` §4.2(a), §4.5, §8
- **Probe script:** `Tools/probes/onehmc_probe.ps1` (PowerShell 5.1; Python is not installed). Read-only: it never starts a recording, changes a setting or touches firmware.
- **Raw logs:** `<repo>\Saved\DeviceProbes\onehmc\probe-20261014-*.log` (gitignored, not committed)

---

## 1. What was observed (2026-10-14)

### 1.1 Not found from the P620 (2026-10-14)

| Fact | Command | Result |
|---|---|---|
| P620 network | `Get-NetAdapter -Physical` | One wired adapter (Marvell AQtion 10GbE). No Wi-Fi adapter (`netsh wlan show interfaces`: no wireless interface). |
| Discovery sweep | `Tools\probes\onehmc_probe.ps1 -Mode Discover` | No host answered on the Soma's ports (:9000 record, :8080 firmware). |
| Name lookup | `Resolve-DnsName soma.local / onehmc.local / synapse.local` | Not found. Windows mDNS itself works on this PC, so the exact `soma-xxxx.local` name will resolve once the P620 is on the Soma's network. |

This is expected: Madi confirmed on 2026-10-15 that **the Soma runs its own network** (§7). Discovery and every measurement below happen once the P620's second network card is connected to it.

---## 2. Still unknown (all of §4.2(a)'s list, plus discovery)

Nothing below has been observed. None of it may be used by adapter code until it is observed.

| # | Unknown | How it will be observed |
|---|---|---|
| U1 | Exact hostname (`soma-xxxx.local`) and IP | Read from the unit label/screen. Then `Resolve-DnsName <name>` on the Soma's network. |
| U2 | Network model: Soma runs its own network (Madi). Wired or Wi-Fi AP? Addressing (DHCP or fixed)? | Observe on connection |
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

1. **§4.5 assumes the P620 can be on the Soma's network path.** The P620 has **no Wi-Fi interface** (observed) and the Soma runs its own network (Madi). The P620 reaches it through a second wired network card; if the Soma's network is Wi-Fi only, that card needs a wired path to it. Offload is after takes, not background (§7).
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

1. What is the exact hostname or IP of the Soma (label or screen)?
2. Is the Soma's own network wired (Ethernet port on the Soma or Synapse) or a Wi-Fi access point? The P620 has no Wi-Fi.
3. Has the 0.2.0 update been done yet?

---
## 7. Owner update (Madi, 2026-10-15)

- **This weekend: 1 actor, stereo.**
- **The Soma runs its own network.** The P620 will join it with a separate Ethernet card (the P620's main connection stays on the Marvell 10GbE). Give the Soma-side card a static address and **no default gateway** so internet traffic never routes to the Soma.
- **Offload is offline**: files come off the unit after takes (handoff §4.5 "batch at breaks" or "media pull"), not streamed live. Live preview over the network is still to be checked; if absent, the HMC feeds show "no preview" and the agents run on the first downloaded file (handoff §4.2(a)).
