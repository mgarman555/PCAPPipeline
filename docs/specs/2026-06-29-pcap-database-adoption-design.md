# PCAPTool databases → Mocap Manager (PCap) data model

**Date:** 2026-06-29
**Status:** Design (implementation next)
**Follows:** `2026-06-29-ue58-mocap-manager-integration-design.md`

## Goal

Make PCAPTool's databases drive Epic's Performance Capture **data model** (not
just spawn actors), so the Mocap Manager's Motion/Record/Review tabs can consume
our roster and productions directly.

## What Epic's model actually is (verified from the 5.8 source)

The PCap database lives in the **`PerformanceCaptureWorkflow`** module's
**`Private/`** folder (`PCapDatabase.h`, `/Script/PerformanceCaptureWorkflow`).
Two kinds of objects:

**Data assets** — `UPrimaryDataAsset` subclasses (`BlueprintType`):
| Class | Key fields |
|---|---|
| `UPCapPerformerDataAsset` | `PerformerName` (FName), `LiveLinkSubject` (FLiveLinkSubjectName), `PerformerActorClass` (`TSoftClassPtr<ACapturePerformer>`), `BaseSkeletalMesh`, `PerformerProportionedMesh`, `IKRig`, `PerformerColor` |
| `UPCapCharacterDataAsset` | `CharacterName`, `SourcePerformerAsset` (→Performer), `CaptureCharacterClass` (`TSoftClassPtr<ASkeletalMeshActor>`), `SkeletalMesh`, `IKRig`, `Retargeter` (`UIKRetargeter`) |
| `UPCapPropDataAsset` | `PropName`, `LiveLinkSubject`, `PropOffsetTransform`, `PropComponentClass` (`UPCapPropComponent`), `PropStaticMesh`, `PropSkeletalMesh`, `CustomPropClass`, `bHiddenInGame` |

**DataTable records** — `FTableRowBase` rows (each has `FGuid UID`, `bIsArchived`):
| Struct | Holds |
|---|---|
| `FPCapProductionRecord` | `ProductionName`, `ProductionNotes` |
| `FPCapSessionRecord` | name/notes, owning `ProductionUID`, content-folder paths, soft-refs to `Performers`/`Characters`/`Props`, a `TakesDataTable`, a `SessionSlateTable`, `SessionTemplate` |
| `FPCapTakeRecord` | `RecordedTake` (LevelSequence), timecodes, `Framerate`, `MocapStageRootTransform`, `TakeStatus` (👍/👎/neutral), `Rating` (0–5), `SessionUID` |
| `FPCapSlateRecord` | `Slate`, `SlateNote`, `SlateStatus`, `SessionUID` |

Rows live in `UPCapDataTable` (a `UDataTable`). A session owns its own Takes and
Slates DataTables; productions/sessions live in project-level tables.

## The constraint that shapes the design

- The data-asset / record classes are in a **private** module folder → we
  **cannot `#include`** them or link directly.
- The record-creation hook (`UPerformanceCaptureDatabaseHelper`) is a
  `WITH_EDITOR`, `Abstract`, Blueprint-only **stub** — there is **no native CRUD
  API** to call.

So we integrate by **reflection** (the pattern already in
`PCAPTakeRecorderSubsystem`): resolve classes/structs by `/Script` path, set
`FProperty` values, and write DataTable rows via the public `UDataTable` API.
No new compile-time dependency on the Workflow module's private headers.

## Direction: Epic is canonical; PCAPTool extends it

**Decision (operator, 2026-06-29):** use everything the Performance Capture /
Mocap Manager plugin already provides, then add onto it. Epic's data assets and
DataTable records are the **authoritative store** for performers, characters,
props, productions, sessions, takes, and slates. PCAPTool does **not** keep a
parallel `UMocapDatabase` model of those — it reads Epic's, and adds the
capabilities Epic lacks (HMC monitoring, VCam, Vicon volume, audio, per-take
processing, call-sheet flow).

```
Performers / Characters / Props  → Epic data assets (canonical) + PCAPTool extension
Productions / Sessions / Takes / Slates → Epic FPCap*Record DataTables (canonical)
HMC · VCam · Vicon · Audio · Processing · Call flags → PCAPTool, attached to the above
```

So the core entity *is* the `UPCapPerformerDataAsset` / `UPCapPropDataAsset` /
record; PCAPTool's databases become **viewers/editors over Epic's data** plus an
**extension layer** for our extra fields. `UMocapDatabase` and the roster
DataAssets are retired as the primary store (kept only as a migration source).

### The boundary problem
Epic's data-asset/record classes live in the Workflow plugin's **private**
module, so PCAPTool's C++ can neither `#include` nor subclass them. Two ways to
read/extend across that boundary:
- **Reflection** for reads/writes (load the assets as `UObject`, get/set
  `FProperty` by name; query the DataTables by row struct path) — keeps our tools
  in C++.
