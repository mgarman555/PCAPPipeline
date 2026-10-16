# Body camera: device fact sheet

Observed on the P620, Windows 11 Pro 10.0.26300, on **2026-10-14**, by the body-camera device agent.
Tool: `ffmpeg version 9.0.1-full_build-www.gyan.dev` (winget Gyan.FFmpeg, on PATH; see `CLAUDE.local.md`).
All test recordings and logs: `<repo>\Saved\DeviceProbes\body-camera\` (gitignored, `.gitignore:3: Saved/`).

## Status (2026-10-16): Logitech C920S connected and probed. PASS at 1280x720 @ 30 fps

See §8 for the measurements. The rest of this sheet (§0 to §7) is the 2026-10-14 record of the HDMI dongle, kept for history.

## 0. Status on 2026-10-14: the body webcam was NOT connected (BLOCKED)

Madi's body camera is described as a "4K USB webcam, 60 fps unknown" (handoff §1). **No such device is attached to the P620, and none has ever been installed on it.**

- The only physical video capture device is `USB3.0 HD Video` (USB `VID_534D` `PID_2109`), an HDMI-to-USB capture dongle (§1).
- `Get-PnpDevice` without `-PresentOnly` (includes previously seen, now absent devices), filtered on classes `Camera` and `Image`, returns only that one device. Filtering all non-OK devices on `cam|video|webcam|uvc|4k` returns nothing. So no other camera has ever been enumerated on this Windows install.
- The other dshow video entries are software: `OBS Virtual Camera` (`@device_sw_...`) and `LSVCam` (`@device_sw_...`, reported as type "none"). Neither is real capture hardware.

**What Madi needs to do:** plug the 4K USB webcam **directly into a USB 3.x port on the P620** (not through the HDMI dongle, not through a USB 2 hub), then re-run this probe. The webcam's own modes, real fps per mode, and the recommended mode are unknown until then. Everything below about modes and fps describes the **dongle**, not the webcam.

**Could the webcam be feeding through the dongle over HDMI?** No, not right now:
- The dongle's input is HDMI. A USB webcam has no HDMI output, so it cannot feed this dongle. Only a camera with an HDMI out (camcorder, mirrorless, action cam) could.
- The dongle is currently receiving **no picture**: every captured frame is uniform near-black (signalstats mean luma `YAVG = 7.00`, min 7.00, max 7.00 on all 300/301/601 frames, §3). Windows camera privacy is **Allow** (HKCU and HKLM `ConsentStore\webcam` = `Allow`, HKCU `NonPackaged` = `Allow`), so these are not privacy-blanked frames; the black is the dongle's no-signal output.
- Even if an HDMI camera were connected, the dongle's ceiling is **1920x1080 MJPEG** (no 4K mode exists, §2), so 4K could never arrive through it.

## 1. Device identity: `USB3.0 HD Video` = MacroSilicon MS2109 HDMI capture dongle

Commands: `Get-PnpDevice -PresentOnly`, `Get-PnpDeviceProperty -InstanceId <id>`, `Get-CimInstance Win32_PnPSignedDriver` (2026-10-14).

| Property | Value |
|---|---|
| USB composite parent | `USB\VID_534D&PID_2109\8&2059D3C1&0&2`, service `usbccgp` |
| Hardware IDs | `USB\VID_534D&PID_2109&REV_2100`, `USB\VID_534D&PID_2109` |
| Bus-reported description | `USB3.0 HD Video` (the name is the vendor string; see speed note below) |
| Interface 0 (video) | `USB\VID_534D&PID_2109&MI_00\9&53F166&0&0000`, class `Camera`, compatible ID `USB\Class_0e&SubClass_03` (USB Video Class), driver `usbvideo.inf` (Microsoft inbox UVC driver), 10.0.26100.9711 |
| Interface 2 (audio) | `USB\VID_534D&PID_2109&MI_02\9&53F166&0&0002`, "USB Digital Audio", driver `wdma_usb.inf` 10.0.26100.9457 (dshow name `Digital Audio Interface (USB Digital Audio)`) |
| Interface 4 (HID) | `USB\VID_534D&PID_2109&MI_04\...`, `input.inf`, HID vendor-defined (typically the chip's firmware/control channel) |
| Location | `Port_#0002.Hub_#0003`, parent `USB\ROOT_HUB30\7&282b35b&0&0` (root port of an AMD USB 3.10 xHCI controller) |
| First installed / last arrival | 2026-09-09 21:11:04 / 2026-10-14 15:50:56 |
| Vendor-specific driver | none; Microsoft inbox drivers only |

