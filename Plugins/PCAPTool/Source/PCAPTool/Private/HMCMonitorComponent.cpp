#include "HMCMonitorComponent.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Modules/ModuleManager.h"

UHMCMonitorComponent::UHMCMonitorComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UHMCMonitorComponent::BeginPlay()
{
    Super::BeginPlay();
    LoadConfig();
}

void UHMCMonitorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DisconnectAll();
    Super::EndPlay(EndPlayReason);
}

// ─── Registration ─────────────────────────────────────────────────────────────

void UHMCMonitorComponent::RegisterDevice(const FHMCDeviceConfig& Config)
{
    RegisteredConfigs.Add(Config.DeviceName, Config);

    // Initialise status entry
    FHMCDeviceStatus& Status = DeviceStatuses.FindOrAdd(Config.DeviceName);
    Status.DeviceName  = Config.DeviceName;
    Status.IPAddress   = Config.IPAddress;
    Status.ActorName   = Config.ActorName;
    Status.ConnectionState = EHMCConnectionState::Disconnected;

    // Seed two camera feeds (Top and Bottom) keyed by actor name
    TArray<FHMCCameraFeed>& Feeds = CameraFeeds.FindOrAdd(Config.ActorName);
    bool bHasThisDevice = Feeds.ContainsByPredicate(
        [&](const FHMCCameraFeed& F){ return F.DeviceName == Config.DeviceName; });

    if (!bHasThisDevice)
    {
        FHMCCameraFeed Top;
        Top.DeviceName  = Config.DeviceName;
        Top.ActorName   = Config.ActorName;
        Top.Role        = EHMCCameraRole::Top;
        Top.FeedState   = EHMCFeedState::Disconnected;
        Top.CameraIndex = 0;
        Feeds.Add(Top);

        FHMCCameraFeed Bot;
        Bot.DeviceName  = Config.DeviceName;
        Bot.ActorName   = Config.ActorName;
        Bot.Role        = EHMCCameraRole::Bottom;
        Bot.FeedState   = EHMCFeedState::Disconnected;
        Bot.CameraIndex = 1;
        Feeds.Add(Bot);
    }

    SaveConfig();
}

void UHMCMonitorComponent::UnregisterDevice(const FString& DeviceName)
{
    DisconnectDevice(DeviceName);
    RegisteredConfigs.Remove(DeviceName);
    DeviceStatuses.Remove(DeviceName);

    // Remove this device's feeds from all actor groups
    for (auto& Pair : CameraFeeds)
    {
        Pair.Value.RemoveAll([&DeviceName](const FHMCCameraFeed& F){
            return F.DeviceName == DeviceName;
        });
    }

    SaveConfig();
}

TArray<FHMCDeviceConfig> UHMCMonitorComponent::GetRegisteredDevices() const
{
    TArray<FHMCDeviceConfig> Out;
    RegisteredConfigs.GenerateValueArray(Out);
    return Out;
}

// ─── Connection ───────────────────────────────────────────────────────────────

void UHMCMonitorComponent::ConnectDevice(const FString& DeviceName)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    // Close existing socket if any
    if (TSharedPtr<IWebSocket>* ExistingSocket = ActiveSockets.Find(DeviceName))
    {
        if ((*ExistingSocket)->IsConnected())
        {
            (*ExistingSocket)->Close();
        }
    }

    FString Endpoint = Config->WebSocketEndpoint;
    if (Endpoint.IsEmpty())
    {
        Endpoint = FString::Printf(TEXT("ws://%s/ws"), *Config->IPAddress);
    }

    TSharedPtr<IWebSocket> Socket = FWebSocketsModule::Get().CreateWebSocket(Endpoint, TEXT("ws"));

    Socket->OnConnected().AddUObject(this, &UHMCMonitorComponent::HandleConnected, DeviceName);
    Socket->OnConnectionError().AddUObject(this, &UHMCMonitorComponent::HandleConnectionError, DeviceName);
    Socket->OnClosed().AddUObject(this, &UHMCMonitorComponent::HandleClosed, DeviceName);
    Socket->OnMessage().AddUObject(this, &UHMCMonitorComponent::HandleMessage, DeviceName);
    Socket->OnRawMessage().AddUObject(this, &UHMCMonitorComponent::HandleRawMessage, DeviceName);

    ActiveSockets.Add(DeviceName, Socket);
    Socket->Connect();
}

