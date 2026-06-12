#pragma once

#include "CoreMinimal.h"

// ── Raw controller state (one frame), parsed from the WVCAM raw-broadcast packet ─────
// Joystick axes are signed and centred near 0 (WVCAM getAxisValue with deadband=0);
// encoders are absolute, monotonic counts. The exact full-scale / centre is confirmed
// from a rig capture — see AxisMax/Deadband on FVCamInputLayer.
struct FVCamControllerInput
{
    float LeftJoyX = 0.f, LeftJoyY = 0.f, RightJoyX = 0.f, RightJoyY = 0.f;
    float LeftEnc  = 0.f, RightEnc  = 0.f;
    bool LeftTrigger = false,  RightTrigger = false;
    bool LeftUp = false,  LeftDown = false,  LeftLeft = false,  LeftRight = false;
    bool RightUp = false, RightDown = false, RightLeft = false, RightRight = false;
    bool LeftA = false, LeftB = false, RightA = false, RightB = false;
};

enum class EVCamButtonLayout : uint8
{
    Default = 0,          // joystick translation + encoders (vcamv4_default.py)
    Variation1 = 1,       // d-pad translation + joystick zoom (vcamv4_variation1.py)
    InvertedDefault = 2,  // Variation 1 mirrored L<->R (madi_vcam_inverted_default.py)
};

// What the layer wants the subsystem to do this frame. Pure output → unit-testable.
struct FVCamInputIntents
{
    FVector NavigateRate = FVector::ZeroVector;   // X=U, Y=W, Z=V (units/sec) — axis map TUNE ON RIG
    float   ZoomDelta = 0.f;                       // focal-length mm this frame
    bool    bHold = false;                         // momentary hold (trigger held)

    bool  bSetTranslationGain = false; float TranslationGain = 0.5f;
    bool  bSetZoomGain = false;        float ZoomGain = 1.0f;

    // Edge actions — true only on the frame the button goes down (or up, where noted).
    bool bZeroEverything = false;
    bool bSavePosition = false, bDeletePosition = false, bGotoPrev = false, bGotoNext = false;
    bool bLensNext = false, bLensPrev = false;
    bool bWorldScaleNext = false, bWorldScalePrev = false;
    bool bResetSonyXY = false;

    bool bShifted = false;   // current map (for the HUD / panel mapping readout)
};

/**
 * Pure C++ port of the WVCAM button-mapping logic (the three vcamv4 scripts).
 * Feed it a raw frame; it returns the intents the subsystem applies. Holds the small
 * state the scripts hold (shift, gains, encoder baselines, Sony XY, hold). No engine
 * dependency → fully unit-testable. The provider (WVCAM UDP broadcast, or future HID)
 * fills FVCamControllerInput; this layer is identical regardless of source.
 */
class PCAPTOOL_API FVCamInputLayer
{
public:
    EVCamButtonLayout Layout = EVCamButtonLayout::Default;

    // Tunables — confirm from a rig capture of the raw-broadcast packet.
    float AxisMax = 2048.f;          // joystick full-scale magnitude
    float Deadband = 100.f;          // joystick deadband (counts), matches the scripts
    float CountsPerRev = 16384.f;    // encoder counts/rev
    float TranslationRateScale = 10.f / 800.f;  // script gain stages 10 * 1/800
    float DpadStep = 500.f;          // per-axis d-pad nudge magnitude (script: 500*incr)
    float ZoomPrecisionDefault = 300.f;  // mm/rev (encoder zoom, Default layout)
    float ZoomPrecisionJoy = 10.f;       // joystick zoom precision (Variation1/Inverted)
    float ZoomGainPrecision = 200.f;     // joystick zoom-gain precision (Variation1/Inverted)

    FVCamInputIntents Process(const FVCamControllerInput& In, float DtSeconds);

private:
    bool  bInit = false;
    bool  bShifted = false;
    bool  bHold = false;
    FVCamControllerInput Prev;
    float PrevLeftEnc = 0.f, PrevRightEnc = 0.f;
    float TransGain = 0.5f, ZoomGain = 1.0f;
    float SonyRawX = 0.f, SonyRawY = 0.f;

    static bool Pressed(bool bNow, bool bWas)  { return bNow && !bWas; }
    static bool Released(bool bNow, bool bWas) { return !bNow && bWas; }
    float DB(float V) const { return FMath::Abs(V) < Deadband ? 0.f : V; }

    // Shared helper
    void AccumulateSony(float JoyX, float JoyY);

    void ProcessDefault(const FVCamControllerInput& In, float Dt, FVCamInputIntents& Out);
    void ProcessVariation1(const FVCamControllerInput& In, float Dt, FVCamInputIntents& Out);
    void ProcessInverted(const FVCamControllerInput& In, float Dt, FVCamInputIntents& Out);
};
