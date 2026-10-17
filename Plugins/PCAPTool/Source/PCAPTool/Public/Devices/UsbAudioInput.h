#pragma once

#include "CoreMinimal.h"
#include "PCAPDeviceInterfaces.h"
#include "Containers/Ticker.h"
#include <atomic>

namespace Audio { class FAudioCapture; }

// Level math for the meter and the silence / clipping alarms (pure, tested).
struct PCAPTOOL_API FPCAPAudioLevel
{
    // Peak |sample| of one channel (1-based) in an interleaved float block; 0 if out of range.
    static float BlockPeak(const float* Interleaved, int32 NumFrames, int32 NumChannels, int32 Channel1Based);
    // dBFS, floored at -120.
    static float ToDb(float Linear);
};

// Any Windows audio input (USB interface, receiver, mic jack) recorded through Take Recorder's
// Microphone Audio source. Engine side from docs/devices/rode.md §4 (read from 5.8 source):
// the input device is global on UTakeRecorderMicrophoneAudioManager (DeviceId), the channel is
// per source (1-based), and assets are always 44.1 kHz / 16-bit mono. A second WASAPI shared
// stream on the same endpoint feeds the live level.
class PCAPTOOL_API FUsbAudioInput : public IPCAPAudioSource
{
public:
    static FName StaticVendorId() { return FName(TEXT("UsbAudioInput")); }

    FUsbAudioInput();
    virtual ~FUsbAudioInput() override;

    // ─── IPCAPAudioSource ───────────────────────────────────────────────────────
    virtual FName VendorId() const override { return StaticVendorId(); }
    // Host = the input's Windows name ("Analogue 1 + 2 (Focusrite USB Audio)"); exact match
    // first, else the first name containing it.
    virtual void  Connect(const FPCAPDeviceAddress& Address) override;
    virtual FPCAPDeviceHealth GetHealth() const override { return Health; }
    virtual float GetLevelDb() const override;
    virtual bool  ArmTakeRecorderSource(UTakeRecorderSources* Sources) override;

    // ─── Audio-input specific ───────────────────────────────────────────────────
    void  Disconnect();
    void  SetChannel(int32 InChannel1Based) { Channel = FMath::Max(1, InChannel1Based); }
    int32 GetChannel() const { return Channel; }
    FString GetDeviceId() const { return DeviceId; }
    FString GetDeviceName() const { return DeviceName; }
    int32 GetSampleRate() const { return SampleRate; }
    int32 GetDeviceChannels() const { return DeviceChannels; }
    // Peak reached -0.5 dBFS within the last 2 s.
    bool  IsClipping() const;
    // Seconds since the level was last above FloorDb (large when it never was).
    double SecondsSinceSignal() const;

    // Device presence and health. Called by the core ticker; tests call it directly.
    void Pump();

private:
    bool OpenStream(int32 DeviceIndex);
    void CloseStream();
    bool Tick(float DeltaTime);

    TUniquePtr<Audio::FAudioCapture> Capture;
    FString Wanted;           // name as configured
    FString DeviceName;       // resolved Windows name
    FString DeviceId;         // WASAPI endpoint id (what Take Recorder records from)
    int32   DeviceChannels = 0;
    int32   SampleRate = 0;
    int32   Channel = 1;

    std::atomic<float>  Peak { 0.f };
    std::atomic<double> LastCallback { 0.0 };
    std::atomic<double> LastSignal { 0.0 };
    std::atomic<double> LastClip { 0.0 };

    double NextEnumerate = 0.0;
    FPCAPDeviceHealth Health;
    FTSTicker::FDelegateHandle TickHandle;
};
