# PCAP TOOL — HMC MONITOR HANDOFF (Session 5 → next session)

**Machine:** RSPN-RTEAST — `C:\Users\mocapstaff\Desktop\PCAPPipeline`
**Engine:** UE 5.7.4 · **Repo:** `github.com/mgarman555/PCAPPipeline` (public)
**Plugin:** `Plugins/PCAPTool/`
**Build:** editor CLOSED → Visual Studio **Rebuild Solution** (Development Editor | Win64). Live Coding can't apply header changes — always full-rebuild after pulling. macOS has no UE toolchain, so all C++ here is authored brace/symbol-checked only; **the Windows compile is the real test.**

---

## 0. GET THE CODE — and a CRITICAL git note

```
cd C:\Users\mocapstaff\Desktop\PCAPPipeline
git fetch origin && git checkout main && git reset --hard origin/main   # foolproof sync
git rev-parse --short HEAD     # confirm you're on the latest
```
If a plain `git pull` ever "does nothing" but the code is missing, it's a blocked fast-forward (stray/linter edits) — `git reset --hard origin/main` fixes it.

**⚠️ Two workstreams share `main`.** While Session 5 built the HMC monitor, a **parallel data-model migration** (Phase 1/2) was also committing + pushing to `main`:
- `c3faa04 refactor(pcap): rename HMC ActorName -> ActorID across subsystem, component, Slate panels` — **the HMC structs and all my Slate panels now use `ActorID`, not `ActorName`.** If you grep older handoffs/specs that say `ActorName`, it's `ActorID` now (`FHMCDeviceConfig.ActorID`, `FHMCDeviceStatus.ActorID`, `FHMCCameraFeed.ActorID`).
- `FStageConfig` was promoted to a `UStageConfigAsset` DataAsset; `FProduction`/`FShootDay` ref it by soft ptr.
- Phase 2 Take Recorder spec + deps (`TakeRecorder/TakesCore/MovieScene`) and `DrivenTarget` on `FShotSubject` were added.
- **This migration was authored by the other workstream and has NOT been verified to compile together with the HMC code on Windows.** First Windows rebuild may surface integration errors — they'd be in the data-model layer, not the HMC logic. Read the compile errors; most likely any remaining `ActorName` reference or a missing include for the soft-ref'd assets.

---

## 1. ARCHITECTURE — two parallel HMC paths (do not merge)

`UPCAPToolSubsystem` (UEngineSubsystem, `GEditor` timer — ticks in-editor) and `UHMCMonitorComponent` (on a PCAPManager actor, world timer — does NOT tick outside PIE) are **independent** implementations of the same HMC logic. **The Slate UI uses the SUBSYSTEM.** The component is for the (not-yet-built) UMG path.

> **Recurring trap:** the two drift. `AssignActor` was added to the component but not the subsystem and broke the Setup-panel build (`'AssignActor' is not a member of UPCAPToolSubsystem`). When you add a method the Slate panels call, add it to **both** (or at least the subsystem). Methods the panels use that now exist on both: RegisterDevice, UnregisterDevice, GetRegisteredDevices, AssignActor, ConnectAll/ConnectDevice, DisconnectDevice, GetDeviceStatus, GetAllDeviceStatuses, GetFeedsForActor, SetCameraRole, GetLastFrame, GetEffectiveIssueFlags, SendDeviceCommand, SaveConfig.

**Transport is HTTP only** (no WebSocket anywhere). Status: `GET http://[IP]/control?cmd=no&param=`. Video: `GET http://[IP]/video?cam=0` (Top) / `cam=1` (Bot) — **0-based** (`cam=2` returns HTTP 400; this was a real bug). Commands: `GET http://[IP]/control?cmd=<X>&param=<Y>[&extra=val]&_=<ticks>`.

**Files:**
- `Public/PCAPToolTypes.h` — `FHMCDeviceStatus` (telemetry), `FHMCDeviceConfig`, `FHMCCameraFeed`, enums incl. `EHMCConnectionState`, `EHMCCameraRole`, `EHMCIssueFlag`/`EHMCIssueSeverity`/`EHMCManualIssue`.
- `Private/PCAPToolSubsystem.{h,cpp}` — the live HMC engine (Slate path). Poll, frame pump, texture, commands, issue flags.
- `Private/HMCMonitorComponent.{h,cpp}` — mirror for the component path.
- `Private/PCAPToolStatics.{h,cpp}` — shared pure helpers: `EvaluateCameraIssues`, `GetIssueSeverity`, `GetIssueBannerText`, `FrameHasSubject` (face heuristic).
- `Private/SPCAPToolPanel.cpp` — host tab (Setup/Preview buttons → `SWidgetSwitcher`, opens on Preview).
- `Private/SHMCPreviewPanel.{h,cpp}` — Preview (read-only monitor, all devices).
- `Private/SHMCSetupPanel.{h,cpp}` — Setup (master-detail: list + selected-device controls).

