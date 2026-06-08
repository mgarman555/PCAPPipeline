# WBP_HMCMonitorPanel — Blueprint Build Guide

One Editor Utility Widget. Two modes (Setup / Preview) on a widget switcher.
Opens in **Preview** by default. This single panel replaces the older
`WBP_HMCSetup` / `WBP_HMCPreview` / `WBP_HMCOperatorPanel` guides — those are
deprecated.

**Location:** `Content/PCAPTool/Widgets/WBP_HMCMonitorPanel.uasset`
**Parent:** Editor Utility Widget
**Reference mockup:** `hmc_monitor_v3.html` — read it before building.

---

## 0 — DATA ARCHITECTURE (read this first — it is why the last builds were blank)

The panel talks to a `UHMCMonitorComponent` on a `PCAPManager` actor in the
level. Everything below depends on getting this wiring right.

**The one rule that was broken before:** *the component's own poll timer does
not fire on an editor-placed actor outside PIE.* So the widget must drive polling
itself, and **video frames are pulled automatically as part of each status poll**
— you do not request frames separately.

```
EUW looping timer (every PollIntervalSeconds)
  → HMCComp.PollAllDevicesNow()
      → for each connected device: HTTP GET /control
          → OnStatusUpdated(DeviceName, Status)   ← vitals, issue flags
          → (C++ then auto-pulls both camera frames)
              → OnFrameReceived(DeviceName, CameraIndex, Frame)  ← Top=0, Bot=1
```

So the widget only has to:
1. `ConnectAll()` once (starts the connection + populates the poll set).
2. Bind `OnStatusUpdated`, `OnFrameReceived`, `OnConnectionChanged`.
3. Run **one** looping timer that calls `PollAllDevicesNow()`.

That is the entire data path. If frames or vitals are blank, it is one of those
three things — not the transport. Watch the Output Log for
`[PCAPTool] HMC ORION -> Connected` and any `frame request failed (HTTP …)`
warnings; the C++ layer logs both.

> Do **not** call `PollHMCDevicesNow` — that is the legacy `UPCAPToolEditorWidget`
> on a different registry. Use `PollAllDevicesNow` on the component.

### C++ surface the widget uses (all BlueprintCallable on UHMCMonitorComponent)

| Call | Purpose |
|---|---|
| `ConnectAll()` / `ConnectDevice(Name)` | start connection (synchronous → Connected) |
| `PollAllDevicesNow()` | drive one poll cycle (status + auto frames) — call on the EUW timer |
| `RegisterDevice(FHMCDeviceConfig)` | add/update a device; leave `WebSocketEndpoint` empty |
| `AssignActor(DeviceName, ActorName)` | reassign a device to an actor (Combo_Actor) |
| `GetAllDeviceStatuses()` / `GetDeviceStatus(Name)` | read vitals (`FHMCDeviceStatus`) |
| `GetFeedsForActor(ActorName)` | feeds for one actor (Preview grouping) |
| `SetCameraRole(Name, CameraIndex, Role)` | View-position buttons |
| `SendDeviceCommand(Name, Cmd, Param, ExtraKey, ExtraVal)` | exposure/gain/lights |
| `SetManualIssue(Name, EHMCManualIssue, bSet)` / `GetManualIssue(...)` | operator flags |
| `GetEffectiveIssueFlags(Name, CameraIndex)` | hardware ∪ manual flags for one feed |

And on **UPCAPToolStatics** (Blueprint function library — pure):

| Call | Purpose |
|---|---|
| `GetIssueSeverity(Flags) → EHMCIssueSeverity` | None / Amber / Red → border color |
| `GetIssueBannerText(Flags) → String` | the banner line for a feed |

So a feed's border + banner is exactly two calls — no bit math in Blueprint:
```
Flags    = HMCComp.GetEffectiveIssueFlags(DeviceName, CameraIndex)
Severity = UPCAPToolStatics.GetIssueSeverity(Flags)   // → border color
Banner   = UPCAPToolStatics.GetIssueBannerText(Flags) // → banner text
```

---

## 1 — PCAPManager actor (one-time level setup)

1. Place an Empty Actor in the level, name it **PCAPManager**.
2. Add component **HMC Monitor Component**.
3. Set **Poll Interval Seconds = 2.0**.
4. Registration happens at runtime from the panel's Setup mode.

---

## 2 — Widget variables

