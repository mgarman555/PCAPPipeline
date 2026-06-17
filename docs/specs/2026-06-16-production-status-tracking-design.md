# Production status & shoot-progress reference

**Date:** 2026-06-16
**Status:** design (approved in brainstorm; pending spec review)

## Goal

Make each production in the **Production Database** *remember where it is*: a manual lifecycle **phase**, an automatic **shoot-progress** readout (which days are shot, how many takes, the last shot date), and its **associated stage**. The point is a **reference, not a guard** — at a glance you can tell what's prepped vs what's actually in the can, so you know whether to add a day and never re-shoot one.

## Motivation

"How far are we into testing — and never override data." Two facts make this simple:

1. **Takes are already append-only.** `PCAPTakeRecorderSubsystem` records each take with the *next* number for its shot and a unique `Day/Session/Shot/TakeID` asset path, stamping `FTake.RecordedAt = UtcNow()` and appending to `Shot->Takes`. A normal RECORD physically cannot overwrite an existing take.
2. So "never override data" is already true at the take level. What's missing is **visibility** — you can't currently see which days hold data. This feature adds that visibility; it is the reference that keeps you from re-shooting a day.

No active guard or lock is in scope (see Non-goals).

## Design

### What a production's card shows (Production Database detail)

```
RILEY — Riley Feature
Phase:      [ Testing ▾ ]         ← manual: Prep · Testing · Shooting · Wrapped
Stage:      Osborne_LAEast        ← FProduction.ActiveStageConfig, shown (already stored)
Last shot:  2026-06-12            ← most recent take RecordedAt across the production (“—” if none)

Days
  D001 · shot 2026-06-10
  D002 · Prep                     ← added, zero takes
  D003 · shot 2026-06-12
```

### A compact line in the Call Sheet header

For the active production, the Call Sheet header shows a one-line reference: `Testing · last shot 2026-06-12` (or `· not shot yet`). Read-only; the Call Sheet's production interaction is otherwise unchanged (the `+` create-or-call stays as is).

### Rules — everything is derived except Phase

| Field | Source |
|---|---|
| Day is **shot** | the day has ≥1 take (any `Session.Shots[].Takes`); otherwise **Prep** |
| Day **shot date** | the latest `FTake.RecordedAt` among that day's takes (date portion) |
| Production **last shot** | the maximum `FTake.RecordedAt` across all the production's takes |
| Production **days shot** | count of days with ≥1 take, over `Days.Num()` |
| Production **take count** | total takes across the production |
| **Phase** | the only manually-set value (`FProduction.Phase`) |

Dates render date-only (`YYYY-MM-DD`).

## Data model

One new stored field; everything else is computed.

```cpp
UENUM(BlueprintType)
enum class EProductionPhase : uint8
{
    Prep      UMETA(DisplayName="Prep"),
    Testing   UMETA(DisplayName="Testing"),
    Shooting  UMETA(DisplayName="Shooting"),
    Wrapped   UMETA(DisplayName="Wrapped"),
};

// FProduction gains:
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Status")
EProductionPhase Phase = EProductionPhase::Prep;
```

No change to `FTake`, `FShootDay`, or the record backend — `RecordedAt` is already stamped on record.

## Pure helpers (testable)

Add static, struct-only helpers to `UPCAPToolStatics` so the logic is unit-testable without the editor:

```cpp
static bool      IsDayShot(const FShootDay& Day);             // any take present
static FDateTime DayLastShotTime(const FShootDay& Day);       // max RecordedAt in the day (0 if none)
static int32     CountDaysShot(const FProduction& Prod);      // days with >=1 take
static int32     CountTakes(const FProduction& Prod);         // total takes
static FDateTime ProductionLastShot(const FProduction& Prod); // max RecordedAt across the production (0 if none)
static FString   FormatShotDate(const FDateTime& When);       // "YYYY-MM-DD", or "—" if When == 0
```

The UI calls these; it holds no progress logic of its own.

## Components touched

| File | Change |
|---|---|
| `PCAPToolTypes.h` | add `EProductionPhase` + `FProduction::Phase` |
| `PCAPToolStatics.{h,cpp}` | add the six pure helpers above |
| `SPCAPProductionDatabasePanel.{h,cpp}` | card detail: Phase dropdown, Stage line, Last-shot line, Days list with per-day Prep/shot-date |
| `SPCAPCallSheetPanel.cpp` | header: compact `Phase · last shot <date>` for the active production |

## Testing

Automation tests for the pure helpers (matches the existing pattern for `MakeShotSubjectFromRoster` etc.): build an `FProduction` in memory with days/sessions/shots/takes at known `RecordedAt` values and assert `IsDayShot`, `DayLastShotTime`, `CountDaysShot`, `CountTakes`, `ProductionLastShot`, and `FormatShotDate` (including the empty/`—` and single-day edge cases).

## Non-goals (deferred)

- **No active override guard or per-day lock.** Takes are append-only already; this feature is reference-only. A re-seed confirm or day-lock can be added later if a real workflow needs it.
- **Phase labels are a fixed enum** for v1 (Prep/Testing/Shooting/Wrapped). Custom labels can come later if needed.
- No change to how takes are recorded or named.

## Open questions

None — resolved in the brainstorm.