`VID_534D PID_2109` is the MacroSilicon MS2109 HDMI capture chip. It **does** present as a standard UVC device (class 0x0E), so FFmpeg dshow can open it like a webcam, but it has no sensor: it digitises whatever arrives on its HDMI input.

USB speed (inference, not measured directly): despite the "USB3.0" name, the uncompressed YUY2 modes cap at 1920x1080 @ 5 fps, 1280x720 @ 10 fps and 640x480 @ 30 fps (§2), which matches a USB 2.0 High-Speed (480 Mbit/s) link. MJPEG is what makes 60 fps possible.

## 2. Full mode table (dongle)

Command: `ffmpeg -hide_banner -f dshow -list_options true -i video="USB3.0 HD Video"` (2026-10-14). Single pin `Capture`. Each line also appears a second time with a colour-space suffix; duplicates are omitted here.

| Format | Resolution | Advertised fps range |
|---|---|---|
| vcodec=mjpeg | 1920x1080 | 10 to 60.0002 |
| vcodec=mjpeg | 1600x1200 | 10 to 60.0002 |
| vcodec=mjpeg | 1360x768 | 10 to 60.0002 |
| vcodec=mjpeg | 1280x1024 | 10 to 60.0002 |
| vcodec=mjpeg | 1280x960 | 10 to 60.0002 |
| vcodec=mjpeg | 1280x720 | 10 to 60.0002 |
| vcodec=mjpeg | 1024x768 | 10 to 60.0002 |
| vcodec=mjpeg | 800x600 | 10 to 60.0002 |
| vcodec=mjpeg | 720x576 | 10 to 60.0002 |
| vcodec=mjpeg | 720x480 | 10 to 60.0002 |
| vcodec=mjpeg | 640x480 | 10 to 60.0002 |
| pixel_format=yuyv422 | 1920x1080 | 5 |
| pixel_format=yuyv422 | 1600x1200 | 5 |
| pixel_format=yuyv422 | 1360x768 | 8 |
| pixel_format=yuyv422 | 1280x1024 | 8 |
| pixel_format=yuyv422 | 1280x960 | 8 |
| pixel_format=yuyv422 | 1280x720 | 10 |
| pixel_format=yuyv422 | 1024x768 | 10 |
| pixel_format=yuyv422 | 800x600 | 5 to 20 |
| pixel_format=yuyv422 | 720x576 | 5 to 25 |
| pixel_format=yuyv422 | 720x480 | 5 to 30 |
| pixel_format=yuyv422 | 640x480 | 5 to 30 |

**No 3840x2160 (4K) mode exists**, so 4K60 and 4K30 cannot be tested on this device.

## 3. Measured delivered fps (dongle, no HDMI signal)

All runs: 10 s, `-rtbufsize 1024M`, input `-vcodec mjpeg`, encode `libx264 -preset veryfast -crf 18 -pix_fmt yuv420p`. "pt" = `-fps_mode passthrough` (no duplication, so the frame count is what the device really delivered). Delivered fps = ffprobe `nb_read_frames` / `format.duration`.

Example command (1080p60 passthrough):
```
ffmpeg -hide_banner -y -f dshow -rtbufsize 1024M -vcodec mjpeg -video_size 1920x1080 -framerate 60 -i video="USB3.0 HD Video" -t 10 -fps_mode passthrough -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p m1080p60_pt.mp4
ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames,avg_frame_rate,r_frame_rate:format=duration -of compact m1080p60_pt.mp4
```