```
HMCComp            — HMCMonitorComponent (Object Ref) — found in Construct
ActiveDeviceName   — String  — selected device in Setup sidebar
bSetupMode         — Bool    — false = Preview (default)
CurrentDayID       — String  — from active session in PCAPDatabase
CurrentProjectCode — String  — from active session
PollTimerHandle    — Timer Handle — the looping EUW poll timer
```

---

## 3 — Event Construct (the data wiring)

```
1. Get All Actors Of Class (PCAPManager) → [0] → Get Component HMCMonitorComponent
     → set HMCComp.   (If null: log and bail — no manager in level.)
2. CurrentDayID / CurrentProjectCode ← UPCAPToolSettings.GetDatabase()
     → active production → active day.
3. Bind HMCComp.OnStatusUpdated     → OnStatusUpdated
   Bind HMCComp.OnFrameReceived     → OnFrameReceived      ← do not skip this one
   Bind HMCComp.OnConnectionChanged → OnConnectionChanged
4. HMCComp.ConnectAll()             ← starts connection for saved devices
5. Set Timer by Function Name "PollTick", Time = HMCComp.PollIntervalSeconds,
     Looping = true → store PollTimerHandle.
6. Switcher_Mode.SetActiveWidgetIndex(1)  // Preview
7. RefreshSidebar ; RefreshPreview
```

**Function `PollTick`** (the heartbeat):
```
HMCComp.PollAllDevicesNow()
```

**Event Destruct:** `Clear Timer by Handle (PollTimerHandle)`.

**OnFrameReceived(DeviceName, CameraIndex, Frame):**
```
Find the feed cell for (DeviceName, CameraIndex)   // CameraIndex 0 = Top, 1 = Bot
If Frame valid: Image.SetBrushFromTexture(Frame), hide "No Feed"
Else:           show "No Feed"
```

**OnStatusUpdated(DeviceName, Status):** repaint vitals, issue banners/borders
(via GetEffectiveIssueFlags → severity/text), recording indicator, timecode,
quip — for the matching device's feeds/cards in whichever mode is active.

**OnConnectionChanged(DeviceName, State):** update the sidebar dot and, in
Preview, the offline overlay.

---

## 4 — SETUP VIEW

Top bar (title, "Day D548 · Precipice" from active session, Setup/Preview
buttons). Body = sidebar + main area.

**Sidebar (175px)** — `RefreshSidebar`: one row per `GetAllDeviceStatuses()`:
- dot: green = `ConnectionState == Connected`, gray = Offline/Disconnected,
  amber = `GetIssueSeverity(GetEffectiveIssueFlags(dev,0) | …(dev,1))` is Amber/Red
- device name, IP, actor (green if assigned, dim if not)
- click → set `ActiveDeviceName`, refresh main area
- empty state: "No devices — tap ＋ to add"

**Main area** — Identity card + Camera Feeds card for `ActiveDeviceName`:

- **Identity card:** Device Name / IP (editable), Actor button (opens picker
  populated from the current day's `FShotSubject.ActorName` values — dedupe, sort;
  on pick call `HMCComp.AssignActor(ActiveDeviceName, ActorName)`), Timecode
  (read-only, from `FApp::GetTimecode()` — see §6).
