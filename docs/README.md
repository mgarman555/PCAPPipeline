# PCAP Pipeline — documentation

Everything that isn't code: design specs, implementation plans, UMG build guides, handoffs, and per-tool how-tos. New to the repo? Start with the [root README](../README.md) for the big picture and [CONTRIBUTING](../CONTRIBUTING.md) for how the repo is worked.

Each substantial feature is brainstormed into a dated `YYYY-MM-DD-<topic>-design.md` spec (and often a matching `-plan.md`) before it's built. Newer specs supersede older ones; git history holds anything removed.

## Per-tool how-tos — [`tools/`](tools/)

Short usage pages, one per tool (also linked from the [plugin README](../Plugins/PCAPTool/README.md)):

- [Call Sheet](tools/call-sheet.md) — prep the shoot day
- [Databases](tools/databases.md) — the permanent libraries
- [Operator Console](tools/operator-console.md) — run the takes
- [HMC Monitor](tools/hmc-monitor.md) — per-camera health checks
- [VCam Operator](tools/vcam-operator.md) — virtual camera
- [Take Browser](tools/take-browser.md) — label takes, run the end-of-day queue
- [Volume Visualizer](tools/volume-visualizer.md) — see the floor to scale

## Design specs & plans — [`specs/`](specs/)

**Call Sheet**
- [2026-06-16 single-page Call Sheet + Production library](specs/2026-06-16-callsheet-singlepage-design.md) — **current.** One scrollable sheet; header pickers; chip + searchable "+ call" callouts.
- [2026-06-15 Call Sheet prep hub + shared widgets](specs/2026-06-15-callsheet-prep-hub-design.md) — the prior step: databases as pure libraries, `SPCAPRosterCard`, VCam database.

**Data model & record backend**
- [2026-06-09 Phase 1 — data-model migration](specs/2026-06-09-pcap-phase1-data-model-migration-design.md) ([plan](specs/2026-06-09-pcap-phase1-data-model-migration.md)) — the `FProduction → FShootDay → FSession → FShot` hierarchy + roster DataAssets.
- [2026-06-09 Phase 2 — Take Recorder backend](specs/2026-06-09-pcap-phase2-take-recorder-design.md) — driving RECORD/STOP from the Operator Console.

**Volume Visualizer**
- [2026-06-15 design](specs/2026-06-15-volume-visualizer-design.md) ([plan](specs/2026-06-15-volume-visualizer-plan.md)) — placeable actor; Vicon markers as dots/labels to scale in the stage FBX; Live Link stand-in (Phase 1) + Vicon SDK marker cloud (Phase 2).

**HMC (head-mounted camera)**
- [2026-06-16 pipeline-watcher definition](specs/2026-06-16-hmc-pipeline-watcher-definition-design.md) — **current.** The auto per-camera watcher (pipeline×config, lighting, focus, framing). Plans: [core](specs/2026-06-16-hmc-watcher-core.md) · [UI](specs/2026-06-16-hmc-watcher-ui.md).
- [2026-06-10 capture-checks design](specs/2026-06-10-hmc-capture-checks-design.md) — the original per-camera health-check model.

**Operator**
- [2026-06-12 Operator Console comfort + integration](specs/2026-06-12-operator-console-comfort-integration.md).
- [2026-08-09 Send shot to Mocap Manager](specs/2026-08-09-mocap-manager-send-shot-design.md) — **current.** The Operator Console action that stages a called shot onto Epic's Performance Capture actors; closes follow-up #1 of the 5.8 integration.

**Take Browser & post-take**
- [2026-08-09 Take Browser + processing queue](specs/2026-08-09-take-browser-and-processing-queue-design.md) — **current.** Layer 5. Filter/label takes and drive `FTakeProcessingState`. Read the honesty section: body solve and HMC solve happen outside Unreal, so every step is operator *attestation*, not execution.

**Engine / platform**
- [2026-06-29 UE 5.8 — Mocap Manager / Performance Capture integration](specs/2026-06-29-ue58-mocap-manager-integration-design.md) — **current.** Engine bump to 5.8; `UPCAPMocapBridge` drives Epic's `ACapturePerformer` / `UPCapPropComponent` from the called shot.
- [2026-06-29 PCAPTool databases → Mocap Manager (PCap) data model](specs/2026-06-29-pcap-database-adoption-design.md) — **current.** Reflection-based sync projecting our roster/productions onto Epic's `UPCapPerformerDataAsset` / `UPCapPropDataAsset` + `FPCap*Record` DataTables.
- [2026-08-09 Stage alignment](specs/2026-08-09-pcap-stage-alignment-design.md) — **current.** Pairs `UStageConfigAsset` to Epic's stage by `AssetUID` and reports divergence *before* a shoot day. Closes follow-up #3.
- [2026-08-09 Take-record reconciliation](specs/2026-08-09-pcap-take-record-reconciliation-design.md) — **current.** Publishes `FTake` into `FPCapTakeRecord` / `FPCapSlateRecord`, plus the deferral policy that stops PCAPTool and the Mocap Manager double-recording. Closes follow-up #4.

## Build & CI — [`ci-setup.md`](ci-setup.md)

[CI setup and operations](ci-setup.md) — registering the self-hosted Windows runner that builds `PCAPPipelineEditor` on every push, the security posture behind having no `pull_request` trigger, and how to read a failed build. Until that runner is registered, the Windows compile gate is still manual: **nothing in this repo is verified until it builds on Windows.**

**VCam**
- [2026-06-12 VCam panel design](specs/2026-06-12-vcam-panel-design.md) ([core plan](specs/2026-06-12-vcam-panel-c++-core.md)) + [input-layer design](specs/2026-06-12-vcam-input-layer-design.md) — WVCAM replacement; controller input over UDP.

## UMG build guides — [`build-guides/`](build-guides/)

Step-by-step guides for hand-building the HMC UMG widget blueprints: [Monitor](build-guides/WBP_HMCMonitorPanel_BuildGuide.md) · [Operator](build-guides/WBP_HMCOperatorPanel_BuildGuide.md) · [Preview](build-guides/WBP_HMCPreview_BuildGuide.md) · [Setup](build-guides/WBP_HMCSetup_BuildGuide.md).

## Handoffs — [`handoffs/`](handoffs/)

Historical session handoff notes (point-in-time; may be stale): [HMC session 5](handoffs/HMC_SESSION5_HANDOFF.md) · [Realtime Operator](handoffs/REALTIME_OPERATOR_HANDOFF.md) (the `claude/gallant-cerf-fe61d0` branch's pending work).
