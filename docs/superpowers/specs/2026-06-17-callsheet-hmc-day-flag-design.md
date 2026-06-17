# Call Sheet "HMCs used today?" day flag — design

**Date:** 2026-06-17
**Status:** Approved (Madi, "go ahead and build")
**Sub-project:** #2 of the HMC-library / DB-presets decomposition (see `2026-06-16-hmc-library-and-db-presets-design.md`).

## Goal

Let the Call Sheet record, per shoot day, whether head-mounted facial capture (HMCs)
is planned — a single day-level yes/no — and gently surface that fact in the HMC
Monitor when a day is active but unmarked.

## Background — what already exists (do NOT rebuild)

- **Actor scoping is already done.** The HMC Setup actor dropdown
  (`SHMCSetupPanel::BuildActorDropdown`) lists only `CalledActorIDsForActiveDay()`,
  which reads `UMocapDatabase::GetActiveDay()->CalledActorIDs`. So "the HMC only finds
  the people called in the call sheet for the day" is already true; it shows
  "(no actors called on the active day)" when the day's call sheet is empty.
- **Pipeline stays in Capture.** The MetaHuman/Faceware pipeline and capture
  configuration are chosen on the HMC Monitor / Capture side, not the Call Sheet.
  The Call Sheet records only the day-level yes/no.

The one missing piece: `FShootDay` has `CalledActorIDs/CalledPropIDs/CalledVCamIDs` but
no HMC marker, so "are HMCs used today?" cannot be stored.

## Design

### 1. Data — one boolean on the day

Add to `FShootDay` (`PCAPToolTypes.h`):

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Call Sheet")
bool bHMCsUsed = false;   // "HMCs used today?" — head-mounted facial capture planned
```

Defaults off. Serializes with the `UMocapDatabase` asset exactly like the called-X
arrays — no new storage, inherited day lifecycle (copy/save/active-scoping) for free.

### 2. DB API — active-day accessors

On `UMocapDatabase`, mirroring the existing `IsActorCalled`/`SetActorCalled` pattern
(both operate on the active day, `ActiveProductionCode` + `ActiveDayID`):

```cpp
bool IsHMCDay() const;        // GetActiveDay() && GetActiveDay()->bHMCsUsed
void SetHMCDay(bool bUsed);   // sets the flag on the active day; no-op if none
```

`IsHMCDay()` returns false when there is no active day. Callers that must distinguish
"no active day" from "active day, unmarked" check `GetActiveDay()` directly (the HMC
Monitor hint does this).

### 3. Call Sheet UI — a day-level toggle

A compact toggle row, **"HMCs used today?"**, as its own `SScrollBox` slot in
`SPCAPCallSheetPanel::BuildSheet()`, placed **between the Stage area and Called actors**
(HMC capture is faces-on-actors, so it reads naturally just above who's called). It is
a single `SCheckBox` + label + muted hint — it deliberately does NOT reuse
`BuildCallSection` (that helper is for checklists of roster items; this is one switch).
On change → `SetHMCDay(checked)` + `MarkPackageDirty()`. Only rendered on the has-day
path of `BuildSheet` (same as the called sections).

### 4. HMC Monitor note — gentle, non-blocking

A thin strip at the top of **HMC Setup** (`SHMCSetupPanel`, between the header and the
device-list/detail body), shown only when there is an active day and it is not flagged:

> Today isn't marked as an HMC day — set it in the Call Sheet if you're capturing faces.

Visibility binds to a helper that returns true only when `GetActiveDay()` exists and its
`bHMCsUsed` is false. Nothing is hidden, disabled, or gated. Styled like the existing
informational hints (muted/amber text). Not added to Preview (the live-watch screen).

### 5. Explicitly NOT changing

- Actor scoping (already built).
- The pipeline / capture-configuration pickers (stay on the Capture/HMC Monitor side).
- Day **readiness** — `GetActiveDayReadiness` is untouched. The flag is a marker + a
  Monitor hint, not a readiness gate (per the chosen behavior).

## Testing

- **Data-model unit test** (`PCAPDataModelTests.cpp`, mirroring `FPCAPDayCalloutTest`):
  a new `FPCAPHMCDayTest` asserts `IsHMCDay()` is false by default, true after
  `SetHMCDay(true)`, and false again after `SetHMCDay(false)`.
- **UI** (toggle + banner) is verified by inspection in the editor on the Mac and on the
  rig/Windows build — Slate wiring isn't unit-tested in this module.

## Files

- Modify: `Public/PCAPToolTypes.h` (`FShootDay::bHMCsUsed`)
- Modify: `Public/MocapDatabase.h` + `Private/MocapDatabase.cpp` (`IsHMCDay`/`SetHMCDay`)
- Modify: `Private/Tests/PCAPDataModelTests.cpp` (`FPCAPHMCDayTest`)
- Modify: `Public/SPCAPCallSheetPanel.h` + `Private/SPCAPCallSheetPanel.cpp` (toggle)
- Modify: `Public/SHMCSetupPanel.h` + `Private/SHMCSetupPanel.cpp` (Monitor hint)