void UHMCMonitorComponent::ConnectAll()
{
    for (const auto& Pair : RegisteredConfigs)
    {
        ConnectDevice(Pair.Key);
    }
}

void UHMCMonitorComponent::DisconnectDevice(const FString& DeviceName)
{
    if (TSharedPtr<IWebSocket>* Socket = ActiveSockets.Find(DeviceName))
    {
        (*Socket)->Close();
        ActiveSockets.Remove(DeviceName);
    }
    SetConnectionState(DeviceName, EHMCConnectionState::Disconnected);
    MarkFeedsDisconnected(DeviceName);
}

void UHMCMonitorComponent::DisconnectAll()
{
    TArray<FString> DeviceNames;
    ActiveSockets.GenerateKeyArray(DeviceNames);
    for (const FString& Name : DeviceNames)
    {
        DisconnectDevice(Name);
    }
}

// ─── Status ───────────────────────────────────────────────────────────────────

FHMCDeviceStatus UHMCMonitorComponent::GetDeviceStatus(const FString& DeviceName) const
{
    const FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName);
    return Status ? *Status : FHMCDeviceStatus();
}

TArray<FHMCDeviceStatus> UHMCMonitorComponent::GetAllDeviceStatuses() const
{
    TArray<FHMCDeviceStatus> Out;
    DeviceStatuses.GenerateValueArray(Out);
    return Out;
}

// ─── Camera Feeds ─────────────────────────────────────────────────────────────

TArray<FHMCCameraFeed> UHMCMonitorComponent::GetFeedsForActor(const FString& ActorName) const
{
    const TArray<FHMCCameraFeed>* Feeds = CameraFeeds.Find(ActorName);
    if (!Feeds) return {};

    // Populate FrameTexture from cache before returning
    TArray<FHMCCameraFeed> Out = *Feeds;
    for (FHMCCameraFeed& Feed : Out)
    {
        const FString Key = FString::Printf(TEXT("%s_%d"), *Feed.DeviceName, Feed.CameraIndex);
        if (const TObjectPtr<UTexture2D>* Tex = FrameTextureCache.Find(Key))
        {
            Feed.FrameTexture = *Tex;
        }
    }
    return Out;
}

TArray<FHMCCameraFeed> UHMCMonitorComponent::GetAllFeeds() const
{
    TArray<FHMCCameraFeed> Out;
    for (const auto& Pair : CameraFeeds)
    {
        for (const FHMCCameraFeed& Feed : Pair.Value)
        {
            FHMCCameraFeed FeedCopy = Feed;
            const FString Key = FString::Printf(TEXT("%s_%d"), *Feed.DeviceName, Feed.CameraIndex);
            if (const TObjectPtr<UTexture2D>* Tex = FrameTextureCache.Find(Key))
            {
                FeedCopy.FrameTexture = *Tex;
            }
            Out.Add(FeedCopy);
        }
    }
    return Out;
}

void UHMCMonitorComponent::SetFeedState(const FString& DeviceName, EHMCCameraRole Role, EHMCFeedState State)
{
    // Prevent manually setting Disconnected — that is a live signal only
    if (State == EHMCFeedState::Disconnected) return;

    for (auto& Pair : CameraFeeds)
    {
        for (FHMCCameraFeed& Feed : Pair.Value)
        {
            if (Feed.DeviceName == DeviceName && Feed.Role == Role)
            {
                Feed.FeedState = State;
                return;
            }
        }
    }
}

void UHMCMonitorComponent::SetCameraRole(const FString& DeviceName, int32 CameraIndex, EHMCCameraRole NewRole)
{
    for (auto& Pair : CameraFeeds)
    {
        for (FHMCCameraFeed& Feed : Pair.Value)
        {
            if (Feed.DeviceName == DeviceName && Feed.CameraIndex == CameraIndex)
            {
                Feed.Role = NewRole;
                return;
            }
        }
    }
}

