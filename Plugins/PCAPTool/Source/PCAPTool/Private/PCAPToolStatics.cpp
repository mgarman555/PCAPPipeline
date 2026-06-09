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
        HMC_Issue_NoFace;

    const int32 AmberMask =
        HMC_Issue_Underexposed | HMC_Issue_DroppedFrames | HMC_Issue_ClipNotReady |
        HMC_Issue_HighCPU      |
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
    if (Flags & HMC_Issue_LowBattery)    return TEXT("Battery low · Eyes on it");
    if (Flags & HMC_Issue_LowStorage)    return TEXT("Storage critical · Wrap soon");
    if (Flags & HMC_Issue_HighTemp)      return TEXT("Overheating · Check rig");
    // Amber (warning) issues
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
