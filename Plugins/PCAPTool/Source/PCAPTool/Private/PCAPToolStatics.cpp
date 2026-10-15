#include "PCAPToolStatics.h"
#include "ActorRosterEntry.h"
#include "StageConfigAsset.h"
#include "PCAPPerformerExtension.h"
#include "HMCRigEntry.h"

FString UPCAPToolStatics::GenerateTakeID(const FString& DayNumber, const FString& ShotSlot, const FString& TakeNumber)
{
    const FString Day  = FString::Printf(TEXT("%03d"), FCString::Atoi(*DayNumber));
    const FString Shot = FString::Printf(TEXT("%03d"), FCString::Atoi(*ShotSlot));
    const FString Take = FString::Printf(TEXT("%03d"), FCString::Atoi(*TakeNumber));
    return FString::Printf(TEXT("%s%s_%s"), *Day, *Shot, *Take);
}

FString UPCAPToolStatics::GenerateNextTakeNumber(const FShot& Shot)
{
    int32 Max = 0;
    for (const FTake& Take : Shot.Takes)
    {
        const int32 Num = FCString::Atoi(*Take.TakeNumber);
        if (Num > Max) Max = Num;
    }
    return FString::Printf(TEXT("%03d"), Max + 1);
}

FShotSubject UPCAPToolStatics::MakeShotSubjectFromRoster(const UActorRosterEntry* Entry)
{
    FShotSubject Subject;
    if (!Entry)
    {
        return Subject;
    }

    Subject.ActorID       = Entry->ActorID;
    Subject.CharacterName = FString();
    Subject.bIsActive     = false;

    Subject.BodyStream     = Entry->DefaultBodyStream;
    Subject.bHasBodyStream = !Entry->DefaultBodyStream.LiveLinkSubjectName.IsNone();

    Subject.FaceStream     = Entry->DefaultFaceStream;
    Subject.bHasFaceStream = !Entry->DefaultFaceStream.LiveLinkSubjectName.IsNone();

    Subject.AudioStreams = Entry->DefaultAudioStreams;
    return Subject;
}

FString UPCAPToolStatics::ShotSlotForType(EShotType ShotType, int32 ShotIndex)
{
    switch (ShotType)
    {
        case EShotType::Calibration: return TEXT("901");
        case EShotType::TestShot:    return TEXT("902");
        case EShotType::Retargeting: return TEXT("903");
        default:                     return FString::Printf(TEXT("%03d"), FMath::Max(1, ShotIndex));
    }
}

bool UPCAPToolStatics::IsCalibrationSlot(const FString& Slot)
{
    if (Slot == TEXT("901")) return true;
    return Slot.Len() == 3 && Slot[0] == TEXT('9') && Slot[1] >= TEXT('1') && Slot[1] <= TEXT('4')
        && FChar::IsDigit(Slot[2]);
}

FPCAPCalibrationNeeds UPCAPToolStatics::ResolveCalibrationNeeds(const FShootDay& Day, const UStageConfigAsset* Stage)
{
    FPCAPCalibrationNeeds N;
    FPCAPCalibrationNeeds FromRows;
    FromRows.bAudio = FromRows.bFacial = FromRows.bStereo = FromRows.bBody = false;
    int32 Rows = 0;
    TSet<FString> WithRow;
    for (const FPCAPPerformerCaptureSetup& S : Day.CaptureSetups)
    {
        if (!Day.CalledActorIDs.Contains(S.ActorID)) continue;   // rows for un-called actors don't count
        ++Rows;
        WithRow.Add(S.ActorID);
        FromRows.bAudio  |= !S.AudioSystem.IsEmpty();
        FromRows.bFacial |= !S.FacialSystem.IsEmpty();
        FromRows.bStereo |= !S.FacialSystem.IsEmpty() && S.bStereo;
        FromRows.bBody   |= !S.MocapSystem.IsEmpty();
        if (!S.FacialSystem.IsEmpty()) FromRows.NeutralFaceActors.AddUnique(S.ActorID);
    }
    if (Rows > 0)
    {
        // Called actors without a row yet are assumed facial (the row appears once the Call Sheet draws).
        for (const FString& A : Day.CalledActorIDs)
            if (!WithRow.Contains(A)) FromRows.NeutralFaceActors.AddUnique(A);
        return FromRows;
    }
    if (Stage)
    {
        N.bAudio  = !Stage->AudioSystemName.IsEmpty();
        N.bFacial = !Stage->FacialSystem.IsEmpty();
        N.bStereo = N.bFacial && Stage->bFacialStereo && !Stage->bFacialMono;   // both allowed = undecided until Capture Setup
        N.bBody   = !Stage->MocapSystem.IsEmpty();
    }
    if (N.bFacial) N.NeutralFaceActors = Day.CalledActorIDs;
    return N;
}

