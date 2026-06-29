#include "VCamInputLayer.h"

// Faithful port of WVCAM vcam_default.py / vcam_sony.py (updateMovement + updateButtons).
// All numeric constants come from controller.py + the binding ctors (see header / design doc).

FVCamInputIntents FVCamInputLayer::Process(const FVCamControllerInput& In, float DtSeconds)
{
    FVCamInputIntents Out;

    if (!bInit)
    {
        // getAxisValue(normalize=True) is relative to the value at (re)load; capture the baseline.
        Base  = In;
        bInit = true;
    }

    if (Layout == EVCamButtonLayout::Sony) { ProcessSony(In, DtSeconds, Out); }
    else                                   { ProcessDefault(In, DtSeconds, Out); }

    return Out;
}

// ── Layout 0: Default (vcam_default.py) — momentary shift on held left_x ───────
void FVCamInputLayer::ProcessDefault(const FVCamControllerInput& In, float Dt, FVCamInputIntents& Out)
{
    const bool bShifted = In.LeftX;   // isShifted() = isButtonPressed('left_x')
    Out.bShifted = bShifted;
    Out.Mapping  = bShifted ? EVCamMapping::Shifted : EVCamMapping::Standard;

    // ── Gain stacks (shared master gain for translation + rotation) ──
    const float TransGain01 = Gain01(In.LeftGain);                 // left_gain/4095
    const float ZoomTrim    = 1.f - Gain01(In.RightGain);          // INVERTED: 1 - right_gain/4095
    const float MasterTrans = TransGain01 * TransGainStage1 * TransGainStage2;   // *10*(1/800)
    const float MasterZoom  = ZoomTrim    * ZoomGainStage1  * ZoomGainStage2;    // *10*(1/100)
    Out.TranslationGain = TransGain01;
    Out.ZoomGainTrim    = ZoomTrim;

    // ── Translation (Vector(tu, tv, tw) → X=U, Y=W, Z=V) ──
    const float tu = Move(In.LeftLeftY, Base.LeftLeftY);
    const float tw = Move(In.LeftLeftX, Base.LeftLeftX);
    const float tv = bShifted ? 0.f : Move(In.RightLeftY, Base.RightLeftY);
    Out.TranslationSpeed = FVector(tu, tw, tv) * MasterTrans;

    // ── Rotation (shifted) vs Zoom (standard) ──
    if (bShifted)
    {
        const float Roll  =  Move(In.RightRightX, Base.RightRightX);
        const float Yaw   = -Move(In.RightLeftX,  Base.RightLeftX);
        const float Pitch =  Move(In.RightLeftY,  Base.RightLeftY);
        Out.RotationSpeed = FVector(Roll, Yaw, Pitch) * MasterTrans;
    }
    else
    {
        Out.ZoomSpeed = Move(In.RightRightY, Base.RightRightY) * MasterZoom;
    }

    // ── Buttons ──
    const float Hold = HoldOnceDelay();
    const auto eLY  = Edges[BLY    ].Update(In.LeftY,     Dt, Hold);
    const auto eLB  = Edges[BLB    ].Update(In.LeftB,     Dt, Hold);
    const auto eLA  = Edges[BLA    ].Update(In.LeftA,     Dt, Hold);
    const auto eLD  = Edges[BLDown ].Update(In.LeftDown,  Dt, Hold);
    const auto eLLe = Edges[BLLeft ].Update(In.LeftLeft,  Dt, Hold);
    const auto eLRi = Edges[BLRight].Update(In.LeftRight, Dt, Hold);
    const auto eRA  = Edges[BRA    ].Update(In.RightA,    Dt, Hold);
    const auto eRY  = Edges[BRY    ].Update(In.RightY,    Dt, Hold);
    const auto eRB  = Edges[BRB    ].Update(In.RightB,    Dt, Hold);
    const auto eRLe = Edges[BRLeft ].Update(In.RightLeft, Dt, Hold);
    const auto eRRi = Edges[BRRight].Update(In.RightRight,Dt, Hold);
    // Keep the remaining timers advancing (right_up/down, left_up, left_x) even if unbound here.
    Edges[BLX].Update(In.LeftX, Dt, Hold); Edges[BLUp].Update(In.LeftUp, Dt, Hold);
    Edges[BRUp].Update(In.RightUp, Dt, Hold); Edges[BRDown].Update(In.RightDown, Dt, Hold);
    Edges[BRX].Update(In.RightX, Dt, Hold);

    if (eLY.bReleasedShort) { Out.bGotoPrev = true; }
    if (eLY.bHoldOnce)      { Out.bGotoCurrent = true; }
    if (eLB.bPressed)       { Out.bSavePosition = true; }
    if (eLD.bPressed)       { Out.bPlaybackToggle = true; }
    if (eLLe.bReleasedShort || eRLe.bPressed) { Out.bScrubBack = true; }
    if (eLRi.bReleasedShort || eRRi.bPressed) { Out.bScrubFwd  = true; }

    if (!bShifted)
    {
        if (eRA.bPressed) { Out.bZeroEverything = true; }
        if (eRY.bPressed) { Out.bLensNext = true; }
        if (eRB.bPressed) { Out.bLensPrev = true; }
        if (eLA.bPressed) { Out.bToggleLocked = true; }
    }
    else
    {
        if (eRY.bPressed) { Out.bWorldScaleNext = true; }
        if (eRB.bPressed) { Out.bWorldScalePrev = true; }
        if (eLA.bPressed) { Out.bToggleFlightMode = true; }
    }
}

