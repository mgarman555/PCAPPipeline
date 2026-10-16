#include "Devices/MarkerlessBodyCamera.h"

#include "PCAPTakeOffload.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogPCAPBodyCam, Log, All);

// ─── FFmpeg progress ──────────────────────────────────────────────────────────

bool FPCAPFfmpegProgress::ParseLine(const FString& Line)
{
    FString Key, Value;
    if (!Line.TrimStartAndEnd().Split(TEXT("="), &Key, &Value)) return false;
    Key.TrimStartAndEndInline();
    Value.TrimStartAndEndInline();
    if (Key == TEXT("frame"))       { Frame = FCString::Atoi(*Value); return true; }
    if (Key == TEXT("fps"))         { Fps   = FCString::Atof(*Value); return true; }
    if (Key == TEXT("dup_frames"))  { Dup   = FCString::Atoi(*Value); return true; }
    if (Key == TEXT("drop_frames")) { Drop  = FCString::Atoi(*Value); return true; }
    if (Key == TEXT("progress"))    { bEnded = (Value == TEXT("end")); return true; }
    return false;
}

// ─── MJPEG reassembly ─────────────────────────────────────────────────────────

void FPCAPMjpegAssembler::Feed(const uint8* Data, int32 Num)
{
    if (Num <= 0) return;
    Buffer.Append(Data, Num);
    if (Buffer.Num() > 8 * 1024 * 1024) Buffer.Reset();   // garbage stream: start over

    for (;;)
    {
        int32 Soi = INDEX_NONE;
        for (int32 i = 0; i + 1 < Buffer.Num(); ++i)
            if (Buffer[i] == 0xFF && Buffer[i + 1] == 0xD8) { Soi = i; break; }
        if (Soi == INDEX_NONE)
        {
            // Keep a trailing 0xFF: it may be the first half of the next SOI.
            const bool bKeep = Buffer.Num() > 0 && Buffer.Last() == 0xFF;
            Buffer.Reset();
            if (bKeep) Buffer.Add(0xFF);
            return;
        }

        int32 Eoi = INDEX_NONE;
        for (int32 i = Soi + 2; i + 1 < Buffer.Num(); ++i)
            if (Buffer[i] == 0xFF && Buffer[i + 1] == 0xD9) { Eoi = i; break; }
        if (Eoi == INDEX_NONE)
        {
            if (Soi > 0) Buffer.RemoveAt(0, Soi, EAllowShrinking::No);
            return;
        }

        Latest = TArray<uint8>(Buffer.GetData() + Soi, Eoi + 2 - Soi);
        ++Frames;
        Buffer.RemoveAt(0, Eoi + 2, EAllowShrinking::No);
    }
}

// ─── Adapter ──────────────────────────────────────────────────────────────────

FMarkerlessBodyCamera::FMarkerlessBodyCamera()
{
    Health.VendorId = StaticVendorId();
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FMarkerlessBodyCamera::Tick), 0.05f);
}

FMarkerlessBodyCamera::~FMarkerlessBodyCamera()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    Disconnect();
}

FString FMarkerlessBodyCamera::RecordFilePath(const FPCAPTakeIdentity& Take)
{
    return PCAPOffload::BodyTakeDir(Take.CaptureFolder, Take.TakeID) / (Take.TakeID + TEXT("_body.mp4"));
}

FString FMarkerlessBodyCamera::BuildArgs(const FString& InDevice, const FPCAPBodyCameraMode& InMode,
    const FString& InRecordFile, int32 InPreviewPort, int32 PreviewFps)
{
    // -n: never overwrite an existing take file. -progress on stderr feeds health.
    const FString Input = FString::Printf(
        TEXT("-hide_banner -nostats -loglevel error -n -progress pipe:2 -stats_period 0.5 ")
        TEXT("-f dshow -rtbufsize 1024M -vcodec mjpeg -video_size %dx%d -framerate %d -i video=\"%s\" "),
        InMode.Width, InMode.Height, InMode.Fps, *InDevice);
    const FString PreviewOut = FString::Printf(
        TEXT("-c:v mjpeg -q:v 5 -f mjpeg \"udp://127.0.0.1:%d?pkt_size=1316\""), InPreviewPort);

    if (InRecordFile.IsEmpty())
        return Input + FString::Printf(TEXT("-vf fps=%d,scale=640:-2 "), PreviewFps) + PreviewOut;

    // body-camera.md §5 recipe: one owner, two outputs. 1 s GOP + flush keeps a killed file readable.
    return Input
        + FString::Printf(TEXT("-filter_complex \"[0:v]split=2[rec][pv];[pv]fps=%d,scale=640:-2[pvo]\" "), PreviewFps)
        + FString::Printf(TEXT("-map \"[rec]\" -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p -g %d -flush_packets 1 ")
                          TEXT("-movflags +frag_keyframe+empty_moov+default_base_moof \"%s\" "), InMode.Fps, *InRecordFile)
        + TEXT("-map \"[pvo]\" ") + PreviewOut;
}