int32 UPCAPToolStatics::AddCalibrationShots(FShootDay& Day, const FPCAPCalibrationNeeds& Needs)
{
    if (Day.Sessions.Num() == 0)
    {
        FSession S; S.SessionID = TEXT("S01"); S.Label = TEXT("Main");
        Day.Sessions.Add(S);
    }
    FSession& Sess = Day.Sessions[0];

    int32 Added = 0;
    auto Add = [&](const TCHAR* Slot, const TCHAR* Desc)
    {
        if (Sess.Shots.ContainsByPredicate([Slot](const FShot& S){ return S.ShotID == Slot; })) return;
        FShot Shot;
        Shot.ShotID      = Slot;
        Shot.ShotType    = EShotType::Calibration;
        Shot.Description = Desc;
        Sess.Shots.Add(Shot);
        ++Added;
    };

    if (Needs.bAudio)  Add(AudioCalibrationSlot(), TEXT("Audio calibration: level check per mic"));
    // Stereo: Epic's Calibration Takes page asks for a board take at the start and the end of
    // every session (and after a significant helmet adjustment).
    if (Needs.bFacial) Add(FacialCalibrationStartSlot(), Needs.bStereo
        ? TEXT("Facial calibration: stereo board, start of session")
        : TEXT("Facial calibration: start of session"));
    if (Needs.bFacial && Needs.bStereo) Add(FacialCalibrationEndSlot(), TEXT("Facial calibration: stereo board, end of session"));
    if (Needs.bBody)   Add(BodyCalibrationSlot(), TEXT("Body calibration"));

    // One neutral-face take per facial actor per day (Madi's rule). Matched by the actor on
    // the shot, not by slot, so calling or un-calling someone never renumbers anyone else.
    TArray<FString> Actors = Needs.NeutralFaceActors;
    Actors.Sort();
    for (const FString& Actor : Actors)
    {
        if (Actor.IsEmpty()) continue;
        const bool bHas = Sess.Shots.ContainsByPredicate([&Actor](const FShot& S)
        {
            return S.ShotType == EShotType::Calibration && S.Description.StartsWith(NeutralFaceDescPrefix())
                && S.Subjects.ContainsByPredicate([&Actor](const FShotSubject& Sub){ return Sub.ActorID == Actor; });
        });
        if (bHas) continue;

        FString Slot;
        for (int32 i = 1; i <= 9 && Slot.IsEmpty(); ++i)
        {
            const FString Candidate = FString::Printf(TEXT("94%d"), i);
            if (!Sess.Shots.ContainsByPredicate([&Candidate](const FShot& S){ return S.ShotID == Candidate; })) Slot = Candidate;
        }
        if (Slot.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] No free neutral-face slot (941-949) for %s"), *Actor);
            continue;
        }
        FShot Shot;
        Shot.ShotID      = Slot;
        Shot.ShotType    = EShotType::Calibration;
        Shot.Description = FString(NeutralFaceDescPrefix()) + Actor;
        FShotSubject Sub;
        Sub.ActorID        = Actor;
        Sub.bIsActive      = true;
        Sub.bHasFaceStream = true;
        Shot.Subjects.Add(Sub);
        Sess.Shots.Add(Shot);
        ++Added;
    }
    return Added;
}

FPCAPPerformerCaptureSetup UPCAPToolStatics::MakeDefaultCaptureSetup(const FString& ActorID,
    const UStageConfigAsset* Stage, const UPCAPPerformerExtension* Performer)
{
    FPCAPPerformerCaptureSetup R;
    R.ActorID = ActorID;
    if (Stage)
    {
        R.MocapSystem  = Stage->MocapSystem;
        R.FacialSystem = Stage->FacialSystem;
        R.AudioSystem  = Stage->AudioSystemName;
        R.bStereo      = Stage->bFacialStereo || !Stage->bFacialMono;   // mono only when the stage is mono-only
    }
    if (Performer)
    {
        if (!Performer->DefaultMocapSystem.IsEmpty())  R.MocapSystem  = Performer->DefaultMocapSystem;
        if (!Performer->DefaultFacialSystem.IsEmpty()) { R.FacialSystem = Performer->DefaultFacialSystem; R.bStereo = Performer->bDefaultStereo; }
        if (!Performer->DefaultAudioSystem.IsEmpty())  R.AudioSystem  = Performer->DefaultAudioSystem;
        if (!Performer->HMCRig.IsNull())               R.Helmet       = Performer->HMCRig;
    }
    return R;
}

