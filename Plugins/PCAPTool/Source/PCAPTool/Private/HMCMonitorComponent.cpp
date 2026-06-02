#include "HMCMonitorComponent.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"
#include "Modules/ModuleManager.h"

UHMCMonitorComponent::UHMCMonitorComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UHMCMonitorComponent::BeginPlay()
{
    Super::BeginPlay();

    if (DeviceRegistry.Num() > 0)
    {
        GetWorld()->GetTimerManager().SetTimer(
            PollTimerHandle,
            this,
            &UHMCMonitorComponent::PollAllDevices,
            PollIntervalSeconds,
            true,
            0.0f
        );
    }
}

void UHMCMonitorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorld()->GetTimerManager().ClearTimer(PollTimerHandle);
    Super::EndPlay(EndPlayReason);
}

// ─── Device Registration ─────────────────────────────────────────────────────

void UHMCMonitorComponent::RegisterDevice(const FString& DeviceID, const FString& IPAddress)
{
    FDeviceRecord& Record = DeviceRegistry.FindOrAdd(DeviceID);
    Record.IPAddress = IPAddress;
    Record.Status.DeviceID = DeviceID;
    Record.Status.IPAddress = IPAddress;
    Record.Status.IsReachable = false;

    if (GetWorld() && GetWorld()->GetTimerManager().IsTimerActive(PollTimerHandle))
    {
        GetWorld()->GetTimerManager().SetTimer(
            PollTimerHandle,
            this,
            &UHMCMonitorComponent::PollAllDevices,
            PollIntervalSeconds,
            true,
            0.0f
        );
    }
}

FHMCDeviceStatus UHMCMonitorComponent::GetDeviceStatus(const FString& DeviceID) const
{
    const FDeviceRecord* Record = DeviceRegistry.Find(DeviceID);
    return Record ? Record->Status : FHMCDeviceStatus();
}

TArray<FHMCDeviceStatus> UHMCMonitorComponent::GetAllDeviceStatuses() const
{
    TArray<FHMCDeviceStatus> Out;
    for (const auto& Pair : DeviceRegistry)
    {
        Out.Add(Pair.Value.Status);
    }
    return Out;
}

// ─── Commands ─────────────────────────────────────────────────────────────────

void UHMCMonitorComponent::SendCommand(const FString& DeviceID, const FString& Command)
{
    const FDeviceRecord* Record = DeviceRegistry.Find(DeviceID);
    if (!Record) return;

    const FString URL = FString::Printf(TEXT("http://%s/control?cmd=%s&param="),
        *Record->IPAddress, *Command);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(2.0f);
    Request->ProcessRequest();
    // No response handling — next poll cycle picks up the state change
}

// ─── Status Polling ───────────────────────────────────────────────────────────

void UHMCMonitorComponent::PollAllDevices()
{
    for (const auto& Pair : DeviceRegistry)
    {
        PollDevice(Pair.Key, Pair.Value.IPAddress);
    }
}

void UHMCMonitorComponent::PollDevice(const FString& DeviceID, const FString& IPAddress)
{
    const FString URL = FString::Printf(TEXT("http://%s/control?cmd=no&param="), *IPAddress);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(1.5f);

    Request->OnProcessRequestComplete().BindUObject(
        this,
        &UHMCMonitorComponent::OnPollResponse,
        DeviceID,
        IPAddress
    );

    Request->ProcessRequest();
}

void UHMCMonitorComponent::OnPollResponse(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    FString DeviceID,
    FString IPAddress)
{
    FDeviceRecord* Record = DeviceRegistry.Find(DeviceID);
    if (!Record) return;

    FHMCDeviceStatus& Status = Record->Status;
    Status.LastPollTime = FDateTime::UtcNow();

    if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
    {
        Status.IsReachable = false;
        OnStatusUpdated.Broadcast(Status);
        return;
    }

    Status.IsReachable = true;

    TSharedPtr<FJsonObject> JsonObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

    if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
    {
        double RecordingVal = 0.0;
        JsonObj->TryGetNumberField(TEXT("nRecording"), RecordingVal);
        Status.IsRecording = RecordingVal != 0.0;

        double BattVoltage = 0.0;
        JsonObj->TryGetNumberField(TEXT("batteryVoltage"), BattVoltage);
        Status.BatteryVoltage = static_cast<float>(BattVoltage);

        double StorageMB = 0.0;
        JsonObj->TryGetNumberField(TEXT("availableStorageInMB"), StorageMB);
        Status.AvailableStorageMB = static_cast<float>(StorageMB);
    }

    OnStatusUpdated.Broadcast(Status);
}

// ─── Video Frames ─────────────────────────────────────────────────────────────

void UHMCMonitorComponent::RequestVideoFrame(const FString& DeviceID, int32 CameraIndex)
{
    const FDeviceRecord* Record = DeviceRegistry.Find(DeviceID);
    if (!Record) return;

    const FString URL = FString::Printf(TEXT("http://%s/video?cam=%d"),
        *Record->IPAddress, CameraIndex);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(3.0f);

    Request->OnProcessRequestComplete().BindUObject(
        this,
        &UHMCMonitorComponent::OnVideoFrameResponse,
        DeviceID,
        CameraIndex
    );

    Request->ProcessRequest();
}

UTexture2D* UHMCMonitorComponent::GetLastFrame(const FString& DeviceID, int32 CameraIndex) const
{
    const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceID, CameraIndex);
    const TObjectPtr<UTexture2D>* Found = FrameTextures.Find(Key);
    return Found ? Found->Get() : nullptr;
}

void UHMCMonitorComponent::OnVideoFrameResponse(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    FString DeviceID,
    int32 CameraIndex)
{
    if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
    {
        OnFrameReceived.Broadcast(DeviceID, CameraIndex, nullptr);
        return;
    }

    const TArray<uint8>& RawBytes = Response->GetContent();

    IImageWrapperModule& ImageWrapperModule =
        FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
    TSharedPtr<IImageWrapper> ImageWrapper =
        ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawBytes.GetData(), RawBytes.Num()))
    {
        OnFrameReceived.Broadcast(DeviceID, CameraIndex, nullptr);
        return;
    }

    TArray<uint8> Uncompressed;
    if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, Uncompressed))
    {
        OnFrameReceived.Broadcast(DeviceID, CameraIndex, nullptr);
        return;
    }

    const int32 Width  = ImageWrapper->GetWidth();
    const int32 Height = ImageWrapper->GetHeight();

    // HTTP callbacks fire on game thread in UE5 — texture creation is safe here.
    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
    if (!Texture)
    {
        OnFrameReceived.Broadcast(DeviceID, CameraIndex, nullptr);
        return;
    }

    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Data, Uncompressed.GetData(), Uncompressed.Num());
    Mip.BulkData.Unlock();
    Texture->UpdateResource();

    // Store in UPROPERTY map to prevent GC until the next frame replaces it.
    const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceID, CameraIndex);
    FrameTextures.Add(Key, Texture);

    OnFrameReceived.Broadcast(DeviceID, CameraIndex, Texture);
}
