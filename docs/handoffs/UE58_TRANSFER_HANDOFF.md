# Transferring PCAPPipeline to a UE 5.8 machine — Handoff

**For a fresh Claude Code (or developer) session on the target Windows + UE 5.8 box · June 2026**

Self-contained: get PCAPPipeline cloned, built, and running on another computer that has Unreal
Engine **5.8**. Covers the engine plugins, the one binary that *isn't* in the repo (the Vicon runtime
DLL), and where the Performance Capture migration currently stands.

---

## 0. TL;DR

```bat
:: on the 5.8 Windows machine
git clone https://github.com/mgarman555/PCAPPipeline.git
cd PCAPPipeline
git checkout claude/upbeat-ritchie-qx9nzd   :: the 5.8 branch (PR #19); main is still 5.7
:: right-click PCAPPipeline.uproject -> Generate Visual Studio project files
:: open PCAPPipeline.sln -> build "Development Editor | Win64" -> launch
```

Everything needed is in the repo **except** the Vicon runtime DLL (§4) — and you only need that for the
raw-marker Volume Visualizer. No Git LFS required.

---

## 1. Prerequisites on the target machine

- **Unreal Engine 5.8** (Epic Games Launcher build). The `.uproject` `EngineAssociation` is `5.8`.
- **Visual Studio 2022** (MSVC v143) with the **Game development with C++** workload + a Windows
  10/11 SDK + .NET. The repo ships a **`.vsconfig`** — in the VS Installer use
  *More ▸ Import configuration* → pick `PCAPPipeline/.vsconfig` to get the exact component set.
- **Git** — *no Git LFS needed* (the `.uasset`s and the Vicon `.lib` are committed as plain blobs).
- **Engine plugins** present in the 5.8 install (all stock — see §3): Performance Capture
  Core/Workflow, LiveLink (+Camera/Lens/ControlRig), Takes, SequencerScripting, IKRig, RigLogic,
  MetaHuman, PythonScriptPlugin, EditorScriptingUtilities.
- *(Optional — Vicon raw markers only)* the **Vicon DataStream SDK runtime DLL** (§4).

---

## 2. Get the code