// ─── Commands ─────────────────────────────────────────────────────────────────

void UHMCMonitorComponent::SendCommand(const FString& DeviceName, const FString& Command)
{
    TSharedPtr<IWebSocket>* Socket = ActiveSockets.Find(DeviceName);
    if (!Socket || !(*Socket)->IsConnected()) return;

    const FString Payload = FString::Printf(TEXT("{\"cmd\":\"%s\"}"), *Command);
    (*Socket)->Send(Payload);
}

// ─── Video Frames (HTTP pull) ─────────────────────────────────────────────────

void UHMCMonitorComponent::RequestVideoFrame(const FString& DeviceName, int32 CameraIndex)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    // HTTP cam index is 1-based per HMC API spec
    const FString URL = FString::Printf(TEXT("http://%s/video?cam=%d"),
        *Config->IPAddress, CameraIndex + 1);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(3.0f);

    Request->OnProcessRequestComplete().BindUObject(
        this, &UHMCMonitorComponent::OnVideoFrameResponse, DeviceName, CameraIndex);

    Request->ProcessRequest();
}

UTexture2D* UHMCMonitorComponent::GetLastFrame(const FString& DeviceName, int32 CameraIndex) const
{
    const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const TObjectPtr<UTexture2D>* Found = FrameTextureCache.Find(Key);
    return Found ? Found->Get() : nullptr;
}

void UHMCMonitorComponent::OnVideoFrameResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                                                  bool bWasSuccessful, FString DeviceName, int32 CameraIndex)
{
    if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
    {
        OnFrameReceived.Broadcast(DeviceName, CameraIndex, nullptr);
        return;
    }

    // Decode is thread-safe; texture creation happens here since HTTP callbacks
    // fire on the game thread in UE5.
    const TArray<uint8>& RawBytes = Response->GetContent();
    IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
    TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(EImageFormat::JPEG);

    if (!IW.IsValid() || !IW->SetCompressed(RawBytes.GetData(), RawBytes.Num()))
    {
        OnFrameReceived.Broadcast(DeviceName, CameraIndex, nullptr);
        return;
    }

    TArray<uint8> Uncompressed;
    if (!IW->GetRaw(ERGBFormat::BGRA, 8, Uncompressed))
    {
        OnFrameReceived.Broadcast(DeviceName, CameraIndex, nullptr);
        return;
    }

    UTexture2D* Texture = CreateTextureFromRaw(Uncompressed, IW->GetWidth(), IW->GetHeight());
    if (Texture)
    {
        const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
        FrameTextureCache.Add(Key, Texture);
    }

    OnFrameReceived.Broadcast(DeviceName, CameraIndex, Texture);
}

// ─── WebSocket Handlers ───────────────────────────────────────────────────────

void UHMCMonitorComponent::HandleConnected(FString DeviceName)
{
    AsyncTask(ENamedThreads::GameThread, [this, DeviceName]()
    {
        SetConnectionState(DeviceName, EHMCConnectionState::Connected);

        // Mark feeds as Clear (standby) on connect
        for (auto& Pair : CameraFeeds)
        {
            for (FHMCCameraFeed& Feed : Pair.Value)
            {
                if (Feed.DeviceName == DeviceName &&
                    Feed.FeedState == EHMCFeedState::Disconnected)
                {
                    Feed.FeedState = EHMCFeedState::Clear;
                }
            }
        }
    });
}

void UHMCMonitorComponent::HandleConnectionError(const FString& Error, FString DeviceName)
{
    AsyncTask(ENamedThreads::GameThread, [this, DeviceName, Error]()
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAPTool|HMC] %s connection error: %s"),
            *DeviceName, *Error);
        SetConnectionState(DeviceName, EHMCConnectionState::Offline);
        MarkFeedsDisconnected(DeviceName);
    });
}

void UHMCMonitorComponent::HandleClosed(int32 StatusCode, const FString& Reason,
                                         bool bWasClean, FString DeviceName)
{
    AsyncTask(ENamedThreads::GameThread, [this, DeviceName, StatusCode, bWasClean]()
    {
        EHMCConnectionState NewState = bWasClean
            ? EHMCConnectionState::Disconnected
            : EHMCConnectionState::Offline;

        SetConnectionState(DeviceName, NewState);
        MarkFeedsDisconnected(DeviceName);
        ActiveSockets.Remove(DeviceName);
    });
}