---

## 2. WHAT WORKS (built this session — on `main`)

**Live video, fast.** Both cameras stream concurrently (one chained request per camera, re-armed off HTTP completion — runs without a world timer). Measured **32 fps**, ~9 ms decode, ~150 KB/frame. Re-arm happens *before* decode (pipelines net+decode). Texture is **reused in place** (`UpdateFrameTexture` → `UpdateTextureRegions`, filled synchronously on first/size-change so it never flashes blank) — one stable `UTexture2D` per camera in `FrameTextureCache`.

**No-blink Slate rendering (important pattern).** Both panels **build their cards ONCE** and bind every dynamic value to `*_Lambda` getters reading live status; a 30 fps `OnFastRepaint` `Invalidate(Paint)` updates them in place. `RefreshCards`/`RefreshDeviceList` rebuild **only when the device SET changes** (sorted-name compare). Feed images are persistent `SImage`s whose `FSlateBrush` (in `FeedBrushPersist`, keyed `"Device_Cam"`) is repointed to the stable texture each paint. *Tearing widgets down on a timer = the blink we fought for several commits — don't reintroduce it.*

**Vitals colors** (`SHMCPreviewPanel`/`SHMCSetupPanel` color helpers AND `EvaluateCameraIssues` thresholds, kept in sync): Battery green >14 / amber 13.6–14 / red ≤13.6 V · Storage >100 / 50–100 / <50 GB · Temp <70 / 70–84 / ≥85 °C · CPU <60 / 60–85 / ≥85 %.

