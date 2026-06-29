# UE 5.8 — Mocap Manager / Performance Capture integration

**Date:** 2026-06-29
**Status:** In progress (engine bump + bridge landed; UI wiring TBD)

## Why

Unreal Engine 5.8 ships Epic's official **Performance Capture** plugin with the
**Mocap Manager** — a Stage → Motion → Record → Review workflow built around
engine-native actors. That overlaps heavily with what PCAPTool grew by hand.
Rather than duplicate it, PCAPTool's databases stay the **source of truth** for
who/what is called, and we **drive** the official actors from that data so the
engine's recorder, facial preview, and Auto-cameras work out of the box.

## The two data models

| Concern | PCAPTool | Performance Capture (UE 5.8) |
|---|---|---|
| Performer | `FShotSubject` (body/face Live Link subjects, `DrivenTarget`) | `ACapturePerformer` (`SetLiveLinkSubject`, `SetMocapMesh`) — module `PerformanceCaptureCore` |
| Performer (component) | — | `UPerformerComponent` (`SubjectName`, `ControlledSkeletalMesh`) |
| Retarget to character | `FRetargetConfig`, `UActorRosterEntry.MetaHuman` | `ACaptureCharacter` + `URetargetComponent` / `URetargetAnimInstance` |
| Prop | `FPropEntry` (+ `UPropRosterEntry`) | `UPCapPropComponent` (`SubjectName`, `ControlledComponent`) — module `PerformanceCaptureWorkflowRuntime` |
| Stage | `UStageConfigAsset` | `BP_DemoStage` (duplicate-and-edit) |
| Session/Shot/Take | `FSession` / `FShot` / `FTake` (`UMocapDatabase`) | Mocap Manager session + Take Recorder |

Key insight: Epic's "performers/props" are **actors + components in the level**,
not standalone data assets. So integration = *projecting* PCAPTool's called shot
onto spawned/placed actors, not converting our DataAssets into theirs.

## Plugin availability (verified against the install)

Both plugins were located in the UE 5.8 install and the bridge APIs verified
against the real headers (via Drive):

- **`PerformanceCaptureCore`** — `Engine/Plugins/Animation/PerformanceCaptureCore`.
  `ACapturePerformer : ASkeletalMeshActor` with `SetLiveLinkSubject(FLiveLinkSubjectName)`,
  `SetMocapMesh(USkeletalMesh*)`, `SetEvaluateLiveLinkData(bool)`; module path
  `/Script/PerformanceCaptureCore`. Performer bridge matches exactly.
- **`PerformanceCaptureWorkflow`** — `Engine/Plugins/**VirtualProduction**/PerformanceCaptureWorkflow`
  (not `Animation/`, which is why a first look missed it). `PCapPropComponent.h`
  confirms `UPCapPropComponent : UActorComponent` (`PERFORMANCECAPTUREWORKFLOWRUNTIME_API`)
  with `SetControlledComponent(USceneComponent*)`, `SetLiveLinkSubject(FLiveLinkSubjectName)`,
  `SetEvaluateLiveLinkData(bool)`; module `PerformanceCaptureWorkflowRuntime`. Prop
  bridge matches exactly.

Both plugins are present, so `WITH_PCAP_WORKFLOW=1` and the full bridge (performers
+ props) compiles in. Set it to `0` (and drop the Workflow plugin/module) only to
target a machine that has Core but not Workflow.

The Workflow plugin also ships its **own** Mocap Manager data model —
`UPCapPerformerDataAsset` / `UPCapPropDataAsset` / `UPCapCharacterDataAsset`,
`UPCapSessionTemplate`, and `FPCapProductionRecord` / `FPCapTakeRecord` DataTable
rows (`PCapDatabase.h`). Mapping PCAPTool's `UMocapDatabase` onto *those* records
(not just spawning actors) is the deeper "build on Epic's data model" step — see
follow-ups.

## What landed in this pass

