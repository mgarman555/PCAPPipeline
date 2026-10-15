# PCAP Tool: Home Rebuild Handoff (Device Layer + Alarms)

> **Status, 2026-10-15:** Phase 0 done; Phase 1 code done and awaiting Madi's sign-off; setup UI added on request (Capture Setup, Scanning, calibration and neutral-face shots, editable capture-system lists). Phase 2 blocked until the OneHMC, Rode Wireless GO II and body camera are connected and probed. Owner answers since rev 2: 1 actor, stereo; Soma on its own network with offload after takes; body camera is a Logitech C920S (30 fps max, below the 60 fps rule; iPhone is the fallback). See the [root README](../../README.md) for the progress view and [devices/](../devices/) for the fact sheets.

**Date:** 2026-10-14 · **Author:** Claude (chat session with Madi) · **For:** Claude Code on the P620 (`mgarman-threadripper`, RTX 3080 Ti, UE 5.8)
**Deadline:** one performer, fully working, this weekend (Sat 2026-10-17).
**Status:** Spec, rev 2 (hardware answers folded in). Madi signs off between phases. Nothing here is implemented yet.

---

## 0. Read this first

**What this is.** PCAPTool already has the data model, roster, call sheet, Take Recorder backend, and a per-camera agent system that judges frames against Epic's MetaHuman docs. What it does not have is a device layer for Madi's home hardware: everything that touches a device is hardwired to Technoprops ORION (HTTP `/control`, `/video`). This handoff keeps the brain, rebuilds the device layer as swappable adapters, adds a markerless body camera, adds audio, and adds a floor alarm (red flash + chime + auto-flag) so Madi can walk away and direct.

**Hard rules (unchanged from `ways-of-working`, restated because they decide what you do):**
1. Every capture-quality rule comes from Epic's documentation. Every skill threshold carries a comment with the doc page it comes from. No invented rules. Sources are in §12 and in `Plugins/PCAPTool/Guidelines/manifest.json`.
2. Verify before assuming. Device behavior is observed and written into a device fact sheet (`docs/devices/<device>.md`) before any adapter code depends on it. No vendor contact until reverse-engineering is exhausted.
3. Build on what Epic ships. Mocap Manager (`ACapturePerformer`), Take Recorder, Live Link Hub Capture Manager, MetaHuman Performance. Do not build parallel versions.
4. Madi signs off between phases (§9). Do not roll into the next phase on your own.
5. The repo is **public**. Epic's guideline images are Epic content: `Guidelines/images/` is gitignored and must never be committed. Only the manifest (links) is tracked.
6. Git: fast-forward to `main` only, commit by explicit path, never force-push (see `CONTRIBUTING.md`). `main` must always compile on the P620.

**Stale file warning.** `CurrentErrorCodes` at the repo root shows a `Dt` shadowing error in `VCamInputLayer.cpp`. That is already fixed on `main` (the test file uses `kDt`). Treat that file as an old log. The real first step is a clean build (Phase 0).

---

## 1. Decisions from Madi (locked for this build)

- The pipeline is multi-hardware by design. A **Capture Setup page** chooses, per performer per day, what is used for facial, body, audio, and cameras. Hardware is a dropdown, never a code path.
- **Facial** is a dropdown of vendor setups: Technoprops, Faceware, OneHMC, iPhone/ARKit (more later). Each has **mono / stereo** buttons and a **processing preset** (what MetaHuman Animator solve setup it goes through).
- **Body** is a dropdown: commercial mocap software (Xsens, Shogun, Motive, Giant, existing enums) or **markerless inside Unreal**.
- **Weekend scope:** 1 performer. **OneHMC** facial, **Rode** mic audio, **markerless body** through Unreal. Future: unlimited performers.
- **Facial is not realtime.** It must land in the take's Level Sequence for playback after the take.
- **Downloads and offloading happen inside the project.** Madi should not be dragging files around Explorer.
- **No sync hardware yet.** Takes align by record-start for now. A sync box comes later and becomes the timecode source.
- **Actors, projects, days, takes** come from the roster and database (already built). Setup should be quick for a returning actor.
- **Mid-take failure:** alert **and** flag the take. Detailed alarm behavior gets designed later with Madi; build the minimum in §6 and keep it configurable.
- **Weekend hardware (confirmed 2026-10-14):** OneHMC, which can run mono or stereo and is usually stereo, on firmware **0.2.0** before the shoot. A **4K USB webcam** for body (60 fps support unknown; the device agent finds out). A **Rode wireless kit** (square transmitter/receiver style) with the receiver plugged into the P620. **One performer, a friend of Madi's.** Madi is not in the volume; she operates and directs.
- **Performer prep:** Madi builds the performer's MetaHuman (identity solve) before the shoot. During the shoot she wants to **review each take's facial performance on several game rigs**, not just the performer's MetaHuman (§5.1).
- **Body live preview is wanted** if achievable. Epic's markerless solve stays the real body data (§4.2b).
- **Storage:** ~**1 TB** free on the P620 for capture.
- **Host:** P620 runs Unreal; everything lives in the PCAPTool plugin. Repo has push access. Same agent setup (architect, implementer, verifier) plus a device-integration agent per adapter (§8).