| Mode requested | File | Frames / duration | Delivered fps | MJPEG decode errors | ffmpeg dup / drop | Mean luma |
|---|---|---|---|---|---|---|
| 4K60, 4K30 | n/a | n/a | **not offered** | | | |
| 1080p60 (pt) | `m1080p60_pt.mp4` | 300 / 9.967 s | **30.1** | 302 ("Found EOI before any SOF" / "No JPEG data") | n/a (passthrough) | 7.00 (black) |
| 1080p60 (cfr) | `m1080p60_cfr.mp4` | 600 / 10.000 s | 60 nominal | 302 | **dup=300 drop=0** (half the output frames are duplicates) | |
| 1080p30 (pt) | `m1080p30_pt.mp4` | 301 / 10.033 s | **30.0** | 0 | n/a | 7.00 (black) |
| 720p60 (pt), libx264 | `m720p60_pt.mp4` | 601 / 10.017 s | **60.0** | 0 | n/a | 7.00 (black) |
| 720p60 (pt), h264_nvenc `-preset p5 -rc vbr -cq 18 -b:v 0` | `m720p60_nvenc.mp4` | 601 / 10.017 s | **60.0** | | final `fps= 60` | |

**Why 1080p60 is really 30 fps.** Stream-copy capture (`-c:v copy raw_1080p60_copy.mkv`) shows 601 packets in 10.017 s, but they alternate: 300 packets of 41,109 bytes (a real JPEG) and 301 packets of **4 bytes** (an empty JPEG, EOI marker only), one every ~16 ms (`ffprobe -show_entries packet=pts_time,size`). Requesting 1080p50 gives the same pattern (300 real, 300 empty). So at 1920x1080 the dongle emits a 60 Hz cadence with every other frame empty: **30 real frames per second**. 720p60 has no empty packets (`raw_720p60_copy.mkv`: 301 packets in 5 s, all 18,609 bytes).

**Caveat:** all of the above was measured with **no HDMI signal** (black frames, tiny JPEGs). Real picture content means larger JPEGs and could change delivery. Re-measure with a real source if the dongle is ever used.

## 4. File finalization on stop and on kill

Method: PowerShell `System.Diagnostics.Process` with redirected stdin; 720p60 input; libx264 CRF 18. Graceful stop = write `q` to stdin. Hard kill = `Stop-Process -Force` (TerminateProcess). Each output checked with `ffprobe -count_frames`.

| Test | Output | Result |
|---|---|---|
| `q` on stdin, plain MP4 | `fin_q.mp4` | exit 0, valid, 464 frames / 7.733 s. moov written. **PASS** |
| Hard kill, plain MP4 | `fin_kill.mp4`, `fin2_kill.mp4` | `moov atom not found`, unreadable. **FAIL** (expected: moov is written on close) |
| Hard kill, fragmented MP4, default flushing | `fin_kill_frag.mp4` (36 bytes) | unreadable: data still in ffmpeg's IO buffer when killed |
| Hard kill, MKV, default flushing | `fin_kill.mkv` (0 bytes) | unreadable, same reason |
| Hard kill, fragmented MP4 with `-g 60 -flush_packets 1 -movflags +frag_keyframe+empty_moov+default_base_moof`, killed at ~10 s | `fin2_kill_frag.mp4` | valid, 480 frames / 8.000 s. **PASS** (loses the tail: the unfinished GOP plus encoder lookahead, here ~2 s) |
| Hard kill, MKV with `-g 60 -flush_packets 1` | `fin2_kill.mkv` | readable, 480 frames, duration field N/A (no cues/duration written). **PASS with caveat** |
| `q` on stdin, fragmented MP4 (same flags) | `fin2_q_frag.mp4` | exit 0, valid, 588 frames / 9.800 s. **PASS** |

Note: the black test frames compress to almost nothing, which is why the unflushed kill files were empty. With real footage the buffer fills faster, but `-flush_packets 1` plus a 1 s GOP is what makes the loss bounded. **Not yet verified:** whether Live Link Hub's Mono Video Ingest accepts fragmented MP4. Until that is checked, the adapter should always stop with `q` (normal path, gives a standard-playable file) and treat the fragmented layout as crash insurance.

## 5. One-owner recipe (record + localhost preview in one ffmpeg)

### UDP MJPEG preview: works

Recorder (single process owns the camera; records 60 fps H.264 CRF 18 and emits a 640x360 MJPEG preview over localhost UDP):
```
ffmpeg -hide_banner -y -f dshow -rtbufsize 1024M -vcodec mjpeg -video_size 1280x720 -framerate 60 -i video="USB3.0 HD Video" ^
  -filter_complex "[0:v]split=2[rec][pv];[pv]fps=10,scale=640:-2[pvo]" ^
  -map "[rec]" -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p -g 60 -flush_packets 1 -movflags +frag_keyframe+empty_moov+default_base_moof "<take>\body.mp4" ^
  -map "[pvo]" -c:v mjpeg -q:v 5 -f mjpeg "udp://127.0.0.1:5005?pkt_size=1316"
```
Test reader (stands in for the frame pump / pose process):
```
ffmpeg -hide_banner -y -f mjpeg -i "udp://127.0.0.1:5005?fifo_size=1000000&overrun_nonfatal=1" -c copy preview.mjpeg
```
Reader started 3 s after the recorder, stopped with `q` after ~10 s; recorder then stopped with `q`.

