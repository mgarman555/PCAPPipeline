#include "HMCMonitorComponent.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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

void UHMCMonitorComponent::RegisterDevice(const FString& DeviceID, const FString& IPAddress)
{
    FDeviceRecord& Record = DeviceRegistry.FindOrAdd(DeviceID);
    Record.IPAddress = IPAddress;
    Record.Status.DeviceID = DeviceID;
    Record.Status.IPAddress = IPAddress;
    Record.Status.IsReachable = false;

    // If already running, restart timer so the new device joins the next cycle.
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
    if (!Record)
    {
        return;
    }

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

    if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
    {
        OnStatusUpdated.Broadcast(Status);
        return;
    }

    // HMC API fields: nRecording, batteryVoltage, availableStorageInMB, takename
    double RecordingVal = 0.0;
    JsonObj->TryGetNumberField(TEXT("nRecording"), RecordingVal);
    Status.IsRecording = RecordingVal != 0.0;

    double BattVoltage = 0.0;
    JsonObj->TryGetNumberField(TEXT("batteryVoltage"), BattVoltage);
    Status.BatteryVoltage = static_cast<float>(BattVoltage);

    double StorageMB = 0.0;
    JsonObj->TryGetNumberField(TEXT("availableStorageInMB"), StorageMB);
    Status.AvailableStorageMB = static_cast<float>(StorageMB);

    OnStatusUpdated.Broadcast(Status);
}
