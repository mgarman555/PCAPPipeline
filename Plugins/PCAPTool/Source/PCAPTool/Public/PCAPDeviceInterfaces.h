#pragma once

#include "CoreMinimal.h"
#include "PCAPToolTypes.h"   // FPCAPDeviceHealth, ECaptureConfiguration

class UTakeRecorderSources;

// ─── Device layer (HOME_REBUILD_HANDOFF §4.2) ─────────────────────────────────────
// One interface per category, one class per vendor, catalogued in UPCAPDeviceRegistry.
// Hardware is a dropdown, never a code path: callers talk to these interfaces, and a
// vendor's quirks stay inside its adapter. Adapters only ever describe behavior that a
// device fact sheet (docs/devices/<device>.md) has verified.
//
// Callbacks fire on the game thread (UE HTTP completes there). An adapter must invoke
// every callback it is handed exactly once, success or failure.

// Where a device lives: host/IP for network devices, OS device name for local ones.
struct FPCAPDeviceAddress
{
    FString Host;       // "192.168.50.117", "soma-xxxx.local", or a dshow/WASAPI device name
    int32   Port = 0;   // 0 = vendor default

    FPCAPDeviceAddress() = default;
    explicit FPCAPDeviceAddress(const FString& InHost, int32 InPort = 0) : Host(InHost), Port(InPort) {}
};

// Which take a record/list/download call is about.
struct FPCAPTakeIdentity
{
    FString Production;
    FString DayID;
    FString ShotID;
    FString TakeID;
    FString CaptureFolder;   // <CaptureRoot>/<Production>/Day_<Day>/ (layout: PCAPTakeOffload.h)
};

// A file the device holds for a take.
struct FPCAPRemoteFile
{
    FString   Name;            // as the device names it
    FString   Locator;         // URL or device path the adapter downloads from
    int64     SizeBytes = -1;  // < 0 = unknown
    FDateTime Modified  = FDateTime(0);
};

// Preview frame: bOk, transport status (HTTP code, or -1 when there was no response), and
// the encoded JPEG on success (empty otherwise). The frame pump decodes it.
DECLARE_DELEGATE_ThreeParams(FOnPCAPPreviewFrame, bool /*bOk*/, int32 /*StatusCode*/, const TArray<uint8>& /*Jpeg*/);
DECLARE_DELEGATE_TwoParams(FOnPCAPFileList, bool /*bOk*/, const TArray<FPCAPRemoteFile>& /*Files*/);
DECLARE_DELEGATE_TwoParams(FOnPCAPProgress, int64 /*BytesDone*/, int64 /*BytesTotal*/);   // total < 0 = unknown
DECLARE_DELEGATE_TwoParams(FOnPCAPDone, bool /*bOk*/, const FString& /*Error*/);

// Facial: a device that previews, records internally, and hands files back.
class IPCAPFacialDevice
{
public:
    virtual ~IPCAPFacialDevice() = default;

    virtual FName VendorId() const = 0;                                    // "Technoprops", "OneHMC"
    virtual bool  SupportsConfig(ECaptureConfiguration Config) const = 0;
    virtual void  Connect(const FPCAPDeviceAddress& Address) = 0;
    virtual FPCAPDeviceHealth GetHealth() const = 0;
    virtual int32 NumPreviewCameras() const = 0;
    virtual void  RequestPreviewFrame(int32 CameraIndex, FOnPCAPPreviewFrame OnFrame) = 0;
    virtual void  StartRecord(const FPCAPTakeIdentity& Take) = 0;
    virtual void  StopRecord() = 0;
    virtual void  ListTakeFiles(const FPCAPTakeIdentity& Take, FOnPCAPFileList OnList) = 0;
    virtual void  DownloadFile(const FPCAPRemoteFile& File, const FString& LocalPath,
                               FOnPCAPProgress OnProgress, FOnPCAPDone OnDone) = 0;
};

// Body: a Live Link source (Xsens etc., later) or a video camera (markerless). A markerless
// camera previews frames and records straight to a file in the take folder.
class IPCAPBodySource
{
public:
    virtual ~IPCAPBodySource() = default;

    virtual FName VendorId() const = 0;                                    // "MarkerlessCamera", "Xsens"
    virtual void  Connect(const FPCAPDeviceAddress& Address) = 0;
    virtual FPCAPDeviceHealth GetHealth() const = 0;
    virtual int32 NumPreviewCameras() const = 0;                           // 0 for a pure Live Link source
    virtual void  RequestPreviewFrame(int32 CameraIndex, FOnPCAPPreviewFrame OnFrame) = 0;
    virtual FName GetLiveLinkSubject() const { return NAME_None; }         // set for Live Link sources
    virtual void  StartRecord(const FPCAPTakeIdentity& Take) = 0;
    virtual void  StopRecord() = 0;
    virtual void  ListTakeFiles(const FPCAPTakeIdentity& Take, FOnPCAPFileList OnList) = 0;
};

// Audio: an input recorded by Take Recorder, with a live level for the alarm.
class IPCAPAudioSource
{
public:
    virtual ~IPCAPAudioSource() = default;

    virtual FName VendorId() const = 0;                                    // "UsbAudioInput"
    virtual void  Connect(const FPCAPDeviceAddress& Address) = 0;          // Host = OS input device
    virtual FPCAPDeviceHealth GetHealth() const = 0;
    virtual float GetLevelDb() const = 0;                                  // dBFS; <= -120 = silence
    // Adds/configures this input on the take's sources. False (with Health.LastError) on failure.
    virtual bool  ArmTakeRecorderSource(UTakeRecorderSources* Sources) = 0;
};
