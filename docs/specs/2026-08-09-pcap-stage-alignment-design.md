# PCAP stage alignment — `UStageConfigAsset` ▸ Mocap Manager stage

**2026-08-09 · design**

Follow-up 3 of the [2026-06-29 UE 5.8 Mocap Manager integration](2026-06-29-ue58-mocap-manager-integration-design.md) — "Stage alignment. Reconcile `UStageConfigAsset` with the Mocap Manager Stage tab / `BP_DemoStage`" — which that spec left at **TBD**. `UPCAPStageBridge` landed against this design; the Stage Database wiring landed after it.

`PCAPStageBridge.h` and `PCAPStageBridge.cpp` both point here, and `PCAPStageBridge.cpp` defers to the [Verify checklist](#verify-checklist-windows-first-run) below for the reflection names it could not settle on macOS. That checklist is the acceptance gate for this feature.

---

## The problem

An operator configuring a shoot day configures the same stage twice: once as a PCAPTool `UStageConfigAsset` (what hardware this stage runs) and once in the Mocap Manager's Stage tab (what the engine records into). Nothing connects the two, so they drift, and the drift is only discovered when a take lands in the wrong folder or rides the wrong clock.

## The two models

They overlap far less than their names suggest.

| | PCAPTool `UStageConfigAsset` | Mocap Manager |
|---|---|---|
| **What it is** | a hardware description of a physical stage | `UPCapSessionTemplate`, a naming / foldering / recording contract, plus `APerformanceCaptureStageRoot` placed in a level |
| **Owns** | body / face / audio / vcam systems, Live Link preset, Vicon DataStream host, volume calibration, retarget chain, notes | production + session names, tokenised folder paths, take naming, recording clock source, the edit lock |
| **Shared** | timecode source | recording clock source |

One field genuinely overlaps. That is the whole apply surface, and it is deliberate: pushing PCAPTool's naming opinions onto a template that has none would be inventing data, and pulling Epic's foldering into a hardware description would be noise.

**Decision: `UStageConfigAsset` stays the source of truth for hardware. The template stays the source of truth for naming and foldering. Neither side is migrated into the other.** This is the one place the [2026-06-29 database-adoption](2026-06-29-pcap-database-adoption-design.md) direction ("PCAPTool's databases become viewers/editors over Epic's data") does *not* apply wholesale — Epic has no field for most of what a stage config carries, so the Stage Database stays a PCAPTool-native library with a pairing to Epic on the side, unlike the Actor and Prop databases which now read `UPCAPMocapData`.

## Pairing

By the Epic asset's `AssetUID` (`UPCapDataAsset`'s durable key), stored on `UStageConfigAsset::PCapStageUID`, with `PCapStageAsset` as a soft-ref cache that is never the key. Identical mechanism to `UPCAPPerformerExtension::PCapPerformerUID` and `UPCAPPropExtension::PCapPropUID`, and for the same reason: the pairing has to survive the Epic asset being renamed, moved, or re-pathed mid-production.

Resolution order (`UPCAPStageBridge::ResolvePairedStage`):

1. the cached soft ref, **only while it still reads back the paired UID**;
2. otherwise a UID search across every session template in the project, which re-finds a moved asset;
3. otherwise null — "paired to a stage that is not in this project", which is a different answer from "not paired" and is displayed differently.

Pairing mints an `AssetUID` on the Epic asset if it has none, and saves both packages.

## Apply

`ApplyStageConfigToSession` pushes `TimecodeSource` → `RecordingClockSource` and nothing else.

| PCAPTool `ETimecodeSource` | `EUpdateClockSource` | why |
|---|---|---|
| `Hardware` | `Timecode` | an external signal the recorder rides — the template's own default |
| `Software` | `Platform` | generated in-engine, so the machine clock; keeps the two PCAPTool values distinguishable rather than collapsing them |
| `Clapper` | *(nothing written)* | manual sync recovered in post. There is no running timecode; `EUpdateClockSource` has no enumerator for "no timecode, sync later", and asserting `Tick` or `Platform` would claim a clock PCAPTool does not know |

Resolved by **enumerator name** through the property's own `UEnum`, never by ordinal — two unrelated enums sharing an index would corrupt the value silently.

### The edit lock is a hard precondition

`UPCapSessionTemplate.bIsEditable` goes false once a session has been created from the template, freezing its tokenised strings into that session's serialized data. Writing a locked template retroactively re-points an in-flight session's output folders and take names.

**Apply refuses a locked template outright, and an *unreadable* lock is treated as locked.** Failing closed is the whole point: "we could not see the lock" and "the lock is open" are different, and only one of them is safe to act on.

## Divergence

`CompareStageConfigToSession` answers "what will bite me tomorrow", and is meant to be read **before shoot day**, not after a take goes wrong. Empty means the two agree — it must never mean "could not check", which is why `FPCAPStageDivergence::Kind` separates a real `Mismatch` from `NotPaired` / `Unreadable` / `Unmapped` / `Locked`, and why the read-out struct carries `bRecordingClockSourceRead` / `bIsEditableRead` alongside the values.

The lock is reported as a divergence row even though it is not a value disagreement: a locked template will not take an Apply, so whatever it says now is what the session will run with.

## UI

The Stage Database's detail card carries a **Mocap Manager** section (`SPCAPStageDatabasePanel::BuildDetailFor`), following the shape `SPCAPPropDatabasePanel` uses for `UPCAPMocapData::IsWorkflowAvailable()` — when the Workflow plugin is absent the section says so rather than vanishing.