| Preview `fps=` | Recording (frames / duration) | Recording fps | Preview frames received in ~10 s | Reader errors |
|---|---|---|---|---|
| 10 | 835 / 13.917 s (`onenwr_10.mp4`) | 60.0 | 101 (`preview_udp_10.mjpeg`, 640x360) | 0 |
| 15 | 836 / 13.933 s (`onenwr_15.mp4`) | 60.0 | 150 | 0 |
| 30 | 867 / 14.450 s (`onenwr_30.mp4`) | 60.0 | 312 | 0 |

So 5 to 10 fps for the frame agents and **15+ fps for the pose process are both feasible** from the same process, with the recording holding 60 fps. UDP never blocks the recorder: it records whether or not a reader is attached. Caveat: measured with black frames (~2 KB preview JPEGs). Real 640x360 JPEGs are bigger and are split into many UDP datagrams, so re-check the reader's error count with real footage. Swap `-video_size`/`-framerate` for the webcam's chosen mode once it is connected.

### HTTP `-f mpjpeg -listen 1`: do NOT use

Same command with output 2 = `-f mpjpeg -listen 1 http://127.0.0.1:8090/preview.mjpg` (log `http_rec.log`):
- Before any client connected (5 s in), `http_rec.mp4` existed at **0 bytes**: the whole ffmpeg pipeline waits for the HTTP client before it writes the recording.
- A `curl` client got 176,662 bytes in 4 s. When it disconnected, **ffmpeg died** (exit `-10054`, WSAECONNRESET, `Error writing trailer`). The recording stopped with the preview.
- **FAIL** for the one-owner design: a preview viewer must never be able to stall or kill the take recording.

## 6. Recommended mode

