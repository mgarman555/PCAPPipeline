# Call Sheet — design spec

Status: **design, pending review** · Date: 2026-06-10 · Supersedes the "Shoot Setup" working name.

## 1. Purpose

`Call Sheet` is the **shoot-day prep tool**. One operator uses it before the shoot to declare
*everything that is called for the day* — which project, which stage(s), which shoot day, which
actors, which props — so that the Realtime Operator (and anyone else on the rig) knows exactly what
to look for. It is the "what's on deck today" surface.

It is **day-level**, not per-shot. Call Sheet never says "this actor goes in scene 3." It says
"these actors and these props are called today, on this stage, for this project." Breaking the day
down into individual shots and assigning talent/props per shot happens later, in the Realtime
Operator.

## 2. Place in the toolset

The PCAP toolset is organised by **workflow phase**, not by operator role:

| Phase | Tool | Job |
|-------|------|-----|
| Prep | **Call Sheet** *(this spec)* | Declare the day's project / stage / day / actors / props |
| Run  | Realtime Operator | Per-shot capture; pick from the call sheet, run takes |
| Calibrate | Calibration tool *(separate, not yet specced)* | Calibration passes (T-pose, ROM, etc.) |
| Troubleshoot | HMC Monitor | Live HMC device health |

Call Sheet **absorbs** the three standalone database tabs that exist today (Actor Database, Prop
Database, Stage Database). They stop being top-level tabs and become **sections inside Call Sheet**.
Each database keeps its own simple, consistent UI — Call Sheet is the frame that hosts them and adds
the day-level callout on top.

Net change to the top-level tab list:

- Remove: `Actor Database`, `Prop Database`, `Stage Database` (folded into Call Sheet).
- Add: `Call Sheet`.
- Kept: `HMC Monitor`, `Operator Console` / Realtime Operator (the latter gets the §7 alphabetical fix).
- Open: whether the read-only `Mocap Database` browser stays as its own tab or folds in (see §9).

## 3. UI architecture — left-rail workspace

One nomad tab, `Call Sheet`, built as a left-rail workspace (the native Unreal editor idiom). The
rail is the shoot-day setup spine; selecting an item fills the main pane with that section's editor.
Skin matches the existing dark-green PCAP Slate palette (`ColGreen #4AE080`, `ColText2`, `ColLabel`,
etc.).

```
┌ Call sheet ───────────────────────────  DA · MBS stage · Day_01 ┐
│ Context        │                                                │
│  • Project     │   <main pane — the selected section's editor>  │
│  • Stages      │                                                │
│  • Shoot day   │                                                │
│ Called out     │                                                │
│  • Actors      │                                                │
│  • Props       │                                                │
└────────────────┴────────────────────────────────────────────────┘
```

Rail groups:

- **Context** — `Project`, `Stages`, `Shoot day`. The frame: where and when.
- **Called out** — `Actors`, `Props`. The call sheet itself: who and what.

A persistent header shows the current context breadcrumb (project code · stage · day) so the chosen
day is always visible, and a one-line summary ("On the sheet today: 2 actors · 2 props · 1 stage")
gives the at-a-glance the Operator relies on.

## 4. Sections

### 4.1 Project
Pick the active Production, or create a new one (code + name). Sets `UMocapDatabase.ActiveProductionCode`.
Creating a project creates its content folder (see §8).

