# WBP_HMCOperatorPanel — Blueprint Build Guide

Target location: `Content/PCAPTool/Widgets/WBP_HMCOperatorPanel`

Parent class: **UEditorUtilityWidget** (plain — *not* PCAPToolEditorWidget).
This panel talks directly to a `UHMCMonitorComponent` on a manager actor. It does
**not** touch `UPCAPToolEditorWidget` or its `FHMCDeviceRecord` registry — that is a
separate path. Keep everything here on the component.

Transport is **HTTP polling**. The device is read with `GET http://[IP]/control?cmd=no&param=`.
There is no WebSocket anywhere in this panel.

### Identity and field names

Devices are keyed by **`DeviceName`** (e.g. `"ORION"`). There is no `DeviceID` field
on `FHMCDeviceStatus` — every match, command, and assignment uses `DeviceName`. The
status fields this panel reads, all `BlueprintReadOnly`:

`DeviceName, IPAddress, ActorName, ConnectionState (EHMCConnectionState),
bIsRecording, BatteryVoltage, AvailableStorageMB, CPUUsagePercent,
TemperatureCelsius, LastClipStatus, StatusMessage, CurrentTakeName,
LastUpdateTime, FPS.`

`EHMCConnectionState` = `Disconnected | Connected | Offline`. **Online means
`ConnectionState == Connected`.** Offline is `Offline` *or* `Disconnected`.

### Vital color thresholds (shared with the Setup guide)

| Vital | Green | Yellow | Red |
|---|---|---|---|
| Battery (`BatteryVoltage`, volts — 4S pack) | > 14.0 | 12.0–14.0 | < 12.0 |
| Storage (`AvailableStorageMB` / 1024 = GB) | > 50 GB | 10–50 GB | < 10 GB |
| CPU (`CPUUsagePercent`) | < 60% | 60–80% | > 80% |
| Temp (`TemperatureCelsius`, °C) | < 40 | 40–50 | > 50 |
| Last clip (`LastClipStatus`) | `"Ready"` | anything else | — |

Battery is raw voltage on a 4S LiPo (~14.4–15.5 V nominal). Do **not** map it to a
percentage — a 2S range clamps to 100% forever on this pack.

---

## STEP 0 — Create a PCAPManager actor in the level

1. Place an Empty Actor in the level. Name it **PCAPManager**.
2. Add component: search for **HMC Monitor Component** → add it.
3. Set **Poll Interval Seconds** = 2.0 on the component.
4. Leave device registration to the widget (Setup Mode does this at runtime).

> The component lives on an editor-placed actor, so its internal world timer does
> **not** fire outside PIE. This panel drives polling itself (STEP 2, Construct) by
> calling `PollAllDevicesNow()` on a looping EUW timer.

---

## STEP 1 — Create the child row/card widgets first

### WBP_HMCDeviceCard  (Content/PCAPTool/Widgets/)

Parent: User Widget. This is one card in Live Mode.

**Variables to add (all Expose on Spawn = true):**
- `DeviceName` — String
- `HMCComp` — HMCMonitorComponent (object ref)

**Designer layout** (Canvas, 280px min width):