- **For the actual 4K webcam: BLOCKED** until it is plugged in and probed. The handoff rule still applies: take 4K60 if it really delivers 60; otherwise **prefer 1080p60 over 4K30**, if 1080p60 delivers real 60 fps (check for the empty-frame pattern in §3, not just the frame count).
- **For the MS2109 dongle, if it were ever used** with an HDMI camera: **1280x720 MJPEG @ 60** is the only mode measured at a real 60 fps (meets Epic's "HD at 60 fps" as 720p HD). 1920x1080 "60" is really 30 real frames plus 30 empty, so it does not meet the 60 fps rule. This is measured without a signal (§3 caveat). Capture Setup should show **amber** for this device with the reason "MS2109 dongle: 1080p delivers 30 real fps; 720p60 only; no 4K".
- Encoder: libx264 CRF 18 held 60 fps at 720p (`speed=0.99x`, real-time). h264_nvenc also works (601/601 frames) but nvenc has no CRF; its `-cq 18` is a constant-quality target, not the CRF 18 Epic names. Use **libx264 `-crf 18`** to match Epic's rule word for word.

## 7. Contradictions with the spec / owner notes

1. The owner said the body camera is a "4K USB webcam". The only real video device present is an HDMI capture dongle (MS2109), not a webcam, and no webcam has ever been installed on this machine.
2. The dongle's name says "USB3.0", but its YUY2 mode limits point to a USB 2.0 link, and its top resolution is 1080p (no 4K).
3. The dongle advertises 1080p at 60 fps but delivers 30 real frames per second (every other packet is a 4-byte empty JPEG). The handoff's warning that "webcams often advertise modes they can't sustain" applies here.
4. Handoff §4.2(b) says "emits a low-rate MJPEG preview ... on localhost" without naming a transport. The obvious HTTP (`mpjpeg -listen 1`) transport breaks the one-owner design (blocks the recording until a client connects, and kills ffmpeg when the client leaves). UDP works.
5. Handoff §4.2(b) asks for a clean stop "when the process is killed". A plain MP4 cannot survive a hard kill (no moov). Only fragmented MP4 or MKV with `-flush_packets 1` survives, and the last ~GOP is lost.

---

## 8. Logitech C920S Pro HD (owner update 2026-10-15; probed 2026-10-16)

**Observed 2026-10-16** with `Tools/probes/bodycam_probe.ps1 -Device "HD Pro Webcam C920" -Seconds 20` (summary `Saved/DeviceProbes/body-camera/summary-20261016-151021.md`).

- **Identity:** dshow name `HD Pro Webcam C920` (the C920S enumerates under the C920 name), USB `VID_046D` `PID_08E5`. Interface `MI_02` is its built-in microphone (class MEDIA): do not pick it for audio.
- **Modes:** MJPEG up to 1920x1080 at 5 to 30 fps; YUY2 1080p only at 5 fps; 2560x1472 YUY2 at 2 fps. **No 60 fps mode, no 4K.**
- **Delivered fps** (stream copy, 20 s, bright room, mean luma ~171):

  | Mode | Real fps | Note |
  |---|---|---|
  | 1920x1080 MJPEG @ 30 | **28.1** (563 frames / 19.99 s) | Steadily drops ~6% of frames. A 10 s run gave 28.3. |
  | 1280x720 MJPEG @ 30 | **30.0** (601 / 20.0 s) | No drops. |

- **One-owner recipe at 720p30** (H.264 CRF 18, `-g 30`, fragmented MP4, plus a 10 fps UDP MJPEG preview at 640 wide): recording **29.86 fps** (657 frames / 22.0 s), encoder speed 0.999x, preview **80 of ~80** frames received.
- **Stopping with a real picture** (720p30, fragmented MP4): `q` on stdin gives exit 0 and 217 frames / 7.23 s. A hard kill after 8 s leaves a readable file of 150 frames / 5.0 s (the tail is lost, as in §4).
- **Recommended mode: 1280x720 MJPEG @ 30.** 1080p30 drops frames, and a variable frame rate is a bigger risk to ingest and solve than the lower resolution. Capture Setup shows **amber**: "C920S: 30 fps max; Epic target 60 fps".

Owner-provided background (2026-10-15):
- **Logitech's published spec: 1080p at 30 fps and 720p at 30 fps.** No 60 fps mode. Sources: https://logitech.com/en-ae/products/webcams/c920s-pro-hd-webcam.html , https://service.alaska.edu/TDClient/36/Portal/KB/Article/795/Logitech-C920s-Pro-Full-HD-Webcam
- **Consequence:** Epic's mono-video rule for markerless (handoff §2) is HD or 4K **at 60 fps**. The C920S cannot meet it. Capture Setup must show **amber** for this camera with the reason "C920S: 30 fps max; Epic target 60 fps". The probe will confirm the real delivered fps (it may also deliver less than 30 in low light, since RightLight auto-exposure lowers the frame rate).
- This contradicts the earlier owner note ("4K USB webcam"). For a camera that meets the rule, it needs a real 1080p60 (or 4K60) UVC mode; verify any candidate with the probe before relying on it.

## 9. Fallback: iPhone as the body camera (Madi, 2026-10-15). Not yet tested

If the C920S can't meet the rule, Madi records body video on her iPhone instead. What changes:
- **Meets the 60 fps rule:** iPhones record 1080p60 and 4K60. Set Settings > Camera > Record Video to 1080p 60 or 4K 60, and Formats > "Most Compatible" (H.264) so Live Link Hub's FFmpeg ingest gets a standard file. Lock focus and exposure (AE/AF lock) and mount it on a tripod, straight on at neutral height (Epic Video Capture Guidelines: camera fixed for the whole take).
- **No live feed into Unreal:** the phone records internally, so there is no localhost preview for the body agents during the take. The body checks would run on the file after offload, and Record/Stop on the phone is manual (or triggered from the phone itself).
- **Alignment:** with no shared start command, do a visible + audible clap at the start of every take to line the phone video up with the HMC and the Rode.
- **Offload:** copy the clip into `<take>/body/` after each take (cable or AirDrop). The adapter becomes "import a file for this take", not "run FFmpeg on a webcam".
- To verify before relying on it: frame rate is constant (iPhone video can be variable-frame-rate; check `ffprobe` avg vs r_frame_rate), and Mono Video Ingest accepts the file.
