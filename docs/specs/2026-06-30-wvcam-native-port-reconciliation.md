# WVCAM native port — reconciliation against the real 4.26 sources

**Date:** 2026-06-30
**Status:** Reference — reconciles PCAPTool's self-contained VCam against the extracted 4.26
Fox VFX Lab plugin (`VcamSequencer` / `AVCam` / `Math3d` / `VCamIO`) and the WVCAM Python.
**Decision context:** Option A (fold WVCAM's job into PCAPTool, self-contained on 5.8), not a
verbatim port of the 4.26 plugin.

---

## 0. The load-bearing finding

The 4.26 plugin is a **UDP receiver**: WVCAM did the controller reading *and* the camera
math and streamed a **finished transform** (`pr` block) to the plugin. Confirmed by reading
`AVCam.cpp` end-to-end: it contains **no hold / flightMode / lockPosition / lockRotation /
killRoll / lockRoll** — those live in WVCAM's compiled native module, which is **not on Drive**.

**Consequence:** `FPCAPVCamProcessor`'s hold/flight/lock/roll + speed-integration is a
reconstruction from the WVCAM **Python** (speed formulas) + the `.vcs` defaults + the user
guide. There is nothing in the 4.26 plugin to make it more exact. It is as-pinned-as-files
allow; its final *feel* is confirmed by the on-rig tuning pass, not by more source.

What `AVCam` *does* contain (and is worth pinning against): coordinate conversion, the
zero/parent-compensator math, parenting/platforming, spline-rider, lens apply, recording.

---

## 1. Axis / coordinate convention (`AVCam` §B, `Math3d`)

- Internally radians; `FRotator` in degrees. Quat multiply is Shoemake `t = other × this`.
- Euler↔quat via Ken Shoemake (Graphics Gems IV) `Eul_ToQuat`/`Eul_FromQuat`. **Quirk:** the
  `EulOrdXYZs`/`EulOrdXZYs` codes permute in/out variables — they are NOT naive intrinsic/
  extrinsic orders; replicate the mapping exactly if reproducing.
- MoBu → Unreal translation: **`Tu.X = Tb[0]; Tu.Y = Tb[2]; Tu.Z = Tb[1]`** (swap Y/Z).
- Rotation: `AAEuler(Rb, EulOrdXYZs) → quat → AAEulerFromQuaternion(EulOrdXZYs)`, then
  `Roll = e.x, Pitch = e.y, Yaw = -e.z` (**Yaw negated**). Reverse swaps the order codes.

**For our pipeline:** the camera pose comes from **Vicon Live Link**, not WVCAM's MoBu stream,
so this specific remap is *not* applied verbatim. It is the reference for calibrating
`UPCAPVCamConfig::AlignRigidBody` (the axis-correction offset) on the rig — if the Vicon
convention differs, `AlignRigidBody` is where it's corrected.

## 2. Zero (`AVCam` zeroCompensator)

- Final pose each frame: `final = vcamTransform · (ParentCompensator · parentTransform)`
  (Unreal child × parent).
- `zeroCompensator()` accumulates a **talkback delta** shipped back to WVCAM on the next `tb`
  packet, then cleared — an anti-double-zero "dance" that exists **only because WVCAM has no
  ack**. We own the whole pipeline, so there is no talkback: `FPCAPVCamProcessor::ZeroSpace`
  (`Setup = ApplyAlign(Raw)⁻¹`, so `Setup · Cur = Identity`) is the correct self-contained
  equivalent. **No change needed.**

## 3. Parenting / platforming (`AVCam` getParentTransform) — for the future feature

- Parent = parent/socket/spline-rider transform, with per-axis gates `UseParentTx/Ty/Tz`
  (zero individual translation components), optional `UseParentRotation`, **scale forced to 1**
  (`SetUsingAbsoluteScale(true)`).
- On a parent-option change, **re-solve the compensator so the camera doesn't jump**:
  - keep-T&R: `Cnew = Cold · EPold · EPnew⁻¹`
  - keep-T-only: `Tc_new = Rp_new⁻¹ · (oT_old − Tp_new) − Tvc`
- No Sony raw-X/Y and no separate `Tabc/Tuvw` platform offset in `AVCam` — "platform"
  collapses onto the parent path.

## 4. Spline-rider (`AVCam`) — for the future feature

- Parent can be an `ASplineTrack` with a `FreeRiderComponent` (operator distance) +
  `KeyableRiderComponent` (keyed). `rideKeyableRider` snaps free→keyable distance and
  re-parents zero-preservingly. Drive via `setSplineDistance/Speed`, `MoveRiderToPoint`,
  keyframe add/remove.

## 5. Lens (`AVCam`)

- `MINIMUM_FOCAL_LENGTH = 0.1` (min-clamp only — **no max**; PCAPTool correctly adds a max).
- `DEFAULT_FOCAL_LENGTH = 35`, focus default `1e5`, aperture default `2.8`,
  **filmback 36.0 × 20.25** (set at spawn). Writes `CurrentFocalLength /
  FocusSettings.ManualFocusDistance / CurrentAperture`.
- PCAPTool's focal **table** (18,25,32,40,50,75,100) + clamp [18,100] already matches the
  WVCAM operator-facing lens set, which is the truth for cycling. **TODO (minor):** set the
  CineCamera **filmback default to 36 × 20.25** on `APCAPVCamActor` to match.

## 6. Transport command map (`VCamIO` `cm` → remoteCmd) — spec for controller transport

All guarded `if (isRecording) break;`; sequence via `FindLevelSequenceAtEditor` (UE5-shaped,
`UAssetEditorSubsystem` + `ILevelSequenceEditorToolkit::GetSequencer()`).

| Command | Sequencer action |
|---|---|
| ZEROROOT | `zeroCompensator()` (our `ZeroSpace`) |
| PLAY(rate) | `abs(rate) < SMALL` → Pause; else `SetPlaybackSpeed(rate)` + `OnPlay` |
| TOGGLEPLAYBACK | toggle play/pause |
| SCRUBFRAME(n) | `SetPlaybackStatus(Stepping)` + `SetLocalTime` by `n · GetDisplayRate().AsInterval()` |
| JUMPTOFIRST / LASTFRAME | `SetLocalTime` to playback range lower / upper |
| SETFRAMEIN / OUT | `SetPlaybackRange` to current frame |
| spline set | CREATE/REMOVE/ADD point, RIDEKEYABLE, ADD/REMOVE keyframe, SELECT/JUMPTOSPLINEPOINT, ENTER/EXIT select, SET SPLINE DISTANCE/SPEED |
| parent | SET/CLR PARENT/PLATFORM ACTIVE + PROPAGATER; KEYPARENT → keyCameraToParent |
| STOP / RECORD / SAVEFILE | **no UE case** (MoBu/Sony only; record-arm is the panel button) |

Wire: floats big-endian (`bytesToInt` MSB-first); magic `VCMB` in / `MBVC` out. In our design
these actions are driven by the input layer's intents (`bPlaybackToggle`, `bScrubFwd/Back`,
`bGotoPrev/Current`, etc.) rather than a `cm` packet, but the **actions above are the spec**.

## 7. Units / scale

Centimeters, used directly — **no cm↔m conversion, no world-scale/`Tgain` multiply on
translation in the plugin** (the gain stack `(left_gain/4095)·10·(1/800)` and world-scale are
applied upstream in WVCAM). Scale explicitly killed. This confirms PCAPTool's placement: the
gain + world-scale live in `FVCamInputLayer`, and `FPCAPVCamProcessor` works in cm.

---

## 8. Reconciliation actions

1. **Processor** — hold/flight/lock/roll + integration: keep as-is (no AVCam equivalent);
   validate feel on the rig. `AlignRigidBody` is the axis-calibration knob (see §1).
2. **Lens** — set `APCAPVCamActor` filmback default to **36 × 20.25** (§5). *(minor)*
3. **Take smoothing** — done (Butterworth + slerp module); MovieScene glue next.
4. **Controller → Sequencer transport** — build per §6.
5. **Platforming + spline-rider** — build per §3–4 (later phase).
6. **On-rig** — confirm the §1 axis convention against the live Vicon stream; tune feel.