void FMarkerlessBodyCamera::Connect(const FPCAPDeviceAddress& Address)
{
    Disconnect();
    Device = Address.Host;
    PreviewPort = Address.Port > 0 ? Address.Port : 5005;
    Health.DeviceName = Device;
    Health.LastError.Reset();
    if (Device.IsEmpty())
    {
        Health.LastError = TEXT("No camera name set.");
        return;
    }
    OpenPreviewSocket();
    if (LaunchProcess(BuildArgs(Device, Mode, FString(), PreviewPort)))
        State = EState::Previewing;
}

void FMarkerlessBodyCamera::Disconnect()
{
    StopProcess(3.f);
    CloseSocket();
    State = EState::Idle;
    Health.bConnected = false;
    Health.bRecording = false;
}

void FMarkerlessBodyCamera::RequestPreviewFrame(int32 CameraIndex, FOnPCAPPreviewFrame OnFrame)
{
    static const TArray<uint8> Empty;
    const bool bFresh = CameraIndex == 0 && Assembler.HasFrame()
        && FPlatformTime::Seconds() - LastFrameSeconds < 2.0;
    OnFrame.ExecuteIfBound(bFresh, bFresh ? 200 : -1, bFresh ? Assembler.GetLatest() : Empty);
}

void FMarkerlessBodyCamera::StartRecord(const FPCAPTakeIdentity& Take)
{
    if (IsRecording())
    {
        Health.LastError = TEXT("Already recording.");
        return;
    }
    if (Device.IsEmpty() || Take.CaptureFolder.IsEmpty() || Take.TakeID.IsEmpty())
    {
        Health.LastError = TEXT("Cannot record: camera or take folder not set.");
        return;
    }
    const FString File = RecordFilePath(Take);
    if (IFileManager::Get().FileExists(*File))
    {
        Health.LastError = FString::Printf(TEXT("%s already exists. Not overwriting it."), *File);
        return;
    }

    // The camera has one owner: stop the preview process before opening it again to record.
    StopProcess(3.f);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    Progress = FPCAPFfmpegProgress();
    FrameSamples.Reset();
    MeasuredFps = 0.f;
    RecordFirstFrame = FDateTime(0);
    RecordFile = File;
    if (LaunchProcess(BuildArgs(Device, Mode, File, PreviewPort)))
    {
        State = EState::Recording;
        Health.bRecording = true;
        Health.LastError.Reset();
    }
}

void FMarkerlessBodyCamera::StopRecord()
{
    if (State != EState::Recording) return;
    // "q" lets FFmpeg finish the file (verification #19 / #32). Tick waits for the exit.
    FPlatformProcess::WritePipe(StdInWrite, TEXT("q"));
    State = EState::Stopping;
    StopRequestedSeconds = FPlatformTime::Seconds();
}

void FMarkerlessBodyCamera::ListTakeFiles(const FPCAPTakeIdentity& Take, FOnPCAPFileList OnList)
{
    TArray<FPCAPRemoteFile> Files;
    const FString File = RecordFilePath(Take);
    IFileManager& FM = IFileManager::Get();
    if (FM.FileExists(*File))
    {
        FPCAPRemoteFile F;
        F.Name = FPaths::GetCleanFilename(File);
        F.Locator = File;
        F.SizeBytes = FM.FileSize(*File);
        F.Modified = FM.GetTimeStamp(*File);
        Files.Add(F);
    }
    OnList.ExecuteIfBound(true, Files);
}

bool FMarkerlessBodyCamera::LaunchProcess(const FString& Args)
{
    FPlatformProcess::CreatePipe(StdInRead, StdInWrite, true);
    FPlatformProcess::CreatePipe(OutRead, OutWrite);
    OutPartial.Reset();
    Proc = FPlatformProcess::CreateProc(*FFmpegPath, *Args, false, true, true, nullptr, 0, nullptr,
        OutWrite, StdInRead, OutWrite);
    if (!Proc.IsValid())
    {
        ClosePipes();
        Health.LastError = FString::Printf(TEXT("Could not start FFmpeg (%s). Set the path in Project Settings > PCAP Tool."), *FFmpegPath);
        return false;
    }
    UE_LOG(LogPCAPBodyCam, Log, TEXT("ffmpeg %s"), *Args);
    return true;
}

void FMarkerlessBodyCamera::StopProcess(float TimeoutSec)
{
    if (!Proc.IsValid()) return;
    if (FPlatformProcess::IsProcRunning(Proc))
    {
        FPlatformProcess::WritePipe(StdInWrite, TEXT("q"));
        const double Until = FPlatformTime::Seconds() + TimeoutSec;
        while (FPlatformProcess::IsProcRunning(Proc) && FPlatformTime::Seconds() < Until)
        {
            FPlatformProcess::ReadPipe(OutRead);   // keep the pipe drained so FFmpeg can exit
            FPlatformProcess::Sleep(0.02f);
        }
        if (FPlatformProcess::IsProcRunning(Proc))
        {
            UE_LOG(LogPCAPBodyCam, Warning, TEXT("FFmpeg did not stop in %.1f s; killing it."), TimeoutSec);
            FPlatformProcess::TerminateProc(Proc, true);
        }
    }
    FPlatformProcess::CloseProc(Proc);
    Proc.Reset();
    ClosePipes();
}

