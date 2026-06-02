# WBP_HMCOperatorPanel — Blueprint Build Guide

Target location: Content/PCAPTool/Widgets/WBP_HMCOperatorPanel

Parent class: UEditorUtilityWidget (standard — NOT PCAPToolEditorWidget)
This panel talks directly to UHMCMonitorComponent on a manager actor.

---

## STEP 0 — Create a PCAPManager actor in the level

1. Place an Empty Actor in the level. Name it **PCAPManager**.
2. Add component: search for **HMC Monitor Component** → add it.
3. Set **Poll Interval Seconds** = 2.0 on the component.
4. Leave device registration to the widget (Setup Mode does this at runtime).

---

## STEP 1 — Create the child row/card widgets first

### WBP_HMCDeviceCard  (Content/PCAPTool/Widgets/)

Parent: User Widget. This is one card in Live Mode.

**Variables to add (all Expose on Spawn = true):**
- `DeviceID` — String
- `ActorAssignment` — String
- `HMCComp` — HMCMonitorComponent (object ref)

**Designer layout** (Canvas, 280px min width):

```
[Vertical Box]
  [Border — StatusBar] height=6, full width
    — no children, tint driven by binding
  [Vertical Box — CardBody] padding=12, background #242424
    [Horizontal Box — Header]
      [Text] bind → DeviceID,  font=16px bold
      [Spacer]
      [Text] bind → IP (from status), font=11px, opacity=0.5
    [Text] bind → ActorAssignment, font=13px
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
      [Text] variable: Text_Battery   — e.g. "78%"
      [Spacer]
      [Image — StorageIcon] 16x16
      [Text] variable: Text_Storage   — e.g. "12.3 GB"
      [Spacer]
      [Border — RecDot] 10x10, circle, variable: Dot_Rec
      [Text] variable: Text_LastPoll  — "Updated 1s ago"
    [Horizontal Box — Buttons]
      [Button] variable: Btn_StartRec, text "Start Rec", dark style
      [Button] variable: Btn_StopRec,  text "Stop Rec",  dark style
    [Text "TEST ONLY — use Take Recorder in production"]
      font=10px, opacity=0.4, centered
```

**Graph:**

Function `UpdateFromStatus(FHMCDeviceStatus Status)`:
- Set Text_Battery = Format "{BattPct}%" where BattPct = clamp((Voltage-6.0)/(8.4-6.0)*100, 0, 100) as int
  - Color: green if >50, yellow if 20-50, red if <20
