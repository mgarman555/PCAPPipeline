> **DEPRECATED.** The HMC monitor is now a single two-mode panel. Build from
> `WBP_HMCMonitorPanel_BuildGuide.md` instead — it supersedes this Setup guide,
> the Preview guide, and the OperatorPanel guide, and reflects the current C++
> data-reception architecture. Kept for reference only.

# WBP_HMCSetup — Blueprint Build Guide
Location: Content/PCAPTool/Widgets/WBP_HMCSetup.uasset
Parent: User Widget (plain UUserWidget, not EUW)

This widget is embedded inside WBP_HMCOperatorPanel.
It holds the full setup panel including the card list and save bar.

---

## VARIABLES (add to WBP_HMCSetup)

| Name | Type | Notes |
|---|---|---|
| HMCComp | HMCMonitorComponent (Object Ref) | Set by parent panel on construct |
| DeviceCardList | Array of WBP_HMCDeviceCard_New | Tracks state-A cards |
| ConnectedCardList | Array of WBP_HMCDeviceCard_Connected | Tracks state-B cards |

---

## DESIGNER LAYOUT

```
[Vertical Box — Root]  background=#1A1A1A, fill screen
  [Horizontal Box — PanelHeader]  padding=12,16
    [Text "HMC SETUP"]  11px uppercase, letter-spacing, opacity=0.5
    [Spacer]
    [Button Btn_AddDevice]  text="+ Add device", dark style

  [ScrollBox — CardScroll]  fill remaining height
    [Vertical Box VBox_Cards]  padding=0,8

  [Horizontal Box — SaveBar]  height=52, padding=12,0, background=#111111, border-top 1px #333
    [Text Text_DeviceCount]  "0 devices configured", 11px muted
    [Spacer]
    [Button Btn_SaveComplete]  text="Save and complete setup", success style (#2E7D32 bg)
```

---

## CHILD WIDGETS TO CREATE FIRST

### WBP_HMCDeviceCard_New
One card per device in state A (not yet connected).

**Designer:**
```
[Border — Card]  background=#242424, margin-bottom=8
  [Vertical Box]  padding=16
    [Horizontal Box — Fields]
      [Vertical Box — NameField]  flex=1, margin-right=12
        [Text "NAME"]  10px uppercase, opacity=0.5
        [EditableText Input_DeviceName]  monospace, placeholder="ORION"
        [Text "Label saved to database"]  10px muted mono
      [Vertical Box — IPField]  flex=1
        [Text "IP ADDRESS"]  10px uppercase, opacity=0.5
        [EditableText Input_IPAddress]  monospace, placeholder="192.168.50.x"
        [Text "Maps to this name"]  10px muted mono
    [Border — BottomStrip]  border-top 1px #333, padding=8,12
      [Horizontal Box]
        [Text "Name and IP are saved to the database on completion."]  11px, opacity=0.5
        [Spacer]
        [Button Btn_Remove]  text="Remove", danger style (#B71C1C bg)
```

**Variables:** none needed — parent reads Input_DeviceName.Text and Input_IPAddress.Text directly.

**Btn_Remove OnClicked:** call parent (Get Parent Widget → cast to WBP_HMCSetup) → RemoveCard(this)

---

### WBP_HMCDeviceCard_Connected
One card per device in state B (WebSocket connected).

**Variables (all Expose on Spawn):**
- DeviceName — String
- Status — FHMCDeviceStatus
- Feed0 — FHMCCameraFeed
- Feed1 — FHMCCameraFeed