FShootDay UPCAPToolStatics::SeedNewShootDay(const FString& DayID, const FDateTime& CalendarDate)
{
    FShootDay Day;
    Day.DayID        = DayID;
    Day.CalendarDate = CalendarDate;

    const FString DayPadded = FString::Printf(TEXT("%03d"), FCString::Atoi(*DayID));

    auto MakeTake = [&](const FString& Slot, const FString& TakeNum) -> FTake
    {
        FTake T;
        T.TakeNumber = TakeNum;
        T.ShotID     = Slot;   // slot-only 3-digit ShotID — GenerateTakeID re-pads day+slot for the TakeID
        T.DayID      = DayID;
        T.TakeID     = GenerateTakeID(DayPadded, Slot, TakeNum);
        T.Label      = ETakeLabel::Captured;
        return T;
    };

    auto MakeShot = [&](const FString& Slot, EShotType Type, const FString& Desc, int32 PreseededTakes) -> FShot
    {
        FShot S;
        S.ShotID      = Slot;   // slot-only 3-digit ShotID — GenerateTakeID re-pads day+slot for the TakeID
        S.ShotType    = Type;
        S.Description = Desc;
        for (int32 i = 1; i <= PreseededTakes; ++i)
            S.Takes.Add(MakeTake(Slot, FString::Printf(TEXT("%03d"), i)));
        return S;
    };

    FSession Session;
    Session.SessionID = DayID + TEXT("_S01");
    Session.Label     = TEXT("Morning");

    Session.Shots.Add(MakeShot(TEXT("901"), EShotType::Calibration, TEXT("Calibration"),  1));
    Session.Shots.Add(MakeShot(TEXT("902"), EShotType::TestShot,    TEXT("Test Shot"),     5));
    Session.Shots.Add(MakeShot(TEXT("903"), EShotType::Retargeting, TEXT("Retargeting"),  1));
    Session.Shots.Add(MakeShot(TEXT("001"), EShotType::Production,  TEXT("Production 1"), 0));
    Session.Shots.Add(MakeShot(TEXT("002"), EShotType::Production,  TEXT("Production 2"), 0));
    Session.Shots.Add(MakeShot(TEXT("003"), EShotType::Production,  TEXT("Production 3"), 0));

    Day.Sessions.Add(Session);
    return Day;
}

// ── HMC issue evaluation ────────────────────────────────────────────────────

int32 UPCAPToolStatics::EvaluateCameraIssues(const FHMCDeviceStatus& S, int32 CameraIndex, float TargetFPS)
{
    int32 Flags = HMC_Issue_None;

    // Per-camera signals
    const bool  bStreaming = (CameraIndex == 0) ? S.bStreaming0     : S.bStreaming1;
    const int32 Exposure   = (CameraIndex == 0) ? S.Exposure0       : S.Exposure1;
    const int32 Dropped    = (CameraIndex == 0) ? S.DroppedFrames0  : S.DroppedFrames1;

    if (!bStreaming)                            Flags |= HMC_Issue_NotStreaming;
    if (Exposure > 7000)                        Flags |= HMC_Issue_Overexposed;
    else if (Exposure > 0 && Exposure < 1000)   Flags |= HMC_Issue_Underexposed;
    if (Dropped > 0)                            Flags |= HMC_Issue_DroppedFrames;

    // Device-wide signals — apply to both cameras. Guard against the all-zero
    // default a status carries before its first successful parse.
    //
    // NOTE: ClipNotReady is intentionally NOT evaluated here. "Not Ready" is the
    // normal idle state before/between takes, so flagging it turned every feed
    // amber at rest. Clip state is surfaced in the CLIP vital cell instead; this
    // mask drives only live-feed health.
    // Thresholds match the vital-cell RED levels so border and vitals never disagree.
    if (S.BatteryVoltage > 0.f      && S.BatteryVoltage <= 13.6f)       Flags |= HMC_Issue_LowBattery;
    if (S.AvailableStorageMB > 0.f  && S.AvailableStorageMB < 51200.f)  Flags |= HMC_Issue_LowStorage;  // < 50 GB
    if (S.CPUUsagePercent >= 85.f)                                     Flags |= HMC_Issue_HighCPU;
    if (S.TemperatureCelsius >= 85.f)                                  Flags |= HMC_Issue_HighTemp;

    // MetaHuman Capture Device Requirements: >= 60 fps recommended. Device-wide.
    // S.FPS is the device's reported frameRate; threshold has margin. VERIFY on the
    // rig that frameRate is the capture rate (not the stream rate) before trusting.
    if (S.FPS > 0.f && S.FPS < TargetFPS * 0.92f)                      Flags |= HMC_Issue_LowFPS;

    return Flags;
}