- **Camera Feeds card:** header "ORION · 2 Cameras" + issue pill. Two camera
  columns always (a 1-camera device shows "No second camera" in column 2). Per
  column:
  - **Feed viewport** (1:1): black bg, 3px border (severity color), 3×3 grid
    overlay, issue banner (top, `GetIssueBannerText`), timecode (bottom-left),
    "No Feed" when texture null.
  - **View Position** — 5 buttons Top/Bottom/Left/Right/Center →
    `SetCameraRole(dev, CameraIndex, Role)`; active button highlighted.
  - **Exposure (ms)** + Gang toggle (links both cams). Stepped value cells with
    [−]/[+] → `SendDeviceCommand(dev,"setexposure", NewValue, "cam", "0"|"1")`.
    If Gang on, send to both cams. Cell turns red >7000, amber <1000.
  - **Gain** — one cell + [−]/[+] → `SendDeviceCommand(dev,"setgain", NewValue, "cam", "0"|"1")`. Shows "X dB".
  - **Lights** — Top/Bot sliders (0–100) → `SendDeviceCommand(dev,"setlights", Value, "which", "top"|"bot")`.
  - **Manual issue toggles** (operator-reported — device can't sense these):
    one toggle per `EHMCManualIssue`; state from `GetManualIssue`, set via
    `SetManualIssue(dev, Issue, bOn)`. These feed the border/banner the same as
    hardware flags (already merged by `GetEffectiveIssueFlags`).

> `SendDeviceCommand` command tokens (`setexposure`/`setgain`/`setlights`) are
> best-guess from the MugShot web UI and **unverified against firmware**. If a
> control does nothing, inspect the device page JS for the real token/params and
> adjust the call args — the plumbing stays the same.

**Save bar:** "N devices · Day DXXX · Project". [Disconnect] → `DisconnectAll()`.
[Save Setup] → for each row build `FHMCDeviceConfig` (DeviceName, IPAddress,
ActorName; **WebSocketEndpoint empty**) → `RegisterDevice` → then `ConnectAll` →
`SaveConfig`.

---

## 5 — PREVIEW VIEW

`RefreshPreview`: unique actor names for the current day (same query as the actor
picker). For each actor with registered feeds (`GetFeedsForActor`): one block.
Skip actors with no HMC feed. Connected devices sort first.

Per actor block:
- **Header:** actor name; "DEVICENAME · IP"; timecode (right, green when live);
  recording indicator (gray "standby" / red "RECORDING" / dark "OFFLINE" from
  `Status.bIsRecording` and `ConnectionState`).
- **Feed grid** (adaptive): 1 cam → cam in left col, "No second camera" right;
  2 cams → one per col. Each cell: 4:3, severity border, grid overlay, issue
  banner (`GetIssueBannerText`), view label (TOP/BOT) top-right, timecode
  bottom-left, "No Feed" when null.
- **Vitals bar (6 cells):**
  - Battery — `BatteryVoltage` V — green >14 / amber 12–14 / red <12
  - Storage — `AvailableStorageMB`/1024 GB — green >50 / amber 10–50 / red <10
  - CPU — `CPUUsagePercent` % — green <60 / amber 60–80 / red >80
  - Temp — `TemperatureCelsius` °C — green <40 / amber 40–50 / red >50
  - Last Clip — `LastClipStatus` — green "Ready" / amber otherwise
  - Dropped — `DroppedFrames0 + DroppedFrames1` — green 0 / amber >0
- **Quip row:** `Status.StatusMessage` (italic, muted).

Manual flags are **not** shown as toggles here — Setup-only. They still affect
the border/banner via `GetEffectiveIssueFlags`.

---

## 6 — TIMECODE

Feed/header timecode is the **project master**, not the device (the HMC reports
`"No Wireless"`). Bind to:
```
FApp::GetTimecode()   → FTimecode    (format HH:MM:SS:FF)
FApp::GetFrameRate()  → FFrameRate
Display: "HH:MM:SS:FF  SOURCE · FPS"
```
This is the clock that syncs to body mocap.

---

## 7 — VISUAL SPEC

Exact hex/typography are in the handoff and `hmc_monitor_v3.html` — match them
(root #1e1e1e, cards #222, feed #0d0d0d; green #4CAF50 / amber #FF9800 /
red #F44336; grid rgba(255,235,59,0.13); Courier New throughout; issue-banner
gradients per severity). Border color comes from `GetIssueSeverity`:
None → green, Amber → #FF9800, Red → #F44336.

---

## 8 — VERIFICATION

- [ ] No `[PCAPTool] Orion WS error` on project open (WebSocket is gone)
- [ ] Output Log shows `[PCAPTool] HMC ORION -> Connected` within ~2s of opening
- [ ] Opens in Preview; shows only current-day actors with registered feeds
- [ ] Vitals refresh every 2s; issue borders/banners update from poll data
- [ ] Feed cells show live frames — cam 0 → Top, cam 1 → Bot; "No Feed" when a
      camera isn't streaming (and a `frame request failed` warning appears if the
      device rejects the pull)
- [ ] Setup: clicking ORION loads its identity + feeds; exposure/gain/lights
      buttons send commands; values confirm on the next poll
- [ ] View-position buttons set `EHMCCameraRole` and stay highlighted
- [ ] Actor picker lists current-day subjects; selecting reassigns via `AssignActor`
- [ ] Save Setup → RegisterDevice + ConnectAll + SaveConfig
- [ ] Disconnect → feeds go Disconnected, borders red
- [ ] Manual issue toggles change the border/banner severity
