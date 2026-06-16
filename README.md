# PCAP Pipeline

A performance-capture session-management pipeline for **Unreal Engine 5.7**, built for a solo operator (or small team) running a mocap volume end to end: **prep the day → run the takes → watch the floor.**

The heart of the project is the **PCAPTool** editor plugin — a workflow-organized toolset on top of one shared data model — plus **LiveLinkViconDataStream** for getting Vicon data into the engine.

---

## The toolset

Tools register under **Window ▸ Tools** in two groups — **PCAP Tools** (operator tools) and **Databases** (the libraries).

| Tool | Group | What it does |
|---|---|---|
| **Call Sheet** | PCAP Tools | Single-page day prep — pick production / day / stage, call out actors · props · vcam, build the shot list (CSV import/export) |
| **Operator Console** | PCAP Tools | Navigate the day's shots and run takes (Take Recorder backend) |
| **HMC Monitor** | PCAP Tools | Per-camera head-mounted-camera health checks |
| **VCam Operator** | PCAP Tools | Virtual camera control (a WVCAM replacement) |
| **Actor / Prop / Stage / VCam / Production Database** | Databases | Permanent card libraries — everything ever created |
| **Volume Visualizer** | *placeable actor* | Vicon markers as dots + labels, to scale inside the stage FBX |

## Architecture — the spine

Libraries hold the data, the Call Sheet preps the day from them, Live Link binds the called-out items to their live sources at runtime, and purpose-built monitors watch the floor.

```mermaid
flowchart LR
  DB["Databases<br/>(Actor · Prop · Stage · VCam · Production)"] --> CS["Call Sheet<br/>(day prep / call-out)"]
  CS --> LL["Live Link Hub<br/>(day binding — what streams from where)"]
  LL --> RUN["Operator Console<br/>(run takes)"]
  LL --> MON["Monitors<br/>(Volume Visualizer · HMC)"]
```

**Principles:** databases are pure libraries (no Live Link) that expose shared Slate widgets; the Call Sheet *composes* those widgets rather than re-implementing them; monitoring is distributed into small purpose-built surfaces, not one mega-panel. All tool assets live under one content root, `/Game/PCAPTool/` (see `PCAPToolPaths.h`).

## Plugins

| Plugin | Purpose |
|---|---|
| [`PCAPTool`](Plugins/PCAPTool) | The toolset + data model. Editor module; drops into any UE5 project. |
| `LiveLinkViconDataStream` | Vicon DataStream → Live Link, and the bundled Vicon DataStream SDK that the Volume Visualizer uses for the raw marker cloud. |

## Build & run

- **Engine:** Unreal Engine **5.7.4**. The project builds on **Windows** (MSVC).
- Right-click `PCAPPipeline.uproject` → **Generate Visual Studio project files**, then build **Development Editor / Win64** (or open the `.uproject` and let it compile the modules).
- After a successful build, **restart the editor** — the tool tabs register at module startup — and find them under **Window ▸ Tools**.

## Repository layout

| Path | What |
|---|---|
| `PCAPPipeline.uproject` | The UE project |
| `Source/` | Project-level game module |
| `Plugins/PCAPTool/` | The PCAP toolset plugin ([README](Plugins/PCAPTool/README.md)) |
| `Plugins/LiveLinkViconDataStream/` | Vicon DataStream Live Link + SDK |
| `Config/` | Project config (`.ini`) |
| `Content/` | UE content (assets live under `Content/PCAPTool/`) |
| `Plugins/PCAPTool/Docs/` | Design specs ([index](Plugins/PCAPTool/Docs/README.md)) |

## Documentation

Design specs and build notes live in [`Plugins/PCAPTool/Docs/`](Plugins/PCAPTool/Docs/README.md). Each substantial feature is brainstormed into a dated `*-design.md` spec before it's built.
