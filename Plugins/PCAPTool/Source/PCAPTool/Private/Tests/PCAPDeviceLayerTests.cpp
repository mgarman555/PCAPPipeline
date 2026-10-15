#include "Misc/AutomationTest.h"
#include "PCAPToolTypes.h"
#include "PCAPDeviceRegistry.h"
#include "Devices/TechnopropsFacialDevice.h"

#if WITH_DEV_AUTOMATION_TESTS

// The ORION URLs are byte-identical to what UPCAPToolSubsystem built before the Phase 1
// move (formats copied from main@a15879d PCAPToolSubsystem.cpp).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTechnopropsUrlTest,
    "PCAP.Devices.TechnopropsUrls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTechnopropsUrlTest::RunTest(const FString&)
{
    const FString Ip = TEXT("192.168.50.117");
    TestEqual(TEXT("status"), FTechnopropsFacialDevice::BuildStatusUrl(Ip),
        FString(TEXT("http://192.168.50.117/control?cmd=no&param=")));
    TestEqual(TEXT("command"), FTechnopropsFacialDevice::BuildCommandUrl(Ip, TEXT("startrecording")),
        FString(TEXT("http://192.168.50.117/control?cmd=startrecording&param=")));
    TestEqual(TEXT("device command, no extra"),
        FTechnopropsFacialDevice::BuildDeviceCommandUrl(Ip, TEXT("exposure"), TEXT("4550"), FString(), FString(), 42),
        FString(TEXT("http://192.168.50.117/control?cmd=exposure&param=4550&_=42")));
    TestEqual(TEXT("device command, extra"),
        FTechnopropsFacialDevice::BuildDeviceCommandUrl(Ip, TEXT("setgain"), TEXT("2"), TEXT("cam"), TEXT("1"), 7),
        FString(TEXT("http://192.168.50.117/control?cmd=setgain&param=2&cam=1&_=7")));
    TestEqual(TEXT("video cam0"), FTechnopropsFacialDevice::BuildVideoUrl(Ip, 0),
        FString(TEXT("http://192.168.50.117/video?cam=0")));
    TestEqual(TEXT("video cam1"), FTechnopropsFacialDevice::BuildVideoUrl(Ip, 1),
        FString(TEXT("http://192.168.50.117/video?cam=1")));
    return true;
}

