# PCAPTool

The performance-capture toolset for Unreal Engine 5.7 — a workflow-organized set of editor tools on top of one shared data model. Drops into any UE5 project as an editor plugin.

See the [repo README](../../README.md) for the big picture; this covers the plugin itself.

## Quick start

1. Build the project and open the editor (tabs register at startup — restart after a fresh build).
2. **Window ▸ Tools ▸ PCAP Tools ▸ Call Sheet.** In the header, type a **production code** (e.g. `RILEY`) ↵, then a **day id** (e.g. `001`) ↵.
3. Pick a **stage** from the header dropdown; **+ call** your actors / props / vcam; build the **shot list** (or *Import CSV*).
4. Populate the libraries any time in **Window ▸ Tools ▸ Databases** (Actor / Prop / Stage / VCam / Production).
5. Drop a **PCAP Volume Visualizer** actor in the level + assign a stage config to see the floor; run takes from the **Operator Console**.

## Tools

- **[Call Sheet](../../docs/tools/call-sheet.md)** — single scrollable day sheet: production/day/stage header + readiness, called actors/props/vcam (chips + searchable "+ call" picker), and the shot list.
- **[Databases](../../docs/tools/databases.md)** (Actor · Prop · Stage · VCam · Production) — permanent card libraries; each entry is a DataAsset (Production is a struct in the master DB). They're pure data — no Live Link — and expose the shared `SPCAPRosterCard`.
- **[Operator Console](../../docs/tools/operator-console.md)** — navigate the day's shots, drive RECORD/STOP via the Take Recorder backend.
- **[HMC Monitor](../../docs/tools/hmc-monitor.md)** — per-camera head-mounted-camera health checks.
- **[VCam Operator](../../docs/tools/vcam-operator.md)** — virtual camera (WVCAM replacement; controller input over UDP).
- **[Volume Visualizer](../../docs/tools/volume-visualizer.md)** — placeable actor; draws Vicon markers as dots + labels, to scale in the stage FBX (Live Link stand-in now; raw markers via the Vicon SDK).

## Data model

- **`UMocapDatabase`** — the master DB asset (auto-created at `/Game/PCAPTool/Databases/MasterPCAPDatabase` on first open). Holds the `FProduction → FShootDay → FSession → FShot` hierarchy plus the active production/day/session selection and the day call-out (`CalledActorIDs` / `CalledPropIDs` / `CalledVCamIDs`).
- **Library DataAssets** — `UActorRosterEntry`, `UPropRosterEntry`, `UStageConfigAsset`, `UPCAPVCamConfig`: one asset per item, under `/Game/PCAPTool/Databases/{Actors,Props,Stages,VCams}`.
- **Paths** — every tool derives asset paths from `PCAPToolPaths.h` (never hardcode `/Game/...`). Folder names are space-free (UE forbids spaces in package paths).

## Source map

| Area | Files |
|---|---|
| Types + data model | `PCAPToolTypes.h`, `MocapDatabase.{h,cpp}` |
| Library assets | `ActorRosterEntry.h`, `PropRosterEntry.h`, `StageConfigAsset.h`, `VCamConfig.h` |
| Shared UI | `SPCAPRosterCard.{h,cpp}` |
| Tools (Slate) | `SPCAPCallSheetPanel`, `SPCAP{Actor,Prop,Stage,VCam,Production}DatabasePanel`, `SPCAPOperatorConsole`, HMC panels, `SPCAPVCamPanel` |
| Volume viz | `PCAPVolumeVisualizer.{h,cpp}`, `PCAPMarkerSource.h`, `PCAPLiveLinkMarkerSource`, `PCAPViconSDKMarkerSource` |
| Record backend | `PCAPTakeRecorderSubsystem.{h,cpp}` |
| Module + tabs | `PCAPTool.cpp`, `PCAPToolModule.h`, `PCAPToolPaths.h`, `PCAPToolSettings.{h,cpp}` |

## Build notes

- Editor module (`PCAPTool`, `LoadingPhase: Default`). Key deps in `PCAPTool.Build.cs`: Slate/SlateCore, UnrealEd, LiveLink/LiveLinkInterface, TakeRecorder/TakesCore/MovieScene, PropertyEditor, AssetRegistry, DesktopPlatform; the Vicon DataStream SDK is referenced conditionally (`WITH_VICON_SDK`) from the sibling plugin.
- Authored on macOS (UE 5.4 as API reference), compiled on Windows (UE 5.7.4).

Design specs live in the repo's [`docs/`](../../docs/README.md); per-tool usage how-tos are in [`docs/tools/`](../../docs/tools/). See [CONTRIBUTING](../../CONTRIBUTING.md) for the build + git workflow.
