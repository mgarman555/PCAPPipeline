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

## What landed in this pass

- **Engine bump 5.7 → 5.8**: `PCAPPipeline.uproject` (EngineAssociation +
  `PerformanceCaptureCore` / `PerformanceCaptureWorkflow` enabled),
  `LiveLinkViconDataStream.uplugin` EngineVersion, `PCAPTool.uplugin` deps, README.
- **Build deps**: `PerformanceCaptureCore`, `PerformanceCaptureWorkflowRuntime`
  added to `PCAPTool.Build.cs`.
- **`UPCAPMocapBridge`** (`PCAPMocapBridge.h/.cpp`) — the adapter:
  - `ResolvePerformerSubject(FShotSubject)` → body subject, else face.
  - `SpawnPerformerForSubject` / `ConfigurePerformer` → `ACapturePerformer`,
    bound to the resolved Live Link subject and (when `DrivenTarget` is a
    `USkeletalMesh`) its mocap mesh.
  - `SpawnPropActor` → a static/skeletal mesh actor carrying a
    `UPCapPropComponent` bound to the prop's Live Link subject.
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
3. **Stage alignment.** Reconcile `UStageConfigAsset` with the Mocap Manager
   Stage tab / `BP_DemoStage`.
4. **Take Recorder reconciliation.** Decide whether `PCAPTakeRecorderSubsystem`
   defers to the Mocap Manager's recorder when the official session is active.
5. **HMC / facial preview.** Evaluate the new 5.8 facial-preview window against
   the in-house HMC Monitor — keep, replace, or run side-by-side.

## Build / verify note

These changes target UE 5.8 and must be compiled on the Windows toolchain with
the Performance Capture plugin enabled (the engine modules aren't present in
this Linux authoring environment, so the bridge was written against Epic's
published 5.8 API, not a local build). Confirm `ACapturePerformer`,
`UPCapPropComponent`, and `USkeletalMeshComponent::SetSkeletalMeshAsset`
resolve against the installed engine on the first build.
