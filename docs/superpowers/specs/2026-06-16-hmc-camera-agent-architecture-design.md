# HMC Camera Agent — Per-Camera, Doc-Grounded Checking (Design Spec)

**Date:** 2026-06-16 · **Status:** Design — for Madi's review · **Component:** `Plugins/PCAPTool` (HMC Capture Monitor)
**Builds on:** the doc-grounded catalog in `2026-06-16-hmc-pipeline-watcher-definition-design.md` (the "curriculum") and the watcher core/UI already on `main`.

---

## 1. Plain summary

Put a **dedicated inspector (agent) on each camera**. Each agent runs a **checklist** — focus, exposure, lighting, framing, steadiness, and (on a stereo rig) the calibration board — and tracks + flags problems on *that* feed. The checklist is set by **what's being shot (pipeline = MetaHuman) and how (configuration = stereo headrig / mono / tripod)**, exactly as the Unreal MetaHuman docs require. A small **dispatcher (orchestrator)** hands each camera its agent and ticks the right boxes for that pipeline × configuration.

It runs **entirely inside Unreal** (no external/cloud calls), and it is **behavior-preserving**: it reorganizes the detection we already built — it does not change what's detected or add new detection.

## 2. Taught strictly from the documentation

This is a hard requirement: **every checklist item's criteria come verbatim from the Unreal MetaHuman docs**, not invented. The authoritative "curriculum" is the catalog spec (`2026-06-16-hmc-pipeline-watcher-definition-design.md`), which carries the exact quotes + image sources. In code, each skill:
- encodes its trip-points as **doc-derived constants** with the **doc quote + page** in a comment above them, and
- is traceable to a documentation source (so it's auditable that the agent follows the docs).
Thresholds remain **tunable on the rig** (the docs give rules, not always exact numbers) — but the *rule* each skill enforces is the documented one.

## 3. Architecture

- **Skill** (`IHMCSkill`) — one self-contained unit that tracks + flags **one** element. `Evaluate(const FHMCSkillContext&) → FHMCSkillResult`. May hold its own per-camera state (e.g. Stability keeps the centroid history + bump latch). Reports whether it is **active** for the current context (so an untuned/not-applicable check greys rather than reads a false green).
  Skills: `FocusSkill`, `ExposureSkill`, `LightingSkill`, `FramingSkill`, `StabilitySkill`, `BoardSkill` (+ future `LandmarkSkill`).
- **Context** (`FHMCSkillContext`) — everything a skill reads for one frame: the `FHMCImageMetrics`, the resolved `FPipelineCheckProfile`, the camera's `FHMCFramingRef`, device status (fps / dropped / exposure), timing (now, dt), camera index.
- **Result** (`FHMCSkillResult`) — the `EHMCIssueFlag` bit(s) it raises, severity, a reason string ("lit from below" / "too high" / "board over-rotated"), and `bActive`.
- **Agent** (`FHMCCameraAgent`) — **one per camera**. Owns its composed skill list + their state. `Tick(context)` runs each skill, OR's the flags, collects reasons, and applies the hold/hysteresis. Produces that camera's aggregated issue flags + status text.
- **Orchestrator** (`FHMCAgentOrchestrator`, owned by the subsystem) — spins **one agent per camera per device**, and **composes each agent's skills from `GetDefinition(pipeline, config)`**. Reconfigures an agent when its pipeline or configuration changes.

Each unit has one job and a clear interface: a skill knows only how to judge its element from a context; the agent knows only how to run a set of skills and aggregate; the orchestrator knows only how to compose and route. Skills can be reasoned about and tuned in isolation.

## 4. The skills — what each tracks + its documented rule

| Skill | Tracks / flags | Documented rule (source) | Signal it uses |
|---|---|---|---|
| **Focus** | sharp vs soft/defocused | "focused on the **nasolabial area**" (Using a [Stereo] Head Mount) | var-of-Laplacian over the nasolabial band; active only once `FocusMin` tuned |
| **Exposure** | over / under | "a **slight underexposure** is usually preferable to an overexposure" (Using a Head Mount) | blown-pixel fraction + mean luma |
| **Lighting** | even vs below / side / back / shadow | "**uniform, shadow free, and predominantly frontal**"; the tripod page's named bad-light cases | region luma + where the brightness sits |
| **Framing** | centered / too high / low / off-axis / too close / far | "**centered… face parallel**"; "**~30° cutoff**"; per-config size bands | subject centroid + size vs frame / reference |
| **Stability** | bump / slip | "**stable relative to the face… well-fitted mount**"; "camera fixed throughout each take" | centroid jump + variance over time |
| **Board** *(stereo)* | board present / framed / occluded | Calibration Takes good/bad board cases | edge energy + bright-region position/size |
| **Landmark** *(future)* | lip-seal / inner eyelid / true >30° | "lip seal and the inside of the upper eyelid visible as often as possible" | needs CV — inactive until then |

(Frame-rate / dropped-frames remain a device-level check feeding the same flags.)

## 5. Composition per pipeline × configuration

| Pipeline · Configuration | Skills the orchestrator gives the agent |
|---|---|
| MetaHuman · any HMC | Focus, Exposure, Lighting, Framing, Stability |
| MetaHuman · **Stereo head-mount** | + **Board** |
| MetaHuman · **Mono tripod** | Framing (looser bands) + Background *(future Distance/Background skills)* |
| Faceware · any | all skills present but **inactive** until its docs land |
| *(future)* Landmark | present but **inactive** until CV — keeps the framework complete per the docs |

Composition is driven by `FPipelineCheckProfile` (already config-aware via `GetDefinition`); the orchestrator includes a skill when its check is enabled, and keeps disabled ones present-but-inactive so the UI greys them honestly.

## 6. Integration (reuse + plug-in)

- The heavy pixel pass (`AnalyzeFrameBGRA`) still runs **once per frame**; skills only **consume** its metrics → real-time and cheap.
- `OnVideoFrameResponse` hands the metrics to the **orchestrator** instead of calling `MapMetricsToAutoFlags` + the inline stability block directly. That logic **moves into the skills** (Framing/Exposure/Lighting/Focus skills ← `MapMetricsToAutoFlags`; Stability skill ← the inline bump/variance block; Board skill ← `ClassifyBoardFrame`).
- `GetEffectiveIssueFlags` **combines the device-level hardware flags** (from `EvaluateCameraIssues` — streaming / battery / storage / cpu / temp / dropped / fps) **with the agent's image-check flags**, so the existing UI — feed borders, per-camera check boxes, status lines, framing/lighting hints, calibration section — is **unchanged**. Same flags, same reasons, the image checks now produced by modular skills.

## 7. Scope / boundaries

- **In-engine, real-time, no new deps.** Behavior-preserving (same detection, same flags, same UI).
- **Not** adding new detection capability — this is the agent/skills/orchestrator *re-architecture*. The landmark/CV skills stay defined-but-inactive (they'd need the ML/CV deps that are out of scope).
- Hysteresis/hold behavior preserved (moves into the agent).

## 8. Implementation phases (for the plan)

1. **Interfaces** — `IHMCSkill`, `FHMCSkillContext`, `FHMCSkillResult` (+ a base for stateful skills).
2. **Skills** — wrap the existing detection as the six skills, each citing its doc rule; move `MapMetricsToAutoFlags` / the inline stability block / `ClassifyBoardFrame` into them.
3. **Agent** — compose + tick + aggregate + hysteresis.
4. **Orchestrator** — one agent per camera; compose from `GetDefinition`; reconfigure on pipeline/config change.
5. **Wire-in** — subsystem `OnVideoFrameResponse` → orchestrator; `GetEffectiveIssueFlags` → agent; verify UI parity.
6. *(optional)* surface the doc rules in the Setup reference rubric.

Verification: brace-check + subagent compile-review + Madi's Windows build; the bar is **behavior parity** with the current monitor (same green/red + reasons), now produced by per-camera agents.

## 9. Sources
The documented criteria live in `2026-06-16-hmc-pipeline-watcher-definition-design.md` (with quotes + Epic image IDs): Using a Head Mount, Using a Stereo Head Mount, Using a Tripod, Realtime Animation Guidelines, Capture Device Requirements, Performer Requirements, Expressions for Likeness Calibration, Calibration Takes.
