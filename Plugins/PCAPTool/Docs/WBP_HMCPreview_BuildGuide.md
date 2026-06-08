> **DEPRECATED.** The HMC monitor is now a single two-mode panel. Build from
> `WBP_HMCMonitorPanel_BuildGuide.md` instead — it supersedes this Preview guide,
> the Setup guide, and the OperatorPanel guide, and reflects the current C++
> data-reception architecture. Kept for reference only.

# WBP_HMCPreview — Blueprint Build Guide
Location: Content/PCAPTool/Widgets/WBP_HMCPreview.uasset
Parent: User Widget (plain UUserWidget)

Read-only feed monitor. No controls except the feed state label.
Operator watches this during a take.

---

## VARIABLES

| Name | Type | Notes |
|---|---|---|
| HMCComp | HMCMonitorComponent (Object Ref) | Set by parent panel |

---

## CHILD WIDGET: WBP_HMCActorGroup
One per actor who has registered feeds.

**Variables (Expose on Spawn):**
- ActorName — String
- HMCComp — HMCMonitorComponent (Object Ref)

**Designer:**
```
[Vertical Box]  padding=0,0,0,16

  [Horizontal Box — ActorRow]  padding=0,0,0,8
    [Text Text_ActorName]  13px bold
    [Text Text_DeviceInfo]  10px mono, opacity=0.5, margin-left=8
      — format: "{DeviceName} · {IPAddress}"

  [Uniform Grid Panel — FeedGrid]  fill width, min cell width=50%
    — cells added dynamically, two per row

  [Border — Separator]  height=1, background=#333333, margin-top=16
```

**Function BuildFeeds(TArray<FHMCCameraFeed> Feeds):**
- Clear FeedGrid
- For each Feed in Feeds:
  - Create WBP_HMCFeedCell_Preview (DeviceName, CameraIndex, HMCComp)
  - Call cell.SetFeedData(Feed)
  - Add to FeedGrid at next slot
- Set Text_ActorName = ActorName
- Set Text_DeviceInfo from first feed's device status

**Function UpdateFeed(FHMCCameraFeed Feed):**
- Find cell in FeedGrid by DeviceName + CameraIndex
- Call cell.SetFeedData(Feed)

---

## CHILD WIDGET: WBP_HMCFeedCell_Preview
Preview-mode feed cell — read-only. No role dropdown, no toggle button.

**Variables (Expose on Spawn):**
- DeviceName — String
- CameraIndex — Integer

**Designer:**
```
[Vertical Box]  padding=6

  [Border — FrameBorder]  variable: Border_Frame
    3px border, border-radius=4
    [Overlay]
      [Border — FrameInner]  background=#000000, fill
        [Image Img_Feed]  fill
      [Text Text_RoleLabel]  top-left, 9px uppercase, opacity=0.4
        — e.g. "TOP", "BOTTOM"
      [Text Text_Timecode]  bottom-left, 9px mono, rgba(255,255,255,0.5)
      [Overlay — DisconnectedOverlay]  hidden by default, fill
        [Border]  background=rgba(8,8,8,0.85), fill
        [Vertical Box]  centered
          [Image]  camera-off icon, tint=rgba(255,80,80,0.5), 32x32
          [Text "DISCONNECTED"]  12px, color=rgba(255,80,80,0.5)
        — Grid overlay (yellow lines) drawn on top of everything:
        [Image Img_GridOverlay]  fill, opacity=0.28
          Use a 3x3 grid texture asset (create once, reuse everywhere)

  [Text Text_FeedStateLabel]  centered below frame
    "Clear" in #4CAF50
    "Needs fix" in #F44336
    "Disconnected" in #F44336 (pulsing opacity)
```

**Function SetFeedData(FHMCCameraFeed Feed):**
- If Feed.FrameTexture not null AND FeedState != Disconnected:
  - Set Img_Feed brush = Feed.FrameTexture
  - Hide DisconnectedOverlay
- If FeedState == Disconnected OR texture null:
  - Show DisconnectedOverlay
- Text_RoleLabel = role string (Top/Bottom/etc.)
- Text_Timecode = Feed.Timecode, hidden if Disconnected
- Drive Border_Frame color:
  - Clear → #4CAF50
  - NeedsFix → #F44336
  - Disconnected → #F44336 + start Anim_Pulse
- Drive Text_FeedStateLabel text and color
- Drive Text_FeedStateLabel animation: play Anim_Pulse if Disconnected

**Disconnected pulse:** same Anim_Pulse as in WBP_HMCFeedCell (render opacity 1→0.2→1 at 0.5s).
Create the animation on this widget too, or factor out into a shared widget if BP supports it.

---

## WBP_HMCPreview DESIGNER

```
[Vertical Box — Root]  background=#1A1A1A, fill screen

  [Horizontal Box — PanelHeader]  padding=12,16, border-bottom 1px #2A2A2A
    [Text "HMC PREVIEW"]  11px uppercase, letter-spacing, opacity=0.5
    [Spacer]
    [Horizontal Box — Legend]  align=center
      [Border Swatch_Clear]       10x10, bg=#4CAF50, border-radius=2, margin-right=4
      [Text "Clear"]              10px, opacity=0.6, margin-right=12
      [Border Swatch_Issue]       10x10, bg=#F44336, border-radius=2, margin-right=4
      [Text "Issue"]              10px, opacity=0.6, margin-right=12
      [Border Swatch_Disconnected] 10x10, bg=#F44336, border-radius=2, margin-right=4
        — pulse animation on this swatch too
      [Text "Disconnected"]       10px, opacity=0.6

  [ScrollBox — ContentScroll]  fill
    [Vertical Box VBox_ActorGroups]  padding=16
```

---

## WBP_HMCPreview GRAPH

**Event Construct:**
- (HMCComp is set by parent)
- Bind HMCComp.OnStatusUpdated → OnStatusUpdated
- Bind HMCComp.OnFrameReceived → OnFrameReceived
- Call RefreshActorGroups

**Function RefreshActorGroups:**
- Clear VBox_ActorGroups
- Get unique actor names from HMCComp.GetAllFeeds() (collect ActorName values, deduplicate)
- For each ActorName:
  - Create WBP_HMCActorGroup (ActorName, HMCComp)
  - Call group.BuildFeeds(HMCComp.GetFeedsForActor(ActorName))
  - Add to VBox_ActorGroups

**OnStatusUpdated(DeviceName, Status):**
- Find actor group for Status.ActorName
- Call group.UpdateFeed for each feed where DeviceName matches

**OnFrameReceived(DeviceName, CameraIndex, Frame):**
- Find the FHMCCameraFeed that matches DeviceName + CameraIndex via GetAllFeeds()
- Find the actor group for that feed's ActorName
- Build a minimal FHMCCameraFeed with the new FrameTexture
- Call group.UpdateFeed(feed)

---

## GRID OVERLAY TEXTURE
Create once: Content/PCAPTool/Textures/T_GridOverlay_3x3
- 512x512 white texture with 3x3 grid lines at 33% and 67% (horizontal and vertical)
- Line stroke: 1px, color white
- Background: transparent (alpha = 0)
- Export as PNG with alpha, import to UE as Texture2D with alpha channel

Reuse this texture in every feed cell (both Setup and Preview).
Set Image brush to this texture, Tint = yellow (#FFEB3B), Image Color Opacity = 0.28.
