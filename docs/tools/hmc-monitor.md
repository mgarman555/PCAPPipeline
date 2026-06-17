# HMC Monitor

Per-camera health checks for the **head-mounted cameras**. Confirms each camera is seeing what it should — framing, focus, lighting, frame rate — so you catch a bad HMC feed before it costs you a take.

**Open:** Window ▸ Tools ▸ PCAP Tools ▸ **HMC Monitor**.

## What it watches

The watcher runs automatic per-camera checks (pipeline × config): **lighting direction**, **nasolabial focus**, **too-close / too-far framing**, and **30 fps**. The UI gives clean camera feeds, a config picker, a **scan-readiness gate**, and a focus helper. A stereo calibration mode is included.

## Setup

The HMC UMG panels are hand-built blueprints — follow the build guides if you're (re)creating them: [Monitor](../build-guides/WBP_HMCMonitorPanel_BuildGuide.md) · [Operator](../build-guides/WBP_HMCOperatorPanel_BuildGuide.md) · [Preview](../build-guides/WBP_HMCPreview_BuildGuide.md) · [Setup](../build-guides/WBP_HMCSetup_BuildGuide.md).

> Design: [`specs/2026-06-16-hmc-pipeline-watcher-definition-design.md`](../specs/2026-06-16-hmc-pipeline-watcher-definition-design.md) · earlier model: [`specs/2026-06-10-hmc-capture-checks-design.md`](../specs/2026-06-10-hmc-capture-checks-design.md)