1. Clone: `git clone https://github.com/mgarman555/PCAPPipeline.git`
2. **Check out the 5.8 branch:** `git checkout claude/upbeat-ritchie-qx9nzd`
   - ⚠️ **`main` is still UE 5.7.** The 5.8 upgrade + Performance Capture enablement lives on this
     branch (draft **PR #19**). Once that PR merges, `main` becomes 5.8 and you can use `main`.
3. Bundled plugins arrive with the clone — nothing to download separately:
   - `Plugins/PCAPTool/` — our editor toolset.
   - `Plugins/LiveLinkViconDataStream/` — Vicon → Live Link, with the bundled SDK **headers + `.lib`**.

---

## 3. Plugins — what must be enabled / installed

Every non-bundled plugin is a **stock UE 5.8 engine plugin**; the `.uproject` already enables them, so
they only need to *exist* in the install. If a load warning appears, confirm in `Edit ▸ Plugins`.

| Plugin | Source | Notes |
|---|---|---|
| **PerformanceCaptureCore** | engine 5.8 | **NEW** — the mocap data model |
| **PerformanceCaptureWorkflow** | engine 5.8 (Beta) | **NEW** — the Mocap Manager panel |
| PCAPTool | **in repo** | our editor toolset |
| LiveLinkViconDataStream | **in repo** | Vicon Live Link + SDK |
| LiveLink / LiveLinkCamera / LiveLinkLens / LiveLinkControlRig | engine | streaming |
| Takes | engine | Take Recorder backend |
| SequencerScripting | engine | sequence scripting |
| IKRig | engine | declared by `PCAPTool.uplugin` |
| RigLogic, MetaHuman | engine | MetaHuman pipeline |
| PythonScriptPlugin, EditorScriptingUtilities | engine | scripting |

If `Edit ▸ Plugins` is missing **MetaHuman / RigLogic / Performance Capture**, install them through the
Launcher (they're part of the 5.8 install options / Fab for MetaHuman) and restart.

---

## 4. The one thing not in the repo — Vicon runtime DLL

The repo ships the Vicon SDK **headers + import lib** (`ViconDataStreamSDK_CPP.lib`) so the project
**compiles** with `WITH_VICON_SDK=1` on Win64. It does **not** ship the **runtime** DLL (it's
delay-loaded via `PublicDelayLoadDLLs`):

- **File needed:** `ViconDataStreamSDK_CPP.dll` — the runtime matching the bundled `.lib`/headers
  (Vicon DataStream SDK; the bundled Vicon UE plugin is v1.12 / "Unreal5Plugin16"). Easiest is to copy
  it off the current build machine.
- **Where to put it:** anywhere Windows' delay-load search finds it — simplest is the project's
  **`Binaries/Win64/`** (created after the first build), or the engine's `Binaries/Win64/`, or any
  folder on `PATH`.
- **Without it:** the build still succeeds and the **editor still launches** (delay-load); every tool
  works **except** the Volume Visualizer's raw Vicon-marker cloud. The Live Link stand-in path is
  unaffected.

---

## 5. Build

1. Right-click `PCAPPipeline.uproject` → **Generate Visual Studio project files**.
2. Open `PCAPPipeline.sln`, set **Development Editor / Win64**, Build. *(Or double-click the
   `.uproject` and let it compile the modules.)*
3. **Restart the editor after a fresh build** — PCAPTool's tool tabs register at module startup.
4. **Build errors?** This project is authored without a local engine; the convention is to paste the
   MSVC error log into **`CurrentErrorCodes`** at the repo root and hand it back for fixes. (That file
   currently holds an old 5.7 error — overwrite it with any new 5.8 output.)

---

## 6. First-run verification

- [ ] Project opens in 5.8 with no missing-plugin errors.
- [ ] `Window ▸ Virtual Production ▸ Mocap Manager` opens *(proves the Performance Capture plugins loaded)*.
- [ ] `Window ▸ Tools ▸ PCAP Tools` shows Call Sheet / Operator Console / HMC Monitor / VCam Operator *(proves PCAPTool loaded)*.
- [ ] `Window ▸ Tools ▸ Databases` shows the five DB tabs.
- [ ] The master DB at `/Game/PCAPTool/Databases/MasterPCAPDatabase` resolves (wired in `DefaultGame.ini`).
- [ ] *(if the Vicon DLL is placed)* Volume Visualizer raw markers stream.

---

## 7. Where the Performance Capture migration stands

- **Done — Phase 0** (this branch / PR #19): engine → 5.8; `PerformanceCaptureCore` +
  `PerformanceCaptureWorkflow` enabled; docs bumped. **Config/docs only — no code changes yet.**
- **Next — Phase 1:** build-smoke on 5.8; fix any PCAPTool 5.7→5.8 API breaks via `CurrentErrorCodes`.
- **Then — Phase 2:** adopt Epic's data model (performers/props/characters, sessions/slates/takes).
  - **To unblock Phase 2**, drop the two engine plugins' `Source/` trees somewhere readable (Google
    Drive, or a gitignored `Reference/` folder): `PerformanceCaptureCore`, `PerformanceCaptureWorkflow`.
- **Full plan:** [`docs/specs/2026-06-28-performance-capture-integration-design.md`](../specs/2026-06-28-performance-capture-integration-design.md).

---

## 8. Gotchas

- **Don't build from `main`** — it's 5.7. Use the 5.8 branch above until PR #19 merges.
- **`Binaries/ Intermediate/ Saved/ DerivedDataCache/`** are gitignored — never copy them between
  machines; let each machine regenerate its own.
- **Line endings / encoding** are enforced by `.gitattributes` (LF; UTF-8 no-BOM for `.uproject` /
  `.uplugin`) — let git handle it, don't bulk-"fix" line endings.
- **`Reference/` and `_vendor/`** are gitignored — engine plugin source staged there for API matching
  won't be committed (by design; we build on top of the engine's copy, we don't vendor it).
- The `.uproject` / `.uplugin` files are JSON — if you hand-edit, keep them valid or the project won't
  load.

---

## 9. Alternative — drop just the PCAPTool plugin into a *different* 5.8 project

PCAPTool is written to drop into any UE5 project:

1. Copy `Plugins/PCAPTool/` (and `Plugins/LiveLinkViconDataStream/` if you need Vicon) into the target
   project's `Plugins/`.
2. In the target `.uproject`, enable: PerformanceCaptureCore, PerformanceCaptureWorkflow,
   LiveLink(+Camera/Lens/ControlRig), Takes, SequencerScripting, IKRig. (`PCAPTool.uplugin` declares
   these as deps, so enabling PCAPTool pulls most in.)
3. Copy `Content/PCAPTool/` for the prebuilt DB assets, and set in `DefaultGame.ini`:
   `[/Script/PCAPTool.PCAPToolSettings]` → `DatabaseAsset=/Game/PCAPTool/Databases/MasterPCAPDatabase`
   (or let PCAPTool auto-create a fresh master DB on first open).
4. Generate project files, build, restart the editor.