EHMCIssueSeverity UPCAPToolStatics::GetIssueSeverity(int32 Flags)
{
    const int32 RedMask =
        HMC_Issue_NotStreaming | HMC_Issue_Overexposed |
        HMC_Issue_LowBattery   | HMC_Issue_LowStorage   | HMC_Issue_HighTemp |
        HMC_Issue_NoFace       | HMC_Issue_OutOfFocus   | HMC_Issue_FramingDrift |
        HMC_Issue_Bumped       | HMC_Issue_Unstable     | HMC_Issue_LowFPS       |
        HMC_Issue_TooClose     | HMC_Issue_TooFar;

    const int32 AmberMask =
        HMC_Issue_Underexposed | HMC_Issue_DroppedFrames | HMC_Issue_ClipNotReady |
        HMC_Issue_HighCPU      | HMC_Issue_UnevenLight   |
        HMC_Manual_FaceOffAxis | HMC_Manual_HeadsetShift | HMC_Manual_OutOfFocus |
        HMC_Manual_LipSeal     | HMC_Manual_Eyelid;

    if (Flags & RedMask)   return EHMCIssueSeverity::Red;
    if (Flags & AmberMask) return EHMCIssueSeverity::Amber;
    return EHMCIssueSeverity::None;
}

FString UPCAPToolStatics::GetIssueBannerText(int32 Flags)
{
    // Red (hard) issues — highest priority
    if (Flags & HMC_Issue_NoFace)        return TEXT("NO FACE IN FRAME · Reframe");
    if (Flags & HMC_Issue_NotStreaming)  return TEXT("Camera disconnected · Check device");
    if (Flags & HMC_Issue_Overexposed)   return TEXT("Overexposed · Reduce exposure");
    if (Flags & HMC_Issue_FramingDrift)  return TEXT("Framing drifted · Re-center to the reference");
    if (Flags & HMC_Issue_Bumped)        return TEXT("Headset bumped · Re-seat & re-set the reference");
    if (Flags & HMC_Issue_Unstable)      return TEXT("Unstable mount · Camera isn't holding still on the face");
    if (Flags & HMC_Issue_OutOfFocus)    return TEXT("Out of focus · Adjust lens");
    if (Flags & HMC_Issue_LowBattery)    return TEXT("Battery low · Eyes on it");
    if (Flags & HMC_Issue_LowStorage)    return TEXT("Storage critical · Wrap soon");
    if (Flags & HMC_Issue_HighTemp)      return TEXT("Overheating · Check rig");
    if (Flags & HMC_Issue_LowFPS)        return TEXT("Low frame rate · Below the target fps");
    if (Flags & HMC_Issue_TooClose)      return TEXT("Too close · Pull the camera back");
    if (Flags & HMC_Issue_TooFar)        return TEXT("Too far · Move the camera closer");
    // Amber (warning) issues
    if (Flags & HMC_Issue_UnevenLight)   return TEXT("Uneven lighting · Flatten the light");
    if (Flags & HMC_Issue_Underexposed)  return TEXT("Underexposed · Raise exposure or gain");
    if (Flags & HMC_Issue_DroppedFrames) return TEXT("Frames dropped · Check connection");
    if (Flags & HMC_Issue_ClipNotReady)  return TEXT("Last clip not verified · SSD may still be writing");
    if (Flags & HMC_Issue_HighCPU)       return TEXT("Running hot · Keep takes short");
    // Operator-reported (manual)
    if (Flags & HMC_Manual_FaceOffAxis)  return TEXT("Face off-axis · Reposition performer");
    if (Flags & HMC_Manual_HeadsetShift) return TEXT("Headset shifted · Re-seat rig");
    if (Flags & HMC_Manual_OutOfFocus)   return TEXT("Out of focus · Adjust lens");
    if (Flags & HMC_Manual_LipSeal)      return TEXT("Lip seal not visible · Adjust camera");
    if (Flags & HMC_Manual_Eyelid)       return TEXT("Eyelid not visible · Adjust camera");
    return TEXT("All clear · Ready to record");
}