void UHMCMonitorComponent::HandleMessage(const FString& Message, FString DeviceName)
{
    // JSON decode is safe on any thread
    TSharedPtr<FJsonObject> JsonObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);

    if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid()) return;

    // Capture parsed values for game thread
    double RecordingVal = 0, BattV = 0, StorageMB = 0, CPU = 0, Temp = 0, FPS = 0;
    FString TakeName, ClipStatus;

    JsonObj->TryGetNumberField(TEXT("nRecording"),        RecordingVal);
    JsonObj->TryGetNumberField(TEXT("batteryVoltage"),    BattV);
    JsonObj->TryGetNumberField(TEXT("availableStorageInMB"), StorageMB);
    JsonObj->TryGetNumberField(TEXT("cpuUsage"),          CPU);
    JsonObj->TryGetNumberField(TEXT("temperature"),       Temp);
    JsonObj->TryGetNumberField(TEXT("fps"),               FPS);
    JsonObj->TryGetStringField(TEXT("takename"),          TakeName);
    JsonObj->TryGetStringField(TEXT("lastClipStatus"),    ClipStatus);

    AsyncTask(ENamedThreads::GameThread, [this, DeviceName,
        RecordingVal, BattV, StorageMB, CPU, Temp, FPS, TakeName, ClipStatus]()
    {
        FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName);
        if (!Status) return;

        Status->bIsRecording      = RecordingVal != 0.0;
        Status->BatteryVoltage    = static_cast<float>(BattV);
        Status->AvailableStorageMB= static_cast<float>(StorageMB);
        Status->CPUUsagePercent   = static_cast<float>(CPU);
        Status->TemperatureCelsius= static_cast<float>(Temp);
        Status->FPS               = static_cast<float>(FPS);
        Status->CurrentTakeName   = TakeName;
        Status->LastClipStatus    = ClipStatus;
        Status->LastUpdateTime    = FDateTime::UtcNow();
        Status->StatusMessage     = GenerateStatusQuip(*Status);

        OnStatusUpdated.Broadcast(DeviceName, *Status);
    });
}

void UHMCMonitorComponent::HandleRawMessage(const void* Data, SIZE_T Size,
                                             SIZE_T BytesRemaining, FString DeviceName)
{
    // Only process complete frames (BytesRemaining == 0)
    if (BytesRemaining != 0) return;

    // Copy bytes — safe on any thread
    TArray<uint8> Bytes;
    Bytes.Append(static_cast<const uint8*>(Data), Size);

    // JPEG decode is thread-safe
    IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
    TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(EImageFormat::JPEG);

    if (!IW.IsValid() || !IW->SetCompressed(Bytes.GetData(), Bytes.Num())) return;

    TArray<uint8> RawBGRA;
    if (!IW->GetRaw(ERGBFormat::BGRA, 8, RawBGRA)) return;

    const int32 W = IW->GetWidth();
    const int32 H = IW->GetHeight();

    // Alternate camera index per device (0 then 1 then 0...)
    int32& Counter = BinaryFrameCounter.FindOrAdd(DeviceName);
    const int32 CameraIndex = Counter % 2;
    Counter++;

    // Texture creation must be on game thread
    AsyncTask(ENamedThreads::GameThread, [this, DeviceName, CameraIndex, W, H,
                                           RawBGRA = MoveTemp(RawBGRA)]() mutable
    {
        UTexture2D* Texture = CreateTextureFromRaw(RawBGRA, W, H);
        if (Texture)
        {
            const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
            FrameTextureCache.Add(Key, Texture);
        }
        OnFrameReceived.Broadcast(DeviceName, CameraIndex, Texture);
    });
}

// ─── Utilities ────────────────────────────────────────────────────────────────

UTexture2D* UHMCMonitorComponent::CreateTextureFromRaw(const TArray<uint8>& RawBGRA,
                                                        int32 Width, int32 Height)
{
    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
    if (!Texture) return nullptr;

    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Data, RawBGRA.GetData(), RawBGRA.Num());
    Mip.BulkData.Unlock();
    Texture->UpdateResource();

    return Texture;
}