**Designer:**
```
[Border — Card]  background=#242424, margin-bottom=8
  [Vertical Box]  padding=0

    [Horizontal Box — Identity]  padding=16,12
      [Vertical Box — LeftCol]  flex=1
        [Horizontal Box]
          [Text Text_DeviceName]  15px bold
          [Text Text_IP]  10px mono, opacity=0.5, margin-left=8
        [Horizontal Box]
          [Text "ACTOR"]  10px uppercase, opacity=0.5, margin-right=6
          [ComboBoxString Combo_Actor]  width=200
      [Vertical Box — RightCol]  align=right
        [Horizontal Box]
          [Border Dot_Connected]  10x10, circle, bg=#4CAF50
          [Text "Connected"]  11px bold, color=#4CAF50, margin-left=6
        [Button Btn_Remove]  text="✕", 24x24, danger micro style

    [Horizontal Box — FeedArea]  padding=0,8,0,0
      [WBP_HMCFeedCell — Cell0]  50% width (expose CameraIndex=0)
      [WBP_HMCFeedCell — Cell1]  50% width (expose CameraIndex=1)

    [Border — StatusBar]  border-top 1px #333, padding=8,12
      [Horizontal Box]
        [Text Text_Quip]  flex=1, 11px italic, opacity=0.6, border-right 1px #333, padding-right=12
        [Border — VitalCell BATTERY]  padding=8,4, border-right 1px #333
          [Vertical Box]
            [Text "BATTERY"]  10px uppercase, opacity=0.4
            [Text Text_Battery]  11px bold  (color driven by voltage)
        [Border — VitalCell SPACE]  same structure
          [Text "SPACE"]
          [Text Text_Storage]  color driven by GB
        [Border — VitalCell CPU]
          [Text "CPU"]
          [Text Text_CPU]  color driven by %
        [Border — VitalCell TEMP]
          [Text "TEMP"]
          [Text Text_Temp]  color driven by °C
        [Border — VitalCell LAST CLIP]
          [Text "LAST CLIP"]
          [Text Text_LastClip]  color: green if "Ready", yellow otherwise

    [Border — BottomRow]  border-top 1px #333, padding=12
      [Button Btn_Disconnect]  text="Disconnect", danger style, left-aligned
```

**Function UpdateFromStatus(FHMCDeviceStatus NewStatus):**
- Text_DeviceName = NewStatus.DeviceName
- Text_IP = NewStatus.IPAddress
- Text_Quip = NewStatus.StatusMessage
- Battery: Text_Battery = format "{V} V" — green >14V, yellow 12-14V, red <12V
- Storage: Text_Storage = format "{GB} GB" (AvailableStorageMB/1024) — green >50GB, yellow 10-50, red <10
- CPU: Text_CPU = format "{%}%" — green <60, yellow 60-80, red >80
- Temp: Text_Temp = format "{°C}°C" — green <40, yellow 40-50, red >50
- LastClip: Text_LastClip = LastClipStatus — green if "Ready", else yellow

**Function UpdateFeed(FHMCCameraFeed Feed):**
- Route to Cell0 or Cell1 based on Feed.CameraIndex
- Call cell.SetFeedData(Feed)

**Btn_Disconnect OnClicked:**
- Get parent widget → cast to WBP_HMCSetup → call HMCComp.DisconnectDevice(DeviceName)

**Combo_Actor OnSelectionChanged:**
- Call HMCComp actor to update ActorName on config (not yet wired — mark TODO for Session 4)

---

### WBP_HMCFeedCell
Reusable feed cell used in both Setup and Preview.

**Variables (Expose on Spawn):**
- DeviceName — String
- CameraIndex — Integer
- HMCComp — HMCMonitorComponent (Object Ref)

**Designer:**
```
[Vertical Box]  padding=6

  [Border — FrameBorder]  variable: Border_Frame
    3px border, border-radius=4, color driven by FeedState
    [Overlay]
      [Border — FrameInner]  background=#000000, fill
        [Image Img_Feed]  fill, hidden if no texture
      [Text Text_NoFeed]  "No Feed", centered, 11px, opacity=0.3
        camera icon above text — use any small icon
      [Text Text_Timecode]  bottom-left, 9px mono, rgba(255,255,255,0.5)
      [Text Text_Disconnected_Overlay]  "OFFLINE", centered, hidden by default
        color rgba(255,80,80,0.5), 12px

  [Horizontal Box — Footer]  padding=4,6
    [ComboBoxString Combo_Role]  options: Top/Bottom/Center/Left/Right
    [Spacer]
    [Button Btn_FeedState]  text driven by state
      Clear → "● Clear" green
      NeedsFix → "● Needs fix" red
```