FString UPCAPToolStatics::GetLightingHintText(EHMCLightDir Dir)
{
    switch (Dir)
    {
        case EHMCLightDir::Below:  return TEXT("lit from below");
        case EHMCLightDir::Side:   return TEXT("lit from one side");
        case EHMCLightDir::Back:   return TEXT("back-lit (face in shadow)");
        case EHMCLightDir::Shadow: return TEXT("hard shadows on the face");
        default:                   return FString();
    }
}

bool UPCAPToolStatics::FrameHasSubject(const TArray<uint8>& BGRA, int32 Width, int32 Height)
{
    // Same rule as the metrics (light + texture), so the red "no face" border and the
    // checks never disagree. The analysis grid is ~128 cells on the long side: cheap.
    return AnalyzeFrameBGRA(BGRA, Width, Height).bHasSubject;
}

// ── Automatic image analysis (Capture Monitor) ──────────────────────────────

FHMCImageMetrics UPCAPToolStatics::AnalyzeFrameBGRA(const TArray<uint8>& BGRA, int32 Width, int32 Height,
                                                    FVector2D FocusRegionCenter, float FocusRegionExtent)
{
    FHMCImageMetrics M;   // bValid stays false on any guard
    const int32 NumBytes = BGRA.Num();
    if (Width <= 1 || Height <= 1 || NumBytes < Width * Height * 4)
        return M;

    // Downsample to a shared grid (~128 cells on the long side). Nearest-sample
    // keeps enough high-frequency content for a relative focus measure.
    const int32 LongSide = FMath::Max(Width, Height);
    const int32 Step = FMath::Max(1, LongSide / 128);
    const int32 GW   = FMath::Max(2, Width  / Step);
    const int32 GH   = FMath::Max(2, Height / Step);

    TArray<float> L;
    L.SetNumUninitialized(GW * GH);
    for (int32 gy = 0; gy < GH; ++gy)
    {
        const int32 sy = FMath::Min(Height - 1, gy * Step);
        for (int32 gx = 0; gx < GW; ++gx)
        {
            const int32 sx = FMath::Min(Width - 1, gx * Step);
            const int32 i  = (sy * Width + sx) * 4;
            const float B = BGRA[i], G = BGRA[i + 1], R = BGRA[i + 2];
            L[gy * GW + gx] = 0.114f * B + 0.587f * G + 0.299f * R;   // Rec.601 luma 0..255
        }
    }

    // One pass: luma sum / blown / crushed, brightness-weighted centroid, quadrant means.
    const int32 N = GW * GH;
    double Sum = 0.0, Blown = 0.0, Crushed = 0.0, WX = 0.0, WY = 0.0;
    double Quad[4] = { 0,0,0,0 };
    int32  QuadN[4] = { 0,0,0,0 };
    double CenterSum = 0.0; int32 CenterN = 0;   // central region (for back-lit detection)
    for (int32 gy = 0; gy < GH; ++gy)
        for (int32 gx = 0; gx < GW; ++gx)
        {
            const float v = L[gy * GW + gx];
            Sum += v;
            if (v >= 250.f) ++Blown;
            if (v <= 5.f)   ++Crushed;
            const double nx = (double)gx / (GW - 1);
            const double ny = (double)gy / (GH - 1);
            WX += v * nx; WY += v * ny;
            const int32 q = (gx < GW / 2 ? 0 : 1) + (gy < GH / 2 ? 0 : 2);
            Quad[q] += v; ++QuadN[q];
            if (FMath::Abs(nx - 0.5) < 0.18 && FMath::Abs(ny - 0.5) < 0.18) { CenterSum += v; ++CenterN; }
        }

    const double Mean = Sum / N;                 // 0..255
    M.MeanLuma    = (float)(Mean / 255.0);
    M.BlownFrac   = (float)(Blown   / N);
    M.CrushedFrac = (float)(Crushed / N);

    // Brightness-weighted centroid + spread (the IR-lit face is the bright mass).
    if (Sum > KINDA_SMALL_NUMBER)
    {
        const double cx = WX / Sum, cy = WY / Sum;
        M.SubjectCenter = FVector2D(cx, cy);
        double VarSum = 0.0;
        for (int32 gy = 0; gy < GH; ++gy)
            for (int32 gx = 0; gx < GW; ++gx)
            {
                const float v  = L[gy * GW + gx];
                const double nx = (double)gx / (GW - 1);
                const double ny = (double)gy / (GH - 1);
                VarSum += v * ((nx - cx) * (nx - cx) + (ny - cy) * (ny - cy));
            }
        M.SubjectSize = (float)FMath::Clamp(2.0 * FMath::Sqrt(VarSum / Sum), 0.0, 1.0);
    }

    // Regional spread across quadrants -> uneven lighting / side-light / shadow.
    double qm[4];
    for (int32 q = 0; q < 4; ++q) qm[q] = (QuadN[q] > 0) ? Quad[q] / QuadN[q] : 0.0;
    double QMin = qm[0], QMax = qm[0];
    for (int32 q = 1; q < 4; ++q) { QMin = FMath::Min(QMin, qm[q]); QMax = FMath::Max(QMax, qm[q]); }
    M.RegionSpread = (Mean > KINDA_SMALL_NUMBER) ? (float)((QMax - QMin) / Mean) : 0.f;

    // Lighting direction from the region means -> Epic's documented bad-light cases.
    {
        const double topM  = (qm[0] + qm[1]) * 0.5, botM   = (qm[2] + qm[3]) * 0.5;
        const double leftM = (qm[0] + qm[2]) * 0.5, rightM = (qm[1] + qm[3]) * 0.5;
        const double ctrM  = (CenterN > 0) ? CenterSum / CenterN : Mean;
        M.TopMean    = (float)(topM  / 255.0); M.BottomMean = (float)(botM   / 255.0);
        M.LeftMean   = (float)(leftM / 255.0); M.RightMean  = (float)(rightM / 255.0);
        M.CenterMean = (float)(ctrM  / 255.0);
        const double inv   = (Mean > KINDA_SMALL_NUMBER) ? 1.0 / Mean : 0.0;
        const double below = (botM - topM) * inv;
        const double side  = FMath::Abs(leftM - rightM) * inv;
        const double back  = (Mean - ctrM) * inv;
        if      (below > 0.30)            M.LightDir = EHMCLightDir::Below;
        else if (side  > 0.30)            M.LightDir = EHMCLightDir::Side;
        else if (back  > 0.30)            M.LightDir = EHMCLightDir::Back;
        else if (M.RegionSpread > 0.40f)  M.LightDir = EHMCLightDir::Shadow;
        else                              M.LightDir = EHMCLightDir::Even;
    }

    // Focus = variance of the Laplacian, measured over the focus region (the
    // nasolabial band by default), exposure-normalized. The Max/Min bounds collapse
    // the loop to nothing for tiny grids, matching the original empty-loop safety.
    const int32 rx0 = FMath::Max(1,      (int32)((FocusRegionCenter.X - FocusRegionExtent) * GW));
    const int32 rx1 = FMath::Min(GW - 2, (int32)((FocusRegionCenter.X + FocusRegionExtent) * GW));
    const int32 ry0 = FMath::Max(1,      (int32)((FocusRegionCenter.Y - FocusRegionExtent) * GH));
    const int32 ry1 = FMath::Min(GH - 2, (int32)((FocusRegionCenter.Y + FocusRegionExtent) * GH));
    double LapSum = 0.0, LapSq = 0.0; int32 LapN = 0;
    for (int32 gy = ry0; gy <= ry1; ++gy)
        for (int32 gx = rx0; gx <= rx1; ++gx)
        {
            const float c   = L[gy * GW + gx];
            const float lap = 4.f * c
                - L[gy * GW + (gx - 1)] - L[gy * GW + (gx + 1)]
                - L[(gy - 1) * GW + gx] - L[(gy + 1) * GW + gx];
            LapSum += lap; LapSq += (double)lap * lap; ++LapN;
        }
    if (LapN > 0)
    {
        const double LapMean = LapSum / LapN;
        const double LapVar  = FMath::Max(0.0, LapSq / LapN - LapMean * LapMean);
        // Normalize by mean luma so the score is roughly exposure-independent. NOTE: the
        // +1.0 epsilon dominates at very low luma, so calibrate FocusMin against frames at
        // the real operating brightness (read the live value from the Setup read-out / log).
        M.FocusScore = (float)(LapVar / (Mean * Mean + 1.0));
    }

    // Whole-frame edge energy — a checkerboard calibration board reads very high &
    // uniform; a smooth hand/face/occlusion reads low. Drives the board coarse check.
    double ELapSum = 0.0, ELapSq = 0.0; int32 ELapN = 0;
    for (int32 gy = 1; gy <= GH - 2; ++gy)
        for (int32 gx = 1; gx <= GW - 2; ++gx)
        {
            const float c   = L[gy * GW + gx];
            const float lap = 4.f * c
                - L[gy * GW + (gx - 1)] - L[gy * GW + (gx + 1)]
                - L[(gy - 1) * GW + gx] - L[(gy + 1) * GW + gx];
            ELapSum += lap; ELapSq += (double)lap * lap; ++ELapN;
        }
    if (ELapN > 0)
    {
        const double ELapMean = ELapSum / ELapN;
        const double ELapVar  = FMath::Max(0.0, ELapSq / ELapN - ELapMean * ELapMean);
        M.EdgeEnergy = (float)(ELapVar / (Mean * Mean + 1.0));
    }

    // Dark<->bright flips between grid neighbours (checkerboard signature).
    {
        float Lo = L[0], Hi = L[0];
        for (const float v : L) { Lo = FMath::Min(Lo, v); Hi = FMath::Max(Hi, v); }
        const float R = Hi - Lo;
        if (R > 40.f)   // need real contrast before calling anything "dark" or "bright"
        {
            const float DarkMax = Lo + 0.3f * R, BrightMin = Hi - 0.3f * R;
            auto Side = [&](float v) { return v <= DarkMax ? -1 : (v >= BrightMin ? 1 : 0); };
            int32 Flips = 0, Pairs = 0;
            for (int32 gy = 0; gy < GH; ++gy)
                for (int32 gx = 0; gx < GW; ++gx)
                {
                    const int32 s = Side(L[gy * GW + gx]);
                    if (gx + 1 < GW) { ++Pairs; if (s != 0 && s == -Side(L[gy * GW + gx + 1])) ++Flips; }
                    if (gy + 1 < GH) { ++Pairs; if (s != 0 && s == -Side(L[(gy + 1) * GW + gx])) ++Flips; }
                }
            M.TransitionFrac = Pairs > 0 ? (float)Flips / Pairs : 0.f;
        }
    }

    // Interim "face present": some light AND some texture (a flat lit wall is empty). Uses the
    // shared profile defaults; replaced by a real face detector later.
    static const FPipelineCheckProfile SubjectDefaults;
    M.bHasSubject = (M.MeanLuma > SubjectDefaults.SubjectLumaMin) && (M.EdgeEnergy > SubjectDefaults.SubjectEdgeMin);
    M.bValid = true;
    return M;
}