```
[Vertical Box]
  [Border — StatusBar] height=6, full width
    — no children, tint driven by binding
  [Border — OfflineOverlay] full card, #000000 @ 0.5, hidden by default
    [Text "OFFLINE"] centered, 18px bold
  [Vertical Box — CardBody] padding=12, background #242424
    [Horizontal Box — Header]
      [Text] bind → DeviceName,  font=16px bold
      [Spacer]
      [Text] bind → IP (from status), font=11px, opacity=0.5
    [Text] variable: Text_Actor, font=13px   — bound to status.ActorName
    [Horizontal Box — VideoArea]
      [Border — TopCamBox] 50% width, 16:9 aspect
        [Image] variable: Img_TopCam   (default: "No Feed" overlay)
        [Text "No Feed"] variable: Text_NoFeedTop, centered, gray
      [Border — BotCamBox] 50% width, 16:9 aspect
        [Image] variable: Img_BotCam
        [Text "No Feed"] variable: Text_NoFeedBot, centered, gray
    [Horizontal Box — Labels]
      [Text "Top"] 50% width, centered, 11px muted
      [Text "Bot"] 50% width, centered, 11px muted
    [Horizontal Box — StatusRow]
      [Image — BattIcon] 16x16
      [Text] variable: Text_Battery   — e.g. "14.8V"
      [Spacer]
      [Image — StorageIcon] 16x16
      [Text] variable: Text_Storage   — e.g. "12.3 GB"
      [Spacer]
      [Text] variable: Text_CPU       — e.g. "42%"
      [Spacer]
      [Text] variable: Text_Temp      — e.g. "38°C"
      [Spacer]
      [Border — RecDot] 10x10, circle, variable: Dot_Rec
      [Text] variable: Text_LastUpdate  — "Updated 1s ago"
    [Horizontal Box — Buttons]
      [Button] variable: Btn_StartRec, text "Start Rec", dark style
      [Button] variable: Btn_StopRec,  text "Stop Rec",  dark style
    [Text "TEST ONLY — use Take Recorder in production"]
      font=10px, opacity=0.4, centered
```

**Graph:**

Function `UpdateFromStatus(FHMCDeviceStatus Status)`:

First compute online once: `bOnline = (Status.ConnectionState == Connected)`.

- `Text_Battery` = Format `"{Volts}V"` (1 decimal) from `Status.BatteryVoltage`
  - Color by battery threshold above.
- `Text_Storage` = Format `"{GB} GB"` where `GB = Status.AvailableStorageMB / 1024.0` (1 decimal)
  - Color by storage threshold.
