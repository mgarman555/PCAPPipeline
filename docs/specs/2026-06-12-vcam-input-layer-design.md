# VCAM Input Layer — Design (from the WVCAM Python scripts)

**Date:** 2026-06-12
**Status:** Design — derived from the authoritative Technoprops sources (Madi provided 2026-06-12). Supersedes the handoff's joystick section where they differ.
**Engine:** UE 5.7.4
**Depends on:** the VCAM C++ core (`docs/superpowers/plans/2026-06-12-vcam-panel-c++-core.md`).
**Sources:** `vcamv4_default.py`, `vcamv4_variation1.py`, `madi_vcam_inverted_default.py`, `Instructions for WVCAM & MotionBuilder.pdf` (ILM Technoprops, Apr 16 2024), and the two controller-mapping photos.

---

> ## ⚠️ CORRECTED 2026-06-29 against the real WVCAM sources
>
> This design was written from script *names that don't exist* in the actual WVCAM
> distribution (`vcamv4_default.py` etc. were guessed). The real Fox VFX Lab WVCAM Python
> (now on Drive: `vcontrols/bindings/vcam_default.py`, `vcam_sony.py`, `controller.py`,
> `device_maps.py`) is the ground truth, and the port was realigned to it. **Where this
> document and the list below disagree, the list wins** (it matches the shipping code +
> the host tests in `Plugins/PCAPTool/HostTests`).
>
> **Real device ("vcam", device_maps.py).** Two sticks per hand grip, not single joysticks:
> `left_left_x/y`, `left_right_x/y`, `right_left_x/y`, `right_right_x/y`; two **absolute
> thumbwheels** `left_gain`, `right_gain` (0..4095); 16 buttons (`left_x/y/a/b` + left d-pad,
> `right_x/y/a/b` + right d-pad). **There is no trigger and no encoder.**
>
> **SHIFT = the `left_x` button held** (momentary, Default layout) — not a trigger.
>
> **Real layouts = Default (`vcam_default.py`) + Sony (`vcam_sony.py`).** The invented
> "Variation 1 / Inverted Default" are gone. Sony is a *latched* 3-state map machine
> (SONY → STANDARD → SHIFTED, cycled by a 2 s hold-once on `left_x`).
>
> **WVCAM computes per-frame *speeds*, not positions** (a native module integrates them).
> The input layer therefore outputs gain-stacked speeds:
> - translation/rotation share `masterGain = (left_gain/4095) · 10 · (1/800)`
> - zoom uses an **inverted** stage-0: `(1 − right_gain/4095) · 10 · (1/100)`
> - movement sticks are read start-relative with a **100-count deadband**.
>
> **Tables:** lens `(18,25,32,40,50,75,100)` mm, focal clamp `[18,100]`; world-scale
> `[1,2,3,5,10]`.
>
> **Wire format (corrected):** the broadcast (`pcap_vcam_raw_broadcast.py`) now sends the 10
> real axes (`left_left_x` … `right_gain`, raw absolute counts) + the 16 real buttons over
> UDP `:7401`; `PCAPVCamSubsystem::OnInputPacket` parses those exact keys.
>
> **Not recoverable from Python (native `vcam` module, not on Drive):** the exact transform
> composition, motion integration clock, and hold/zeroSpace re-solve. `FPCAPVCamProcessor`
> is a faithful reconstruction of these; final feel must be tuned on the rig.
>
> Sections 2–onward below are the original (guessed) model, kept for history.

---

## 1. The data-flow reality (resolves the handoff contradiction)

The three `.py` files run **inside WVCAM** (`import vcam`). Each does two jobs:
1. **Maps** raw tombstone inputs → camera actions, driving **MotionBuilder** via a `controller` module (`TransformController`, `LensController`, …) and `vcam.sendPyString(...)`.
2. **Broadcasts** a *tiny supplementary* feed: `vcam.setOutputNumber(0, zoomGain)` / `setOutputNumber(1, SonyY)` and `vcam.setActivationBitfield(...)` (PDF Network tab: multicast `239.255.40.33:9005`).