- **Blueprint / Python** glue where the classes are first-class (`Blueprintable`,
  `/Script/PerformanceCaptureWorkflow`) — natural for subclassing/extension.

The extension mechanism (sidecar extension asset vs. Blueprint subclass of the
PCap asset) is the open decision below.

## Field mapping (what PCAPTool reads from Epic, and what it adds)

Below, the Epic asset/field is the **canonical** value PCAPTool reads; the
PCAPTool-only fields (HMC rig, audio streams, digital-double extras, processing
state) are what the extension layer **adds** alongside.

**`UActorRosterEntry` → `UPCapPerformerDataAsset`**
- `PerformerName` ← `ActorID`
- `LiveLinkSubject` ← `DefaultBodyStream.LiveLinkSubjectName`
- `BaseSkeletalMesh` ← (performer base mesh; from roster when present)
- `IKRig` ← `FRetargetConfig.IKRigSource`
- `PerformerActorClass` ← default `ACapturePerformer`

**`UActorRosterEntry` (digital double) → `UPCapCharacterDataAsset`**
- `CharacterName` ← `ActorID` (or character name)
- `SourcePerformerAsset` ← the performer asset above
- `SkeletalMesh` ← `MetaHuman` (resolved body mesh)
- `IKRig` ← `FRetargetConfig.IKRigTarget`; `Retargeter` ← `FRetargetConfig.IKRetargeter`

**`UPropRosterEntry` → `UPCapPropDataAsset`**
- `PropName` ← `PropID`
- `LiveLinkSubject` ← `DefaultLiveLinkName`
- `PropStaticMesh` / `PropSkeletalMesh` ← `PropAsset` (by resolved type)

**`FTake` → `FPCapTakeRecord`**
- `RecordedTake` ← `MasterSequence`; `DateTimeCreated` ← `RecordedAt`;
  `TakeStatus` ← `ETakeLabel` (Best→ThumbsUp, Burn→ThumbsDown, else Neutral);
  notes → `SlateNote` on the matching `FPCapSlateRecord`.

## Implementation plan

1. **`UPCAPMocapData`** (reflection access layer in PCAPTool) — the read side:
   - `EnumeratePerformers()/Props()/Characters()` via the Asset Registry
     (`/Script/PerformanceCaptureWorkflow.PCapPerformerDataAsset` etc.).
   - Typed getters by `FProperty` (`GetPerformerSubject`, `GetPerformerMesh`, …)
     so our Slate panels and tools read Epic's assets without the private headers.
   - Read the production/session/take/slate rows from their `UPCapDataTable`s.
2. **Extension layer** (the "add onto it" side) — attach PCAPTool-only data
   (HMC rig, audio streams, processing state, call flags) to an Epic entity by
   its `AssetUID`/record `UID`. Mechanism = the open decision below.
3. **Re-point the tools** at Epic's data, one at a time:
   - Databases/Call Sheet → list & pick Epic performers/props/characters.
   - HMC Monitor / VCam → resolve their target by Epic performer (LiveLink subject).
   - Operator Console / Take Recorder → Mocap Manager session + `FPCapTakeRecord`.
4. **Migration**: one-time importer that turns existing `UActorRosterEntry` /
   `UPropRosterEntry` / `FProduction` into Epic assets + records (reuses the
   field mapping above), after which `UMocapDatabase` is retired.
5. **Guarding**: everything behind `WITH_PCAP_WORKFLOW`; reflection lookups log and
   no-op if a class/property moved.

## Open decision — how to "add onto" an Epic asset

Epic's data assets are `Blueprintable` but in a private C++ module. Two ways to
attach PCAPTool's extra fields:
- **A. Sidecar extension asset** — a PCAPTool `UDataAsset`
  (`UPCAPPerformerExtension`, etc.) that soft-refs the Epic asset by `AssetUID`
  and holds our extra fields. Keeps our data in C++-native types we fully own;
  Epic's asset stays untouched. Pairing is by GUID.
- **B. Blueprint subclass of the Epic asset** — a BP child of
  `UPCapPerformerDataAsset` with our added fields. Most "native" (Mocap Manager
  treats it as a performer asset); our C++ reads the added fields by reflection.

Leaning **A** (clean C++ ownership, no fork of Epic's asset, survives plugin
updates) — pending operator confirmation.

## Verify note

Reflection-resolved names to confirm at first run (log on null):
`/Script/PerformanceCaptureWorkflow.PCapPerformerDataAsset`,
`.PCapPropDataAsset`, `.PCapCharacterDataAsset`, and the row structs
`.PCapProductionRecord` / `.PCapSessionRecord` / `.PCapTakeRecord` / `.PCapSlateRecord`,
plus their property names (`PerformerName`, `LiveLinkSubject`, `PropName`, …).
