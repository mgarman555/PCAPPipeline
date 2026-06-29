#pragma once

#include "CoreMinimal.h"

// ─────────────────────────────────────────────────────────────────────────────
// Pure C++ port of the WVCAM controller mapping (Fox VFX Lab "vcam" device).
//
// Ground truth: the WVCAM Python (vcontrols/bindings/vcam_default.py, vcam_sony.py +
// controller.py + device_maps.py). The earlier port guessed an invented "vcamv4" device;
// this one matches the REAL device map and the two real layouts (Default + Sony).
//
// Key facts the Python pins down (see docs/specs/2026-06-12-vcam-input-layer-design.md):
//   • The device has two sticks per hand grip (left_left/left_right, right_left/right_right),
//     two absolute thumbwheels (left_gain, right_gain, 0..4095), and 16 buttons.
//   • SHIFT (Default layout) = the left_x button HELD (not a trigger — there is no trigger).
//   • WVCAM Python computes per-frame *speeds*, not positions: it deadbands the raw axis,
//     multiplies by a gain stack, and hands the result to a native module that integrates it.
//     So this layer outputs SPEEDS (units/sec); the subsystem integrates them into Navigate.
//   • Gain stack (shared by translation + rotation):
//       masterGain = (left_gain/4095) * 10.0 * (1/800)
//     Zoom gain stack (note the INVERTED stage-0):
//       zoomGain   = (1 - right_gain/4095) * 10.0 * (1/100)
//   • Movement axes are read start-relative (normalize) with a 100-count deadband.
// ─────────────────────────────────────────────────────────────────────────────

// One controller frame: RAW absolute axis counts + button states, exactly as the WVCAM
// raw-broadcast script emits them for the "vcam" device. Axis names mirror device_maps.py.
struct FVCamControllerInput
{
    // Sticks (raw absolute counts, ~0..4095; centre ~mid-scale). Made start-relative by the layer.
    float LeftLeftX = 0.f,  LeftLeftY = 0.f,  LeftRightX = 0.f,  LeftRightY = 0.f;
    float RightLeftX = 0.f, RightLeftY = 0.f, RightRightX = 0.f, RightRightY = 0.f;
    // Absolute thumbwheels (0..4095) — read absolute (NOT start-relative), used as gain trims.
    float LeftGain = 0.f, RightGain = 0.f;

    // Buttons. left_x = SHIFT (Default). a/b = lower buttons, x/y = upper, plus the d-pad.
    bool LeftX = false,  LeftY = false,  LeftA = false,  LeftB = false;
    bool LeftUp = false, LeftDown = false, LeftLeft = false, LeftRight = false;
    bool RightX = false, RightY = false, RightA = false, RightB = false;
    bool RightUp = false, RightDown = false, RightLeft = false, RightRight = false;
};

// The two real WVCAM layouts. (The old Variation1/InvertedDefault were invented — removed.)
enum class EVCamButtonLayout : uint8
{
    Default = 0,   // vcam_default.py — momentary shift on held left_x
    Sony    = 1,   // vcam_sony.py    — latched 3-state map machine (SONY/STANDARD/SHIFTED)
};

// Sony's latched mapping mode (also used as the "shifted?" readout for the panel HUD).
enum class EVCamMapping : uint8
{
    Standard = 0,
    Shifted  = 1,
    Sony     = 2,
};

// What the layer wants the subsystem to do this frame. Pure output → unit-testable.
// Speeds are already gain-stacked (the WVCAM master gain is applied here); the subsystem
// integrates them over dt into the Navigate offset / focal length.
struct FVCamInputIntents
{
    // Per-frame speeds (WVCAM units/sec).
    FVector TranslationSpeed = FVector::ZeroVector;  // X=U (fwd/back), Y=W (left/right), Z=V (up/down)
    FVector RotationSpeed    = FVector::ZeroVector;   // X=Roll, Y=Yaw, Z=Pitch (deg/sec)
    float   ZoomSpeed        = 0.f;                    // focal-length mm/sec

    // Live gain trims (for the HUD readout). TranslationGain = left_gain/4095;
    // ZoomGainTrim = 1 - right_gain/4095 (the inverted stage-0 actually applied).
    float TranslationGain = 0.f;
    float ZoomGainTrim    = 1.f;

    // Edge actions — true only on the frame the action fires.
    bool bZeroEverything = false;   // right_a (STANDARD): zeroSpace + zeroSetupAndNav
    bool bSavePosition   = false;   // left_b
    bool bGotoPrev       = false;   // left_y release
    bool bGotoCurrent    = false;   // left_y hold-once
    bool bLensNext = false, bLensPrev = false;             // right_y / right_b (STANDARD)
    bool bWorldScaleNext = false, bWorldScalePrev = false;  // right_y / right_b (SHIFTED)
    bool bToggleLocked    = false;  // left_a (Default STANDARD)
    bool bToggleFlightMode = false; // left_a (SHIFTED)
    bool bToggleHold      = false;  // left_a (Sony STANDARD)
    bool bPlaybackToggle  = false;  // left_down (Default)
    bool bScrubBack = false, bScrubFwd = false;             // left_left / left_right release
    bool bResetSonyXY = false;      // Sony right_x

