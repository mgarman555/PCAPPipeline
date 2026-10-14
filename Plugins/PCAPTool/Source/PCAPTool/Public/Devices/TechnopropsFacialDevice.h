#pragma once

#include "CoreMinimal.h"
#include "PCAPDeviceInterfaces.h"

// Technoprops ORION/MugShot transport, moved out of UPCAPToolSubsystem unchanged (Phase 1:
// "code moved, not rewritten" — same URLs, verbs, timeouts and control.json fields).
// The subsystem still owns polling cadence, fail debounce, the frame pump and the agents;
// this class owns only the wire protocol.
//
//   status:  GET http://<ip>/control?cmd=no&param=                    (2.5 s timeout)
//   command: GET http://<ip>/control?cmd=<cmd>&param=                 (1.5 s)
//   write:   GET http://<ip>/control?cmd=<cmd>&param=<p>[&k=v]&_=<ticks> (1.5 s)
//   frame:   GET http://<ip>/video?cam=<0|1>   (0-based; cam=2 -> HTTP 400)  (3.0 s)
class PCAPTOOL_API FTechnopropsFacialDevice : public IPCAPFacialDevice
{
public:
    static FName StaticVendorId() { return FName(TEXT("Technoprops")); }

    // ─── IPCAPFacialDevice ──────────────────────────────────────────────────────
    virtual FName VendorId() const override { return StaticVendorId(); }
    virtual bool  SupportsConfig(ECaptureConfiguration Config) const override;
    virtual void  Connect(const FPCAPDeviceAddress& Address) override;   // stores the host; HTTP is stateless
    virtual FPCAPDeviceHealth GetHealth() const override { return Health; }
    virtual int32 NumPreviewCameras() const override { return 2; }
    virtual void  RequestPreviewFrame(int32 CameraIndex, FOnPCAPPreviewFrame OnFrame) override;
    // Not implemented: the ORION record/list/download requests have not been verified
    // against the device (the old UMG guide's "startrecording" token is a guess). These
    // report failure instead of sending anything.
    virtual void  StartRecord(const FPCAPTakeIdentity& Take) override;
    virtual void  StopRecord() override;
    virtual void  ListTakeFiles(const FPCAPTakeIdentity& Take, FOnPCAPFileList OnList) override;
    virtual void  DownloadFile(const FPCAPRemoteFile& File, const FString& LocalPath,
                               FOnPCAPProgress OnProgress, FOnPCAPDone OnDone) override;

    // ─── ORION-specific (used by UPCAPToolSubsystem) ───────────────────────────
    // bOk = request succeeded with HTTP 200; Body = control.json text when bOk.
    DECLARE_DELEGATE_TwoParams(FOnStatusPolled, bool /*bOk*/, const FString& /*Body*/);
    void PollStatus(FOnStatusPolled OnPolled);

    void SendCommand(const FString& Command);
    void SendDeviceCommand(const FString& Cmd, const FString& Param,
                           const FString& ExtraKey, const FString& ExtraVal);

    // Parses control.json into Status (telemetry fields only). False if the body is not a
    // JSON object, in which case Status is untouched.
    static bool ParseControlJson(const FString& Body, FHMCDeviceStatus& Status);

    // Refresh the vendor-neutral health from the subsystem's view of the device.
    void UpdateHealth(const FHMCDeviceStatus& Status);

    // URL builders (exact formats the device's web UI uses).
    static FString BuildStatusUrl(const FString& Host);
    static FString BuildCommandUrl(const FString& Host, const FString& Command);
    static FString BuildDeviceCommandUrl(const FString& Host, const FString& Cmd, const FString& Param,
                                         const FString& ExtraKey, const FString& ExtraVal, int64 CacheBusterTicks);
    static FString BuildVideoUrl(const FString& Host, int32 CameraIndex);

    const FString& GetHost() const { return Host; }

private:
    FString Host;
    FPCAPDeviceHealth Health;
};
