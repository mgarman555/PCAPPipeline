# Call Sheet → single-page + Production library — design spec

**Date:** 2026-06-16  **Status:** approved (Madi), building
Follows `2026-06-15-callsheet-prep-hub-design.md`; part of the suite redesign ([[pcap-suite-redesign]]).

## Goal
Rebuild the Call Sheet as a **single scrollable page** (no left rail) that reads like a real call sheet, and add a **Production library** (browser over existing productions). Actors, props, and **vcam** are all called out the same way (chips + "+ call" picker).

## Layout (top → bottom, one `SScrollBox`)
1. **Header card** — Production · Day · Stage pickers (`SComboButton`, reuse existing builders) + a readiness pill (`GetActiveDayReadiness`) + the stage's systems summary line + **Spawn volume visualizer** button + a collapsible **Stage setup** expander (the `IDetailsView` from the current Stages section; edits update the stage preset).
2. **Called actors** — chips of called actors (`SPCAPRosterCard`-style mini chip: dot/initials + id + `×` to uncall) + a **"+ call actor"** picker.
3. **Called props** — same, props.
4. **Called vcam** — same, vcams.
5. **Shots** — the existing shot list (CSV import/export + add slot + rows); reuse `BuildShotsSection`.

The rail, `BuildRailItem`, and the embedded `SPCAPActorDatabasePanel`/`Prop`/`Stage`/`VCam` sections are **removed** — management lives in the Database tabs; the Call Sheet only calls out + shows status.

## The "+ call" picker (one shared helper)
`BuildCallSection(Title, Items, IsCalled, SetCalled)` — type-erased via lambdas so one helper serves actors/props/vcam:
- `Items`: `TArray<TPair<FString /*id*/, FString /*display*/>>` gathered from the library (asset registry).
- chips = ids where `IsCalled(id)`; `×` → `SetCalled(id,false)` + refresh.
- "+ call" = `SComboButton` whose menu is a searchable checklist (`SSearchBox` + scrollable `SCheckBox` rows) of all `Items`; toggling a row → `SetCalled(id, checked)` + refresh the section.
- Wiring: actors→`Is/SetActorCalled`; props→`Is/SetPropCalled`; vcam→`Is/SetVCamCalled` (all exist on `UMocapDatabase`).

## Production library (new connecting DB)
`SPCAPProductionDatabasePanel` — a card grid over the master DB's `Productions` (FProduction **structs**, not assets — no migration). Tile source = `TArray<FString>` project codes; each tile = `SPCAPRosterCard` (title = ProjectCode, subtitle = ProductionName, dot). Click → detail popup: rename ProductionName, day count, set-active. "+ new" → append an `FProduction` + `MarkPackageDirty`. Registered in the **Databases** menu group (`ProdDBTabName`). The Call Sheet's Production picker still works off `DB->Productions`; this gives productions the same browsable card UI as the other libraries.

## Files
**New:** `Public/SPCAPProductionDatabasePanel.h` + `Private/…cpp`.
**Modified:** `Private/SPCAPCallSheetPanel.cpp` + `Public/…h` (single-page rewrite: drop rail + `BuildRailItem` + `BuildSectionContent` + embedded-panel cases; add `BuildSheet`, `BuildCallSection`, header composition; keep `BuildProjectSection`/`BuildShootDaySection`/`BuildStagesSection`/`BuildShotsSection` as header/section helpers). `PCAPTool.cpp` + `PCAPToolModule.h` (register `ProdDBTabName`).

## Build sequence (guarded commits)
- **1** — `SPCAPProductionDatabasePanel` + registration (new, contained).
- **2** — Call Sheet single-page rewrite (header + called sections via `BuildCallSection` + shots; remove rail/embeds).

## Testing
Pure-ish: `Is/SetXCalled` round-trip already covered. Manual: single page renders; pickers call/uncall; Production DB tab lists/creates; stage setup edits persist. Author-only (UE 5.7 builds on Windows) — Madi compiles.

## Notes / risk
- Big rewrite of `SPCAPCallSheetPanel` — keep the proven sub-builders (project/day/stage/shots) and recompose; the new code is the call-picker + Production panel.
- Shared file watch: `PCAPTool.cpp`/`PCAPToolModule.h` (registration), `PCAPToolTypes.h` (FProduction) — guarded commits, parallel sessions active.
- Deferred (unchanged): shared-card retrofit of the management grids; VP-element tools (need plugins synced).