// ── Layout 1: Sony (vcam_sony.py) — latched 3-state map machine ───────────────
void FVCamInputLayer::ProcessSony(const FVCamControllerInput& In, float Dt, FVCamInputIntents& Out)
{
    const float Hold = HoldOnceDelay();

    // left_x hold-once cycles the latched map (init SONY): STANDARD→SHIFTED→SONY→STANDARD.
    if (Edges[BLX].Update(In.LeftX, Dt, Hold).bHoldOnce)
    {
        switch (Mapping)
        {
            case EVCamMapping::Standard: Mapping = EVCamMapping::Shifted;  break;
            case EVCamMapping::Shifted:  Mapping = EVCamMapping::Sony;     break;
            default:                     Mapping = EVCamMapping::Standard; break;
        }
    }
    Out.Mapping  = Mapping;
    Out.bShifted = (Mapping == EVCamMapping::Shifted);

    const bool bStdOrSony = (Mapping == EVCamMapping::Standard || Mapping == EVCamMapping::Sony);

    // ── Gains ──
    const float TransGain01 = Gain01(In.LeftGain);
    const float ZoomTrim    = 1.f - Gain01(In.RightGain);
    const float MasterTrans = TransGain01 * TransGainStage1 * TransGainStage2;
    const float MasterZoom  = ZoomTrim    * ZoomGainStage1  * ZoomGainStage2;
    Out.TranslationGain = TransGain01;
    Out.ZoomGainTrim    = ZoomTrim;

    // ── Translation — Sony's vertical-translate axis is right_right_y (not right_left_y) ──
    const float tu = Move(In.LeftLeftY, Base.LeftLeftY);
    const float tw = Move(In.LeftLeftX, Base.LeftLeftX);
    const float tv = bStdOrSony ? Move(In.RightRightY, Base.RightRightY) : 0.f;
    Out.TranslationSpeed = FVector(tu, tw, tv) * MasterTrans;

    // ── Rotation (SHIFTED) vs Zoom (else); Sony swaps the rotation/zoom axes vs Default ──
    if (Mapping == EVCamMapping::Shifted)
    {
        const float Roll  =  Move(In.RightLeftX,  Base.RightLeftX);
        const float Yaw   = -Move(In.RightRightX, Base.RightRightX);
        const float Pitch =  Move(In.RightRightY, Base.RightRightY);
        Out.RotationSpeed = FVector(Roll, Yaw, Pitch) * MasterTrans;
    }
    else
    {
        Out.ZoomSpeed = Move(In.RightLeftY, Base.RightLeftY) * MasterZoom;
    }

    // ── Sony XY accumulator (the only axis WVCAM integrates; fixed per-tick, no dt) ──
    const float xRateRaw = Move(In.LeftRightX, Base.LeftRightX) * SonyMetersToCm / AxisCounts;
    const float yRateRaw = Move(In.LeftRightY, Base.LeftRightY) * SonyMetersToCm / AxisCounts;
    SonyRawX += xRateRaw * TransGain01;
    SonyRawY += yRateRaw * TransGain01;

    // ── Buttons ──
    const auto eLY = Edges[BLY].Update(In.LeftY, Dt, Hold);
    const auto eLA = Edges[BLA].Update(In.LeftA, Dt, Hold);
    const auto eLB = Edges[BLB].Update(In.LeftB, Dt, Hold);
    const auto eRA = Edges[BRA].Update(In.RightA, Dt, Hold);
    const auto eRY = Edges[BRY].Update(In.RightY, Dt, Hold);
    const auto eRB = Edges[BRB].Update(In.RightB, Dt, Hold);
    const auto eRX = Edges[BRX].Update(In.RightX, Dt, Hold);
    // advance the rest
    Edges[BLUp].Update(In.LeftUp, Dt, Hold); Edges[BLDown].Update(In.LeftDown, Dt, Hold);
    Edges[BLLeft].Update(In.LeftLeft, Dt, Hold); Edges[BLRight].Update(In.LeftRight, Dt, Hold);
    Edges[BRUp].Update(In.RightUp, Dt, Hold); Edges[BRDown].Update(In.RightDown, Dt, Hold);
    Edges[BRLeft].Update(In.RightLeft, Dt, Hold); Edges[BRRight].Update(In.RightRight, Dt, Hold);

    if (eLY.bReleasedShort) { Out.bGotoPrev = true; }
    if (eLY.bHoldOnce)      { Out.bGotoCurrent = true; }

    if (Mapping == EVCamMapping::Standard)
    {
        if (eRA.bPressed) { Out.bZeroEverything = true; }
        if (eRY.bPressed) { Out.bLensNext = true; }
        if (eRB.bPressed) { Out.bLensPrev = true; }
        if (eLA.bPressed) { Out.bToggleHold = true; }
        if (eLB.bPressed) { Out.bSavePosition = true; }
    }
    else if (Mapping == EVCamMapping::Shifted)
    {
        if (eRY.bPressed) { Out.bWorldScaleNext = true; }
        if (eRB.bPressed) { Out.bWorldScalePrev = true; }
        if (eLA.bPressed) { Out.bToggleFlightMode = true; }
        if (eLB.bPressed) { Out.bSavePosition = true; }
    }
    else // SONY — transport / take / live / cone / save-file are deferred (no UE equivalents yet).
    {
        if (eRA.bPressed) { Out.bZeroEverything = true; }
        if (eRX.bPressed) { Out.bResetSonyXY = true; SonyRawX = 0.f; SonyRawY = 0.f; }
    }
}