FPipelineCheckProfile UPCAPToolStatics::GetPipelineProfile(ECapturePipeline Pipeline)
{
    // The struct defaults ARE the MetaHuman HMC profile. Each pipeline owns its checks.
    switch (Pipeline)
    {
        case ECapturePipeline::FaceWareHMC:
        {
            // Faceware is its OWN pipeline — it must not inherit MetaHuman's checks.
            // Its checks are undocumented for now, so it runs none (all checks off)
            // until its pose/quality docs land. Fill in real thresholds then.
            FPipelineCheckProfile P;
            P.bCheckSubject  = false;
            P.bCheckFraming  = false;
            P.bCheckFocus    = false;
            P.bCheckExposure = false;
            P.bCheckLighting = false;
            return P;
        }

        case ECapturePipeline::MetaHumanHMC:
        default:
            return FPipelineCheckProfile();   // full MetaHuman HMC profile
    }
}

FPipelineCheckProfile UPCAPToolStatics::GetDefinition(ECapturePipeline Pipeline,
                                                      ECaptureConfiguration Config)
{
    FPipelineCheckProfile P = GetPipelineProfile(Pipeline);

    // Configuration tweaks. Tripod is a head-and-shoulders shot (smaller face
    // fraction, looser drift); head-mounts are tight, face fills the frame, so the
    // defaults already match the head-mount docs.
    switch (Config)
    {
        case ECaptureConfiguration::MonoTripod:
            P.FramingSizeMin  = 0.20f;
            P.FramingSizeMax  = 0.60f;
            P.FramingDriftTol = 0.12f;
            break;
        case ECaptureConfiguration::StereoHeadMount:
            P.bCheckBoard = true;   // checkerboard calibration take is part of stereo HMC
            break;
        case ECaptureConfiguration::MonoHeadMount:
        case ECaptureConfiguration::PhoneHeadMount:   // phone = mono head-mount + TrueDepth; refine on rig
        default:
            break;
    }
    return P;
}