- **Engine bump 5.7 → 5.8**: `PCAPPipeline.uproject` (EngineAssociation +
  `PerformanceCaptureCore` and `PerformanceCaptureWorkflow` enabled),
  `LiveLinkViconDataStream.uplugin` EngineVersion, `PCAPTool.uplugin` deps, README.
  `.Target.cs`: `DefaultBuildSettings` bumped to `V7` (required on an installed
  5.8 engine — V6 diverges from the shared `UnrealEditor` warning-level settings
  and UBT rejects it), with `IncludeOrderVersion` pinned to `Unreal5_7` to keep
  existing modules compiling under their original IWYU rules.
- **Build deps**: `PerformanceCaptureCore` + `PerformanceCaptureWorkflowRuntime`
  added to `PCAPTool.Build.cs`; `WITH_PCAP_WORKFLOW=1` (both plugins present, so the
  prop bridge compiles in — flip to `0` to target a Core-only machine).
- **`UPCAPMocapBridge`** (`PCAPMocapBridge.h/.cpp`) — the adapter:
  - `ResolvePerformerSubject(FShotSubject)` → body subject, else face.
  - `SpawnPerformerForSubject` / `ConfigurePerformer` → `ACapturePerformer`,
    bound to the resolved Live Link subject and (when `DrivenTarget` is a
    `USkeletalMesh`) its mocap mesh.
  - `SpawnPropActor` → a static/skeletal mesh actor; under `WITH_PCAP_WORKFLOW`
    it carries a `UPCapPropComponent` bound to the prop's Live Link subject,
    otherwise it places transform-only.
  - `SpawnShotToStage` → spawns performers + props for a whole `FShot`,
    matching called props to their roster record by `PropID`.

## Mapping decisions

- **Subject precedence.** A performer streams from its **body** subject when it
  has one, else its **face** subject. ARKit/HMC-only actors still spawn a
  performer bound to the face subject.
- **Driven mesh.** Only a `USkeletalMesh` `DrivenTarget` is pushed via
  `SetMocapMesh`. A placed actor / component target is left for the operator to
  wire in the Mocap Manager (we don't second-guess an existing scene binding).
- **Props.** Tracked props bind their Live Link subject and enable evaluation;
  untracked props still place a mesh actor (transform-only) so set-dressing and
  later tracking are one toggle away.
- **No data duplication.** The bridge reads PCAPTool structs and writes engine
  actors. It never copies our DataAssets into PCap assets — the database remains
  the single record.

## Not yet done (follow-ups)

1. **Call site / UI.** Wire `SpawnShotToStage` to an Operator Console action
   ("Send shot to Mocap Manager") using the active `UMocapDatabase` shot +
   `PropRoster`.
2. **Retarget → `ACaptureCharacter`.** Map `FRetargetConfig` /
   `UActorRosterEntry.MetaHuman` onto `URetargetComponent` so performers drive
   their digital double, not just a mocap mesh.
2b. **Adopt the PCap database records.** Map `UMocapDatabase` onto the Workflow
   plugin's own data model (`UPCapPerformerDataAsset` / `UPCapPropDataAsset` /
   `UPCapCharacterDataAsset`, `FPCapTakeRecord`) so the Mocap Manager's own
   Motion/Review tabs read our roster directly — the deeper integration beyond
   spawning actors.
3. **Stage alignment.** Reconcile `UStageConfigAsset` with the Mocap Manager
   Stage tab / `BP_DemoStage`.
4. **Take Recorder reconciliation.** Decide whether `PCAPTakeRecorderSubsystem`
   defers to the Mocap Manager's recorder when the official session is active.
5. **HMC / facial preview.** Evaluate the new 5.8 facial-preview window against
   the in-house HMC Monitor — keep, replace, or run side-by-side.

## Build / verify note

These changes target UE 5.8 and compile on the Windows toolchain. Both bridge
APIs are **header-verified** against the actual install: the performer path
against `CapturePerformer.h` (`SetLiveLinkSubject`, `SetMocapMesh`,
`SetEvaluateLiveLinkData`) and the prop path against `PCapPropComponent.h`
(`SetControlledComponent`, `SetLiveLinkSubject`, `SetEvaluateLiveLinkData`). The
only calls not header-checked are the engine-stock mesh setters
`USkeletalMeshComponent::SetSkeletalMeshAsset` / `UStaticMeshComponent::SetStaticMesh`;
confirm those at first build.