- `Text_CPU` = Format `"{Pct}%"` from `Status.CPUUsagePercent` — color by CPU threshold.
- `Text_Temp` = Format `"{C}°C"` from `Status.TemperatureCelsius` — color by temp threshold.
- `Text_LastUpdate` — driven on Tick from `Status.LastUpdateTime` (see Tick below).
- `StatusBar` tint:
  - `bOnline == false` → gray (#555555)
  - `Status.bIsRecording == true` → red (#CC2222)  [optional pulse via animation/lerp on tick]
  - else → green (#227733)
- Widget opacity: `bOnline ? 1.0 : 0.4`
- `OfflineOverlay` visibility: visible when `!bOnline`, else collapsed.
- `Btn_StartRec` IsEnabled = `bOnline AND NOT Status.bIsRecording`
- `Btn_StopRec`  IsEnabled = `bOnline AND Status.bIsRecording`
- Cache `Status` into a member variable so Tick and the frame handler can read it.

> Frame brushes (`Img_TopCam` / `Img_BotCam`) are set by the panel's
> `OnHMCFrameReceived`, not here — see STEP 2. `UpdateFromStatus` only drives vitals
> and state.

Event Tick (enable tick on the widget):
- Seconds since `Status.LastUpdateTime` → set `Text_LastUpdate`:
  `"Updated Xs ago"`, or `"Never"` if `LastUpdateTime` ticks == 0.

Btn_StartRec OnClicked:
- `HMCComp → SendCommand(DeviceName, "startrecording")`

Btn_StopRec OnClicked:
- `HMCComp → SendCommand(DeviceName, "stoprecording")`

> `SendCommand` fires `GET /control?cmd=[Command]&param=`. The `startrecording` /
> `stoprecording` tokens are unverified against firmware — **TEST ONLY**. Production
> recording goes through Take Recorder, not these buttons.

---

### WBP_HMCDeviceRow  (Content/PCAPTool/Widgets/)

Parent: User Widget. This is one row in Setup Mode.

**Variables:** `DeviceName` (String, ExposeOnSpawn), `IPAddress` (String, ExposeOnSpawn),
`HMCComp` (HMCMonitorComponent, ExposeOnSpawn).

**Designer layout** (Horizontal Box, full width):
```
[EditableText] variable: Input_DeviceName, width=140, hint="ORION"
[EditableText] variable: Input_IP,         width=130, hint="192.168.50.117"
[ComboBoxString] variable: Combo_Actor,    width=140  (populated from active shot subjects)
[Border — PingResult] variable: Border_Ping, width=120
  [Text] variable: Text_State  — "●  Connected" / "●  Offline"
[Button] variable: Btn_Remove, text="✕", width=30
```

**Graph:**

Event Pre Construct or Construct:
- `Input_DeviceName` text = `DeviceName`
- `Input_IP` text = `IPAddress`
- Populate `Combo_Actor` from the active shot's subjects and preselect the device's
  current `ActorName`.

Function `SetConnectionState(EHMCConnectionState State)`:
- `Text_State` = `State == Connected ? "●  Connected" : "●  Offline"`
- Color green when `Connected`, red otherwise.

`Combo_Actor` OnSelectionChanged:
- `HMCComp → AssignActor(Input_DeviceName text, SelectedActorName)`

> `AssignActor(DeviceName, NewActorName)` updates the config and status entries and
> migrates the device's camera feeds to the new actor's group (feeds are keyed by
> actor). It broadcasts `OnStatusUpdated`, so Preview regroups and the card relabels
> automatically.

Btn_Remove OnClicked → call parent widget's `RemoveDeviceRow(this widget ref)`.

---

## STEP 2 — Build WBP_HMCOperatorPanel

Parent: **Editor Utility Widget**. Reference the component via a Blueprint variable
pointing at PCAPManager.

### Variables:
- `HMCComp` — HMCMonitorComponent (set in Construct by finding PCAPManager)
- `DeviceRows` — Array of WBP_HMCDeviceRow (Setup Mode list)
- `DeviceCards` — Array of WBP_HMCDeviceCard (Live Mode cards)
- `bLiveMode` — bool, default false

### Designer layout:

```
[Vertical Box — Root] background=#1A1A1A, fill screen
  [Horizontal Box — TopBar] height=48, padding=12, background=#111111
    [Text "HMC OPERATOR"] 14px bold, white
    [Spacer]
    [Button] Btn_Setup, text="SETUP",  width=80
    [Button] Btn_Live,  text="LIVE",   width=80
  [Widget Switcher] variable: Switcher_Mode
    [Index 0 — Setup Panel]
      [Scroll Box]
        [Vertical Box]
          [Text "REGISTERED DEVICES"] section header style
          [Vertical Box] variable: VBox_DeviceRows
          [Button] Btn_AddDevice, text="+ Add Device"
          [Text "NETWORK"] section header style
          [Horizontal Box]
            [Text "Subnet: 192.168.50.x"] muted
            [Spacer]
            [Text "Poll Interval"]
            [Slider] variable: Slider_PollInterval, min=1, max=10, default=2
            [Text] variable: Text_PollValue — "2.0s"
          [Text "CONNECTION TEST"] section header style
          [Button] Btn_SaveAndConnect, text="Save & Connect All"
          [Text] variable: Text_TestSummary  — "— / — connected"
    [Index 1 — Live Panel]
      [Scroll Box]
        [Wrap Box] variable: WrapBox_Cards, min desired width=280
```

### Graph:

**Event Construct:**
```
Get All Actors Of Class (PCAPManager)  — or find the actor with HMCMonitorComponent
→ Get Component by Class (HMCMonitorComponent)
→ Set HMCComp

Bind HMCComp.OnStatusUpdated     → OnHMCStatusUpdated
Bind HMCComp.OnFrameReceived     → OnHMCFrameReceived
Bind HMCComp.OnConnectionChanged → OnHMCConnectionChanged

Set Timer by Function Name → "PollTick" looping=true, time=HMCComp.PollIntervalSeconds
Call RefreshSetupRows
```

**Function `PollTick`:**
- `HMCComp → PollAllDevicesNow()`

> Drive polling from this EUW timer — the component's own world timer will not fire
> on an editor actor. Do **not** call `PollHMCDevicesNow` (that belongs to the legacy
> `UPCAPToolEditorWidget` and polls a different registry).

**Btn_Setup OnClicked:**
- `Switcher_Mode` SetActiveWidgetIndex = 0; Btn_Setup active style, Btn_Live inactive.

**Btn_Live OnClicked:**
- `Switcher_Mode` SetActiveWidgetIndex = 1; call `RefreshLiveCards`.

**Btn_AddDevice OnClicked:**
- Create Widget `WBP_HMCDeviceRow` (DeviceName="", IPAddress="", HMCComp=HMCComp)
- Add to `VBox_DeviceRows` and `DeviceRows`.

**Function RefreshSetupRows:**
- Clear `VBox_DeviceRows` and `DeviceRows`.
- For each `Status` in `HMCComp.GetAllDeviceStatuses()`:
  - Create `WBP_HMCDeviceRow` (DeviceName=Status.DeviceName, IPAddress=Status.IPAddress, HMCComp=HMCComp)
  - Add to `VBox_DeviceRows` and `DeviceRows`.

**Function RefreshLiveCards:**
- Clear `WrapBox_Cards` and `DeviceCards`.
- For each `Status` in `HMCComp.GetAllDeviceStatuses()`:
  - Create `WBP_HMCDeviceCard` (DeviceName=Status.DeviceName, HMCComp=HMCComp)
  - Add to `WrapBox_Cards`, store in `DeviceCards`, then call `card.UpdateFromStatus(Status)`.

**OnHMCStatusUpdated(DeviceName, Status):**
- Live Mode: find card in `DeviceCards` where `card.DeviceName == DeviceName` →
  `card.UpdateFromStatus(Status)`.
- Setup Mode: find row by `DeviceName` → `row.SetConnectionState(Status.ConnectionState)`.
- Update `Text_TestSummary`: count `ConnectionState == Connected` / total.

**OnHMCFrameReceived(DeviceName, CameraIndex, Frame):**
- Find card in `DeviceCards` by `DeviceName`.
- `CameraIndex == 0` → Top: if `Frame` not null, set `card.Img_TopCam` brush, hide `Text_NoFeedTop`; else show it.
- `CameraIndex == 1` → Bot: if `Frame` not null, set `card.Img_BotCam` brush, hide `Text_NoFeedBot`; else show it.

> `CameraIndex` is **0-based** here: 0 = Top, 1 = Bot. The `cam=N+1` offset
> (`/video?cam=1` = Top, `/video?cam=2` = Bot) lives only inside the HTTP URL the
> component builds — the broadcast index is already normalized.

**OnHMCConnectionChanged(DeviceName, ConnectionState):**
- Setup Mode: find row by `DeviceName` → `row.SetConnectionState(ConnectionState)`.
- Live Mode: the next `OnHMCStatusUpdated` repaints the card; nothing extra needed here.

**Slider_PollInterval OnValueChanged:**
- `Text_PollValue` = Format `"{value}s"`
- `HMCComp.PollIntervalSeconds = value`

**Btn_SaveAndConnect OnClicked:**
- For each `row` in `DeviceRows`:
  - Make `FHMCDeviceConfig`: `DeviceName` = row.Input_DeviceName text, `IPAddress` =
    row.Input_IP text, `ActorName` = row.Combo_Actor selected, **`WebSocketEndpoint`
    left empty**.
  - `HMCComp.RegisterDevice(Config)`
- `HMCComp.ConnectAll()`

> `ConnectAll()` flips each device to `Connected` synchronously and starts its poll
> timer — there is no async handshake. The New→Connected transition fires immediately
> through `OnConnectionChanged`. `WebSocketEndpoint` stays empty: the field still
> exists on the config (and in `HMCConfig.json`) but is unused — no schema migration.

---

## STEP 3 — Open the panel

Content Browser → right-click `WBP_HMCOperatorPanel` → **Run Editor Utility Widget**
(or Window menu → Editor Utility Widgets → WBP_HMCOperatorPanel).

Within one poll interval (~2s) registered devices show `Connected` with live vitals,
and frame cells route cam 0 → Top, cam 1 → Bot.