EHMCBoardState UPCAPToolStatics::ClassifyBoardFrame(const FHMCImageMetrics& M, const FPipelineCheckProfile& P)
{
    if (!M.bValid || !M.bHasSubject)   return EHMCBoardState::NotDetected;
    if (M.EdgeEnergy < P.BoardEdgeMin) return EHMCBoardState::NotDetected;   // smooth -> no board / occluded
    if (M.TransitionFrac < P.BoardTransitionMin) return EHMCBoardState::NotDetected;   // no checker pattern (e.g. a face)
    if (M.SubjectSize > P.BoardSizeMax) return EHMCBoardState::TooClose;
    if (M.SubjectSize < P.BoardSizeMin) return EHMCBoardState::TooFar;
    if (FVector2D::Distance(M.SubjectCenter, FVector2D(0.5, 0.5)) > P.BoardCenterTol)
        return EHMCBoardState::OffCenter;
    return EHMCBoardState::Good;
}

FString UPCAPToolStatics::GetBoardStateText(EHMCBoardState State)
{
    switch (State)
    {
        case EHMCBoardState::Good:        return TEXT("Board OK");
        case EHMCBoardState::TooClose:    return TEXT("Board too close · pull back");
        case EHMCBoardState::TooFar:      return TEXT("Board too far · move closer");
        case EHMCBoardState::OffCenter:   return TEXT("Board off-centre");
        case EHMCBoardState::NotDetected: return TEXT("No board / occluded");
        default:                          return FString();
    }
}