### 4.2 Stages
Pick the active stage config for the day from the Stage library, and create/edit stage configs
inline. This is the existing Stage Database UI (a stage = location + per-element checklist: body /
face / audio / vcam / timecode + preset), simplified to match the rest of Call Sheet. Sets the day's
`ActiveStageConfig` (falls back to the production's).

### 4.3 Shoot day
Pick or create the `ShootDay` (DayID + calendar date) under the active project. Sets
`UMocapDatabase.ActiveDayID`. Creating a day adds an `FShootDay` to the production.

### 4.4 Actors
The actor library, rendered as a clean list that **shows only the headshot, the `actorID`, and the
real name — nothing else**. All the rich detail (face scan, MetaHuman, default streams) stays in the
Actor Database asset; it is not surfaced here.

Each row has a **Call** toggle. Toggling builds the day's called-actor list. This section *is* the
Actor Database — browse, create, and edit happen here; there is no separate Actor Database tab.
`+ new actor` creates the roster asset inline, writing the same `UActorRosterEntry` everywhere.

### 4.5 Props
Identical pattern to Actors: the prop library (mesh thumbnail + `propID` + display name), a **Call**
toggle that builds the day's called-prop list, and `+ new prop` inline.

## 5. Data model change

Day-level callouts need storage. Today callouts only exist per-shot (`FShotSubject` /
`FPropEntry` on `FShot`). Add to `FShootDay`:

```cpp
// Day call sheet — what is called for the whole day (roster ID references).
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Call Sheet")
TArray<FString> CalledActorIDs;   // → UActorRosterEntry.ActorID

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Call Sheet")
TArray<FString> CalledPropIDs;    // → UPropRosterEntry.PropID
```

String-ID references mirror how shots already reference the roster (`FShotSubject.ActorID` is a
string). The Realtime Operator resolves these IDs against the rosters. The existing per-shot
structures are untouched — the Operator populates them, defaulting from the call sheet.

## 6. Realtime Operator integration

When the Operator assigns talent/props to a shot, the picker is seeded from the day's call sheet
(`CalledActorIDs` / `CalledPropIDs`) and **prioritises** those entries. The full Actor/Prop databases
remain reachable for anything off-sheet — the call sheet is a prioritised default, not a hard
restriction. (Detail lives in the Realtime Operator spec; this is the contract Call Sheet must
satisfy.)

## 7. Global UI rules

These apply across the whole toolset, not just Call Sheet:

1. **Alphabetical everywhere.** Every list and every database reference renders in alphabetical
   order — actor / prop / stage / project pickers, the Call Sheet sections, dropdowns. Days and shots
   use zero-padded IDs (`Day_01`, `010`), so alphabetical equals chronological for them. Already
   compliant: the Actor / Prop / Stage DB panels sort by ID. To fix: the Operator Console pickers and
   the Mocap Database tree currently use raw array order.
2. **Each database keeps its own simple UI**, consistent in look across sections.
3. **`+ new` is always available where a roster is shown** — both when calling actors/props to the
   day and in the database view of that same section — performing the identical create. (Each
   database lives only inside Call Sheet; the section is the database UI.)

## 8. Files & folders (CRUD)

Call Sheet edits the project structure by creating / updating / removing the underlying `.uasset`
files and folders — new project → new folder; new day → new `FShootDay`; create/delete a roster
entry → create/delete its asset.

**Database folder consolidation ("all databases in one folder").** Today the three rosters live in
three sibling folders:

```
/Game/Mocap/_Roster/Actors/
/Game/Mocap/_Roster/Props/
/Game/Mocap/_Roster/StageConfigs/
```

Proposed (RECOMMENDED — one parent, three typed subfolders, so the content browser stays legible):

```
/Game/Mocap/Database/Actors/
/Game/Mocap/Database/Props/
/Game/Mocap/Database/Stages/
```

`Content/Mocap` is currently empty on disk, so this is a clean-slate choice with no migration. The
literal alternative — a single flat folder holding all three asset types together — is possible
(roster IDs are unique) but mixes types in one view. **This is the one open decision below.**

## 9. Open decisions

1. **Folder layout** — one parent + three subfolders (recommended) vs. one flat folder.
2. **Actor sort key** — alphabetical by `actorID` (camelCase, e.g. `kevinDorman`) or by real name
   (e.g. last name). Default assumed: by the primary label shown (`actorID`).
3. **Stages per day** — single active stage per day (assumed) or multiple.
4. **Mocap Database browser** — keep the read-only tree as its own tab, or fold it into Call Sheet
   as an overview.

## 10. Out of scope

- Realtime Operator changes (own spec) — Call Sheet only guarantees the call-sheet contract in §6.
- The Calibration tool (own spec).
- Take recording / capture (already built — Phase 2 backend).

## 11. As-built (update log)

What actually shipped diverges from the original sections above — this records the final state (supersedes §8's paths and resolves §9's open decisions).

**Tool content area** (one source of truth: `PCAPPaths` in `PCAPToolPaths.h`). NOTE: UE forbids spaces in package paths, so folder names are space-free:
```
/Game/PCAPTool/
├── Databases/        Actors/ · Props/ · Stages/ · MasterPCAPDatabase
├── PipelineTools/    per-tool asset areas (created on demand via PCAPPaths::ToolDir)
└── Productions/      recorded takes
```
- The master database asset is **`MasterPCAPDatabase`** (the C++ class is still `UMocapDatabase` — a full class rename was deferred). It is **auto-created + self-assigned** on first tool open via `UPCAPToolSettings::GetOrCreateDatabase()` — zero manual setup.

**Menu** — `Window ▸ Tools` has two sibling groups:
- **PCAP Tools** — Call Sheet · Operator Console · HMC Monitor
- **Databases** — Actor Database · Prop Database · Stage Database (their own setup, not folded away as §2 originally said)
- The read-only **Mocap Database** browser was removed from the menu (its `SPCAPDatabasePanel` files are kept, unregistered).

**Database UIs** — rebuilt as modern **card galleries** (`STileView`): a grid of thumbnail cards; clicking a card opens a scrim-backed **detail popup** (`SOverlay`). Actor card = headshot, Prop = mesh, Stage = stage-reference-mesh + a tool-summary subtitle. Call Sheet still hosts these same panels in its Actors/Props/Stages sections (where the per-row/detail **Call** toggle drives the day callout).

**Stage** — gained a `StageReferenceMesh` (any mesh) field: the card thumbnail + the systems together let you see a stage's layout and tools at a glance.

**§9 decisions, resolved:** folder = one parent + typed subfolders (`/Game/PCAPTool/Databases/{Actors,Props,Stages}`); actor sort = by `actorID`; Mocap browser = removed from tools. Alphabetical-everywhere (§7.1) is done, including the Operator Console pickers + the (now-removed) Mocap tree.