- **Paired stage** — a combo listing every session template in the project, plus "(not paired)" to unpair. This is the only way `PCapStageUID` is set; hand-typing a GUID into the raw DataAsset details panel is not a workflow anyone will follow on a shoot day.
- **Lock line** — why an Apply would be refused, shown before the operator clicks.
- **Apply / Compare** — disabled when there is nothing to apply to.
- **Divergence report** — populated on card open for a paired stage, so the pre-shoot read costs no clicks. Unpaired cards stay quiet; the pairing row already says they are unpaired.

Level-side `APerformanceCaptureStageRoot` actors are **read-only** here (`FindPlacedStageRoots`). The base class is `Abstract`, so every real stage in a level is a Blueprint subclass (`BP_DemoStage` and duplicates). The bridge never spawns one.

## Reflection, not headers

The Workflow plugin's stage classes carry no API macro and live in a private module folder, so every Epic-side read and write is by `/Script` path and `FProperty` name — the pattern already set by `UPCAPMocapData` and `UPCAPTakeRecorderSubsystem`. Every lookup logs once and no-ops rather than assuming. This also keeps `MovieScene` (a private dependency) out of a public header's reach.

The cost is that a name that moved between engine versions degrades to a logged no-op instead of a build break — which is the right trade for a module whose acceptance gate is a Windows build the author cannot run.

---

## Verify checklist (Windows, first run)

`PCAPStageBridge.cpp` marks each constant with what the 5.8 header read did and did not settle. These are the open ones. Work down the list with the editor open and the output log filtered to `[PCAP]`.

| # | What | Marked | How to verify | If wrong |
|---|---|---|---|---|
| 1 | `GStageRootClassPaths` — which module exports `APerformanceCaptureStageRoot`. The header is `PCapStageRoot.h` but the class inside is `APerformanceCaptureStageRoot`; the two do not match and the header alone did not settle the module. | **module UNVERIFIED**, two candidates tried | Open a level with a `BP_DemoStage` in it and call `FindPlacedStageRoots`. On success the log carries `[PCAP] Mocap Manager stage root resolved at '<path>'` once. On failure it carries the "not found at either candidate" warning. | Replace both candidates with the resolved `/Script` path. Also update the module list in `PCAPTool.Build.cs` if it is a third module — see the `[plugin-workflow-module]` row in [ci-setup.md](../ci-setup.md). |
| 2 | `GUpdateAllFieldsFunctionName` parameter list. Verified as a `UFUNCTION(BlueprintCallable)`; its signature was not. | **parameters UNVERIFIED**, call guarded on `NumParms == 0` | Apply to a paired, unlocked template. A log line `…UpdateAllFields() takes parameters — not called` means the guard tripped. | Build the parameter struct and pass it to `ProcessEvent`, or drop the call if the 5.8 asset no longer needs it after a non-tokenised write. |
| 3 | `GPCapSessionTemplateClassPath`. Verified from the header, not from a running editor. | VERIFIED (header) | Open the Stage Database. If the Mocap Manager section reads "Performance Capture Workflow plugin not available", `IsStageModelAvailable()` returned false. | Correct the `/Script` path. |
| 4 | `AssetUID` reachable by `FindFProperty` despite being private behind `GetAssetUID()`. | VERIFIED (reflection ignores access specifiers) | Pair a stage config to a template and confirm the log names a non-zero GUID. | If it reads all-zeros the property name moved; find it in `PCapDataAsset.h`. |
| 5 | `bIsEditable`, `ProductionName`, `SessionName`, `RecordingClockSource` property names. | VERIFIED (header) | Compare a paired stage. Any row whose Mocap Manager column reads `('<name>' did not resolve)` names the property that moved. | Correct the constant. Never widen the fail-closed lock behaviour to compensate. |
| 6 | `EUpdateClockSource` enumerator names `Timecode` / `Platform`. | VERIFIED (MovieScene) | Apply with timecode source `Hardware`, then again with `Software`; check the template's Recording Clock Source in the details panel. A log line `'EUpdateClockSource' has no enumerator named '<x>'` means a name moved. | Correct the mapping in `ClockSourceNameFor`. Do **not** fall back to an ordinal. |
| 7 | `/Script/LiveLink.LiveLinkPreset` + its `ApplyToClient()` signature, used by the Stage Database's "Apply now" beside the Live Link preset field. | UNVERIFIED — resolved by reflection, same discipline | Set a stage's preset to a real `ULiveLinkPreset` asset path and click **Apply now**. The toast names the failure when a lookup misses. | Correct the `/Script` path or the function name in `SPCAPStageDatabasePanel.cpp`. |

## Still open

- **`bAutoConnectDataStream` is declared and never read.** `APCAPVolumeVisualizer::EnsureSource` still gates on its own `bUseRawMarkers`, so the per-stage "this volume auto-connects" intent from the [volume-visualizer design](2026-06-15-volume-visualizer-design.md) is unhonoured. The read side belongs in `PCAPVolumeVisualizer.cpp`.
- **`StageReferenceMesh` accepts any `UObject` but only a `UStaticMesh` is drawn.** `RefreshFromStageConfig` casts and falls through with no log line; the Stage Database warns on the card, but the actor should log too.
- **No stage-root ▸ stage-config pairing.** Only the session template is paired. A placed `BP_DemoStage` and a `UStageConfigAsset` still have no link, so the volume calibration (`VizUnitScale` / `VizOriginOffset` / `VizYaw`) cannot be reconciled against the Epic stage's own transform.