    // State readouts.
    bool         bShifted = false;  // Default: left_x held. Sony: mapping==Shifted.
    EVCamMapping Mapping  = EVCamMapping::Standard;
};

// Tracks press/release/hold-once edges for one button (reproduces base.py bindButton timing).
struct FVCamButtonEdge
{
    bool  bWasDown   = false;
    bool  bHoldFired = false;
    float HeldTime   = 0.f;

    struct FResult { bool bPressed = false; bool bReleasedShort = false; bool bHoldOnce = false; };

    // Advance one frame. HoldDelay matches base.py holdOnceDelay (0.3s; Sony sets 2.0s).
    FResult Update(bool bDown, float Dt, float HoldDelay)
    {
        FResult R;
        if (bDown && !bWasDown) { R.bPressed = true; HeldTime = 0.f; bHoldFired = false; }
        else if (bDown)
        {
            HeldTime += Dt;
            if (!bHoldFired && HeldTime >= HoldDelay) { R.bHoldOnce = true; bHoldFired = true; }
        }
        else if (!bDown && bWasDown)
        {
            // onRelease fires only for a SHORT press (hold-once already consumed a long press).
            if (!bHoldFired) { R.bReleasedShort = true; }
        }
        bWasDown = bDown;
        return R;
    }
};

/**
 * Pure C++ port of the WVCAM button/axis mapping. Feed it a raw frame + dt; it returns the
 * intents the subsystem applies. Holds the small state the scripts hold (shift / Sony map,
 * start-relative axis baselines, button-edge timers, Sony XY accumulator). No engine
 * dependency → fully host-unit-testable.
 */
class PCAPTOOL_API FVCamInputLayer
{
public:
    EVCamButtonLayout Layout = EVCamButtonLayout::Default;

    // Tunables — pinned to the WVCAM scripts (constants table, input-layer design doc).
    static constexpr float AxisCounts        = 4095.f;       // 12-bit absolute range / gain divisor
    static constexpr float MoveDeadband      = 100.f;        // movement-stick deadband (counts)
    static constexpr float TransGainStage1   = 10.f;         // transform.setGain(1, 10.0)
    static constexpr float TransGainStage2   = 1.f / 800.f;  // transform.setGain(2, 1/800)
    static constexpr float ZoomGainStage1    = 10.f;         // zoomSpeed.setGain(1, 10.0)
    static constexpr float ZoomGainStage2    = 1.f / 100.f;  // zoomSpeed.setGain(2, 1/100)
    static constexpr float SonyMetersToCm    = 100.f;        // accumulateSonyXY GAIN_m_TO_cm

    // base.py hold-once delay; Sony overrides to 2.0s (setButtonHoldTime).
    float HoldOnceDelay() const { return Layout == EVCamButtonLayout::Sony ? 2.0f : 0.3f; }

    FVCamInputIntents Process(const FVCamControllerInput& In, float DtSeconds);

    // Sony's integrated joystick offset (left_right stick → cm), exposed for platforming/HUD.
    float GetSonyRawX() const { return SonyRawX; }
    float GetSonyRawY() const { return SonyRawY; }

private:
    bool bInit = false;

    // Start-relative baselines (captured on first frame; getAxisValue(normalize=True) parity).
    FVCamControllerInput Base;

    // Sony latched mapping (init SONY, cycles STANDARD→SHIFTED→SONY on left_x hold-once).
    EVCamMapping Mapping = EVCamMapping::Sony;

    // Sony integrated XY offset (the one place WVCAM integrates an axis, fixed per-tick).
    float SonyRawX = 0.f, SonyRawY = 0.f;

    // Button-edge timers, indexed by EBtn (prefixed to avoid clashing with local result vars).
    enum EBtn { BLX, BLY, BLA, BLB, BLUp, BLDown, BLLeft, BLRight,
                BRX, BRY, BRA, BRB, BRUp, BRDown, BRLeft, BRRight, BtnCount };
    FVCamButtonEdge Edges[BtnCount];

    // Deadbanded, start-relative axis read (mirrors getAxisValue(axis, deadband=100)).
    float Move(float Raw, float BaseVal) const
    {
        const float V = Raw - BaseVal;
        return FMath::Abs(V) < MoveDeadband ? 0.f : V;
    }
    // Absolute thumbwheel 0..1 (normalize=False, /4095).
    static float Gain01(float Raw) { return FMath::Clamp(Raw / AxisCounts, 0.f, 1.f); }

    void ProcessDefault(const FVCamControllerInput& In, float Dt, FVCamInputIntents& Out);
    void ProcessSony(const FVCamControllerInput& In, float Dt, FVCamInputIntents& Out);
};