// control.json parse fills the same FHMCDeviceStatus fields the subsystem used to.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTechnopropsParseTest,
    "PCAP.Devices.TechnopropsParseControlJson",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTechnopropsParseTest::RunTest(const FString&)
{
    const FString Body = TEXT("{\"nRecording\":1,\"batteryVoltage\":15.4,\"availableStorageInMB\":20480,"
        "\"cpuUsage\":35,\"cpuTemp\":41,\"frameRate\":60,\"skippedFrames0\":2,\"skippedFrames1\":0,"
        "\"exposure0\":4550,\"exposure1\":4600,\"gain0\":2,\"gain1\":3,\"topLights\":80,\"bottomLights\":70,"
        "\"streaming0\":1,\"streaming1\":0,\"width\":2048,\"height\":1536,\"rotation0\":90,\"rotation1\":270,"
        "\"boomPos\":1,\"takename\":\"T001\",\"lastMovieIntegrityStatus\":\"Ready\"}");

    FHMCDeviceStatus S;
    S.DeviceName = TEXT("ORION");
    TestTrue(TEXT("parses"), FTechnopropsFacialDevice::ParseControlJson(Body, S));
    TestTrue(TEXT("recording"), S.bIsRecording);
    TestEqual(TEXT("battery"), S.BatteryVoltage, 15.4f);
    TestEqual(TEXT("storage"), S.AvailableStorageMB, 20480.f);
    TestEqual(TEXT("cpu"), S.CPUUsagePercent, 35.f);
    TestEqual(TEXT("temp"), S.TemperatureCelsius, 41.f);
    TestEqual(TEXT("fps"), S.FPS, 60.f);
    TestEqual(TEXT("dropped0"), S.DroppedFrames0, 2);
    TestEqual(TEXT("exposure1"), S.Exposure1, 4600);
    TestEqual(TEXT("gain1"), S.Gain1, 3);
    TestEqual(TEXT("top lights"), S.TopLights, 80);
    TestEqual(TEXT("bottom lights"), S.BottomLights, 70);
    TestTrue(TEXT("streaming0"), S.bStreaming0);
    TestFalse(TEXT("streaming1"), S.bStreaming1);
    TestEqual(TEXT("width"), S.FrameWidth, 2048);
    TestEqual(TEXT("rotation1"), S.Rotation1, 270);
    TestEqual(TEXT("boom"), S.BoomPos, 1);
    TestEqual(TEXT("take"), S.CurrentTakeName, FString(TEXT("T001")));
    TestEqual(TEXT("clip"), S.LastClipStatus, FString(TEXT("Ready")));
    TestEqual(TEXT("name untouched"), S.DeviceName, FString(TEXT("ORION")));

    // Missing fields reset to 0 (same as before: Val = 0.0 before each read).
    FHMCDeviceStatus S2 = S;
    TestTrue(TEXT("parses sparse"), FTechnopropsFacialDevice::ParseControlJson(TEXT("{\"frameRate\":30}"), S2));
    TestEqual(TEXT("sparse fps"), S2.FPS, 30.f);
    TestEqual(TEXT("sparse exposure reset"), S2.Exposure0, 0);
    TestEqual(TEXT("sparse take kept"), S2.CurrentTakeName, FString(TEXT("T001")));

    FHMCDeviceStatus S3 = S;
    TestFalse(TEXT("garbage rejected"), FTechnopropsFacialDevice::ParseControlJson(TEXT("not json"), S3));
    TestEqual(TEXT("garbage leaves status"), S3.Exposure0, 4550);
    return true;
}

// Registry lists the built-in Technoprops adapter and creates it; unknown vendors are null.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPDeviceRegistryCatalogTest,
    "PCAP.Devices.RegistryCatalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPDeviceRegistryCatalogTest::RunTest(const FString&)
{
    TestTrue(TEXT("Technoprops listed"), UPCAPDeviceRegistry::GetFacialVendors().Contains(FName(TEXT("Technoprops"))));
    TestTrue(TEXT("Technoprops installed"), UPCAPDeviceRegistry::IsVendorInstalled(FName(TEXT("Technoprops"))));

    const TSharedPtr<IPCAPFacialDevice> Dev = UPCAPDeviceRegistry::CreateFacialDevice(FName(TEXT("Technoprops")));
    TestTrue(TEXT("created"), Dev.IsValid());
    if (Dev.IsValid())
    {
        TestEqual(TEXT("vendor id"), Dev->VendorId(), FName(TEXT("Technoprops")));
        TestEqual(TEXT("two preview cams"), Dev->NumPreviewCameras(), 2);
        TestTrue(TEXT("stereo supported"), Dev->SupportsConfig(ECaptureConfiguration::StereoHeadMount));
        TestFalse(TEXT("body camera not supported"), Dev->SupportsConfig(ECaptureConfiguration::FixedBodyCamera));
        TestFalse(TEXT("health starts disconnected"), Dev->GetHealth().bConnected);
    }

    TestFalse(TEXT("OneHMC not installed yet (Phase 2)"), UPCAPDeviceRegistry::IsVendorInstalled(FName(TEXT("OneHMC"))));
    TestFalse(TEXT("unknown vendor null"), UPCAPDeviceRegistry::CreateFacialDevice(FName(TEXT("NoSuchVendor"))).IsValid());
    return true;
}