int32 UPCAPToolStatics::MapMetricsToAutoFlags(const FHMCImageMetrics& M,
                                              const FPipelineCheckProfile& P,
                                              const FHMCFramingRef& Ref)
{
    int32 Flags = HMC_Issue_None;
    // Image-quality checks need a subject in frame; NoFace is set separately.
    if (!M.bValid || !M.bHasSubject)
        return Flags;

    if (P.bCheckFocus && P.FocusMin > 0.f && M.FocusScore < P.FocusMin)
        Flags |= HMC_Issue_OutOfFocus;

    if (P.bCheckExposure)
    {
        if (M.BlownFrac > P.BlownFracMax) Flags |= HMC_Issue_Overexposed;
        if (M.MeanLuma  < P.MeanLumaMin)  Flags |= HMC_Issue_Underexposed;
    }

    if (P.bCheckLighting)
    {
        const bool bUneven = P.bClassifyLightDir ? (M.LightDir != EHMCLightDir::Even)
                                                 : (M.RegionSpread > P.RegionSpreadMax);
        if (bUneven) Flags |= HMC_Issue_UnevenLight;
    }

    if (P.bCheckFraming)
    {
        // Absolute size band (independent of the captured reference): too close / far.
        if (M.SubjectSize > P.FramingSizeMax) Flags |= HMC_Issue_TooClose;
        if (M.SubjectSize < P.FramingSizeMin) Flags |= HMC_Issue_TooFar;

        // Drift from the captured reference (only once it has been set).
        if (Ref.bSet)
        {
            const float Drift     = (float)FVector2D::Distance(M.SubjectCenter, Ref.Center);
            const float SizeDelta = FMath::Abs(M.SubjectSize - Ref.Size);
            if (Drift > P.FramingDriftTol || SizeDelta > P.FramingDriftTol * 1.5f)
                Flags |= HMC_Issue_FramingDrift;
        }
    }
    return Flags;
}