**Therefore:** WVCAM's broadcast carries **only** zoom-gain, Sony XY, and the map/activation bits — **not** translation, lens, zero/hold, or button presses (those are consumed internally to drive MoBu). The handoff's "read joystick state from WVCAM's broadcast" is true only for that tiny feed; it is **not** enough for UE to own the mapping.

**Decision (V-IN-D1) — DECIDED 2026-06-12: Option A** (B is the fallback if A doesn't behave on-rig).
- **(A) CHOSEN — WVCAM raw-broadcast script** — a WVCAM-side Python (`Plugins/PCAPTool/WVCAM/pcap_vcam_raw_broadcast.py`) reads *all* raw axes/buttons and sends them over UDP; UE parses + maps. Keeps WVCAM as a silent shim only.
- **(B) Fallback — Direct HID** — if A proves unworkable (e.g. WVCAM can't expose raw button state), reverse-engineer the tombstone USB (USBPcap/Wireshark) and read it directly; WVCAM removed.

**Wire format (A):** JSON over UDP to `127.0.0.1:7401` (host/port configurable on both ends). Fields: `seq` (counter) + the 6 axes/encoders (`left_joy_x/y`, `right_joy_x/y`, `left_enc`, `right_enc`, 0–4095 / raw counts) + the 14 buttons (`left/right_trigger`, the 8 d-pad dirs, `left/right_a/b`, 0/1). The mapping logic below is **identical** regardless of provider — only the provider feeding `FVCamControllerInput` changes.

**Build order (A):** (1) the WVCAM script — testable standalone on the rig; (2) `FVCamInputLayer` pure logic + tests (UE); (3) UDP receiver + `Networking`/`Sockets` Build.cs deps, wired into the subsystem.

---

## 2. Raw controller input model

What the scripts read (`getAxisValue(name, deadband)` / `bindButton(name, …)`):

```
Axes (0–4095, deadband 100):  left_joy_x, left_joy_y, right_joy_x, right_joy_y
Encoders (counts, 16384/rev):  left_enc, right_enc
Triggers (button):             left_trigger, right_trigger
D-pad (button, per side):      left_up/down/left/right, right_up/down/left/right
Face buttons:                  left_a, left_b, right_a, right_b
```

Proposed C++ (pure, provider-agnostic):
```cpp
struct FVCamControllerInput
{
    float LeftJoyX=0, LeftJoyY=0, RightJoyX=0, RightJoyY=0;   // 0..4095
    float LeftEnc=0, RightEnc=0;                              // absolute encoder counts
    bool  LeftTrigger=false, RightTrigger=false;
    bool  LeftUp=false, LeftDown=false, LeftLeft=false, LeftRight=false;
    bool  RightUp=false, RightDown=false, RightLeft=false, RightRight=false;
    bool  LeftA=false, LeftB=false, RightA=false, RightB=false;
};
```
Button semantics the scripts rely on: **onPress** (edge), **onRelease** (edge), **onHold** (held, repeating), **onHoldOnce** (fired once after `holdTime`=2.0s). The input layer tracks prior state to derive edges + hold timing (`activationStickyTime`=0.5s, `mappingRefreshTime`=1.0s).

---

## 3. Constants (exact, from the scripts)

| Constant | Default.py | Variation1.py | Inverse.py |
|---|---|---|---|
| `countsPerRev` | 16384 | 16384 | 16384 |
| joystick deadband | 100 | 100 | 100 |
| axis range | 0–4095 | 0–4095 | 0–4095 |
| translation-gain Δ | `dEnc/16384` | `dEnc/16384` | `dEnc/16384` |
| translation gain clamp | 0.01–1.0 | 0.01–1.0 | 0.01–1.0 |
| zoom-gain clamp | 0.05–1.0 | 0.01–1.0 | 0.01–1.0 |
| `zoomPrecision` | 300 (mm/rev, enc) | 10 (joy) | 10 (joy) |
| `zoomGainPrecision` | — (enc path) | 200 | 200 |
| Sony `GAIN_m_TO_cm` | 100.0 | 100.0 | 100.0 |
| Sony accumulate | `axis*100/4095 * gain` | same | same |
| trans increment (dpad) | n/a (joy) | `500 * incr` | `500 * incr` |
| init zoom gain / trans gain | 1.0 / 0.5 | 1.0 / 0.5 | 1.0 / 0.5 |
| button hold time | 2.0 s | 2.0 s | 2.0 s |

---

## 4. The three layouts (authoritative maps)

`Trigger` toggles STANDARD↔SHIFTED while held. Translation only applies when **not** Lock Position.

### Layout 0 — Default (`vcamv4_default.py`, display "Default") — joystick translation + encoders
- **Shift:** `left_trigger`
- **Translation (both maps):** `tu=left_joy_y`, `tw=left_joy_x`; STANDARD adds `tv=right_joy_y` → `Vector(tu,tv,tw)` as a *rate*.
- **Gain encoder:** `left_enc` → zoom gain (STANDARD) / translation gain (SHIFTED), `dEnc/16384`.
- **Zoom encoder:** `right_enc` → zoom delta `dEnc*300/16384`.
- **STANDARD:** right_up=Record, right_right=Play, right_left=JumpToFirstFrame, right_down=Stop; right_b=Lens+, right_a=Lens−; right_trigger=Hold (momentary); left_down=NewTake, left_up=CopyTake, left_left=ScrubBack, left_right=ScrubFwd; left_a=ZeroEverything, left_b=CreateCone(=Save Position).
- **SHIFTED:** left_left=GotoPrevPos (onHoldOnce=GotoCurrent), left_right=GotoNextPos; right_up=LiveOn, right_down=LiveOff, right_right=SaveFile; right_b=NextWorldScale, right_a=PrevWorldScale; left_a=ResetSonyXY, left_b=DeleteCone; SHIFTED also accumulates Sony XY from `right_joy_x/y`.

### Layout 1 — Variation 1 (`vcamv4_variation1.py`, display "Variation 1") — d-pad translation + joystick zoom *(matches the photos)*
- **Shift:** `left_trigger`
- **Translation (both maps, d-pad, `500*incr`):** right_up=+U, right_down=−U, right_right=+W, right_left=−W; left_up=+V, left_down=−V. (onPress + onHold repeat.)
- **Gain encoder:** SHIFTED only → `left_enc` = translation gain.
- **Zoom-gain joystick:** `left_joy_y` → zoom gain `axis/(4095*200)` (always).
- **Zoom joystick:** STANDARD → `right_joy_y` → zoom delta `axis*10/4095`.
- **STANDARD:** right_b=Lens+, right_a=Lens−; right_trigger=Hold; left_a=ZeroEverything, left_b=CreateCone.
- **SHIFTED:** left_left=GotoPrevPos, left_right=GotoNextPos; right_b=NextWorldScale, right_a=PrevWorldScale; left_a=ResetSonyXY, left_b=DeleteCone; Sony XY from `right_joy_x/y`.

### Layout 2 — Inverted Default (`madi_vcam_inverted_default.py`, display "Variation Inverse") — Variation 1 mirrored L↔R
- **Shift:** `right_trigger`
- **Translation:** left d-pad = U/W, right_up/down = V (mirror of V1).
- **Zoom-gain joystick:** `right_joy_y`; **Zoom (STANDARD):** `left_joy_y`; Sony from `left_joy_x/y`.
- **STANDARD/SHIFTED:** face buttons mirrored (left↔right).
- ⚠️ **Apparent bug in the source:** in `updateButtons` STANDARD, `left_b`/`left_a` are bound twice (lens cycle **and** zero/cone); the later binding wins, so **lens cycling is lost** in this layout and `left_b=ZeroEverything`, `left_a=CreateCone`. **Do not replicate** — implement the sensible mirror (lens on the mirrored buttons) and flag for Madi to confirm intent.

### Action → UE subsystem API mapping
| Script action | UE `UPCAPVCamSubsystem` | Status |
|---|---|---|
| `transform.zeroSpace`+`zeroSetupAndNav` (ZeroEverything) | `ZeroSpace()` | ✅ exists |
| `transform.toggleHold` | `SetHold(bool)` | ✅ |
| `lens.selectNext/Previous` | `CycleFocalLengthUp/Down()` | ✅ |
| `transform.savePosition` (CreateCone) | `SaveCurrentPosition()` | ✅ |
| `transform.deletePosition` (DeleteCone) | `DeleteSavedPosition(idx)` | ✅ |
| `transform.gotoNext/PreviousPosition` | `GotoSavedPosition(idx)` | ✅ |
| gain encoder/joy → trans/zoom gain | `SetTranslationGain/SetZoomGain` | ✅ |
| `tuvwSpeed.setSpeed(rate)` (joy/dpad translation) | **NEW:** `SetNavigateRate(FVector)` → subsystem accumulates `Navigate += rate*dt` | ➕ add |
| `zoomDelta.addDelta` | **NEW:** `AddFocalLengthDelta(float)` | ➕ add |
| `selectNext/PreviousWorldScale` | **NEW:** world-scale preset list + cycle on config | ➕ add |
| Sony XY accumulate / reset | **NEW:** platform offset (defer — see §6) | ⏸ defer |
| Record/Play/Stop/New/CopyTake, Live On/Off, SaveFile | PCAP record controller / out of vcam scope | ⏸ map later |

---

## 5. Processor validation (against the PDF) + refinements

The shipped `FPCAPVCamProcessor` is **confirmed correct** by the PDF "Further Explanation" (p.7-9): chain order, Kill Roll ("won't zero setup/navigate rolls" — our step-3 placement), Hold release ("populates setup… can introduce rolls" — our `Held*Cur⁻¹` + the documented artifact), Flight Mode axis basis, Zero Space, World Scale (1m→5m). No change needed to the locked chain.

**Refinement R1 (rate-based Navigate):** the PDF "Rate | Speed" + the scripts' `tuvwSpeed.setSpeed()` show translation is a **rate**: the joystick/d-pad sets a velocity and `Navigate` accumulates at `rate × dt` each frame. Implement in the **subsystem tick** (it has dt), feeding `Config.Navigate.Translation += FlightBasis(rate) * dt`. Flight Mode chooses the basis (camera vs world) — this is where step-12 flight mode becomes live. The processor stays pure (applies Navigate as-is); the *accumulation* is the subsystem's job. Keeps the existing processor unit tests valid.

**Refinement R2 (Setup/Navigate Rate fields):** WVCAM exposes a per-axis Rate|Speed on both Setup and Navigate (auto-drift). Low priority; add to config if Madi wants auto-moves.

---

## 6. Deferred (with reason)

- **Platforming (Parent/Platform)** — a real WVCAM feature (PDF p.5/8): the camera can be parented to a moving platform; "Sony XY" / "Platform Transformation" feeds platform offsets. Not in our data model. Defer until needed; would add a platform-parent ref + offset to the config.
- **Transport / Live / SaveFile** actions — MoBu-specific; in PCAP these belong to the record controller / operator console, not the vcam. Map later if the Default layout's transport row is wanted on-rig.

---

## 7. Recommended architecture

```
IVCamInputProvider (interface)  ──raw──▶  FVCamInputLayer (pure logic)  ──intents──▶  UPCAPVCamSubsystem API
   ├─ FWvcamBroadcastProvider (UDP, path A)                 (3 layouts, gains,
   └─ FHidProvider (path B, future)                          state machine, Sony XY)
```
- `FVCamInputLayer` is **pure + unit-testable** (gain math, shift state machine, edge/hold detection, per-layout button→action table). It calls the subsystem API (or returns an intent list the subsystem applies).
- The **provider** is the only data-source-specific piece (V-IN-D1). Until chosen, the layer is driven by tests / the panel.

---

## 8. Decisions for Madi
- **V-IN-D1:** input data source — (A) new WVCAM raw-broadcast script, or (B) wait for HID. (Mapping logic is built either way.)
- **V-IN-D2:** the inverted layout's apparent double-binding bug — confirm intended mirror (lens cycling restored?).
- **V-IN-D3:** are the Default layout's transport controls (Record/Play/Stop/Take) wanted in the UE vcam, or handled by the Operator Console?
- **V-IN-D4:** world-scale presets — seed list (e.g. 1, 2, 5, 10)?

---

## 9. Risks
- The `vcontrols.controller` module (TransformController/LensController internals) was **not** provided — behaviors are inferred from the PDF + call sites. The live smoke test against the real rig is the final validation.
- Encoder values are absolute counts; the layer must track `prev` per encoder to derive deltas (as the scripts do) and handle wrap.
