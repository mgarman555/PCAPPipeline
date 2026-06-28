# Performance Capture integration — design

**Status:** Design · approach approved (*build on top*) · build pending (Madi compiles on Windows / UE 5.8)
**Engine:** UE **5.8** · plugins **Performance Capture Core** + **Performance Capture Workflow** (Mocap Manager) — both **Beta**
**Date:** 2026-06-28

## Why

UE 5.8 ships Epic's **Performance Capture** plugins, whose **Mocap Manager** panel
(`Window ▸ Virtual Production ▸ Mocap Manager`) already does much of what PCAPTool was built to do —
sessions, performers / props / characters, a stage, a recorder, and take review — plus new-in-5.8
**facial-animation preview** and **procedural auto-cameras**. Rather than keep reinventing that
backend, we **build on top of Epic's plugins**: adopt their data model where it overlaps, keep the
tools that are unique to us, and spend our effort on the **UI** (stock Mocap Manager is functional but
rough) and the extra elements we want.

## Decision — build on top, don't fork *(chosen 2026-06-28)*

- **Enable** `PerformanceCaptureCore` + `PerformanceCaptureWorkflow` in the project and as PCAPTool
  plugin deps. *(done — Phase 0)*
- **Adopt** Epic's runtime data model (performers, props, characters, retargeting, sessions, slates,
  takes) instead of our parallel one, wherever they overlap.
- **Keep** the tools Epic has no equivalent for: HMC head-cam health monitor, VCam-over-UDP operator,
  Vicon raw-marker Volume Visualizer, Call Sheet day-prep.
- **Replace / augment the UI:** our Slate panels become the front end over Epic's data, instead of
  stock Mocap Manager.
- **Do not vendor engine source.** The engine supplies it at build time; we only reference Epic's
  headers while coding (see *Source reference*).

## The target — Performance Capture plugins (5.8)

Two plugins:

- **Performance Capture Core** (runtime) — the data model & actors.
- **Performance Capture Workflow** (editor) — the Mocap Manager panel + asset-authoring flows, plus a
  `PerformanceCaptureWorkflowRuntime` helper module.

Confirmed **Core** classes (from the public API ref; confirm exact signatures against the 5.8 source
Madi shares):

| Class | Role |
|---|---|
| `ACapturePerformer` | Actor — a performer in the scene (skeletal mesh + Live Link). |
| `ACaptureCharacter` | Actor — a character driven by a performer (retargeted). |
| `UPerformerComponent` | Drives a performer actor from a Live Link subject. |
| `URetargetComponent` | Retargets performer → character. |
| `URetargetAnimInstance` | Anim instance backing the retarget. |
| `UPCapPropComponent` | *(Workflow runtime)* a prop driven by Live Link. |

Asset / data types the Mocap Manager authors (**names to confirm against source**):

- **PCapPerformer** data asset — performer definition (mesh + IKRig + Live Link subject).
- **PCapProp** data asset — prop definition (static/skeletal mesh + Live Link subject + offset).
- **Character** data asset — performer ref + retarget asset + IKRig + mesh + `CaptureCharacter` class.
- **Session** + **Production** structs — data tables in `Content/Pcap` (folder generation, active session).
- **Slates** table — predefined recording names / metadata (CSV import).
- **Takes Data** table — recorded takes, 1–5★ ratings, CSV export.

Mocap Manager tabs: **Sessions → Stage → Motion** (LiveLink / Performers / Props / Characters) **→
Record** (Slates / Mocap Recorder) **→ Review** (Take View).

## Mapping — current PCAPTool ↔ Epic 5.8

