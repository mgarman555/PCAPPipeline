# PCAP Tool Suite — Status & Verify Guide

**Built across this session (data model + Phase 2 + the operator tool suite). Everything is on `main`. Nothing below has been compiled yet — that's the next step on the build host.**

---

## The tabs (Window menu, after a full rebuild + editor restart)

| Tab | What it is | Status |
|---|---|---|
| **Operator Console** | The unified solo surface — navigate shots, see context, run takes | new, first cut |
| **Actor Database** | Talent library — defaults + digital double + headshot | new |
| **Prop Database** | Prop library — with mesh thumbnails | new |
| **Stage Database** | Stages = location + what-to-record checklist | new |
| **Mocap Database** | Read-only browser of the whole DB | verified earlier |
| **HMC Monitor** | HMC device monitor | (parallel session) |

*(Tabs register in `StartupModule()` — they only appear after a **full editor restart**, not Live Coding.)*

## How they fit together
- The **three databases** (Actor / Prop / Stage) are libraries you populate once — each creates DataAssets under `Content/Mocap/_Roster/{Actors,Props,StageConfigs}/`.
- The **Operator Console** reads the `UMocapDatabase` hierarchy (Production→Day→Session→Shot) and the libraries, and drives the **Phase 2 record backend** (`UPCAPTakeRecorderSubsystem`) for RECORD/STOP/Next Take.

## Build-loop watch items (where a 5.7 nit is most likely — paste any red)
- **Asset ops** (all DB tools): `GetAssetsByClass`, `CreatePackage`/`FEditorFileUtils::PromptForCheckoutAndSave`, `ObjectTools::DeleteSingleObject`, `OpenEditorForAsset`.
- **`SObjectPropertyEntryBox`** + the `PropertyEditor` dep (Actor/Prop asset slots).
- **`FAssetThumbnail`/`FAssetThumbnailPool`** (Prop mesh + Actor headshot previews) — in `UnrealEd`.
- **`SComboButton` + `FMenuBuilder`** enum/nav pickers (Stage DB + Console).
- **Console**: `RegisterActiveTimer`, the RECORD/STOP `? :` ternary in the slot, `GEngine->GetEngineSubsystem<UPCAPTakeRecorderSubsystem>()`.
- **Phase 2**: the `/Script/...` reflection class lookup + Take Recorder calls.
- *Self-checked: every DB/recorder method and struct field the Console uses is present in the headers.*

## To verify (quick, per tool)
1. **Actor / Prop / Stage Database**: tab opens → `+ new <id> ↵` creates an asset under `_Roster/` → fill the form, **Save** → re-open shows it. Prop/Actor: assign a mesh/headshot → thumbnail renders.
2. **Operator Console**: tab opens → pick Production/Day/Session → shot list populates with status glyphs (`★`/`✓`/`○`) + take counts → select a shot → context shows talent + takes → **RECORD** is gated on streams being green (calls Phase 2).

## Known first-cut limits (deliberate, for the next pass)
- **Console**: record wiring is a first cut; no ambient health strip yet; *setup* editing (add days/shots, assign talent from the libraries) isn't in the Console yet — that's the next layer.
- **Phase 2**: audio + VCam source arming and per-stream asset-ref resolution are a later reflection pass.
- **Prop/Stage list rows**: text only (no mini-thumbnails in the list yet).
