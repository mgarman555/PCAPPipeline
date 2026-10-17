#include "Misc/AutomationTest.h"
#include "Devices/MarkerlessBodyCamera.h"
#include "PCAPDeviceRegistry.h"
#include "PCAPTakeOffload.h"
#include "PCAPToolSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PCAPBodyCamTest
{
    static void AddJpeg(TArray<uint8>& Out, uint8 Fill, int32 Len)
    {
        Out.Append({ 0xFF, 0xD8 });
        for (int32 i = 0; i < Len; ++i) Out.Add(Fill);
        Out.Append({ 0xFF, 0xD9 });
    }
}

// FFmpeg command lines match the verified recipe (body-camera.md §5, §8).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPBodyCamArgsTest,
    "PCAP.Devices.BodyCamera.Args",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPBodyCamArgsTest::RunTest(const FString&)
{
    const FPCAPBodyCameraMode Mode;   // C920S default
    TestEqual(TEXT("default mode 720p30"), FString::Printf(TEXT("%dx%d@%d"), Mode.Width, Mode.Height, Mode.Fps), FString(TEXT("1280x720@30")));

    const FString Pv = FMarkerlessBodyCamera::BuildArgs(TEXT("HD Pro Webcam C920"), Mode, FString(), 5005);
    TestTrue(TEXT("dshow device quoted"), Pv.Contains(TEXT("-i video=\"HD Pro Webcam C920\"")));
    TestTrue(TEXT("mode"), Pv.Contains(TEXT("-vcodec mjpeg -video_size 1280x720 -framerate 30")));
    TestTrue(TEXT("preview to localhost udp"), Pv.Contains(TEXT("udp://127.0.0.1:5005?pkt_size=1316")));
    TestTrue(TEXT("preview 10 fps"), Pv.Contains(TEXT("fps=10,scale=640:-2")));
    TestFalse(TEXT("preview-only does not encode a file"), Pv.Contains(TEXT("libx264")));
    TestTrue(TEXT("never overwrite"), Pv.Contains(TEXT(" -n ")));
    TestTrue(TEXT("progress on stderr"), Pv.Contains(TEXT("-progress pipe:2")));

    const FString Rec = FMarkerlessBodyCamera::BuildArgs(TEXT("HD Pro Webcam C920"), Mode, TEXT("C:/cap/x_body.mp4"), 5005);
    TestTrue(TEXT("one owner, two outputs"), Rec.Contains(TEXT("split=2[rec][pv]")));
    TestTrue(TEXT("CRF 18"), Rec.Contains(TEXT("-c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p -g 30 -flush_packets 1")));
    TestTrue(TEXT("fragmented MP4"), Rec.Contains(TEXT("+frag_keyframe+empty_moov+default_base_moof")));
    TestTrue(TEXT("file quoted"), Rec.Contains(TEXT("\"C:/cap/x_body.mp4\"")));
    TestTrue(TEXT("preview still on"), Rec.Contains(TEXT("udp://127.0.0.1:5005")));

    // Record path lands where Mono Video Ingest and the offload layout expect it.
    FPCAPTakeIdentity T; T.CaptureFolder = TEXT("C:/cap/testing/Day_001"); T.TakeID = TEXT("001001_002");
    TestEqual(TEXT("record path"), FMarkerlessBodyCamera::RecordFilePath(T),
        FString(TEXT("C:/cap/testing/Day_001/body/001001_002/001001_002_body.mp4")));
    TestTrue(TEXT("ingest-safe file name"), PCAPOffload::IsIngestSafeName(FPaths::GetBaseFilename(FMarkerlessBodyCamera::RecordFilePath(T))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPBodyCamProgressTest,
    "PCAP.Devices.BodyCamera.Progress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPBodyCamProgressTest::RunTest(const FString&)
{
    FPCAPFfmpegProgress P;
    for (const TCHAR* L : { TEXT("frame=657"), TEXT("fps=29.86"), TEXT("dup_frames=3"), TEXT("drop_frames=1"), TEXT("progress=continue") })
        TestTrue(FString::Printf(TEXT("parses %s"), L), P.ParseLine(L));
    TestEqual(TEXT("frame"), P.Frame, 657);
    TestTrue(TEXT("fps"), FMath::IsNearlyEqual(P.Fps, 29.86f, 0.01f));
    TestEqual(TEXT("dup"), P.Dup, 3);
    TestEqual(TEXT("drop"), P.Drop, 1);
    TestFalse(TEXT("not ended"), P.bEnded);
    TestTrue(TEXT("end"), P.ParseLine(TEXT("progress=end")) && P.bEnded);
    TestFalse(TEXT("other keys ignored"), P.ParseLine(TEXT("bitrate=4163.0kbits/s")));
    TestFalse(TEXT("error text ignored"), P.ParseLine(TEXT("Could not find video device")));
    return true;
}

// Preview JPEGs arrive split over 1316-byte UDP packets; reassemble whole frames only.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPBodyCamMjpegTest,
    "PCAP.Devices.BodyCamera.MjpegReassembly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPBodyCamMjpegTest::RunTest(const FString&)
{
    TArray<uint8> Stream = { 0x00, 0x13, 0xFF };   // junk before the first frame
    PCAPBodyCamTest::AddJpeg(Stream, 0x11, 3000);
    PCAPBodyCamTest::AddJpeg(Stream, 0x22, 5000);
    TArray<uint8> Third; PCAPBodyCamTest::AddJpeg(Third, 0x33, 2000);
    Stream.Append(Third.GetData(), 1000);           // third frame cut off mid-way

    FPCAPMjpegAssembler A;
    for (int32 i = 0; i < Stream.Num(); i += 1316)
        A.Feed(Stream.GetData() + i, FMath::Min(1316, Stream.Num() - i));
    TestEqual(TEXT("two whole frames"), A.GetFrameCount(), 2);
    TestEqual(TEXT("latest is the second"), A.GetLatest().Num(), 5004);
    TestTrue(TEXT("latest starts with SOI"), A.GetLatest()[0] == 0xFF && A.GetLatest()[1] == 0xD8);
    TestEqual(TEXT("latest content"), (int32)A.GetLatest()[100], 0x22);

    A.Feed(Third.GetData() + 1000, Third.Num() - 1000);   // rest of the third frame
    TestEqual(TEXT("third completes"), A.GetFrameCount(), 3);
    TestEqual(TEXT("third content"), (int32)A.GetLatest()[100], 0x33);

    // An SOI marker split across two packets still lines up.
    FPCAPMjpegAssembler B;
    TArray<uint8> F; PCAPBodyCamTest::AddJpeg(F, 0x44, 10);
    B.Feed(F.GetData(), 1);
    B.Feed(F.GetData() + 1, F.Num() - 1);
    TestEqual(TEXT("split SOI"), B.GetFrameCount(), 1);

    TestTrue(TEXT("registered as a body vendor"), UPCAPDeviceRegistry::GetBodyVendors().Contains(FMarkerlessBodyCamera::StaticVendorId()));
    return true;
}

// Real hardware: records 6 s through the adapter. Skips (passes with a note) when the camera
// configured in Project Settings > PCAP Tool > Body Camera is not plugged in.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPBodyCamHardwareTest,
    "PCAP.Hardware.BodyCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPBodyCamHardwareTest::RunTest(const FString&)
{
    const UPCAPToolSettings* S = GetDefault<UPCAPToolSettings>();
    const FString FFmpeg = S->FFmpegPath.FilePath.IsEmpty() ? FString(TEXT("ffmpeg.exe")) : S->FFmpegPath.FilePath;
    const FString Device = S->BodyCameraDevice;

    int32 Rc = 0; FString Out, Err;
    FPlatformProcess::ExecProcess(*FFmpeg, TEXT("-hide_banner -list_devices true -f dshow -i dummy"), &Rc, &Out, &Err);
    if (!(Out + Err).Contains(FString::Printf(TEXT("\"%s\""), *Device)))
    {
        AddInfo(FString::Printf(TEXT("Skipped: camera \"%s\" not found (or FFmpeg not at %s)."), *Device, *FFmpeg));
        return true;
    }

    const FString Scratch = FPaths::ConvertRelativePathToFull(
        FPaths::AutomationTransientDir() / (TEXT("PCAPBodyCam_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    FPCAPTakeIdentity Take; Take.CaptureFolder = Scratch / TEXT("testing/Day_001"); Take.TakeID = TEXT("001001_001");

    TSharedPtr<IPCAPBodySource> Src = UPCAPDeviceRegistry::CreateBodySource(FMarkerlessBodyCamera::StaticVendorId());
    FMarkerlessBodyCamera* Cam = static_cast<FMarkerlessBodyCamera*>(Src.Get());
    if (!TestNotNull(TEXT("adapter"), Cam)) return false;

    auto PumpFor = [Cam](double Seconds, TFunction<bool()> Until)
    {
        const double End = FPlatformTime::Seconds() + Seconds;
        while (FPlatformTime::Seconds() < End)
        {
            Cam->Pump();
            if (Until && Until()) return true;
            FPlatformProcess::Sleep(0.02f);
        }
        return false;
    };

    Cam->Connect(FPCAPDeviceAddress(Device, 5097));
    TestTrue(TEXT("preview frames arrive"), PumpFor(8.0, [Cam]() { return Cam->GetPreviewFrameCount() >= 10; }));
    TestTrue(TEXT("connected"), Cam->GetHealth().bConnected);
    bool bGotFrame = false;
    Cam->RequestPreviewFrame(0, FOnPCAPPreviewFrame::CreateLambda([&bGotFrame](bool bOk, int32, const TArray<uint8>& Jpeg)
    { bGotFrame = bOk && Jpeg.Num() > 2 && Jpeg[0] == 0xFF && Jpeg[1] == 0xD8; }));
    TestTrue(TEXT("preview frame is a JPEG"), bGotFrame);

    Cam->StartRecord(Take);
    TestTrue(TEXT("recording"), Cam->IsRecording());
    PumpFor(6.0, nullptr);
    const int32 PreviewWhileRecording = Cam->GetPreviewFrameCount();
    TestTrue(TEXT("first frame time logged"), Cam->GetRecordFirstFrameTime().GetTicks() > 0);
    // Frame rate depends on the room: the C920 halves it in low light (body-camera.md §8).
    const float LiveFps = Cam->GetHealth().FPS;
    TestTrue(TEXT("health fps reported"), LiveFps > 5.f);
    if (LiveFps < 25.f) AddWarning(FString::Printf(TEXT("Body camera at %.1f fps while recording: add light (the C920 drops to ~15 fps in low light)."), LiveFps));
    Cam->StopRecord();
    TestTrue(TEXT("stops cleanly"), PumpFor(10.0, [Cam]() { return !Cam->IsRecording(); }));
    TestTrue(TEXT("preview resumes"), PumpFor(8.0, [Cam, PreviewWhileRecording]() { return Cam->GetPreviewFrameCount() > PreviewWhileRecording + 5; }));

    const FString File = FMarkerlessBodyCamera::RecordFilePath(Take);
    TestTrue(TEXT("file in the take folder"), IFileManager::Get().FileExists(*File));
    FString Probe;
    const FString FFprobe = FPaths::GetPath(FFmpeg).IsEmpty() ? FString(TEXT("ffprobe.exe")) : FPaths::GetPath(FFmpeg) / TEXT("ffprobe.exe");
    FPlatformProcess::ExecProcess(*FFprobe, *FString::Printf(TEXT("-v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames:format=duration -of default=nw=1 \"%s\""), *File), &Rc, &Probe, &Err);
    FString FramesStr, DurStr;
    for (const FString& L : TArray<FString>([&Probe]() { TArray<FString> A; Probe.ParseIntoArrayLines(A); return A; }()))
    {
        if (L.StartsWith(TEXT("nb_read_frames="))) FramesStr = L.RightChop(15);
        if (L.StartsWith(TEXT("duration="))) DurStr = L.RightChop(9);
    }
    const int32 Frames = FCString::Atoi(*FramesStr);
    const double Dur = FCString::Atod(*DurStr);
    AddInfo(FString::Printf(TEXT("Recorded %d frames / %.2f s = %.2f fps"), Frames, Dur, Dur > 0 ? Frames / Dur : 0.0));
    TestTrue(TEXT("file is readable and about 6 s"), Frames > 60 && Dur > 4.0);
    if (Dur > 0 && Frames / Dur < 29.0)
        AddWarning(FString::Printf(TEXT("Recorded %.1f fps, not 30: low light (see body-camera.md §8)."), Frames / Dur));
    TestTrue(TEXT("passes the body ingest check"), PCAPOffload::CheckIngestFolder(Take.CaptureFolder / TEXT("body"), 1).Num() == 0);

    int32 Listed = 0;
    Cam->ListTakeFiles(Take, FOnPCAPFileList::CreateLambda([&Listed](bool, const TArray<FPCAPRemoteFile>& F) { Listed = F.Num(); }));
    TestEqual(TEXT("listed"), Listed, 1);

    // Recording again into the same take must not overwrite it.
    Cam->StartRecord(Take);
    TestFalse(TEXT("refuses to overwrite"), Cam->IsRecording());

    Cam->Disconnect();
    TestTrue(TEXT("overwrite refusal reported"), Cam->GetHealth().LastError.Contains(TEXT("already exists")));
    IFileManager::Get().DeleteDirectory(*Scratch, false, true);
    return true;
}

#endif