**Function SetFeedData(FHMCCameraFeed Feed):**
- If Feed.FrameTexture not null: set Img_Feed brush = Feed.FrameTexture, hide Text_NoFeed
- If null: show Text_NoFeed
- Set Text_Timecode = Feed.Timecode
- Drive Border_Frame tint color:
  - Clear → #4CAF50
  - NeedsFix → #F44336
  - Disconnected → #F44336 (pulsing — see animation below)
- Show/hide Text_Disconnected_Overlay based on FeedState == Disconnected
- Set Combo_Role selected option from Feed.Role

**Disconnected pulse animation:**
Create a UMG animation named "Anim_Pulse" on this widget:
- Track: Border_Frame Render Opacity
- Keyframe 0s: 1.0 → Keyframe 0.5s: 0.2 → Keyframe 1.0s: 1.0
- Set looping
Play this animation when FeedState = Disconnected. Stop it otherwise.

**Btn_FeedState OnClicked:**
- Toggle current FeedState between Clear and NeedsFix
- Call HMCComp.SetFeedState(DeviceName, CurrentRole, NewState)
- Update button text/color

**Combo_Role OnSelectionChanged:**
- Call HMCComp.SetCameraRole(DeviceName, CameraIndex, NewRole)

---

## WBP_HMCSetup GRAPH

**Event Construct:**
- (HMCComp is set by parent — do not find actor here)
- Bind HMCComp.OnStatusUpdated → OnStatusUpdated
- Bind HMCComp.OnConnectionChanged → OnConnectionChanged
- Call RefreshCards

**Function RefreshCards:**
- Clear VBox_Cards
- For each status in HMCComp.GetAllDeviceStatuses:
  - If ConnectionState == Connected: create WBP_HMCDeviceCard_Connected, add to VBox_Cards
  - Else: create WBP_HMCDeviceCard_New (pre-fill Name and IP), add to VBox_Cards
- Update Text_DeviceCount

**OnStatusUpdated(DeviceName, Status):**
- Find connected card in ConnectedCardList by DeviceName
- Call card.UpdateFromStatus(Status)
- For each feed in HMCComp.GetFeedsForActor(Status.ActorName): call card.UpdateFeed(feed)

**OnConnectionChanged(DeviceName, ConnectionState):**
- If now Connected: transition that device's card from New to Connected
  - Remove its WBP_HMCDeviceCard_New from VBox_Cards
  - Create WBP_HMCDeviceCard_Connected with current status
  - Add to VBox_Cards

**Btn_AddDevice OnClicked:**
- Create WBP_HMCDeviceCard_New (no args)
- Add to VBox_Cards
- Add to DeviceCardList
- Update Text_DeviceCount

**Function RemoveCard(Widget):**
- Remove from VBox_Cards
- Remove from DeviceCardList
- Update Text_DeviceCount

**Btn_SaveComplete OnClicked:**
1. For each card in DeviceCardList (state A):
   - Read Input_DeviceName.Text and Input_IPAddress.Text
   - Build FHMCDeviceConfig: DeviceName=Name, IPAddress=IP, WebSocketEndpoint="ws://"+IP+"/ws"
   - Call HMCComp.RegisterDevice(config)
2. HMCComp.SaveConfig() — already called inside RegisterDevice, but call explicitly too
3. HMCComp.ConnectAll()
4. Update Text_DeviceCount
(Cards transition to Connected state via OnConnectionChanged as sockets open)