// ORION status -> vendor-neutral health: connection, sums, unknown battery %, offline reason.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPTechnopropsHealthTest,
    "PCAP.Devices.TechnopropsHealth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPTechnopropsHealthTest::RunTest(const FString&)
{
    FTechnopropsFacialDevice Dev;
    Dev.Connect(FPCAPDeviceAddress(TEXT("192.168.50.117")));

    FHMCDeviceStatus S;
    S.DeviceName = TEXT("ORION");
    S.ConnectionState = EHMCConnectionState::Connected;
    S.DroppedFrames0 = 2; S.DroppedFrames1 = 3;
    S.AvailableStorageMB = 1024.f; S.FPS = 60.f;
    S.IssueFlags0 = HMC_Issue_LowBattery; S.IssueFlags1 = HMC_Issue_DroppedFrames;
    Dev.UpdateHealth(S);
    FPCAPDeviceHealth H = Dev.GetHealth();
    TestTrue(TEXT("connected"), H.bConnected);
    TestEqual(TEXT("vendor"), H.VendorId, FName(TEXT("Technoprops")));
    TestEqual(TEXT("name"), H.DeviceName, FString(TEXT("ORION")));
    TestEqual(TEXT("dropped summed"), H.DroppedFrames, 5);
    TestTrue(TEXT("battery % unknown"), H.BatteryPercent < 0.f);
    TestEqual(TEXT("flags OR'd"), H.IssueFlags, (int32)(HMC_Issue_LowBattery | HMC_Issue_DroppedFrames));
    TestTrue(TEXT("no error while connected"), H.LastError.IsEmpty());

    S.ConnectionState = EHMCConnectionState::Offline;
    Dev.UpdateHealth(S);
    TestFalse(TEXT("offline not connected"), Dev.GetHealth().bConnected);
    TestFalse(TEXT("offline has reason"), Dev.GetHealth().LastError.IsEmpty());

    S.ConnectionState = EHMCConnectionState::Connected;
    Dev.UpdateHealth(S);
    TestTrue(TEXT("reason cleared on reconnect"), Dev.GetHealth().LastError.IsEmpty());
    return true;
}

// Saved assets store enums by index: existing values must keep theirs and new ones append.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPEnumAppendOnlyTest,
    "PCAP.DataModel.EnumAppendOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPEnumAppendOnlyTest::RunTest(const FString&)
{
    TestEqual(TEXT("EBodySystem::Giant"),           (int32)EBodySystem::Giant, 4);
    TestEqual(TEXT("EBodySystem::MarkerlessVideo"), (int32)EBodySystem::MarkerlessVideo, 5);
    TestEqual(TEXT("EFaceSystem::ARKit"),           (int32)EFaceSystem::ARKit, 2);
    TestEqual(TEXT("EFaceSystem::OneHMC"),          (int32)EFaceSystem::OneHMC, 3);
    TestEqual(TEXT("EFaceSystem::Faceware"),        (int32)EFaceSystem::Faceware, 4);
    TestEqual(TEXT("EAudioSystem::Timecode"),       (int32)EAudioSystem::Timecode, 3);
    TestEqual(TEXT("EAudioSystem::UsbAudioInput"),  (int32)EAudioSystem::UsbAudioInput, 4);
    TestEqual(TEXT("ECapturePipeline::FaceWareHMC"),    (int32)ECapturePipeline::FaceWareHMC, 1);
    TestEqual(TEXT("ECapturePipeline::MarkerlessBody"), (int32)ECapturePipeline::MarkerlessBody, 2);
    TestEqual(TEXT("ECaptureConfiguration::PhoneHeadMount"),  (int32)ECaptureConfiguration::PhoneHeadMount, 3);
    TestEqual(TEXT("ECaptureConfiguration::FixedBodyCamera"), (int32)ECaptureConfiguration::FixedBodyCamera, 4);

    FTake T;
    TestFalse(TEXT("new take not flagged"), T.bAutoFlagged);
    TestEqual(TEXT("new take syncs by record start"), T.Sync, ESyncMethod::RecordStart);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
