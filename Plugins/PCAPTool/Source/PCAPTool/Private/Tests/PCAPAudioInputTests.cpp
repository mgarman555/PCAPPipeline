#include "Misc/AutomationTest.h"
#include "Devices/UsbAudioInput.h"
#include "PCAPDeviceRegistry.h"
#include "PCAPToolSettings.h"
#include "AudioCaptureCore.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "TakeRecorderSources.h"
#include "TakeRecorderMicrophoneAudioManager.h"
#include "TakeRecorderMicrophoneAudioSource.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PCAPAudioTest
{
    // The configured input if it is present, else the Focusrite (a known-good input on the P620).
    static FString PickInput()
    {
        Audio::FAudioCapture Enum;
        TArray<Audio::FCaptureDeviceInfo> Devices;
        Enum.GetCaptureDevicesAvailable(Devices);
        const FString Configured = GetDefault<UPCAPToolSettings>()->AudioInputDevice;
        for (const Audio::FCaptureDeviceInfo& D : Devices)
            if (!Configured.IsEmpty() && D.DeviceName.Equals(Configured, ESearchCase::IgnoreCase)) return D.DeviceName;
        for (const Audio::FCaptureDeviceInfo& D : Devices)
            if (D.DeviceName.Contains(TEXT("Focusrite"))) return D.DeviceName;
        return FString();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPAudioLevelTest,
    "PCAP.Devices.AudioInput.Level",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPAudioLevelTest::RunTest(const FString&)
{
    // Interleaved stereo: channel 1 peaks at 0.5, channel 2 at -1.0.
    const float Block[] = { 0.1f, -0.2f,  -0.5f, 0.3f,  0.25f, -1.0f };
    TestTrue(TEXT("ch1 peak"), FMath::IsNearlyEqual(FPCAPAudioLevel::BlockPeak(Block, 3, 2, 1), 0.5f));
    TestTrue(TEXT("ch2 peak"), FMath::IsNearlyEqual(FPCAPAudioLevel::BlockPeak(Block, 3, 2, 2), 1.0f));
    TestEqual(TEXT("channel out of range"), FPCAPAudioLevel::BlockPeak(Block, 3, 2, 3), 0.f);
    TestEqual(TEXT("channel 0 is none"), FPCAPAudioLevel::BlockPeak(Block, 3, 2, 0), 0.f);
    TestTrue(TEXT("full scale = 0 dB"), FMath::IsNearlyEqual(FPCAPAudioLevel::ToDb(1.f), 0.f, 0.01f));
    TestTrue(TEXT("half = -6 dB"), FMath::IsNearlyEqual(FPCAPAudioLevel::ToDb(0.5f), -6.02f, 0.05f));
    TestEqual(TEXT("silence floor"), FPCAPAudioLevel::ToDb(0.f), -120.f);

    TestTrue(TEXT("registered as an audio vendor"), UPCAPDeviceRegistry::IsVendorInstalled(FUsbAudioInput::StaticVendorId()));
    FUsbAudioInput Missing;
    Missing.Connect(FPCAPDeviceAddress(TEXT("No Such Input 12345")));
    TestFalse(TEXT("missing input not connected"), Missing.GetHealth().bConnected);
    TestTrue(TEXT("missing input says so"), Missing.GetHealth().LastError.Contains(TEXT("not found")));
    TestFalse(TEXT("cannot arm without a device"), Missing.ArmTakeRecorderSource(NewObject<UTakeRecorderSources>()));
    return true;
}

// Real input: metering stream opens and delivers, and arming adds a Take Recorder mic source
// on the right device and channel. Skips when neither the configured input nor a Focusrite exists.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPAudioInputHardwareTest,
    "PCAP.Hardware.AudioInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPAudioInputHardwareTest::RunTest(const FString&)
{
    const FString Name = PCAPAudioTest::PickInput();
    if (Name.IsEmpty()) { AddInfo(TEXT("Skipped: no configured audio input and no Focusrite.")); return true; }

    UTakeRecorderMicrophoneAudioManager* Manager = GetMutableDefault<UTakeRecorderMicrophoneAudioManager>();
    const FAudioInputDeviceProperty Before = Manager->GetAudioInputDevice();

    FUsbAudioInput In;
    In.SetChannel(1);
    In.Connect(FPCAPDeviceAddress(Name));
    const double End = FPlatformTime::Seconds() + 3.0;
    while (FPlatformTime::Seconds() < End && !In.GetHealth().bConnected) { In.Pump(); FPlatformProcess::Sleep(0.05f); }
    TestTrue(FString::Printf(TEXT("%s delivers audio"), *Name), In.GetHealth().bConnected);
    TestTrue(TEXT("sample rate known"), In.GetSampleRate() > 0);
    TestTrue(TEXT("has channels"), In.GetDeviceChannels() >= 1);
    TestFalse(TEXT("endpoint id known"), In.GetDeviceId().IsEmpty());
    AddInfo(FString::Printf(TEXT("%s: %d ch, %d Hz, level %.1f dB"), *Name, In.GetDeviceChannels(), In.GetSampleRate(), In.GetLevelDb()));

    UTakeRecorderSources* Sources = NewObject<UTakeRecorderSources>();
    TestTrue(TEXT("armed"), In.ArmTakeRecorderSource(Sources));
    UTakeRecorderMicrophoneAudioSource* Mic = nullptr;
    for (UTakeRecorderSource* S : Sources->GetSources())
        if (UTakeRecorderMicrophoneAudioSource* M = Cast<UTakeRecorderMicrophoneAudioSource>(S)) Mic = M;
    if (TestNotNull(TEXT("mic source added"), Mic))
        TestEqual(TEXT("channel 1"), Mic->AudioChannel.AudioInputDeviceChannel, 1);
    TestEqual(TEXT("Take Recorder points at this input"), Manager->GetAudioInputDevice().DeviceId, In.GetDeviceId());
    TestFalse(TEXT("not the system default"), Manager->GetAudioInputDevice().bUseSystemDefaultAudioDevice);

    In.Disconnect();
    Manager->SetAudioInputDevice(Before);
    return true;
}

#endif