| Concern | PCAPTool today | Epic 5.8 | Plan |
|---|---|---|---|
| Productions | `FProduction` in `UMocapDatabase` | Production struct table | Adopt Epic's; migrate our fields onto it |
| Sessions / days | `FShootDay` / `FSession` | Session struct table + Sessions tab | Adopt Epic's session + folder gen |
| Actors → performers | `UActorRosterEntry` | `PCapPerformer` + `ACapturePerformer` | Adopt Epic's; map roster fields |
| Props | `UPropRosterEntry` | `PCapProp` + `UPCapPropComponent` | Adopt Epic's |
| Characters / retarget | *(none)* | Character asset + retarget | **New capability** — adopt |
| Stage | `UStageConfigAsset` + Volume Visualizer | BP_DemoStage + grid / ghost | Keep Vicon viz; optionally wrap their stage |
| Record | Operator Console + `PCAPTakeRecorderSubsystem` | Mocap Recorder + Slates | Re-point our console at Epic's recorder / slates |
| Review | *(none)* | Take View + ratings | **New** — adopt |
| Call Sheet | `SPCAPCallSheetPanel` | *(none)* | Keep; re-source from Epic's session / slate data |
| HMC monitor | full subsystem | *(none; 5.8 has facial preview)* | Keep; later compare to facial preview |
| VCam | `APCAPVCamActor` + UDP | *(none; 5.8 has auto-cameras)* | Keep; later compare to auto-cameras |
| Volume viz | Vicon raw markers | *(none)* | Keep — unique to us |

## Plan (phased)

**Phase 0 — Upgrade & enable.** ✓ *(this commit)*
- `.uproject` → 5.8; enable both plugins; PCAPTool.uplugin deps; live docs to 5.8.
- *Verify (Madi):* open in 5.8, confirm the project loads and **Mocap Manager** appears under
  `Window ▸ Virtual Production`.

**Phase 1 — Reference + build smoke.**
- Madi shares the two plugins' `Source/` trees (Drive or gitignored `Reference/`).
- Confirm PCAPTool compiles on 5.8 *without yet* referencing Epic's modules (pure version bump). Any
  5.7→5.8 breakage inside PCAPTool goes into `CurrentErrorCodes`; fix from the log.

**Phase 2 — Data-model adoption (Core).**
- Add the Performance Capture modules to `PCAPTool.Build.cs`.
- Mapping layer: our roster / session reads + writes go through Epic's assets.
  `UActorRosterEntry`→performer, `UPropRosterEntry`→prop, productions/sessions→their tables; keep our
  IDs as the stable key.

**Phase 3 — Recorder & review.**
- Re-point Operator Console at Epic's Mocap Recorder + Slates; add a Review surface over Takes Data.

**Phase 4 — UI pass + unique tools.**
- Rebuild the front end (the value-add): unify our panels into a cleaner workflow over Epic's data.
  Fold in HMC, VCam, Volume Visualizer. Compare HMC ↔ facial preview and VCam ↔ auto-cameras; decide
  keep / replace per tool.

## Source reference — how I match "their code"

Building on top means we **never commit** Epic's plugin source — the engine provides it at build time.
I only need it as a **reference** while writing the integration. To share it, copy these two folders'
`Source/` trees from the 5.8 install (find them by searching the install for
`PerformanceCaptureCore.uplugin` / `PerformanceCaptureWorkflow.uplugin`):

- `…/UE_5.8/Engine/Plugins/.../PerformanceCaptureCore/`
- `…/UE_5.8/Engine/Plugins/.../PerformanceCaptureWorkflow/`

Drop them in **Google Drive** (readable via MCP) or a local **`Reference/`** folder (gitignored). The
repo can be private for personal use, but with this approach we don't vendor engine code either way.

## Build / verify reality

No engine in the authoring environment — every build checkpoint runs on Madi's Windows 5.8 machine; I
write to the API and fix from `CurrentErrorCodes`. Slate / subsystem code has no local test runner, so
verification = brace/symbol check + compile-review + the Windows build + on-rig behavior.

## Sources

- [Mocap Manager in Unreal Engine (5.8 docs)](https://dev.epicgames.com/documentation/unreal-engine/mocap-manager-in-unreal-engine)
- [Performance Capture Workflow — plugin API index](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/PluginIndex/PerformanceCaptureWorkflow)
- [PerformanceCaptureCore — plugin API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/PerformanceCaptureCore)
- [Unreal Engine 5.8 is now available](https://www.unrealengine.com/news/unreal-engine-5-8-is-now-available)
