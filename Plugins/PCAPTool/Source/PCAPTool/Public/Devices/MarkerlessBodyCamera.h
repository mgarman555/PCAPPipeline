#pragma once

#include "CoreMinimal.h"
#include "PCAPDeviceInterfaces.h"
#include "Containers/Ticker.h"

class FSocket;

// Capture mode for a UVC body camera. Defaults are the C920S's verified mode
// (docs/devices/body-camera.md §8: 1280x720 MJPEG @ 30 holds a real 30 fps; 1080p drops frames).
struct FPCAPBodyCameraMode
{
    int32 Width  = 1280;
    int32 Height = 720;
    int32 Fps    = 30;
};

// FFmpeg "-progress" key=value lines (frame=, fps=, dup_frames=, drop_frames=, progress=).
struct PCAPTOOL_API FPCAPFfmpegProgress
{
    int32 Frame = 0;
    float Fps   = 0.f;
    int32 Dup   = 0;     // frames FFmpeg had to repeat: the camera did not deliver them
    int32 Drop  = 0;
    bool  bEnded = false;

    // True if the line was a key this struct tracks.
    bool ParseLine(const FString& Line);
};

// Rebuilds whole JPEGs from the MJPEG byte stream FFmpeg sends over UDP (SOI FFD8 .. EOI FFD9).
// Keeps only the newest complete frame.
class PCAPTOOL_API FPCAPMjpegAssembler
{
public:
    void Feed(const uint8* Data, int32 Num);
    bool HasFrame() const { return Latest.Num() > 0; }
    const TArray<uint8>& GetLatest() const { return Latest; }
    int32 GetFrameCount() const { return Frames; }

private:
    TArray<uint8> Buffer;
    TArray<uint8> Latest;
    int32 Frames = 0;
};

// Markerless body camera: one FFmpeg child process owns the webcam (handoff §4.2(b)).
// Idle, it streams a 10 fps preview to localhost. Recording restarts it with two outputs:
// H.264 CRF 18 straight into the take folder, plus the same preview. Stop sends "q" so
// the file is finalized; the fragmented MP4 layout keeps a hard kill readable.
// Every behavior here is from docs/devices/body-camera-verification.md (#26-#33, #13-#19).
class PCAPTOOL_API FMarkerlessBodyCamera : public IPCAPBodySource
{
public:
    static FName StaticVendorId() { return FName(TEXT("MarkerlessCamera")); }

    FMarkerlessBodyCamera();
    virtual ~FMarkerlessBodyCamera() override;

    // ─── IPCAPBodySource ────────────────────────────────────────────────────────
    virtual FName VendorId() const override { return StaticVendorId(); }
    // Host = dshow device name ("HD Pro Webcam C920"); Port = preview UDP port (0 = 5005).
    virtual void  Connect(const FPCAPDeviceAddress& Address) override;
    virtual FPCAPDeviceHealth GetHealth() const override { return Health; }
    virtual int32 NumPreviewCameras() const override { return 1; }
    virtual void  RequestPreviewFrame(int32 CameraIndex, FOnPCAPPreviewFrame OnFrame) override;
    virtual void  StartRecord(const FPCAPTakeIdentity& Take) override;
    virtual void  StopRecord() override;
    virtual void  ListTakeFiles(const FPCAPTakeIdentity& Take, FOnPCAPFileList OnList) override;

    // ─── Body-camera specific ───────────────────────────────────────────────────
    void SetMode(const FPCAPBodyCameraMode& InMode) { Mode = InMode; }
    void SetFFmpegPath(const FString& InPath) { FFmpegPath = InPath; }
    void Disconnect();

    // Reads FFmpeg output and preview packets. Called by the core ticker; tests call it directly.
    void Pump();

    // Where StartRecord writes: <CaptureFolder>/body/<TakeID>/<TakeID>_body.mp4.
    static FString RecordFilePath(const FPCAPTakeIdentity& Take);
    // Full FFmpeg argument string. RecordFile empty = preview only.
    static FString BuildArgs(const FString& Device, const FPCAPBodyCameraMode& InMode,
                             const FString& RecordFile, int32 PreviewPort, int32 PreviewFps = 10);

    bool IsRecording() const { return State == EState::Recording || State == EState::Stopping; }
    const FPCAPFfmpegProgress& GetProgress() const { return Progress; }
    int32 GetPreviewFrameCount() const { return Assembler.GetFrameCount(); }
    // Wall-clock time the first recorded frame was reported (FDateTime(0) until then).
    FDateTime GetRecordFirstFrameTime() const { return RecordFirstFrame; }
    FString GetRecordFile() const { return RecordFile; }

private:
    enum class EState : uint8 { Idle, Previewing, Recording, Stopping };

    bool LaunchProcess(const FString& Args);
    // Sends "q" and waits up to TimeoutSec; kills the process if it has not exited.
    void StopProcess(float TimeoutSec);
    void ClosePipes();
    void OpenPreviewSocket();
    void CloseSocket();
    bool Tick(float DeltaTime);

    FPCAPBodyCameraMode Mode;
    FString FFmpegPath = TEXT("ffmpeg.exe");
    FString Device;
    int32   PreviewPort = 5005;

    FProcHandle Proc;
    void* StdInRead  = nullptr;
    void* StdInWrite = nullptr;
    void* OutRead    = nullptr;
    void* OutWrite   = nullptr;
    FString OutPartial;

    FSocket* Socket = nullptr;
    FPCAPMjpegAssembler Assembler;
    double LastFrameSeconds = 0.0;

    EState State = EState::Idle;
    double StopRequestedSeconds = 0.0;
    FString RecordFile;
    FDateTime RecordFirstFrame = FDateTime(0);
    FPCAPFfmpegProgress Progress;
    // (time, frame) samples from -progress; FFmpeg's own fps= includes camera-open time, so the
    // health FPS is measured over the last few seconds instead.
    TArray<TPair<double, int32>> FrameSamples;
    float MeasuredFps = 0.f;
    FPCAPDeviceHealth Health;
    FTSTicker::FDelegateHandle TickHandle;
};