---

## 2. What Epic's 5.8 docs say (the facts this design rests on)

Verified 2026-10-14 against the current pages (§12).

**Markerless body is first-party, but offline.** The *MetaHuman Animator Markerless Motion Capture* plugin (Fab, **experimental**, **Windows only**, **UE 5.8+**) solves body only, or face + body, from ordinary mono video. It needs the MetaHuman Animator plugin, Live Link Hub, and the Capture Manager Editor plugin. It is an **offline batch solve**. There is no realtime body from it. Single Camera mode solves face and body from one video; Dual Camera mode takes face and body from separate cameras and combines them in a Level Sequence. All MetaHumans share one skeleton, so no retarget step for MetaHuman targets; "Performer Skeleton" export exists for non-MetaHuman characters.
> Consequence: the realtime part of body this weekend is **watching the body camera feed** (framing, lighting, blur, performer in frame) with the existing agent system. The body animation arrives after the take, the same way the face does.

**Ingest path.** Live Link Hub, then Capture Manager, then Data Devices, then **Mono Video Ingest** pointed at a folder (recurses subfolders), then Add to Queue, then Start. It transcodes and creates capture data assets under `Content/CaptureManager/Imports/<device>`. FFmpeg can replace Windows Media Framework as the transcoder (Live Link Hub Settings, Use Third Party Encoder) and Epic says it reduces invalid-FPS risk. **Use FFmpeg.**

**Solve.** MetaHuman Performance asset: set Footage Capture Data, then pick tracking:
- Face only: default. The Face Solver default depends on the footage's Device Class: **Stereo HMC Rig Solver** for stereo-HMC-class footage (Epic notes it also does better on greyscale visible-light / IR mono HMC footage), otherwise **Generic Rig Solver**. Toggles: Solve Tongue, Head Stabilization.
- Body only: Facial Tracking off, Body Tracking on, Head Movement Mode = Disable.
- Face + body (one video): both on, Face Detector = Small Face Detector, Face Solver = Small Face Solver.
- Export Animation produces an AnimSequence, onto an Existing Skeleton (auto-retarget to MetaHuman) or Performer Skeleton.

**Video capture rules (mono, face and body).** HD or 4K, **60 fps**, H.264 **CRF 18 or better** (~25 Mbps HD). Face at least 1/30 of image width and height; a face under 40×80 px likely fails detection. Camera fixed for the whole take. Even, diffused light. Body: full body head to feet with margin on all sides, camera straight on at neutral height, one performer in frame, matte surfaces, floor distinct from wall, form-fitting clothing that contrasts with the background, visible footwear, no mirrors or polished floors, and no all-white studio.

**HMC rules.** Focus each lens on the nasolabial area. Camera stable relative to the face (well-fitted mount). Even face light, minimal shadows, no blown patches, room lights not visible in frame. Stereo: centre each camera on the upper philtrum (base of the nostrils). Stereo device: vertically aligned pairs, calibratable with Epic's tools, **min 60 fps**, highest resolution possible (many stereo HMCs are 1296×2048), colour beats mono, visible beats IR. **Stereo storage is ~45 GB per minute** at 60 fps 1440×1920.

**Stereo calibration takes.** Start **and** end of every session, plus mid-session after a significant helmet adjustment. About 20 s. Board flat, matte, rigid, sharp corners, handle on the back. Default pattern 89.5 × 134.2 mm, 7.5 mm squares (sample in `/Engine/Plugins/MetaHuman/MetaHumanCalibrationProcessing/Content/Boards`). Move up, down, left, right, roll, and pitch in the plane of the performer's face. No hand on the pattern, no performer in frame, not inverted, not horizontal, not over-rotated.

**Performer.** No glasses, hats, masks, or hair or hands over the face. 1–2 days stubble max. No heavy makeup, no facial piercings. Adult faces only. Identity: MH1 (neutral only), MH12 (14 expressions), MH50 (56). For every expression: face forward, no head tilt, teeth touching but not clenched unless the expression says otherwise.

**Audio.** At least 16 kHz. High SNR, no reverb or echo, **one speaker per file**.

**Hardware.** The P620 clears Epic's recommendation (16+ physical cores, 32 GB RAM, RTX 3070 / 8 GB or better). **Default RHI must be DirectX 12**: `Config/DefaultEngine.ini` does not set it explicitly, so Phase 0 confirms it in Project Settings.

---

## 3. What's in the repo and what happens to it

Measured from `main` at `e4082a8`. ~17k lines in `Plugins/PCAPTool`.

