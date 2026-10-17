#include "Devices/UsbAudioInput.h"

#include "AudioCaptureCore.h"
#include "HAL/PlatformTime.h"
#include "TakeRecorderSources.h"
#include "TakeRecorderMicrophoneAudioManager.h"
#include "TakeRecorderMicrophoneAudioSource.h"

DEFINE_LOG_CATEGORY_STATIC(LogPCAPAudioIn, Log, All);

// Silence floor for SecondsSinceSignal: the probe's SILENT threshold (rode_probe.ps1).
static constexpr float PCAPAudioSignalFloorDb = -50.f;

// ─── Level math ──────────────────────────────────────────────────────────────

float FPCAPAudioLevel::BlockPeak(const float* Interleaved, int32 NumFrames, int32 NumChannels, int32 Channel1Based)
{
    if (!Interleaved || NumFrames <= 0 || Channel1Based < 1 || Channel1Based > NumChannels) return 0.f;
    float P = 0.f;
    const int32 Ch = Channel1Based - 1;
    for (int32 f = 0; f < NumFrames; ++f)
        P = FMath::Max(P, FMath::Abs(Interleaved[f * NumChannels + Ch]));
    return P;
}

float FPCAPAudioLevel::ToDb(float Linear)
{
    return Linear > 1e-6f ? FMath::Max(-120.f, 20.f * FMath::LogX(10.f, Linear)) : -120.f;
}

// ─── Adapter ─────────────────────────────────────────────────────────────────

FUsbAudioInput::FUsbAudioInput()
{
    Health.VendorId = StaticVendorId();
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FUsbAudioInput::Tick), 0.25f);
}

FUsbAudioInput::~FUsbAudioInput()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    CloseStream();   // the capture callback captures `this`: close it before members go
}

void FUsbAudioInput::Connect(const FPCAPDeviceAddress& Address)
{
    CloseStream();
    Wanted = Address.Host;
    Health.DeviceName = Wanted;
    Health.LastError.Reset();
    if (Wanted.IsEmpty())
    {
        Health.LastError = TEXT("No audio input set (Project Settings > PCAP Tool > Audio).");
        return;
    }

    Audio::FAudioCapture Enum;
    TArray<Audio::FCaptureDeviceInfo> Devices;
    Enum.GetCaptureDevicesAvailable(Devices);
    int32 Index = Devices.IndexOfByPredicate([this](const Audio::FCaptureDeviceInfo& D) { return D.DeviceName.Equals(Wanted, ESearchCase::IgnoreCase); });
    if (Index == INDEX_NONE)
        Index = Devices.IndexOfByPredicate([this](const Audio::FCaptureDeviceInfo& D) { return D.DeviceName.Contains(Wanted); });
    if (Index == INDEX_NONE)
    {
        Health.LastError = FString::Printf(TEXT("Audio input \"%s\" not found."), *Wanted);
        return;
    }

    DeviceName     = Devices[Index].DeviceName;
    DeviceId       = Devices[Index].DeviceId;
    DeviceChannels = Devices[Index].InputChannels;
    SampleRate     = Devices[Index].PreferredSampleRate;
    Health.DeviceName = DeviceName;
    if (Channel > DeviceChannels)
    {
        Health.LastError = FString::Printf(TEXT("%s has %d channel(s); channel %d does not exist."), *DeviceName, DeviceChannels, Channel);
        return;
    }
    OpenStream(Index);
}

bool FUsbAudioInput::OpenStream(int32 DeviceIndex)
{
    Capture = MakeUnique<Audio::FAudioCapture>();
    Audio::FAudioCaptureDeviceParams Params;
    Params.DeviceIndex = DeviceIndex;
    Params.PCMAudioEncoding = Audio::EPCMAudioEncoding::FLOATING_POINT_32;

    Audio::FOnAudioCaptureFunction OnCapture = [this](const void* Data, int32 NumFrames, int32 NumChannels, int32, double, bool)
    {
        const float BlockPeak = FPCAPAudioLevel::BlockPeak(static_cast<const float*>(Data), NumFrames, NumChannels, Channel);
        // Meter ballistics: instant attack, ~0.9 per block release (~1024 frames ≈ 21 ms at 48 kHz).
        Peak.store(FMath::Max(BlockPeak, Peak.load() * 0.9f));
        const double Now = FPlatformTime::Seconds();
        LastCallback.store(Now);
        const float Db = FPCAPAudioLevel::ToDb(BlockPeak);
        if (Db > PCAPAudioSignalFloorDb) LastSignal.store(Now);
        if (Db >= -0.5f) LastClip.store(Now);
    };

    if (!Capture->OpenAudioCaptureStream(Params, MoveTemp(OnCapture), 1024) || !Capture->StartStream())
    {
        Health.LastError = FString::Printf(TEXT("Could not open %s."), *DeviceName);
        Capture.Reset();
        return false;
    }
    UE_LOG(LogPCAPAudioIn, Log, TEXT("Metering %s (%d ch, %d Hz), channel %d"), *DeviceName, DeviceChannels, SampleRate, Channel);
    return true;
}