void FMarkerlessBodyCamera::ClosePipes()
{
    if (StdInRead || StdInWrite) FPlatformProcess::ClosePipe(StdInRead, StdInWrite);
    if (OutRead || OutWrite)     FPlatformProcess::ClosePipe(OutRead, OutWrite);
    StdInRead = StdInWrite = OutRead = OutWrite = nullptr;
}

void FMarkerlessBodyCamera::OpenPreviewSocket()
{
    CloseSocket();
    Socket = FUdpSocketBuilder(TEXT("PCAPBodyPreview"))
        .AsNonBlocking()
        .AsReusable()
        .BoundToAddress(FIPv4Address(127, 0, 0, 1))
        .BoundToPort(PreviewPort)
        .WithReceiveBufferSize(4 * 1024 * 1024)
        .Build();
    if (!Socket) Health.LastError = FString::Printf(TEXT("Could not open preview port %d."), PreviewPort);
}

void FMarkerlessBodyCamera::CloseSocket()
{
    if (!Socket) return;
    Socket->Close();
    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
    Socket = nullptr;
}

bool FMarkerlessBodyCamera::Tick(float)
{
    Pump();
    return true;
}

void FMarkerlessBodyCamera::Pump()
{
    const double Now = FPlatformTime::Seconds();

    // FFmpeg output: -progress key=value lines; anything else is an error line (-loglevel error).
    if (OutRead)
    {
        OutPartial += FPlatformProcess::ReadPipe(OutRead);
        int32 Nl;
        while (OutPartial.FindChar(TEXT('\n'), Nl))
        {
            const FString Line = OutPartial.Left(Nl).TrimEnd();
            OutPartial.RightChopInline(Nl + 1);
            if (Line.IsEmpty()) continue;
            const int32 FrameBefore = Progress.Frame;
            if (!Progress.ParseLine(Line) && !Line.Contains(TEXT("=")))
            {
                Health.LastError = Line;
                UE_LOG(LogPCAPBodyCam, Warning, TEXT("%s"), *Line);
            }
            if (Progress.Frame != FrameBefore) FrameSamples.Emplace(Now, Progress.Frame);
        }
        if (State == EState::Recording && Progress.Frame > 0 && RecordFirstFrame.GetTicks() == 0)
            RecordFirstFrame = FDateTime::UtcNow();

        // Rate over the last ~3 s of samples (needs at least 1 s of span).
        FrameSamples.RemoveAll([Now](const TPair<double, int32>& S) { return Now - S.Key > 3.0; });
        if (FrameSamples.Num() >= 2 && FrameSamples.Last().Key - FrameSamples[0].Key >= 1.0)
            MeasuredFps = float(FrameSamples.Last().Value - FrameSamples[0].Value) / float(FrameSamples.Last().Key - FrameSamples[0].Key);
    }

    // Preview packets.
    if (Socket)
    {
        uint8 Buf[65536];
        uint32 Pending = 0;
        while (Socket->HasPendingData(Pending))
        {
            int32 Read = 0;
            if (!Socket->Recv(Buf, sizeof(Buf), Read) || Read <= 0) break;
            const int32 Before = Assembler.GetFrameCount();
            Assembler.Feed(Buf, Read);
            if (Assembler.GetFrameCount() != Before) LastFrameSeconds = Now;
        }
    }

    // Process lifecycle.
    const bool bRunning = Proc.IsValid() && FPlatformProcess::IsProcRunning(Proc);
    if (State == EState::Stopping)
    {
        if (!bRunning || Now - StopRequestedSeconds > 8.0)
        {
            StopProcess(0.f);   // already exited, or kill after 8 s
            Health.bRecording = false;
            State = EState::Idle;
            if (LaunchProcess(BuildArgs(Device, Mode, FString(), PreviewPort)))
                State = EState::Previewing;
        }
    }
    else if ((State == EState::Previewing || State == EState::Recording) && !bRunning)
    {
        const bool bWasRecording = State == EState::Recording;
        StopProcess(0.f);
        State = EState::Idle;
        Health.bRecording = false;
        if (bWasRecording)
            Health.LastError = TEXT("Body camera stopped during the take.") + (Health.LastError.IsEmpty() ? FString() : TEXT(" ") + Health.LastError);
        else if (Health.LastError.IsEmpty())
            Health.LastError = TEXT("Body camera preview stopped.");
    }

    Health.bConnected = Assembler.HasFrame() && Now - LastFrameSeconds < 2.0;
    Health.FPS = IsRecording() ? MeasuredFps : 0.f;
    Health.DroppedFrames = IsRecording() ? Progress.Dup + Progress.Drop : 0;
    Health.LastUpdate = FDateTime::UtcNow();
}