**Issue borders + banners.** Feed border/banner driven by `GetEffectiveIssueFlags` → `GetIssueSeverity`/`GetIssueBannerText`. Includes a **red "NO FACE IN FRAME"** when the frame has no subject (brightness heuristic `FrameHasSubject`, threshold ~40/255 — tunable). `ClipNotReady` is intentionally NOT a border flag (it's normal at idle).

**Connection robustness.** Status poll debounced: only goes Offline after **3 consecutive** failures (so a transient miss under video load doesn't blank the feed). Poll interval 3 s, timeout 2.5 s. Devices auto-connect when a panel opens (`ConnectAll`, idempotent).

**Preview panel:** per-device cards (auto-grouped), Top/Bot feeds, 6 vitals, recording indicator, quip. Opens by default.

**Setup panel (master-detail):**
- **+ Add Device** → modal `SWindow`: Name + IP, **Connect** / **✕**, caption "Name and IP address are saved to the database upon completion." Connect → `RegisterDevice` (persists to `Saved/PCAPTool/HMCConfig.json`).
- Compact device rows: `NAME → [actor dropdown]` (`SComboButton`, seeded `kevinDorman/madiGarman/mannyTester` — **TODO: source from PCAPDatabase actor roster**) with IP below + live connection state. Picking an actor → `AssignActor`. **The name is clickable → selects the device for the detail panel.** First device auto-selected.
- **✕ button per row REMOVES the device** (this session's last change): `UnregisterDevice` → gone from `HMCConfig.json` (database) AND Preview. (The handler is still named `OnDisconnectDevice` — rename to `OnRemoveDevice` for clarity when convenient.)
- Detail panel (selected device): Top/Bot feeds styled like Preview (issue border + banner) + a per-camera **Position dropdown** (Top/Bottom/Left/Right → `SetCameraRole`).
- Controls (ganged = both cams): **EXPOSURE = three single-digit dropdowns** `[ones].[tenths][hundredths]` (e.g. 4.55); GAIN +/- stepper; TOP/BOT LIGHT +/- steppers; **BOOM dropdown** (Left/Right).
- Bottom button: **"Prepped for Preview"** → `ConnectAll` + `SaveConfig`.

---

## 3. DEVICE COMMANDS — confirmed vs PENDING capture

`SendDeviceCommand(dev, Cmd, Param, ExtraKey, ExtraVal)` builds `GET /control?cmd=<Cmd>&param=<Param>[&ExtraKey=ExtraVal]&_=<ticks>`. The `&_=<ticks>` cache-buster is required (the device/browser caches `GET /control`; without it, repeated/identical commands don't re-fire).

| Control | Token | Param | Status |
|---|---|---|---|
| **Exposure** | `exposure` | **value × 100** (status `exposure0` is value × 1000, so param = `exposure0/10`; 4.55 → param 455) | ✅ **CONFIRMED** from captured request |
| Gain | `gain` | gain value (gain0=2) | ❓ best-guess, untested |
| Top light | `topLights` | 0–100 | ❓ best-guess (lowercase `toplights` did nothing; camelCase is guess #2) |
| Bot light | `bottomLights` | 0–100 | ❓ best-guess |
| Boom | `boom` | 0=Left, 1=Right | ❓ best-guess |

**To confirm a pending token:** ORION web client (`http://192.168.50.117`) → Chrome F12 → Network → Clear → nudge the control once → right-click the new `control` request → **Copy as cURL** → that gives the exact `cmd=` and param scaling. Exposure proved this method; gain/lights/boom just need the same capture, then update the token strings in `SHMCSetupPanel::OnGainStep`/`OnLightStep`/`OnBoomChosen`.

---

## 4. DEVICE REFERENCE — ORION HMC (`192.168.50.117`, firmware 2.12.0)

`control.json` keys → fields (parsed in both `OnPollResponse`):
`nRecording`→bIsRecording, `batteryVoltage`(14.4 nom 4S)→BatteryVoltage, `availableStorageInMB`→AvailableStorageMB, `cpuUsage`→CPUUsagePercent, `cpuTemp`(**not** temperature)→TemperatureCelsius, `frameRate`→FPS, `skippedFrames0/1`→DroppedFrames0/1, `exposure0/1`(value×1000)→Exposure0/1, `gain0/1`→Gain0/1, `topLights`/`bottomLights`→TopLights/BottomLights, `streaming0/1`→bStreaming0/1, `width`(2048)/`height`(1536)→FrameWidth/Height, `rotation0/1`(90)→Rotation0/1, `boomPos`→BoomPos, `takename`→CurrentTakeName, `lastMovieIntegrityStatus`(**not** lastClipStatus)→LastClipStatus.
Cameras are 2048×1536 **rotated 90°** → display portrait 1536×2048; feeds size to this aspect (`ScaleToFit`, whole frame shown — the device pre-rotates, so we don't rotate in-engine).

---

## 5. PENDING / NEXT

1. **Capture & lock gain / lights / boom tokens** (§3) — exposure is the only confirmed one. Until then those controls won't move the device.
2. **Actor dropdown source** — currently hardcoded test names; wire it to the PCAPDatabase actor roster (note: roster entries now use `ActorID`).
3. **Reconcile the data-model migration** (§0) — first Windows rebuild of `main` may need fixes where Phase 1/2 (`ActorID`, `UStageConfigAsset`, Take Recorder) meets the HMC code. If the HMC plugin fails to compile, that's the most likely source, not the HMC logic itself.
4. **UMG path** — the component mirror (`UHMCMonitorComponent`) is fully featured but no UMG widget consumes it yet; `WBP_HMCMonitorPanel_BuildGuide.md` describes it. The Slate panels are the live UI.
5. Rename `OnDisconnectDevice`→`OnRemoveDevice` (cosmetic).
6. Boom dropdown is Left/Right only — extend if more positions exist.

---

## 6. KEY PATTERNS (so the next session doesn't relearn the hard way)

- **Live Slate data = build-once + `*_Lambda` bindings + 30 fps `Invalidate(Paint)`.** Never rebuild card widgets on a timer (blink).
- **Frame pump = per-camera chain re-armed off HTTP completion**, not a timer (works without a world). Re-arm before decode.
- **Texture = reuse in place** (`UpdateTextureRegions`), fill synchronously on (re)create so it's never blank.
- **Status debounce** prevents transient poll misses from blanking feeds.
- **Cache-buster** on every device command.
- **Subsystem/component parity** — add Slate-facing methods to the subsystem.
- Diagnostics live in the Output Log under `[PCAPTool]` (connection transitions, frame failures, `texture created WxH`, and an fps/decode/KB line every 30 frames per camera).
