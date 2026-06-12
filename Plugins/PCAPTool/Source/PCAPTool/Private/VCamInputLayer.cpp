#include "VCamInputLayer.h"

FVCamInputIntents FVCamInputLayer::Process(const FVCamControllerInput& In, float DtSeconds)
{
    FVCamInputIntents Out;

    if (!bInit)
    {
        Prev = In;
        PrevLeftEnc = In.LeftEnc;
        PrevRightEnc = In.RightEnc;
        bInit = true;
    }

    switch (Layout)
    {
        case EVCamButtonLayout::Variation1:      ProcessVariation1(In, DtSeconds, Out); break;
        case EVCamButtonLayout::InvertedDefault: ProcessInverted(In, DtSeconds, Out);   break;
        default:                                 ProcessDefault(In, DtSeconds, Out);    break;
    }

    Out.bShifted = bShifted;
    Out.bHold    = bHold;

    Prev = In;
    PrevLeftEnc = In.LeftEnc;
    PrevRightEnc = In.RightEnc;
    return Out;
}

void FVCamInputLayer::AccumulateSony(float JoyX, float JoyY)
{
    // Parity with the scripts; the platform offset itself is deferred (platforming).
    const float GAIN_m_TO_cm = 100.0f;
    SonyRawX += DB(JoyX) * GAIN_m_TO_cm / 4095.0f * TransGain;
    SonyRawY += DB(JoyY) * GAIN_m_TO_cm / 4095.0f * TransGain;
}

// ── Layout 0: Default — joystick translation + encoders ──────────────────────
void FVCamInputLayer::ProcessDefault(const FVCamControllerInput& In, float /*Dt*/, FVCamInputIntents& Out)
{
    bShifted = In.LeftTrigger;                 // left_trigger held = SHIFTED
    bHold    = In.RightTrigger && !bShifted;   // right_trigger = momentary Hold (STANDARD)

    // Gain encoder = left_enc. STANDARD: zoom gain; SHIFTED: translation gain.
    const float dGainEnc = In.LeftEnc - PrevLeftEnc;
    if (dGainEnc != 0.f)
    {
        const float dGain = dGainEnc / CountsPerRev;
        if (!bShifted) { ZoomGain  = FMath::Clamp(ZoomGain + dGain, 0.05f, 1.0f); Out.bSetZoomGain = true;        Out.ZoomGain = ZoomGain; }
        else           { TransGain = FMath::Clamp(TransGain + dGain, 0.01f, 1.0f); Out.bSetTranslationGain = true; Out.TranslationGain = TransGain; }
    }

    // Translation rate from joysticks. tu=left_joy_y, tw=left_joy_x, tv=right_joy_y (STANDARD only).
    const float tu = DB(In.LeftJoyY);
    const float tw = DB(In.LeftJoyX);
    const float tv = bShifted ? 0.f : DB(In.RightJoyY);
    const float Scale = TransGain * TranslationRateScale;   // TUNE ON RIG
    Out.NavigateRate = FVector(tu * Scale, tw * Scale, tv * Scale);   // X=U, Y=W, Z=V

    // Zoom encoder = right_enc → focal delta.
    const float dZoomEnc = In.RightEnc - PrevRightEnc;
    if (dZoomEnc != 0.f) { Out.ZoomDelta += dZoomEnc * ZoomPrecisionDefault / CountsPerRev; }

    if (!bShifted)
    {
        if (Pressed(In.RightB, Prev.RightB)) Out.bLensNext = true;
        if (Pressed(In.RightA, Prev.RightA)) Out.bLensPrev = true;
        if (Pressed(In.LeftA,  Prev.LeftA))  Out.bZeroEverything = true;
        if (Pressed(In.LeftB,  Prev.LeftB))  Out.bSavePosition = true;
        // Transport (right d-pad play/record/stop, left d-pad take/scrub) deferred — V-IN-D3.
    }
    else
    {
        if (Released(In.LeftLeft,  Prev.LeftLeft))  Out.bGotoPrev = true;   // onRelease in the script
        if (Released(In.LeftRight, Prev.LeftRight)) Out.bGotoNext = true;
        if (Pressed(In.RightB, Prev.RightB)) Out.bWorldScaleNext = true;
        if (Pressed(In.RightA, Prev.RightA)) Out.bWorldScalePrev = true;
        if (Pressed(In.LeftA,  Prev.LeftA)) { Out.bResetSonyXY = true; SonyRawX = 0.f; SonyRawY = 0.f; }
        if (Pressed(In.LeftB,  Prev.LeftB))  Out.bDeletePosition = true;
        AccumulateSony(In.RightJoyX, In.RightJoyY);
    }
}

