# Body camera: verification log

Date: 2026-10-14. Host: P620, Windows 11 Pro 10.0.26300. FFmpeg 9.0.1 (gyan.dev full build, winget).
Device under test: `USB3.0 HD Video`, USB VID_534D PID_2109 REV_2100 (MacroSilicon MS2109 HDMI capture dongle), Microsoft inbox `usbvideo.inf` 10.0.26100.9711. No HDMI signal was present during the tests (all frames black, mean luma 7.00).
Evidence (recordings and logs): `<repo>\Saved\DeviceProbes\body-camera\` (gitignored). Details and full commands: `docs/devices/body-camera.md`.

| # | Behavior | Result | Command / probe | Evidence |
|---|---|---|---|---|
| 1 | Owner's 4K USB webcam is connected and enumerated | **BLOCKED** | `Get-PnpDevice` (present and non-present, classes Camera/Image); `ffmpeg -list_devices true -f dshow -i dummy` | Only the MS2109 dongle and two virtual cameras. Madi must plug the webcam directly into a P620 USB 3.x port, then re-run this log. |
| 2 | Webcam modes listed (`-list_options`) | **BLOCKED** | (needs #1) | |
| 3 | Webcam delivers 60 fps at HD or 4K | **BLOCKED** | (needs #1) | |
| 4 | Webcam feeds in through the HDMI dongle | **FAIL** (not the case) | `ffmpeg ... -frames:v 1 probe_frame_1080.png`; signalstats YAVG | Frames uniform black (YAVG 7.00); a USB webcam has no HDMI out; dongle has no 4K mode anyway |
| 5 | Frames not blanked by Windows camera privacy | **PASS** | `Get-ItemProperty HKCU/HKLM:\...\CapabilityAccessManager\ConsentStore\webcam` | `Value = Allow` (HKCU, HKLM, HKCU NonPackaged); black = no HDMI signal |
| 6 | Dongle identity (VID/PID, UVC class, driver) | **PASS** | `Get-PnpDeviceProperty`, `Win32_PnPSignedDriver` | VID_534D PID_2109, class 0x0E/0x03, usbvideo.inf |
| 7 | Dongle mode list captured | **PASS** | `ffmpeg -hide_banner -f dshow -list_options true -i video="USB3.0 HD Video"` | MJPEG 640x480 to 1920x1080 @ 10 to 60; YUY2 up to 1080p5; no 4K |
| 8 | Dongle 4K60 / 4K30 | **FAIL** (not offered) | same | no 3840x2160 mode |
| 9 | Dongle 1080p60 delivers 60 real fps | **FAIL** (30.1 fps) | `-video_size 1920x1080 -framerate 60 ... -fps_mode passthrough`; ffprobe `nb_read_frames`; `-c:v copy` packet sizes | 300 frames / 9.967 s; 302 empty-JPEG errors; cfr run `dup=300 drop=0`; packets alternate 41,109 B / 4 B |
| 10 | Dongle 1080p30 delivers 30 fps | **PASS** (30.0) | `-video_size 1920x1080 -framerate 30` | 301 / 10.033 s, 0 errors |
| 11 | Dongle 720p60 delivers 60 fps (libx264 CRF 18) | **PASS** (60.0) | `-video_size 1280x720 -framerate 60 ... -c:v libx264 -preset veryfast -crf 18` | 601 / 10.017 s, 0 errors |
| 12 | Dongle 720p60 with h264_nvenc | **PASS** (60.0) | `-c:v h264_nvenc -preset p5 -rc vbr -cq 18 -b:v 0` | 601 / 10.017 s (note: cq 18 is not CRF 18) |
| 13 | Dropped-frame reporting visible in ffmpeg stderr | **PASS** | cfr run, final stderr line | `dup=300 drop=0` printed in progress/final line |
| 14 | Graceful stop (`q` on stdin) finalizes plain MP4 | **PASS** | .NET Process, `StandardInput.Write('q')` | `fin_q.mp4` exit 0, 464 frames / 7.733 s |
| 15 | Hard kill leaves a valid plain MP4 | **FAIL** | `Stop-Process -Force` | `fin_kill.mp4`, `fin2_kill.mp4`: `moov atom not found` |
| 16 | Hard kill leaves a valid fragmented MP4 (`-g 60 -flush_packets 1 -movflags +frag_keyframe+empty_moov+default_base_moof`) | **PASS** | `Stop-Process -Force` at ~10 s | `fin2_kill_frag.mp4` 480 frames / 8.000 s (tail ~2 s lost) |
| 17 | Hard kill, fragmented MP4 / MKV without `-flush_packets 1` | **FAIL** | `Stop-Process -Force` at ~8 s | 36 B / 0 B, unreadable |
| 18 | Hard kill leaves a valid MKV (`-g 60 -flush_packets 1`) | **PASS (caveat)** | `Stop-Process -Force` | `fin2_kill.mkv` 480 frames, duration N/A |
| 19 | Graceful stop of fragmented MP4 | **PASS** | `q` on stdin | `fin2_q_frag.mp4` 588 frames / 9.800 s |
| 20 | Live Link Hub Mono Video Ingest accepts fragmented MP4 | **NOT TESTED** | | needs LLH run |
| 21 | One process records 60 fps CRF 18 + UDP MJPEG preview at 10 fps | **PASS** | recipe in body-camera.md §5 | rec 835 / 13.917 s (60.0); 101 preview frames in ~10 s |
| 22 | Same, preview at 15 fps (pose-process target) | **PASS** | `fps=15` | rec 836 / 13.933 s; 150 preview frames in ~10 s |
| 23 | Same, preview at 30 fps | **PASS** | `fps=30` | rec 867 / 14.450 s; 312 preview frames in ~10 s |
| 24 | HTTP `-f mpjpeg -listen 1` preview is safe for one-owner use | **FAIL** | output 2 = `http://127.0.0.1:8090/preview.mjpg`, `curl -m 4` | recording 0 B until a client connected; client disconnect killed ffmpeg (exit -10054) |
| 25 | Preview and recording tested with real picture content | **NOT TESTED** | | no HDMI source / no webcam; all results are on black frames |
