# HMC Rig Library + Database Snapshot-Presets (Design Spec)

**Date:** 2026-06-16 · **Status:** Design — for Madi's review · **Component:** `Plugins/PCAPTool` (databases + HMC/Capture tool + Call Sheet)

## 1. Goal

Two things, decided together:
1. **HMC becomes a database/library** like Actor/Prop/Stage/VCam — a catalog of named, reusable **physical rigs**. (Today HMC is the outlier: a flat `HMCConfig.json` of live devices, no named library.)
2. **A uniform "snapshot-preset → recall" action on every database** — save the current configuration as a named preset and reload it later.

Built in-engine, matching the existing database pattern (UDataAsset entry + `TSoftObjectPtr` roster in `MocapDatabase` + `SPCAP*DatabasePanel` + Call Sheet "+").

## 2. Decomposition (each its own spec → plan → build)

This spans several subsystems, so it's split; **this spec details Sub-project 1** and outlines 2–3 (each gets its own spec when we reach it):

1. **HMC Rig Library** — the gap; the pilot. *(detailed below)*
2. **Call Sheet "HMCs used today?" flag** — a day-level toggle. *(outline)*
3. **Database snapshot-preset / recall** — uniform across all DBs. *(outline)*

---

## 3. Sub-project 1 — HMC Rig Library  *(build first)*

### Entry: `UHMCRigEntry : public UDataAsset`
One asset per physical rig, saved to `Content/Mocap/_Roster/HMCRigs/[rigName].uasset` (mirrors `UStageConfigAsset` / `UActorRosterEntry`).

| Field | Type | Notes |
|---|---|---|
| `RigName` | `FString` | "Orion" — the displayed name; brand/model lives here |
| `Type` | `ECaptureConfiguration` | the rig's **mount form-factor**, which *is* the capture configuration: **phone head mount / mono head mount / stereo head mount / tripod**. One field (no separate brand). Rides along when the rig is picked and drives the watcher's per-config checklist. |
| `IPAddress` | `FString` | per-rig IP |
| `Thumbnail` | `TSoftObjectPtr<UTexture2D>` | optional, for the database card (like Actor `Headshot`) |
| `Notes` | `FString` | optional |

**Capture-config enum:** extend `ECaptureConfiguration` to the four mount form-factors — **phone head mount / mono head mount / stereo head mount / tripod** (add `PhoneHeadMount`; today's `MonoTripod` is displayed "Tripod"). The rig's `Type` *is* this enum; there is no separate brand field (brand lives in the Name). These are all the types "for now."

### Roster + panel (match the others)
- `MocapDatabase` gains `TArray<TSoftObjectPtr<UHMCRigEntry>> HMCRigs;` (alongside `ActorRoster` / `PropRoster` / `StageConfigs`).
- New **`SPCAPHMCDatabasePanel`** modeled on `SPCAPStageDatabasePanel` — browse / create / name / edit / delete rig entries (cards with name + type + IP + config + thumbnail).
- The Call Sheet's universal **"+"** can create-or-call an HMC rig (consistent with how it handles production/day/stage/actor/prop/vcam).

### Capture / HMC Setup integration (pipeline stays here — no move)
- In HMC Setup, **"Add Device" gains "from library"**: pick a rig from `HMCRigs` → it populates the live device (`FHMCDeviceConfig`) with **Name + IP + CaptureConfig** from the entry. (Manual add-by-IP stays as a fallback.)
- `FHMCDeviceConfig` gains a soft link to its source rig (`FString RigName` or `TSoftObjectPtr<UHMCRigEntry>`), so the live device knows which library rig it came from.
- **Unchanged at runtime:** pipeline (MetaHuman/Faceware), actor (called out per shot from the day's called actors), framing reference, scan-readiness, focus tuning — all stay in the Capture tool exactly as now. CaptureConfig now seeds from the library entry but remains editable in Setup.

### Why this shape
The library is the rig's **durable identity** (what it is + where it is). Per-use, per-day, per-shot state stays in the Capture tool. That keeps the library reusable across productions while the volatile bits live where they're used.

---

## 4. Sub-project 2 — Call Sheet "HMCs used today?" flag  *(outline)*
A simple day-level boolean on `FShootDay` (e.g. `bUsesHMC`) surfaced as a toggle in the Call Sheet. No pipeline/assignment detail in the sheet. Which actors are on HMCs is called out per shot in the Capture tool from the day's called actors. *(Own spec when we build it.)*

## 5. Sub-project 3 — Database snapshot-preset / recall  *(outline)*
A uniform mechanism on every database: **"save the current configuration as a named preset"** + **"recall"** to restore it. Likely a small shared helper + a per-DB hook defining what "current configuration" means (for HMC: the current registered rig set; for Stage: the active stage config; etc.). Pilot the mechanism on one DB, then roll out. *(Own spec when we build it — the design there decides snapshot scope per DB.)*

---

## 6. Data model & code mapping (Sub-project 1)
- **New:** `Public/HMCRigEntry.h` (`UHMCRigEntry` DataAsset).
- **`PCAPToolTypes.h`:** extend `ECaptureConfiguration` with `PhoneHeadMount` (label `MonoTripod` as "Tripod") → phone/mono/stereo head mount + tripod; the rig's `Type` IS this enum.
- **`PCAPToolStatics::GetDefinition`:** add a `PhoneHeadMount` branch (treat as mono head-mount + TrueDepth distance for now; refine on the rig).
- **`MocapDatabase.h`:** add `HMCRigs` roster + any accessor matching the others.
- **New:** `Private/SPCAPHMCDatabasePanel.cpp` + `Public/SPCAPHMCDatabasePanel.h` (pattern: `SPCAPStageDatabasePanel`).
- **`FHMCDeviceConfig`:** add the source-rig link; CaptureConfig seeds from the entry on register.
- **`SHMCSetupPanel`:** "Add from library" picker → populate device from the chosen rig.
- **Call Sheet:** "+" create-or-call an HMC rig.

## 7. Scope / boundaries
- In-engine; matches the established DataAsset/roster/panel pattern.
- **Pipeline + actor + framing/scan/focus stay in the Capture tool** (not the library, not the call sheet).
- Sub-projects 2 and 3 are decomposed out — their own specs/plans.

## 8. Open decisions (for review)
- **Type = capture configuration (one field); brand dropped** (Madi): the rig's `Type` is the mount form-factor (phone/mono/stereo head mount + tripod) = the capture config, so one field, no separate brand (brand lives in the Name). Flag if you want a separate brand field too.
- **Rig link on the live device:** `FString RigName` (loose) vs `TSoftObjectPtr<UHMCRigEntry>` (hard ref, survives rename). Spec leans soft-ptr.
- Sub-project 3's per-DB "what does a snapshot capture" is deferred to its own spec.