| Area | Files | Verdict |
|---|---|---|
| Data model: `UMocapDatabase`, `FProduction → FShootDay → FSession → FShot → FTake`, roster DataAssets, `PCAPToolPaths.h` | `MocapDatabase.*`, `PCAPToolTypes.h`, `ActorRosterEntry.h`, `PropRosterEntry.h`, `StageConfigAsset.*`, `HMCRigEntry.h` | **Keep.** Extend enums and add the setup preset (§7). |
| Call Sheet + Databases (Slate) | `SPCAPCallSheetPanel`, `SPCAP*DatabasePanel`, `SPCAPRosterCard` | **Keep.** Add the Capture Setup section (§4.1). |
| Mocap Manager bridge | `PCAPMocapBridge.*` | **Keep.** It is how Live Link sources reach `ACapturePerformer`. Not needed for the weekend (no live body stream), but leave it untouched. |
| Take Recorder backend | `PCAPTakeRecorderSubsystem.*` | **Keep, refactor.** `AreActiveStreamsReady()` gates record on Live Link status only; it must gate on **device health from the adapters** instead (§5). Add the audio source and the start/stop fan-out to adapters. |
| Frame analysis + agents: `AnalyzeFrameBGRA`, `FPipelineCheckProfile`, `GetDefinition`, `FHMCCameraAgent`, `FHMCAgentOrchestrator`, skills | `PCAPToolStatics.*`, `HMCCameraAgent.h`, `HMCSkills.cpp` | **Keep. This is the brain.** It is pixel-based and device-agnostic already. Add a body pipeline profile (§4.3). |
| HMC live engine: polling, frame pump, texture reuse, no-blink Slate | `PCAPToolSubsystem.*` | **Refactor.** The transport is ORION-specific in four places (`/control?cmd=no`, `/control?cmd=%s`, `/control?cmd=%s&param=%s`, `/video?cam=%d`) plus the `control.json` parse into `FHMCDeviceStatus`. Move those behind `IPCAPFacialDevice` (§4.2). The frame pump, texture reuse, and agent dispatch stay as they are. |
| HMC component mirror | `HMCMonitorComponent.*` | **Retire.** It duplicates the subsystem, only ticks in PIE, and the Session 5 handoff already flags the two drifting. The Slate UI uses the subsystem. Delete it in Phase 1 after confirming nothing references it. |
| HMC Setup / Preview panels | `SHMCSetupPanel`, `SHMCPreviewPanel`, `SPCAPToolPanel` | **Keep, generalize.** Rename in UI to "Camera Monitor". Same no-blink pattern. The body camera shows up as another feed. |
| ORION telemetry fields | `FHMCDeviceStatus` (exposure0, gain0, topLights, boomPos, …) | **Move** into the Technoprops adapter's private state. `FHMCDeviceStatus` becomes the generic `FPCAPDeviceHealth` (§7). Keep the old fields only if `HMCConfig.json` back-compat requires it. |
| VCam (WVCAM replacement), Volume Visualizer, Vicon plugin | `*VCam*`, `PCAPVolumeVisualizer.*`, `Plugins/LiveLinkViconDataStream` | **Leave alone.** Not in this build. |
| WBP build guides / UMG widgets | `docs/build-guides/*`, `Content/PCAPTool/Widget/WBP_HMCOperatorPanel` | **Retire** from the home build path. Slate is the UI. |

---

## 4. Target design

### 4.1 Capture Setup (per performer, per day)

A section of the Call Sheet. For each called performer, one row:

```
[Actor chip]  Facial: [OneHMC ▾] [Mono|Stereo] [Preset ▾]   Body: [Markerless ▾] [Camera ▾]   Audio: [Rode ▾] [Input ▾]   ● health
```

- Defaults come from the actor's roster entry. **Edit Roster Default** and **Override For This Session** are separate buttons with separate code paths (locked rule).
- Facial vendor list: `Technoprops`, `OneHMC`, `Faceware`, `iPhone (Live Link Face)`. Only OneHMC is wired this weekend; the rest show "adapter not installed" and disable Record if chosen.
- **Mono/Stereo** sets `ECaptureConfiguration` (`MonoHeadMount` / `StereoHeadMount`), which already selects the agent skill set. Stereo also turns on the calibration-take gate (start and end of session).
- **Processing preset** = a named `UPCAPFacialProcessingPreset` DataAsset (§7) holding the MetaHuman Performance settings to use after ingest: solver (Stereo HMC Rig / Generic / Small Face), head stabilization, tongue, Device Class expected. Ship two: `MHA_StereoHMC_Default`, `MHA_MonoHMC_Default`.

### 4.2 Adapters

One interface per category, one class per vendor, registered in a `UPCAPDeviceRegistry` (editor subsystem). The UI lists what the registry has.

