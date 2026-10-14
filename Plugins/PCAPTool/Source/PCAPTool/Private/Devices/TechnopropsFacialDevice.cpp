#include "Devices/TechnopropsFacialDevice.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ─── URLs ───────────────────────────────────────────────────────────────────────

FString FTechnopropsFacialDevice::BuildStatusUrl(const FString& InHost)
{
    // Exact endpoint the Technoprops MugShot web UI uses
    return FString::Printf(TEXT("http://%s/control?cmd=no&param="), *InHost);
}

FString FTechnopropsFacialDevice::BuildCommandUrl(const FString& InHost, const FString& Command)
{
    // Technoprops MugShot control endpoint.
    return FString::Printf(TEXT("http://%s/control?cmd=%s&param="), *InHost, *Command);
}

FString FTechnopropsFacialDevice::BuildDeviceCommandUrl(const FString& InHost, const FString& Cmd,
                                                        const FString& Param, const FString& ExtraKey,
                                                        const FString& ExtraVal, int64 CacheBusterTicks)
{
    // Mirrors the proven poll endpoint (GET query on /control). The MugShot web UI
    // uses the same pattern; if firmware rejects GET, switch SetVerb to POST and
    // move the query into the body.
    FString URL = FString::Printf(TEXT("http://%s/control?cmd=%s&param=%s"), *InHost, *Cmd, *Param);
    if (!ExtraKey.IsEmpty())
        URL += FString::Printf(TEXT("&%s=%s"), *ExtraKey, *ExtraVal);
    // Cache-buster (as the device's own web client appends) so repeated/identical
    // commands aren't served from cache and actually re-fire on the device.
    URL += FString::Printf(TEXT("&_=%lld"), (long long)CacheBusterTicks);
    return URL;
}

FString FTechnopropsFacialDevice::BuildVideoUrl(const FString& InHost, int32 CameraIndex)
{
    // Device cameras are 0-based: cam=0 (Top), cam=1 (Bot). cam=2 returns HTTP 400.
    return FString::Printf(TEXT("http://%s/video?cam=%d"), *InHost, CameraIndex);
}

// ─── IPCAPFacialDevice ───────────────────────────────────────────────────────────

bool FTechnopropsFacialDevice::SupportsConfig(ECaptureConfiguration Config) const
{
    // ORION is a two-camera head mount; mono uses one of its cameras.
    return Config == ECaptureConfiguration::StereoHeadMount || Config == ECaptureConfiguration::MonoHeadMount;
}

void FTechnopropsFacialDevice::Connect(const FPCAPDeviceAddress& Address)
{
    Host = Address.Host;
    Health.VendorId = StaticVendorId();
}

void FTechnopropsFacialDevice::RequestPreviewFrame(int32 CameraIndex, FOnPCAPPreviewFrame OnFrame)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BuildVideoUrl(Host, CameraIndex));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(3.0f);
    // Captures only the callback (not this adapter), so a response that lands after the
    // device was unregistered still reaches the subsystem exactly as before the move.
    Request->OnProcessRequestComplete().BindLambda(
        [OnFrame](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            const int32 Code = Response.IsValid() ? Response->GetResponseCode() : -1;
            const bool bOk = bWasSuccessful && Response.IsValid() && Code == 200;
            if (bOk)
            {
                OnFrame.ExecuteIfBound(true, Code, Response->GetContent());
            }
            else
            {
                static const TArray<uint8> Empty;
                OnFrame.ExecuteIfBound(false, Code, Empty);
            }
        });
    Request->ProcessRequest();
}

void FTechnopropsFacialDevice::StartRecord(const FPCAPTakeIdentity& Take)
{
    Health.LastError = TEXT("Technoprops record start not implemented (device request unverified)");
    UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] %s: %s"), *Host, *Health.LastError);
}

void FTechnopropsFacialDevice::StopRecord()
{
    Health.LastError = TEXT("Technoprops record stop not implemented (device request unverified)");
    UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] %s: %s"), *Host, *Health.LastError);
}

void FTechnopropsFacialDevice::ListTakeFiles(const FPCAPTakeIdentity& Take, FOnPCAPFileList OnList)
{
    OnList.ExecuteIfBound(false, TArray<FPCAPRemoteFile>());
}

void FTechnopropsFacialDevice::DownloadFile(const FPCAPRemoteFile& File, const FString& LocalPath,
                                            FOnPCAPProgress OnProgress, FOnPCAPDone OnDone)
{
    OnDone.ExecuteIfBound(false, TEXT("Technoprops file download not implemented (device request unverified)"));
}

