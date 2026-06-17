# Volume Visualizer

See the capture floor **to scale** in the editor — Vicon markers drawn as dots + labels inside your stage's reference FBX, so you can tell where everything sits in the volume. It's a **placeable actor**, not a tab.

## Use it

1. Drag a **PCAP Volume Visualizer** into the level (Place Actors ▸ search "PCAP"), or use **Spawn volume visualizer** in the [Call Sheet](call-sheet.md) header.
2. Assign a **Stage Config** (a `UStageConfigAsset` from the [Stage Database](databases.md)) — the actor draws that stage's reference mesh and connects to the Vicon data stream for it.
3. Markers stream in as dots; solved subjects/props show labels. Marker size is adjustable (default 1 cm).

## Live data

- **Live Link** gives you *solved* subjects — works today (Phase 1).
- The **raw marker cloud** — everything Shogun sees, including unlabeled markers — needs the **Vicon DataStream SDK** (Phase 2), gated behind `WITH_VICON_SDK`.

> Design + plan: [`specs/2026-06-15-volume-visualizer-design.md`](../specs/2026-06-15-volume-visualizer-design.md) · [`specs/2026-06-15-volume-visualizer-plan.md`](../specs/2026-06-15-volume-visualizer-plan.md)
