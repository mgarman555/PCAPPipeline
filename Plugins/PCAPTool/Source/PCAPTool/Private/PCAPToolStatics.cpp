#include "PCAPToolStatics.h"
#include "ActorRosterEntry.h"

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
        T.ShotID     = DayPadded + Slot;
        T.DayID      = DayID;
        T.TakeID     = GenerateTakeID(DayPadded, Slot, TakeNum);
        T.Label      = ETakeLabel::Captured;
        return T;
    };

    auto MakeShot = [&](const FString& Slot, EShotType Type, const FString& Desc, int32 PreseededTakes) -> FShot
    {
        FShot S;
        S.ShotID      = DayPadded + Slot;
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

int32 UPCAPToolStatics::EvaluateCameraIssues(const FHMCDeviceStatus& S, int32 CameraIndex)
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

    return Flags;
}

EHMCIssueSeverity UPCAPToolStatics::GetIssueSeverity(int32 Flags)
{
    const int32 RedMask =
        HMC_Issue_NotStreaming | HMC_Issue_Overexposed |
        HMC_Issue_LowBattery   | HMC_Issue_LowStorage   | HMC_Issue_HighTemp |
        HMC_Issue_NoFace       | HMC_Issue_OutOfFocus   | HMC_Issue_FramingDrift;

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
    if (Flags & HMC_Issue_FramingDrift)  return TEXT("Framing drifted · Re-seat rig / reposition");
    if (Flags & HMC_Issue_OutOfFocus)    return TEXT("Out of focus · Adjust lens");
    if (Flags & HMC_Issue_LowBattery)    return TEXT("Battery low · Eyes on it");
    if (Flags & HMC_Issue_LowStorage)    return TEXT("Storage critical · Wrap soon");
    if (Flags & HMC_Issue_HighTemp)      return TEXT("Overheating · Check rig");
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

bool UPCAPToolStatics::FrameHasSubject(const TArray<uint8>& BGRA, int32 Width, int32 Height)
{
    const int32 NumBytes = BGRA.Num();
    if (NumBytes < 4 || Width <= 0 || Height <= 0) return false;

    // Sample luminance across the frame (every ~64th pixel) — cheap at any res.
    const int32 Step = 64 * 4;   // BGRA = 4 bytes/pixel
    double Sum = 0.0;
    int32 Count = 0;
    for (int32 i = 0; i + 2 < NumBytes; i += Step)
    {
        const double B = BGRA[i];
        const double G = BGRA[i + 1];
        const double R = BGRA[i + 2];
        Sum += 0.114 * B + 0.587 * G + 0.299 * R;   // Rec.601 luma
        ++Count;
    }
    if (Count == 0) return false;

    const double MeanLuma = Sum / Count;

    // IR head-cams light the face brightly when it's in the box; an empty or
    // averted frame is markedly darker. TUNE this on the real feed — start ~40/255.
    const double MeanLumaThreshold = 40.0;
    return MeanLuma > MeanLumaThreshold;
}

// ── Automatic image analysis (Capture Monitor) ──────────────────────────────

FHMCImageMetrics UPCAPToolStatics::AnalyzeFrameBGRA(const TArray<uint8>& BGRA, int32 Width, int32 Height)
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

    // Focus = variance of the Laplacian over the grid, exposure-normalized.
    double LapSum = 0.0, LapSq = 0.0; int32 LapN = 0;
    for (int32 gy = 1; gy < GH - 1; ++gy)
        for (int32 gx = 1; gx < GW - 1; ++gx)
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

    M.bHasSubject = (Mean > 40.0);   // matches FrameHasSubject's luma threshold
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

    if (P.bCheckLighting && M.RegionSpread > P.RegionSpreadMax)
        Flags |= HMC_Issue_UnevenLight;

    if (P.bCheckFraming && Ref.bSet)
    {
        const float Drift     = (float)FVector2D::Distance(M.SubjectCenter, Ref.Center);
        const float SizeDelta = FMath::Abs(M.SubjectSize - Ref.Size);
        if (Drift > P.FramingDriftTol || SizeDelta > P.FramingDriftTol * 1.5f)
            Flags |= HMC_Issue_FramingDrift;
    }
    return Flags;
}