// ── Layout 1: Variation 1 — d-pad translation + joystick zoom (matches the photos) ──
void FVCamInputLayer::ProcessVariation1(const FVCamControllerInput& In, float /*Dt*/, FVCamInputIntents& Out)
{
    bShifted = In.LeftTrigger;
    bHold    = In.RightTrigger && !bShifted;

    // D-pad translation (both maps): U=right_up/down, W=right_right/left, V=left_up/down.
    const float u = (In.RightUp    ? 1.f : 0.f) - (In.RightDown ? 1.f : 0.f);
    const float w = (In.RightRight ? 1.f : 0.f) - (In.RightLeft ? 1.f : 0.f);
    const float v = (In.LeftUp     ? 1.f : 0.f) - (In.LeftDown  ? 1.f : 0.f);
    const float Scale = DpadStep * TransGain * TranslationRateScale;   // TUNE ON RIG
    Out.NavigateRate = FVector(u * Scale, w * Scale, v * Scale);

    if (bShifted)   // SHIFTED: left_enc → translation gain
    {
        const float dGainEnc = In.LeftEnc - PrevLeftEnc;
        if (dGainEnc != 0.f) { TransGain = FMath::Clamp(TransGain + dGainEnc / CountsPerRev, 0.01f, 1.0f); Out.bSetTranslationGain = true; Out.TranslationGain = TransGain; }
    }

    // Zoom gain (always): left_joy_y → axis/(4095*200).
    const float dZoomGain = DB(In.LeftJoyY) / (4095.0f * ZoomGainPrecision);
    if (dZoomGain != 0.f) { ZoomGain = FMath::Clamp(ZoomGain + dZoomGain, 0.01f, 1.0f); Out.bSetZoomGain = true; Out.ZoomGain = ZoomGain; }

    // Zoom (STANDARD): right_joy_y → focal delta axis*10/4095.
    if (!bShifted) { Out.ZoomDelta += DB(In.RightJoyY) * ZoomPrecisionJoy / 4095.0f; }

    if (!bShifted)
    {
        if (Pressed(In.RightB, Prev.RightB)) Out.bLensNext = true;
        if (Pressed(In.RightA, Prev.RightA)) Out.bLensPrev = true;
        if (Pressed(In.LeftA,  Prev.LeftA))  Out.bZeroEverything = true;
        if (Pressed(In.LeftB,  Prev.LeftB))  Out.bSavePosition = true;
    }
    else
    {
        if (Released(In.LeftLeft,  Prev.LeftLeft))  Out.bGotoPrev = true;
        if (Released(In.LeftRight, Prev.LeftRight)) Out.bGotoNext = true;
        if (Pressed(In.RightB, Prev.RightB)) Out.bWorldScaleNext = true;
        if (Pressed(In.RightA, Prev.RightA)) Out.bWorldScalePrev = true;
        if (Pressed(In.LeftA,  Prev.LeftA)) { Out.bResetSonyXY = true; SonyRawX = 0.f; SonyRawY = 0.f; }
        if (Pressed(In.LeftB,  Prev.LeftB))  Out.bDeletePosition = true;
        AccumulateSony(In.RightJoyX, In.RightJoyY);
    }
}

// ── Layout 2: Inverted Default — Variation 1 mirrored L<->R ──────────────────
void FVCamInputLayer::ProcessInverted(const FVCamControllerInput& In, float /*Dt*/, FVCamInputIntents& Out)
{
    bShifted = In.RightTrigger;                 // mirrored: right_trigger = SHIFTED
    bHold    = In.LeftTrigger && !bShifted;     // left_trigger = Hold

    // D-pad translation mirrored: U=left_up/down, W=left_right/left, V=right_up/down.
    const float u = (In.LeftUp     ? 1.f : 0.f) - (In.LeftDown  ? 1.f : 0.f);
    const float w = (In.LeftRight  ? 1.f : 0.f) - (In.LeftLeft  ? 1.f : 0.f);
    const float v = (In.RightUp    ? 1.f : 0.f) - (In.RightDown ? 1.f : 0.f);
    const float Scale = DpadStep * TransGain * TranslationRateScale;   // TUNE ON RIG
    Out.NavigateRate = FVector(u * Scale, w * Scale, v * Scale);

    if (bShifted)   // left_enc → translation gain (the inverted script keeps left_enc here)
    {
        const float dGainEnc = In.LeftEnc - PrevLeftEnc;
        if (dGainEnc != 0.f) { TransGain = FMath::Clamp(TransGain + dGainEnc / CountsPerRev, 0.01f, 1.0f); Out.bSetTranslationGain = true; Out.TranslationGain = TransGain; }
    }

    // Zoom gain mirrored to right joy; zoom (STANDARD) mirrored to left joy.
    const float dZoomGain = DB(In.RightJoyY) / (4095.0f * ZoomGainPrecision);
    if (dZoomGain != 0.f) { ZoomGain = FMath::Clamp(ZoomGain + dZoomGain, 0.01f, 1.0f); Out.bSetZoomGain = true; Out.ZoomGain = ZoomGain; }
    if (!bShifted) { Out.ZoomDelta += DB(In.LeftJoyY) * ZoomPrecisionJoy / 4095.0f; }

    // Buttons mirrored L<->R. NOTE: the source script double-binds left_a/left_b (lens AND
    // zero/cone), losing lens cycling — here we implement the sensible mirror (lens restored).
    // Confirm intent — V-IN-D2.
    if (!bShifted)
    {
        if (Pressed(In.LeftB,  Prev.LeftB))  Out.bLensNext = true;
        if (Pressed(In.LeftA,  Prev.LeftA))  Out.bLensPrev = true;
        if (Pressed(In.RightA, Prev.RightA)) Out.bZeroEverything = true;
        if (Pressed(In.RightB, Prev.RightB)) Out.bSavePosition = true;
    }
    else
    {
        if (Released(In.RightLeft,  Prev.RightLeft))  Out.bGotoPrev = true;
        if (Released(In.RightRight, Prev.RightRight)) Out.bGotoNext = true;
        if (Pressed(In.LeftB, Prev.LeftB)) Out.bWorldScaleNext = true;
        if (Pressed(In.LeftA, Prev.LeftA)) Out.bWorldScalePrev = true;
        if (Pressed(In.RightA, Prev.RightA)) { Out.bResetSonyXY = true; SonyRawX = 0.f; SonyRawY = 0.f; }
        if (Pressed(In.RightB, Prev.RightB)) Out.bDeletePosition = true;
        AccumulateSony(In.LeftJoyX, In.LeftJoyY);
    }
}