```cpp
// Facial: a device that previews, records internally, and hands files back.
class IPCAPFacialDevice {
  virtual FName VendorId() const = 0;                      // "OneHMC"
  virtual bool  SupportsConfig(ECaptureConfiguration) const = 0;
  virtual void  Connect(const FPCAPDeviceAddress&) = 0;
  virtual FPCAPDeviceHealth GetHealth() const = 0;         // polled by the registry
  virtual int32 NumPreviewCameras() const = 0;
  virtual void  RequestPreviewFrame(int32 Cam, FOnPreviewFrame) = 0; // JPEG/BGRA → existing frame pump
  virtual void  StartRecord(const FPCAPTakeIdentity&) = 0;
  virtual void  StopRecord() = 0;
  virtual void  ListTakeFiles(const FPCAPTakeIdentity&, FOnFileList) = 0;
  virtual void  DownloadFile(const FPCAPRemoteFile&, const FString& LocalPath, FOnProgress, FOnDone) = 0;
};
// Body: either a Live Link source (Xsens etc., later) or a video camera (markerless).
class IPCAPBodySource { /* same shape; for markerless: preview frames + record-to-file */ };
// Audio: an input device recorded by Take Recorder, with a live level for the alarm.
class IPCAPAudioSource { /* GetLevelDb(), GetHealth(), ArmTakeRecorderSource(UTakeRecorderSources*) */ };
```

`FPCAPDeviceHealth` = connected, recording, battery %, storage free, fps, dropped frames, temperature, last error, and `int32 IssueFlags` using the existing `EHMCIssueFlag` bits where they apply. **The Technoprops adapter is the existing ORION code moved, not rewritten**, so it keeps working for Precipice.

**Weekend adapters:**

**(a) `FOneHMCFacialDevice`.** What we know (from Madi, not yet verified in code): Soma recorder + Synapse sync box, D-Tap powered, web GUI over Wi-Fi at `soma-xxxx.local`, `:9000` for record, `:8080` for firmware. **Units shipped on pre-0.2.0 firmware with an internal-storage recording bug; the builder said not to record internally until 0.2.0.** Madi is updating to 0.2.0 before the shoot; the adapter reads the firmware version on connect and **blocks Record with a red reason if it is below 0.2.0.** The unit runs mono or stereo (usually stereo); the adapter reports which mode is active and the Capture Setup Mono/Stereo button must match it, or Record is blocked. Unknown and must be observed by the OneHMC device agent before code: how mono/stereo is switched and reported; preview stream format and URL; record start/stop requests; take naming; file list and download endpoints; file container, codec, resolution, fps; whether there are status fields (battery, storage, temp); behavior on Wi-Fi drop mid-take. Method: open the GUI in a browser, read its JS, and capture its network requests. Write it all to `docs/devices/onehmc.md` first. If preview is not available over the network, the HMC feeds show "no preview" and the agents run on the first downloaded file after the take (still useful, but say so in the UI).

**(b) `FMarkerlessBodyCamera`.** One fixed USB/UVC camera (Epic lists any UVC device for mono video; disable autofocus and auto-exposure where possible). One process owns the camera: an **FFmpeg child process** (dshow input) that records **H.264 CRF 18, 60 fps** straight into the take folder, **and** emits a low-rate MJPEG preview (5 to 10 fps) on localhost that the existing frame pump reads. That avoids two apps fighting over the camera on Windows. The body feed runs through the agent system with the `MarkerlessBody` profile (§4.3). FFmpeg path lives in plugin settings; the same FFmpeg is pointed at Live Link Hub's transcoder setting. Device agent verifies: chosen camera actually delivers 60 fps at the chosen resolution, dropped-frame reporting from FFmpeg stderr, and clean stop (file finalized, `moov` written) when Stop is pressed or the process is killed.