- Set Text_Storage = Format "{StorageGB} GB" where StorageGB = AvailableStorageMB / 1024.0, to 1 decimal
- Set Text_LastPoll text via a tick-driven binding (see Tick below)
- StatusBar tint:
  - IsReachable=false → gray (#555555)
  - IsRecording=true  → red (#CC2222)  [add pulsing via animation or lerp on tick]
  - IsRecording=false → green (#227733)
- Set widget opacity: IsReachable=false → 0.4, else 1.0
- Show/hide "OFFLINE" overlay based on IsReachable
- Btn_StartRec IsEnabled = IsReachable AND NOT IsRecording
- Btn_StopRec  IsEnabled = IsReachable AND IsRecording
- If frame texture not null: set Img_TopCam/BotCam brush, hide No Feed text
- If null: show No Feed text

Event Tick (enable tick on the widget):
- Calculate seconds since Status.LastPollTime → set Text_LastPoll
  "Updated Xs ago" or "Never" if LastPollTime ticks == 0

Btn_StartRec OnClicked:
- Call HMCComp → Send Command (DeviceID, "startrecording")

Btn_StopRec OnClicked:
- Call HMCComp → Send Command (DeviceID, "stoprecording")

---

### WBP_HMCDeviceRow  (Content/PCAPTool/Widgets/)

Parent: User Widget. This is one row in Setup Mode.

**Variables:** DeviceID (String, ExposeOnSpawn), IPAddress (String, ExposeOnSpawn)

**Designer layout** (Horizontal Box, full width):
```
[EditableText] variable: Input_DeviceID,  width=140, hint="HMC_Unit_A"
[EditableText] variable: Input_IP,        width=130, hint="192.168.50.21"
[ComboBoxString] variable: Combo_Actor,   width=140  (populated from active shot subjects)
[Border — PingResult] variable: Border_Ping, width=100
  [Text] variable: Text_Ping  — "●  Reachable" or "●  Unreachable"
[Button] variable: Btn_Ping, text="Ping"
[Button] variable: Btn_Remove, text="✕", width=30
```

**Graph:**

Event Pre Construct or Construct:
- Set Input_DeviceID text = DeviceID
- Set Input_IP text = IPAddress

Function `SetPingResult(bool bReachable)`:
- Text_Ping = bReachable ? "●  Reachable" : "●  Unreachable"
- Color green or red

Btn_Ping OnClicked → call parent widget's PingSingleDevice(Input_DeviceID text, Input_IP text)
Btn_Remove OnClicked → call parent widget's RemoveDeviceRow(this widget ref)

---

## STEP 2 — Build WBP_HMCOperatorPanel

Parent: **Editor Utility Widget** → reparent to **PCAPToolEditorWidget** if desired,
OR keep as plain EditorUtilityWidget and get the HMCMonitorComponent directly.

**Recommended: keep parent as UEditorUtilityWidget for simplicity.**
Reference the component via a Blueprint variable pointing at PCAPManager.

### Variables:
- `HMCComp` — HMCMonitorComponent (set in Construct by finding PCAPManager actor)
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
          [Button] Btn_TestAll, text="Test All Devices"
          [Text] variable: Text_TestSummary  — "— / — reachable"
    [Index 1 — Live Panel]
      [Scroll Box]
        [Wrap Box] variable: WrapBox_Cards, min desired width=280
```

### Graph:

**Event Construct:**
```
Get All Actors Of Class (PCAPManager or just find actor with HMCMonitorComponent)
→ Get Component by Class (HMCMonitorComponent)
→ Set HMCComp

Bind HMCComp.OnStatusUpdated → OnHMCStatusUpdated
Bind HMCComp.OnFrameReceived → OnHMCFrameReceived

Set Timer by Function Name → "PollHMCDevicesNow_EUW" looping=true, time=2.0
  (This calls PollHMCDevicesNow on PCAPToolEditorWidget OR directly on HMCComp)

Call RefreshSetupRows
```

**Btn_Setup OnClicked:**
- Switcher_Mode SetActiveWidgetIndex = 0
- Btn_Setup style = active (lighter bg)
- Btn_Live style = inactive

**Btn_Live OnClicked:**
- Switcher_Mode SetActiveWidgetIndex = 1
- RefreshLiveCards

**Btn_AddDevice OnClicked:**
- Create Widget WBP_HMCDeviceRow (DeviceID="", IPAddress="")
- Add to VBox_DeviceRows

**Function RefreshSetupRows:**
- Clear VBox_DeviceRows
- For each status in HMCComp.GetAllDeviceStatuses:
  - Create WBP_HMCDeviceRow (DeviceID=status.DeviceID, IPAddress=status.IPAddress)
  - Add to VBox_DeviceRows

**Function RefreshLiveCards:**
- Clear WrapBox_Cards
- For each status in HMCComp.GetAllDeviceStatuses:
  - Create WBP_HMCDeviceCard (DeviceID, ActorAssignment="", HMCComp=HMCComp)
  - Add to WrapBox_Cards
  - Store in DeviceCards array

**OnHMCStatusUpdated(FHMCDeviceStatus UpdatedStatus):**
- If Live Mode: find card in DeviceCards where card.DeviceID == UpdatedStatus.DeviceID
  → call card.UpdateFromStatus(UpdatedStatus)
- If Setup Mode: find row, call row.SetPingResult(UpdatedStatus.IsReachable)
- Update Text_TestSummary: count reachable / total

**OnHMCFrameReceived(DeviceID, CameraIndex, Frame):**
- Find card in DeviceCards by DeviceID
- If CameraIndex == 1: set card.Img_TopCam brush = Frame (if not null), hide NoFeed text
- If CameraIndex == 2: set card.Img_BotCam brush = Frame (if not null), hide NoFeed text
- If Frame is null: show NoFeed text

**Function PingSingleDevice(DeviceID, IPAddress):**
- Call HMCComp.RegisterDevice(DeviceID, IPAddress) if not already registered
- (Poll result comes back via OnStatusUpdated)

**Slider_PollInterval OnValueChanged:**
- Set Text_PollValue = Format "{value}s"
- Set HMCComp.PollIntervalSeconds = value

**Btn_TestAll OnClicked:**
- For each row in DeviceRows:
  - Call HMCComp.RegisterDevice(row.Input_DeviceID text, row.Input_IP text)
- HMCComp poll cycle handles the rest — OnStatusUpdated updates ping results

---

## STEP 3 — Open the panel

Window menu → Editor Utility Widgets → WBP_HMCOperatorPanel
Or: right-click WBP_HMCOperatorPanel in Content Browser → Run Editor Utility Widget