// ─── ORION-specific ──────────────────────────────────────────────────────────────

void FTechnopropsFacialDevice::PollStatus(FOnStatusPolled OnPolled)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BuildStatusUrl(Host));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(2.5f);   // generous — status competes with the video pulls
    Request->OnProcessRequestComplete().BindLambda(
        [OnPolled](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
            {
                OnPolled.ExecuteIfBound(false, FString());
                return;
            }
            OnPolled.ExecuteIfBound(true, Response->GetContentAsString());
        });
    Request->ProcessRequest();
}

void FTechnopropsFacialDevice::SendCommand(const FString& Command)
{
    // TEST ONLY — fire and forget.
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BuildCommandUrl(Host, Command));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(1.5f);
    Request->ProcessRequest();
}

void FTechnopropsFacialDevice::SendDeviceCommand(const FString& Cmd, const FString& Param,
                                                 const FString& ExtraKey, const FString& ExtraVal)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BuildDeviceCommandUrl(Host, Cmd, Param, ExtraKey, ExtraVal, FDateTime::UtcNow().GetTicks()));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(1.5f);
    Request->ProcessRequest();   // fire and forget — value confirms on next poll
}

bool FTechnopropsFacialDevice::ParseControlJson(const FString& Body, FHMCDeviceStatus& Status)
{
    TSharedPtr<FJsonObject> JsonObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid()) return false;

    double Val = 0.0;

    JsonObj->TryGetNumberField(TEXT("nRecording"),            Val);
    Status.bIsRecording = Val != 0.0;

    Val = 0.0;
    JsonObj->TryGetNumberField(TEXT("batteryVoltage"),        Val);
    Status.BatteryVoltage = static_cast<float>(Val);

    Val = 0.0;
    JsonObj->TryGetNumberField(TEXT("availableStorageInMB"),  Val);
    Status.AvailableStorageMB = static_cast<float>(Val);

    Val = 0.0;
    JsonObj->TryGetNumberField(TEXT("cpuUsage"),              Val);
    Status.CPUUsagePercent = static_cast<float>(Val);

    Val = 0.0;
    JsonObj->TryGetNumberField(TEXT("cpuTemp"),               Val);
    Status.TemperatureCelsius = static_cast<float>(Val);

    Val = 0.0;
    JsonObj->TryGetNumberField(TEXT("frameRate"),             Val);
    Status.FPS = static_cast<float>(Val);

    // Per-camera telemetry
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("skippedFrames0"), Val); Status.DroppedFrames0 = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("skippedFrames1"), Val); Status.DroppedFrames1 = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("exposure0"),      Val); Status.Exposure0      = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("exposure1"),      Val); Status.Exposure1      = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("gain0"),          Val); Status.Gain0          = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("gain1"),          Val); Status.Gain1          = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("topLights"),      Val); Status.TopLights      = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("bottomLights"),   Val); Status.BottomLights   = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("streaming0"),     Val); Status.bStreaming0    = Val != 0.0;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("streaming1"),     Val); Status.bStreaming1    = Val != 0.0;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("width"),          Val); Status.FrameWidth     = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("height"),         Val); Status.FrameHeight    = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("rotation0"),      Val); Status.Rotation0      = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("rotation1"),      Val); Status.Rotation1      = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("boomPos"),        Val); Status.BoomPos        = (int32)Val;

    JsonObj->TryGetStringField(TEXT("takename"),                 Status.CurrentTakeName);
    JsonObj->TryGetStringField(TEXT("lastMovieIntegrityStatus"), Status.LastClipStatus);
    return true;
}

void FTechnopropsFacialDevice::UpdateHealth(const FHMCDeviceStatus& Status)
{
    Health.VendorId      = StaticVendorId();
    Health.DeviceName    = Status.DeviceName;
    Health.bConnected    = Status.ConnectionState == EHMCConnectionState::Connected;
    Health.bRecording    = Status.bIsRecording;
    Health.BatteryPercent = -1.f;   // ORION reports volts (FHMCDeviceStatus::BatteryVoltage), not percent
    Health.StorageFreeMB = Status.AvailableStorageMB;
    Health.FPS           = Status.FPS;
    Health.DroppedFrames = Status.DroppedFrames0 + Status.DroppedFrames1;
    Health.TemperatureC  = Status.TemperatureCelsius;
    Health.IssueFlags    = Status.IssueFlags0 | Status.IssueFlags1;
    Health.LastUpdate    = Status.LastUpdateTime;
}