*Camera mode (Madi's camera is 4K, 60 fps unknown).* The device agent lists every mode the webcam offers (`ffmpeg -f dshow -list_options true -i video="<name>"`), then measures real delivered fps for each candidate, because webcams often advertise modes they can't sustain. Epic's rule is HD or 4K **at 60 fps**. Many 4K webcams only do 4K at 30 and 1080p at 60. **If so, default to 1080p60 over 4K30** (Epic sets 60 fps as the target and only requires a face at least 1/30 of the frame, which 1080p clears for a full-body framing). Record the measured modes in `docs/devices/body-camera.md` and make the mode a setting, not a constant. If the camera can't hold 60 fps at any useful resolution, say so in the fact sheet and Capture Setup shows amber with the reason.

*Live body preview (stretch, Phase 4b).* Epic has no realtime body path in 5.8: the markerless solve is offline, and the realtime MetaHuman path is face only. So live body is a **monitoring preview, not capture data**. Plan: a small pose-estimation process (a MediaPipe-class model) reads the **same localhost preview stream** FFmpeg already emits, so the webcam still has one owner. It publishes a Live Link skeleton subject that drives a preview character in the level. Take Recorder records it as a reference track, labelled "preview, not final". The real body animation still comes from Epic's offline solve on the recorded video. It is worth it for directing (you see the performer move a character live) but it is the first thing cut if the weekend runs short. The device agent must confirm the preview stream framerate is enough to be watchable (target 15+ fps for the pose process, separate from the 5 to 10 fps the frame agents need).

**(c) `FRodeAudioSource`.** Record through Take Recorder's microphone audio source so the audio lands in the take's Level Sequence with no extra step. Live level meter for the alarm (silence for N seconds while recording = alarm; clipping above threshold = warning). Record at 48 kHz (clears Epic's 16 kHz floor). One performer, one mic, one file, per Epic's one-speaker rule. Device agent verifies: which Take Recorder source class records mic input in 5.8 (resolve by `/Script/...` path the same way `AddLiveLinkSource` does), the Rode shows as a selectable input, and the recorded asset sample rate. Madi's setup: a Rode wireless kit with the receiver on the P620 over USB (it enumerates as a USB audio input) and the transmitter on the performer. The device agent records the exact model from the USB device name. **If the model supports on-board recording in the transmitter, turn it on as a safety backup** (a wireless dropout then costs nothing), and add a "drop transmitter files into the take folder" step to offload. Alarm on: receiver missing, silence while recording, clipping, and the receiver's own disconnect if the OS exposes it.

### 4.3 Agents (the floor watchers)

Reuse `FHMCAgentOrchestrator` / `FHMCCameraAgent` for every camera feed, facial or body. One agent per camera. Add:

- `ECapturePipeline::MarkerlessBody` and `ECaptureConfiguration::FixedBodyCamera`, with a `GetDefinition` branch whose thresholds cite the body sections of the Video Capture Guidelines: performer fully in frame with margin, subject size band, exposure, lighting direction, motion blur, single performer.
- Body framing needs a body detector, not the face heuristic. Phase 3 starts with the cheap version (silhouette or bright-region extent plus "touches frame edge" means out of frame), behind the same skill interface. A proper pose model can slot in later, same as the planned LANDMARK tier for faces.
- **Device-health agent** per adapter: turns `FPCAPDeviceHealth` into alarm events (disconnect, stopped recording while Take Recorder is recording, low battery, low storage, fps under target, dropped frames).
- **Reference rubric.** The Setup view shows Epic's good and bad images for the active preset beside the live feed, loaded from `Guidelines/images/` via the manifest (`presets` field). If images are missing, show a one-line "run fetch_guidelines.ps1" note, never an error.

### 4.4 Guidelines manifest

`Plugins/PCAPTool/Guidelines/manifest.json`: 66 images from Epic's 5.8 pages, each tagged `good`/`bad`, category, the presets it applies to, the issue flag or board state it illustrates (`maps_to`, or `operator` when a person judges it), and its source page. `fetch_guidelines.ps1` downloads them into the gitignored `images/`. Skill threshold comments should reference the manifest `id` they were tuned against.

### 4.5 Storage and offload budget (decide before the shoot)

Epic's figure for stereo HMC capture is **~45 GB per minute** (60 fps, 1440×1920). With ~1 TB free, that is roughly **20 minutes of stereo footage in total** before body video, audio, and ingest copies. Ingest also transcodes into new capture data assets, so plan for raw plus imported.

The bigger risk is **offload speed**. If OneHMC files are anywhere near that size and the only path off the unit is Wi-Fi, a one-minute take can take many minutes to download, and offload falls behind the shoot. The OneHMC device agent must measure, on the real unit, before Phase 4 is designed in detail:
- Actual file size per minute in stereo and in mono at the settings Madi will use.
- Download throughput over the network path available (Wi-Fi, and Ethernet if the Soma has it).
- Whether storage can be pulled and read directly on the P620 (card or drive), and how long that takes.

Then the offload design picks one of: **background during the shoot** (only if throughput keeps up), **batch at breaks** (offload queue runs when Madi presses "Offload now" or between setups), or **media pull** (copy from the removed storage, with the tool verifying files against the take list). The Operator Console shows: free space on `CaptureRoot`, estimated minutes left at the current config, and the offload queue backlog in minutes of footage. **Free space below a set threshold (default 15 minutes of capture) is amber; below 5 minutes is red.** The HMC's own free storage is a separate health item.

---

## 5. Take flow (one Record press)

1. **Pre-roll gate** (`AreActiveStreamsReady` becomes `IsFloorReady`): every adapter assigned to a called performer is connected and healthy, no red agent flags, and for stereo the session-start calibration take exists. Amber is allowed with a confirm. Red blocks Record and shows the reason.
2. **Start fan-out:** Take Recorder (audio source, plus any Live Link sources the Mocap Manager has), `IPCAPFacialDevice::StartRecord`, `IPCAPBodySource::StartRecord`. Log each device's acknowledged start time (`FPlatformTime` and wall clock) to the take. This is the alignment data until the sync box exists.
3. **During:** agents and health run; any red event raises the alarm (§6) and appends to the take's alarm log.
4. **Stop fan-out**, then harvest the Level Sequence (existing `HandleTakeFinished`).
5. **Offload (automatic, background):** list and download the HMC take files and move the body video into the **take folder on disk**: `<CaptureRoot>/<Production>/<Day>/<Shot>/<TakeID>/{face,body,audio}/`. `CaptureRoot` is a plugin setting (default `D:/PCAPCapture`). It is **outside** `Content/`; raw video never goes in Git or the asset tree. Write `take.json` (identity, devices, presets, start offsets, alarms, file hashes). Progress shows on the Operator Console; failures raise an amber alarm and are retryable.
6. **Ingest + solve (Phase 5):** queue Capture Manager Mono Video Ingest on the take folder, then create a MetaHuman Performance per stream with the preset's settings, Process, Export Animation, and add the AnimSequence to the take's Level Sequence on the performer's MetaHuman, offset by the logged start delta. **Spike first:** confirm what is scriptable in 5.8 (Python or C++) for Capture Manager ingest and for MetaHuman Performance process/export. If ingest is not scriptable, the tool opens Live Link Hub with the folder path ready and a one-click "ingest done" re-scan; solve/export still automated if the Performance API allows. Document the outcome in `docs/devices/mha-automation.md`.

### 5.1 Review on multiple game rigs

Madi wants to judge a take's facial performance on several characters, not only the performer's MetaHuman. Design:

- A **Rig Set** DataAsset per production: a list of target characters (skeletal mesh or Blueprint) plus, per target, how face animation reaches it.
- **MetaHuman targets:** all MetaHumans share one face rig, so the exported face AnimSequence plays on any of them directly. No mapping step. This is the cheap case and should work first.
- **Non-MetaHuman game rigs** (custom or extracted characters): need a per-rig mapping from MetaHuman face controls/curves to that rig's controls. Use what the engine already provides for this (a Control Rig or Pose Asset that reads MetaHuman curves and drives the target), authored once per rig and stored on the Rig Set entry. No mapping, no entry: the target shows "needs face mapping" instead of silently playing nothing.
- **Take review:** after the solve lands (Phase 5), the take's Level Sequence gets one track per Rig Set target, and the Operator Console has a "Review on: [rig ▾]" switcher (or side-by-side). Body animation follows MetaHuman retarget rules: Existing Skeleton export for MetaHumans, IK Retargeter for others (the repo's `FRetargetConfig` already holds IK Rig/Retargeter refs).
- Timing: this is Phase 5b. It depends on the solve, and facial is not realtime by Madi's decision, so review happens after each take, not live.

---

## 6. Floor alarm (minimum for the weekend)

`UPCAPAlarmSubsystem` (editor subsystem). Inputs: agent stable flags, device health, audio level, offload results. Behavior:

- **Red event while recording:** full-screen red flash overlay on a chosen monitor (own Slate window, topmost, borderless, pulses, so it is visible from across the room), plus a chime (`USoundBase` set in settings, played through the editor's audio device, looping until acknowledged), plus the **take is flagged** (`FTake.bAutoFlagged`, reason, timecode/offset of the first fault, all events appended to `FTake.Alarms`).
- **Red event while idle:** same flash and chime, no flag.
- **Amber:** border pulse on the Operator Console and Camera Monitor, single soft chime, no flag.
- **Acknowledge:** a big button on the overlay, plus a keyboard shortcut. A remote acknowledge (phone or controller) is a later decision.
- **Debounce:** reuse the agents' hysteresis; health events need N consecutive bad polls (existing pattern: 3). Never alarm on a single dropped poll.
- Every threshold and the overlay monitor index live in `UPCAPToolSettings` so Madi can tune them without a rebuild.
- Madi's open note: full-screen red on her OLED repeatedly is a burn-in risk. Settings offer `Overlay: FullScreen | Border | Off`.

---

## 7. Data model changes

In `PCAPToolTypes.h` (append enum values at the end; never reorder, because saved assets store the index):

- `EFaceSystem`: add `OneHMC`, `Faceware`.
- `EBodySystem`: add `MarkerlessVideo`.
- `EAudioSystem`: `RodeWirelessGo2` exists. Add a generic `UsbAudioInput` so any Rode works.
- `ECapturePipeline`: add `MarkerlessBody`.
- `ECaptureConfiguration`: add `FixedBodyCamera`.
- New `FPCAPDeviceHealth` (§4.2). New `FPCAPAlarmEvent { FDateTime When; double TakeOffsetSec; FName Source; int32 Flags; EHMCIssueSeverity Severity; FString Reason; }`.
- `FTake`: add `bool bAutoFlagged`, `TArray<FPCAPAlarmEvent> Alarms`, `FString CaptureFolder`, `TMap<FName,double> DeviceStartOffsets`, `ESyncMethod Sync = RecordStart` (values `RecordStart`, `Timecode`).
- New DataAsset `UPCAPFacialProcessingPreset` (solver choice, head stabilization, tongue, expected device class, notes) under `PCAPPaths::Databases()/FacialPresets`.
- New DataAsset `UPCAPRigSet` (Phase 5b): array of `{ TSoftObjectPtr<UObject> Target; bool bIsMetaHuman; TSoftObjectPtr<UObject> FaceMapping; FRetargetConfig BodyRetarget; }` under `PCAPPaths::Databases()/RigSets`. `FProduction` gets a soft ref to its Rig Set.
- `UActorRosterEntry`: add `FName DefaultFacialVendor`, `ECaptureConfiguration DefaultFacialConfig`, `TSoftObjectPtr<UPCAPFacialProcessingPreset> DefaultFacialPreset`, `FName DefaultBodySource`, `FName DefaultAudioSource`. The existing `MetaHuman` soft ref is the solve and playback target.
- `HMCConfig.json` stays readable (keep the deprecated `WebSocketEndpoint` field as documented).

---

## 8. Agent team

| Agent | Owns | Must not |
|---|---|---|
| **Architect** | This spec, phase plans, interface changes, the device fact sheets' review | Write production code |
| **Implementer** | Code for the phase Madi approved | Merge hardware-touching code without a device agent's PASS log for that exact behavior |
| **Verifier** | Clean build on the P620, automation tests, reading diffs against this spec and against Epic's docs (every threshold has a source comment), checking the end-state checklist | Fix what it finds (it reports; the implementer fixes) |
| **OneHMC device agent** | `docs/devices/onehmc.md`; observes the real Soma; writes a scripted probe (Python or PowerShell under `Tools/probes/`) that exercises connect, preview, start, stop, list, download; signs PASS/FAIL per adapter method against the real unit | Guess endpoints; record internally on pre-0.2.0 firmware |
| **Body camera device agent** | `docs/devices/body-camera.md`; FFmpeg capture recipe for the actual camera; measures real fps and drops; verifies file finalization on stop and on kill | Accept a camera that cannot hold 60 fps without saying so |
| **Audio device agent** | `docs/devices/rode.md`; Take Recorder mic source class path in 5.8; level metering; sample-rate check on the recorded asset | |

Device agent PASS logs live in `docs/devices/<device>-verification.md` with date, firmware or driver version, and the exact command or probe run. The implementer cites the log line in the commit message.

---

## 9. Implementation order (sign-off after each phase)

**Phase 0: Baseline (Fri).** Pull `main`, full rebuild on the P620 (UE 5.8, editor closed). Confirm DX12 is the default RHI. Run `fetch_guidelines.ps1`. Install the Markerless Motion Capture plugin from Fab; enable MetaHuman Animator, Capture Manager Editor; confirm Live Link Hub opens and the FFmpeg transcoder setting is set. Device agents start fact sheets in parallel. *Gate: clean build, plugins present, three fact sheets drafted.*

**Phase 1: Adapter seam (Fri).** Add `IPCAPFacialDevice`, `IPCAPBodySource`, `IPCAPAudioSource`, `UPCAPDeviceRegistry`, `FPCAPDeviceHealth`. Move the ORION transport out of `UPCAPToolSubsystem` into `FTechnopropsFacialDevice` with **no behavior change**. Retire `UHMCMonitorComponent` (C++ only references it in comments; its delegates are duplicated in the subsystem; check `Content/PCAPTool/Widget/WBP_HMCOperatorPanel` in the Reference Viewer before deleting, since a UMG asset may still bind to it). Data model additions from §7. *Gate: builds; ORION path identical (verifier diffs behavior by reading code); existing panels still open.*

**Phase 2: Weekend adapters (Fri–Sat).** OneHMC, body camera, Rode, each only after its device agent's PASS log. Capture Setup row in the Call Sheet. Body feed in Camera Monitor. *Gate: all three devices connect, preview where available, start/stop from Unreal, files appear in the take folder.*

**Phase 3: Agents + alarm (Sat).** `MarkerlessBody` profile and body framing skill, device-health agent, `UPCAPAlarmSubsystem` with overlay, chime, and auto-flag. Pre-roll gate. Reference rubric in Setup. *Gate: pull the HMC's Wi-Fi mid-take, the alarm fires within ~3 s and the take is flagged; walk out of the body frame, the alarm fires; mute the mic, the alarm fires.*

**Phase 4: Offload (Sat).** Mode chosen from the §4.5 measurements (background, batch at breaks, or media pull). Take folder layout, `take.json`, progress, retry, storage readout and low-space alarms. *Gate: three back-to-back takes offload in the chosen mode without manual file moves; free-space and backlog readouts are correct.*

**Phase 4b: Live body preview (stretch, Sat).** Pose-estimation process on the shared preview stream, Live Link skeleton subject, preview character in the level, recorded as a labelled reference track (§4.2b). *Gate: performer moves, preview character follows at a watchable rate, nothing about the real body recording changes.* Cut first if time runs short.

**Weekend line.** Phases 0–4 are the weekend; 4b if time allows. Phase 5 is next.

**Phase 5: Ingest, solve, playback.** Automation spike first (§5 step 6), then automate as far as 5.8 allows. Face and body AnimSequences land in the take's Level Sequence, offset by logged start deltas. *Gate: open a take, press play, see face + body + audio together.*

**Phase 5b: Multi-rig review.** Rig Set DataAsset, MetaHuman targets first, then per-rig face mappings for non-MetaHuman game rigs, review switcher in the Operator Console (§5.1). *Gate: one take plays on the performer's MetaHuman and at least one other character from the Rig Set.*

**Phase 6: Multi-performer and sync.** N performers in Capture Setup, per-performer agents and alarms, sync box as `ESyncMethod::Timecode`, Xsens adapter via Live Link through the existing Mocap Manager bridge.

---

## 10. End-state checklist (weekend)

- [ ] Clean build on the P620 from `main`, UE 5.8, DX12.
- [ ] Guideline images fetched locally; none committed.
- [ ] Capture Setup row picks OneHMC + Mono/Stereo + preset, Markerless + camera, Rode + input. Roster default and session override are separate buttons.
- [ ] Technoprops/ORION behavior unchanged (code moved, not rewritten).
- [ ] OneHMC, body camera, Rode each have a fact sheet and a PASS log.
- [ ] Record press starts all three; Stop stops all three; start offsets logged.
- [ ] Pre-roll gate blocks on red with a readable reason.
- [ ] Body and HMC feeds judged by agents with Epic-sourced thresholds; Setup shows the matching good/bad references.
- [ ] Red during a take: full-screen flash (or border, per setting), chime until acknowledged, take auto-flagged with reason and offset.
- [ ] OneHMC firmware below 0.2.0, or a Mono/Stereo mismatch with Capture Setup, blocks Record with a readable reason.
- [ ] Body camera mode chosen from measured modes (1080p60 preferred over 4K30 if 4K60 isn't real).
- [ ] Storage readout: free space, minutes left at the current config, offload backlog. Amber/red thresholds fire.
- [ ] Files offload into `<CaptureRoot>/<Prod>/<Day>/<Shot>/<TakeID>/` with `take.json`, no manual file moves.
- [ ] Audio is in the take's Level Sequence.
- [ ] `docs/` updated: this handoff's status, device sheets, tool how-tos.

---

## 11. Open questions for Madi (answers change the build)

Answered 2026-10-14: OneHMC is mono or stereo (usually stereo), on 0.2.0 before the shoot. Body camera is a 4K USB webcam, 60 fps unknown. Rode wireless kit, receiver into the P620. Performer is a friend, not Madi. Madi builds the performer's MetaHuman beforehand. ~1 TB free. Still open:

1. **Mono or stereo for this weekend?** Stereo gives the depth solve but needs calibration takes at the start and end of the session, plus ~45 GB/min (about 20 minutes of total footage on 1 TB). Mono is lighter and simpler, with lower solve quality. The device agent's real file-size and offload-speed numbers (§4.5) should go to Madi before she decides.
2. **Which game rigs** go in the Rig Set (§5.1)? MetaHumans are free. Each non-MetaHuman rig needs a one-time face mapping.
3. **Capture folder:** which drive holds the 1 TB? `CaptureRoot` defaults to `D:/PCAPCapture` until Madi says otherwise.

---

## 12. Sources (Epic, UE 5.8 docs, read 2026-10-14)

- Video Capture Guidelines: https://dev.epicgames.com/documentation/metahuman/metahuman-animator-video-capture-guidelines-in-unreal-engine
- Depth Data Capture Guidelines: https://dev.epicgames.com/documentation/metahuman/metahuman-animator-depth-data-capture-guidelines-in-unreal-engine
- Calibration Takes: https://dev.epicgames.com/documentation/metahuman/calibration-takes
- Capture Device Requirements: https://dev.epicgames.com/documentation/metahuman/metahuman-animator-capture-device-requirements
- Performer Guidelines: https://dev.epicgames.com/documentation/metahuman/metahuman-animator-performer-guidelines-in-unreal-engine
- Expressions for Likeness Calibration: https://dev.epicgames.com/documentation/metahuman/expressions-for-likeness-calibration
- Audio Capture Recommendations: https://dev.epicgames.com/documentation/metahuman/audio-capture-recommendations
- Animation from Mono Video Capture (markerless): https://dev.epicgames.com/documentation/metahuman/metahuman-animation-from-mono-video-capture-in-unreal-engine
- 1. Import Your Footage: https://dev.epicgames.com/documentation/metahuman/metahuman-animator-01-import-your-footage-in-unreal-engine
- 2. Process and Export Your Animation: https://dev.epicgames.com/documentation/metahuman/metahuman-animator-02-process-and-export-your-animation-in-unreal-engine
- Hardware Requirements: https://dev.epicgames.com/documentation/metahuman/metahuman-hardware-requirements-in-unreal-engine
- Markerless Motion Capture plugin (Fab): https://www.fab.com/listings/4095b8e0-3eff-44f1-acb4-cb40b99228b9
- Live Link Hub: https://dev.epicgames.com/documentation/unreal-engine/live-link-hub-in-unreal-engine

Prior repo specs this builds on: `docs/specs/2026-06-16-hmc-pipeline-watcher-definition-design.md`, `docs/superpowers/specs/2026-06-16-hmc-camera-agent-architecture-design.md`, `docs/specs/2026-06-29-ue58-mocap-manager-integration-design.md`, `docs/handoffs/HMC_SESSION5_HANDOFF.md`. Note: the watcher spec's image IDs are truncated and point at 5.7-era pages. `Guidelines/manifest.json` supersedes them.
