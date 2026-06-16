# PCAPTool — design docs

Design specs and build notes. Each substantial feature is brainstormed into a dated `YYYY-MM-DD-<topic>-design.md` spec (and sometimes a matching `-plan.md`) before it's built. Newer specs supersede older ones; git history holds anything removed.

## Call Sheet
- [2026-06-16 — single-page Call Sheet + Production library](2026-06-16-callsheet-singlepage-design.md) — **current.** One scrollable sheet; header pickers; chip + searchable "+ call" callouts; the Production library.
- [2026-06-15 — Call Sheet prep hub + shared widgets](2026-06-15-callsheet-prep-hub-design.md) — the prior step: databases as pure libraries, `SPCAPRosterCard`, VCam database, stage dropdown.

## Volume Visualizer
- [2026-06-15 — design](2026-06-15-volume-visualizer-design.md) — placeable actor; Vicon markers as dots/labels to scale in the stage FBX; Live Link stand-in (Phase 1) + Vicon SDK marker cloud (Phase 2).
- [2026-06-15 — implementation plan](2026-06-15-volume-visualizer-plan.md) — task-by-task build (Phase 1).

## HMC (head-mounted camera)
- [2026-06-10 — capture-checks design](2026-06-10-hmc-capture-checks-design.md) — the per-camera health-check model.
- Build guides for the UMG panels: [Monitor](WBP_HMCMonitorPanel_BuildGuide.md) · [Operator](WBP_HMCOperatorPanel_BuildGuide.md) · [Preview](WBP_HMCPreview_BuildGuide.md) · [Setup](WBP_HMCSetup_BuildGuide.md).
- [HMC session 5 handoff](HMC_SESSION5_HANDOFF.md) — historical handoff note.

## Operator
- [2026-06-12 — Operator Console comfort + integration](2026-06-12-operator-console-comfort-integration.md).
- [Realtime Operator handoff](REALTIME_OPERATOR_HANDOFF.md) — the `claude/gallant-cerf-fe61d0` branch's pending work (not yet merged).