void UHMCMonitorComponent::SetConnectionState(const FString& DeviceName, EHMCConnectionState NewState)
{
    FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName);
    if (Status)
    {
        Status->ConnectionState = NewState;
    }
    OnConnectionChanged.Broadcast(DeviceName, NewState);
}

void UHMCMonitorComponent::MarkFeedsDisconnected(const FString& DeviceName)
{
    for (auto& Pair : CameraFeeds)
    {
        for (FHMCCameraFeed& Feed : Pair.Value)
        {
            if (Feed.DeviceName == DeviceName)
            {
                Feed.FeedState = EHMCFeedState::Disconnected;
            }
        }
    }
}

// ─── Status Quip ─────────────────────────────────────────────────────────────

FString UHMCMonitorComponent::GenerateStatusQuip(const FHMCDeviceStatus& S)
{
    if (S.AvailableStorageMB < 10240.f)
        return TEXT("Storage critical. Wrap soon.");
    if (S.BatteryVoltage > 0.f && S.BatteryVoltage < 13.0f)
        return TEXT("Battery low. Eyes on it.");
    if (S.CPUUsagePercent > 80.f)
        return TEXT("Running hot. Keep takes short.");
    if (S.TemperatureCelsius > 50.f)
        return TEXT("Getting warm in here.");
    if (!S.LastClipStatus.IsEmpty() && S.LastClipStatus != TEXT("Ready"))
        return TEXT("Last clip not verified yet.");
    if (S.CPUUsagePercent > 60.f)
        return TEXT("A little busy, but managing.");
    if (S.BatteryVoltage > 15.0f)
        return TEXT("Doing fine, though could use a coffee.");
    return TEXT("All good. Ready to shoot.");
}

// ─── Persistence ─────────────────────────────────────────────────────────────

FString UHMCMonitorComponent::ConfigFilePath()
{
    return FPaths::ProjectSavedDir() / TEXT("PCAPTool") / TEXT("HMCConfig.json");
}

void UHMCMonitorComponent::SaveConfig() const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> DeviceArray;

    for (const auto& Pair : RegisteredConfigs)
    {
        const FHMCDeviceConfig& C = Pair.Value;
        TSharedPtr<FJsonObject> DevObj = MakeShared<FJsonObject>();
        DevObj->SetStringField(TEXT("deviceName"),       C.DeviceName);
        DevObj->SetStringField(TEXT("ipAddress"),        C.IPAddress);
        DevObj->SetStringField(TEXT("actorName"),        C.ActorName);
        DevObj->SetStringField(TEXT("webSocketEndpoint"),C.WebSocketEndpoint);
        DeviceArray.Add(MakeShared<FJsonValueObject>(DevObj));
    }

    Root->SetArrayField(TEXT("devices"), DeviceArray);

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

    const FString Path = ConfigFilePath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    FFileHelper::SaveStringToFile(JsonString, *Path);
}

void UHMCMonitorComponent::LoadConfig()
{
    const FString Path = ConfigFilePath();
    FString JsonString;

    if (!FFileHelper::LoadFileToString(JsonString, *Path)) return;

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

    const TArray<TSharedPtr<FJsonValue>>* DeviceArray;
    if (!Root->TryGetArrayField(TEXT("devices"), DeviceArray)) return;

    for (const TSharedPtr<FJsonValue>& Val : *DeviceArray)
    {
        TSharedPtr<FJsonObject> DevObj = Val->AsObject();
        if (!DevObj.IsValid()) continue;

        FHMCDeviceConfig Config;
        DevObj->TryGetStringField(TEXT("deviceName"),        Config.DeviceName);
        DevObj->TryGetStringField(TEXT("ipAddress"),         Config.IPAddress);
        DevObj->TryGetStringField(TEXT("actorName"),         Config.ActorName);
        DevObj->TryGetStringField(TEXT("webSocketEndpoint"), Config.WebSocketEndpoint);

        if (!Config.DeviceName.IsEmpty())
        {
            RegisterDevice(Config);
        }
    }
}
