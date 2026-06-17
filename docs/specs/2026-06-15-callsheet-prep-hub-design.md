# Call Sheet prep hub + shared widgets — design spec

**Date:** 2026-06-15  **Status:** approved (Madi), building
Part of the suite redesign — see `pcap-suite-redesign` memory.

## Goal
Make Call Sheet the day **prep hub**, backed by **pure database libraries** that share one widget. Spine: **Databases (libraries) → Call Sheet (prep) → Live Link Hub (day binding) → Monitors**.

## Decisions (locked)
- **VCam = a 4th database/library** (`UPCAPVCamConfig`), managed + called out like Actor/Prop/Stage.
- **Stage callout = dropdown of configured stages** (defaults to last used) → the chosen stage's **setup (systems) shows below, editable**; **edits write back to that stage preset** ("preset = last setup").
- **Databases do NOT talk to Live Link** — remove the "streaming now → +add" strips; delete `PCAPLiveSubjects`.
- **Shared widget**: one `SPCAPRosterCard` reused by all libraries + Call Sheet callouts.
- One active stage per day (multi-stage deferred).

## Components
1. **`SPCAPRosterCard`** (new shared Slate widget): thumbnail + ID + name + optional "called" toggle + OnClicked. The single card all grids/callouts compose. Args: thumbnail brush/asset, title, subtitle, bShowCalled, called state + toggle delegate, click delegate.
2. **`SPCAPVCamDatabasePanel`** (new): card-grid of `UPCAPVCamConfig` under `PCAPPaths::` (new `VCamsDir`), mirrors Prop panel, built from `SPCAPRosterCard`. Registered in the Databases menu group (`VCamDBTabName`).
3. **Databases pure**: Actor/Prop panels lose `StreamingBox`/`RefreshStreaming`/`TickStreaming` + the PCAPLiveSubjects include/timer; `PCAPLiveSubjects.h/.cpp` deleted.
4. **Call Sheet** left rail: `Overview · Project · Shoot Day · Shots(CSV) · Stages · Actors · Props · VCam`.
   - Actors/Props/VCam sections → callout picker = `SPCAPRosterCard` grid (bShowCalled) over the library; toggle sets day callout.
   - Stages section → `SComboBox` of configured stages (default last used) + the chosen stage's editable systems below (write back to the asset).
5. **Data model**: `FShootDay` gains `TArray<FString> CalledVCamIDs;` (CalledActorIDs/CalledPropIDs + active-stage ref already exist). DB helpers `IsVCamCalled`/`SetVCamCalled` on `UMocapDatabase` (mirror actor/prop).
6. **Paths**: `PCAPPaths::VCamsDir()` → `Databases()/VCams`.

## Build sequence (guarded commits)
- **A — DB purge**: remove Live Link strips from Actor/Prop, delete `PCAPLiveSubjects`. (Completes "databases pure"; reverses the option-B strips.)
- **B — Shared card + VCam DB**: `SPCAPRosterCard`; `SPCAPVCamDatabasePanel` + tab registration + `VCamsDir`; `CalledVCamIDs` + DB helpers. New code, doesn't disturb working panels.
- **C — Call Sheet**: VCam callout section; Stages dropdown + editable setup; route Actors/Props/VCam callouts through `SPCAPRosterCard`.
- **Deferred**: retrofit existing Actor/Prop/Stage management grids onto `SPCAPRosterCard` (risky uncompiled refactor of working UIs — do once the new card is proven).

## Testing
Pure/automation: `IsVCamCalled`/`SetVCamCalled` toggle, library sort order, stage-preset seed. (`PCAPDataModelTests.cpp` style.)

## Notes / risks
- Author-only (UE 5.7 builds on Windows); Madi compiles. Shared file watch: `FShootDay`/`PCAPToolTypes.h`, `PCAPTool.cpp` registration, `MocapDatabase` — guarded commits, parallel sessions active.
- `UPCAPVCamConfig` has no thumbnail asset → VCam card shows a name/id chip (no mesh), fine.