void FUsbAudioInput::CloseStream()
{
    if (Capture.IsValid())
    {
        Capture->StopStream();
        Capture->CloseStream();
        Capture.Reset();
    }
    Peak.store(0.f);
    Health.bConnected = false;
}

void FUsbAudioInput::Disconnect()
{
    CloseStream();
    Wanted.Reset();
    DeviceId.Reset();
}

float FUsbAudioInput::GetLevelDb() const
{
    return Health.bConnected ? FPCAPAudioLevel::ToDb(Peak.load()) : -120.f;
}

bool FUsbAudioInput::IsClipping() const
{
    return FPlatformTime::Seconds() - LastClip.load() < 2.0;
}

double FUsbAudioInput::SecondsSinceSignal() const
{
    const double L = LastSignal.load();
    return L > 0.0 ? FPlatformTime::Seconds() - L : 1e9;
}

bool FUsbAudioInput::ArmTakeRecorderSource(UTakeRecorderSources* Sources)
{
    if (!Sources || DeviceId.IsEmpty())
    {
        Health.LastError = TEXT("Audio input not connected.");
        return false;
    }
    // The input device is global to Take Recorder: point it at this endpoint.
    UTakeRecorderMicrophoneAudioManager* Manager = GetMutableDefault<UTakeRecorderMicrophoneAudioManager>();
    FAudioInputDeviceProperty Dev = Manager->GetAudioInputDevice();
    if (Dev.bUseSystemDefaultAudioDevice || Dev.DeviceId != DeviceId)
    {
        Dev.bUseSystemDefaultAudioDevice = false;
        Dev.DeviceId = DeviceId;
        Manager->SetAudioInputDevice(Dev);
    }

    UTakeRecorderMicrophoneAudioSource* Src = Sources->AddSource<UTakeRecorderMicrophoneAudioSource>();
    if (!Src)
    {
        Health.LastError = TEXT("Take Recorder would not add a microphone source.");
        return false;
    }
    // AddSource auto-picks the first free channel; use the configured one.
    Src->AudioChannel.AudioInputDeviceChannel = Channel;
    return true;
}

bool FUsbAudioInput::Tick(float)
{
    Pump();
    return true;
}

void FUsbAudioInput::Pump()
{
    const double Now = FPlatformTime::Seconds();

    // Re-check presence every 2 s: an unplugged interface drops out of the WASAPI list.
    if (!Wanted.IsEmpty() && Now >= NextEnumerate)
    {
        NextEnumerate = Now + 2.0;
        Audio::FAudioCapture Enum;
        TArray<Audio::FCaptureDeviceInfo> Devices;
        Enum.GetCaptureDevicesAvailable(Devices);
        const bool bPresent = !DeviceId.IsEmpty() && Devices.ContainsByPredicate(
            [this](const Audio::FCaptureDeviceInfo& D) { return D.DeviceId == DeviceId; });
        if (Capture.IsValid() && !bPresent)
        {
            CloseStream();
            Health.LastError = FString::Printf(TEXT("%s was unplugged."), *DeviceName);
        }
        else if (!Capture.IsValid())
        {
            const FString KeepWanted = Wanted;
            Connect(FPCAPDeviceAddress(KeepWanted));   // comes back on its own when replugged
        }
    }

    const bool bFlowing = Capture.IsValid() && Now - LastCallback.load() < 1.0;
    if (!bFlowing) Peak.store(0.f);
    if (bFlowing && Health.LastError.EndsWith(TEXT("unplugged."))) Health.LastError.Reset();
    Health.bConnected = bFlowing;
    Health.LastUpdate = FDateTime::UtcNow();
}
