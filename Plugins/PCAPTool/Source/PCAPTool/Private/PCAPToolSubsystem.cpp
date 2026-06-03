#include "PCAPToolSubsystem.h"
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

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void UPCAPToolSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadConfig();
}

void UPCAPToolSubsystem::Deinitialize()
{
    DisconnectAll();
    Super::Deinitialize();
}

// ─── Registration ─────────────────────────────────────────────────────────────

void UPCAPToolSubsystem::RegisterDevice(const FHMCDeviceConfig& Config)
{
    RegisteredConfigs.Add(Config.DeviceName, Config);

    FHMCDeviceStatus& Status = DeviceStatuses.FindOrAdd(Config.DeviceName);
    Status.DeviceName      = Config.DeviceName;
    Status.IPAddress       = Config.IPAddress;
    Status.ActorName       = Config.ActorName;
    Status.ConnectionState = EHMCConnectionState::Disconnected;

    TArray<FHMCCameraFeed>& Feeds = CameraFeeds.FindOrAdd(Config.ActorName);
    bool bExists = Feeds.ContainsByPredicate(
        [&](const FHMCCameraFeed& F){ return F.DeviceName == Config.DeviceName; });

    if (!bExists)
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

void UPCAPToolSubsystem::UnregisterDevice(const FString& DeviceName)
{
    DisconnectDevice(DeviceName);
    RegisteredConfigs.Remove(DeviceName);
    DeviceStatuses.Remove(DeviceName);

    for (auto& Pair : CameraFeeds)
    {
        Pair.Value.RemoveAll([&DeviceName](const FHMCCameraFeed& F){
            return F.DeviceName == DeviceName;
        });
    }

    SaveConfig();
}

TArray<FHMCDeviceConfig> UPCAPToolSubsystem::GetRegisteredDevices() const
{
    TArray<FHMCDeviceConfig> Out;
    RegisteredConfigs.GenerateValueArray(Out);
    return Out;
}

// ─── Connection ───────────────────────────────────────────────────────────────

void UPCAPToolSubsystem::ConnectDevice(const FString& DeviceName)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    if (TSharedPtr<IWebSocket>* Existing = ActiveSockets.Find(DeviceName))
    {
        if ((*Existing)->IsConnected()) return; // already connected
        (*Existing)->Close();
    }

    FString Endpoint = Config->WebSocketEndpoint;
    if (Endpoint.IsEmpty())
    {
        Endpoint = FString::Printf(TEXT("ws://%s/ws"), *Config->IPAddress);
    }

    TSharedPtr<IWebSocket> Socket = FWebSocketsModule::Get().CreateWebSocket(Endpoint, TEXT("ws"));

    Socket->OnConnected().AddUObject(this, &UPCAPToolSubsystem::HandleConnected, DeviceName);
    Socket->OnConnectionError().AddUObject(this, &UPCAPToolSubsystem::HandleConnectionError, DeviceName);
    Socket->OnClosed().AddUObject(this, &UPCAPToolSubsystem::HandleClosed, DeviceName);
    Socket->OnMessage().AddUObject(this, &UPCAPToolSubsystem::HandleMessage, DeviceName);
    Socket->OnRawMessage().AddUObject(this, &UPCAPToolSubsystem::HandleRawMessage, DeviceName);

    ActiveSockets.Add(DeviceName, Socket);
    Socket->Connect();
}

void UPCAPToolSubsystem::ConnectAll()
{
    for (const auto& Pair : RegisteredConfigs)
    {
        ConnectDevice(Pair.Key);
    }
}

void UPCAPToolSubsystem::DisconnectDevice(const FString& DeviceName)
{
    if (TSharedPtr<IWebSocket>* Socket = ActiveSockets.Find(DeviceName))
    {
        (*Socket)->Close();
        ActiveSockets.Remove(DeviceName);
    }
    SetConnectionState(DeviceName, EHMCConnectionState::Disconnected);
    MarkFeedsDisconnected(DeviceName);
}

void UPCAPToolSubsystem::DisconnectAll()
{
    TArray<FString> Names;
    ActiveSockets.GenerateKeyArray(Names);
    for (const FString& Name : Names)
    {
        DisconnectDevice(Name);
    }
}

// ─── Status ───────────────────────────────────────────────────────────────────

FHMCDeviceStatus UPCAPToolSubsystem::GetDeviceStatus(const FString& DeviceName) const
{
    const FHMCDeviceStatus* S = DeviceStatuses.Find(DeviceName);
    return S ? *S : FHMCDeviceStatus();
}

TArray<FHMCDeviceStatus> UPCAPToolSubsystem::GetAllDeviceStatuses() const
{
    TArray<FHMCDeviceStatus> Out;
    DeviceStatuses.GenerateValueArray(Out);
    return Out;
}

// ─── Camera Feeds ─────────────────────────────────────────────────────────────

TArray<FHMCCameraFeed> UPCAPToolSubsystem::GetFeedsForActor(const FString& ActorName) const
{
    const TArray<FHMCCameraFeed>* Feeds = CameraFeeds.Find(ActorName);
    if (!Feeds) return {};

    TArray<FHMCCameraFeed> Out = *Feeds;
    for (FHMCCameraFeed& Feed : Out)
    {
        const FString Key = FString::Printf(TEXT("%s_%d"), *Feed.DeviceName, Feed.CameraIndex);
        if (const TObjectPtr<UTexture2D>* Tex = FrameTextureCache.Find(Key))
            Feed.FrameTexture = *Tex;
    }
    return Out;
}

TArray<FHMCCameraFeed> UPCAPToolSubsystem::GetAllFeeds() const
{
    TArray<FHMCCameraFeed> Out;
    for (const auto& Pair : CameraFeeds)
    {
        for (const FHMCCameraFeed& Feed : Pair.Value)
        {
            FHMCCameraFeed Copy = Feed;
            const FString Key = FString::Printf(TEXT("%s_%d"), *Feed.DeviceName, Feed.CameraIndex);
            if (const TObjectPtr<UTexture2D>* Tex = FrameTextureCache.Find(Key))
                Copy.FrameTexture = *Tex;
            Out.Add(Copy);
        }
    }
    return Out;
}

void UPCAPToolSubsystem::SetFeedState(const FString& DeviceName, EHMCCameraRole Role, EHMCFeedState State)
{
    if (State == EHMCFeedState::Disconnected) return; // live signal only

    for (auto& Pair : CameraFeeds)
        for (FHMCCameraFeed& Feed : Pair.Value)
            if (Feed.DeviceName == DeviceName && Feed.Role == Role)
            {
                Feed.FeedState = State;
                return;
            }
}

void UPCAPToolSubsystem::SetCameraRole(const FString& DeviceName, int32 CameraIndex, EHMCCameraRole NewRole)
{
    for (auto& Pair : CameraFeeds)
        for (FHMCCameraFeed& Feed : Pair.Value)
            if (Feed.DeviceName == DeviceName && Feed.CameraIndex == CameraIndex)
            {
                Feed.Role = NewRole;
                return;
            }
}

// ─── Commands ─────────────────────────────────────────────────────────────────

void UPCAPToolSubsystem::SendCommand(const FString& DeviceName, const FString& Command)
{
    TSharedPtr<IWebSocket>* Socket = ActiveSockets.Find(DeviceName);
    if (!Socket || !(*Socket)->IsConnected()) return;

    const FString Payload = FString::Printf(TEXT("{\"cmd\":\"%s\"}"), *Command);
    (*Socket)->Send(Payload);
}

// ─── Video Frames ─────────────────────────────────────────────────────────────

void UPCAPToolSubsystem::RequestVideoFrame(const FString& DeviceName, int32 CameraIndex)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    const FString URL = FString::Printf(TEXT("http://%s/video?cam=%d"),
        *Config->IPAddress, CameraIndex + 1);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(3.0f);
    Request->OnProcessRequestComplete().BindUObject(
        this, &UPCAPToolSubsystem::OnVideoFrameResponse, DeviceName, CameraIndex);
    Request->ProcessRequest();
}

UTexture2D* UPCAPToolSubsystem::GetLastFrame(const FString& DeviceName, int32 CameraIndex) const
{
    const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const TObjectPtr<UTexture2D>* Found = FrameTextureCache.Find(Key);
    return Found ? Found->Get() : nullptr;
}

void UPCAPToolSubsystem::OnVideoFrameResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                                               bool bWasSuccessful, FString DeviceName, int32 CameraIndex)
{
    if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
    {
        OnFrameReceived.Broadcast(DeviceName, CameraIndex, nullptr);
        return;
    }

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

void UPCAPToolSubsystem::HandleConnected(FString DeviceName)
{
    AsyncTask(ENamedThreads::GameThread, [this, DeviceName]()
    {
        SetConnectionState(DeviceName, EHMCConnectionState::Connected);
        for (auto& Pair : CameraFeeds)
            for (FHMCCameraFeed& Feed : Pair.Value)
                if (Feed.DeviceName == DeviceName &&
                    Feed.FeedState == EHMCFeedState::Disconnected)
                    Feed.FeedState = EHMCFeedState::Clear;
    });
}

void UPCAPToolSubsystem::HandleConnectionError(const FString& Error, FString DeviceName)
{
    AsyncTask(ENamedThreads::GameThread, [this, DeviceName, Error]()
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] %s WS error: %s"), *DeviceName, *Error);
        SetConnectionState(DeviceName, EHMCConnectionState::Offline);
        MarkFeedsDisconnected(DeviceName);
    });
}

void UPCAPToolSubsystem::HandleClosed(int32 StatusCode, const FString& Reason,
                                       bool bWasClean, FString DeviceName)
{
    AsyncTask(ENamedThreads::GameThread, [this, DeviceName, bWasClean]()
    {
        SetConnectionState(DeviceName,
            bWasClean ? EHMCConnectionState::Disconnected : EHMCConnectionState::Offline);
        MarkFeedsDisconnected(DeviceName);
        ActiveSockets.Remove(DeviceName);
    });
}

void UPCAPToolSubsystem::HandleMessage(const FString& Message, FString DeviceName)
{
    TSharedPtr<FJsonObject> JsonObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid()) return;

    double RecordingVal = 0, BattV = 0, StorageMB = 0, CPU = 0, Temp = 0, FPS = 0;
    FString TakeName, ClipStatus;

    JsonObj->TryGetNumberField(TEXT("nRecording"),           RecordingVal);
    JsonObj->TryGetNumberField(TEXT("batteryVoltage"),       BattV);
    JsonObj->TryGetNumberField(TEXT("availableStorageInMB"), StorageMB);
    JsonObj->TryGetNumberField(TEXT("cpuUsage"),             CPU);
    JsonObj->TryGetNumberField(TEXT("temperature"),          Temp);
    JsonObj->TryGetNumberField(TEXT("fps"),                  FPS);
    JsonObj->TryGetStringField(TEXT("takename"),             TakeName);
    JsonObj->TryGetStringField(TEXT("lastClipStatus"),       ClipStatus);

    AsyncTask(ENamedThreads::GameThread, [this, DeviceName,
        RecordingVal, BattV, StorageMB, CPU, Temp, FPS, TakeName, ClipStatus]()
    {
        FHMCDeviceStatus* S = DeviceStatuses.Find(DeviceName);
        if (!S) return;

        S->bIsRecording       = RecordingVal != 0.0;
        S->BatteryVoltage     = static_cast<float>(BattV);
        S->AvailableStorageMB = static_cast<float>(StorageMB);
        S->CPUUsagePercent    = static_cast<float>(CPU);
        S->TemperatureCelsius = static_cast<float>(Temp);
        S->FPS                = static_cast<float>(FPS);
        S->CurrentTakeName    = TakeName;
        S->LastClipStatus     = ClipStatus;
        S->LastUpdateTime     = FDateTime::UtcNow();
        S->StatusMessage      = GenerateStatusQuip(*S);

        OnStatusUpdated.Broadcast(DeviceName, *S);
    });
}

void UPCAPToolSubsystem::HandleRawMessage(const void* Data, SIZE_T Size,
                                           SIZE_T BytesRemaining, FString DeviceName)
{
    if (BytesRemaining != 0) return;

    TArray<uint8> Bytes;
    Bytes.Append(static_cast<const uint8*>(Data), Size);

    IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
    TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(EImageFormat::JPEG);
    if (!IW.IsValid() || !IW->SetCompressed(Bytes.GetData(), Bytes.Num())) return;

    TArray<uint8> RawBGRA;
    if (!IW->GetRaw(ERGBFormat::BGRA, 8, RawBGRA)) return;

    const int32 W = IW->GetWidth();
    const int32 H = IW->GetHeight();

    int32& Counter = BinaryFrameCounter.FindOrAdd(DeviceName);
    const int32 CameraIndex = Counter % 2;
    Counter++;

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

UTexture2D* UPCAPToolSubsystem::CreateTextureFromRaw(const TArray<uint8>& RawBGRA,
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

void UPCAPToolSubsystem::SetConnectionState(const FString& DeviceName, EHMCConnectionState NewState)
{
    FHMCDeviceStatus* S = DeviceStatuses.Find(DeviceName);
    if (S) S->ConnectionState = NewState;
    OnConnectionChanged.Broadcast(DeviceName, NewState);
}

void UPCAPToolSubsystem::MarkFeedsDisconnected(const FString& DeviceName)
{
    for (auto& Pair : CameraFeeds)
        for (FHMCCameraFeed& Feed : Pair.Value)
            if (Feed.DeviceName == DeviceName)
                Feed.FeedState = EHMCFeedState::Disconnected;
}

FString UPCAPToolSubsystem::GenerateStatusQuip(const FHMCDeviceStatus& S)
{
    if (S.AvailableStorageMB < 10240.f)   return TEXT("Storage critical. Wrap soon.");
    if (S.BatteryVoltage > 0.f &&
        S.BatteryVoltage < 13.0f)         return TEXT("Battery low. Eyes on it.");
    if (S.CPUUsagePercent > 80.f)         return TEXT("Running hot. Keep takes short.");
    if (S.TemperatureCelsius > 50.f)      return TEXT("Getting warm in here.");
    if (!S.LastClipStatus.IsEmpty() &&
        S.LastClipStatus != TEXT("Ready")) return TEXT("Last clip not verified yet.");
    if (S.CPUUsagePercent > 60.f)         return TEXT("A little busy, but managing.");
    if (S.BatteryVoltage > 15.0f)         return TEXT("Doing fine, though could use a coffee.");
    return TEXT("All good. Ready to shoot.");
}

// ─── Persistence ─────────────────────────────────────────────────────────────

FString UPCAPToolSubsystem::ConfigFilePath()
{
    return FPaths::ProjectSavedDir() / TEXT("PCAPTool") / TEXT("HMCConfig.json");
}

void UPCAPToolSubsystem::SaveConfig() const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> DeviceArray;

    for (const auto& Pair : RegisteredConfigs)
    {
        const FHMCDeviceConfig& C = Pair.Value;
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("deviceName"),        C.DeviceName);
        Obj->SetStringField(TEXT("ipAddress"),         C.IPAddress);
        Obj->SetStringField(TEXT("actorName"),         C.ActorName);
        Obj->SetStringField(TEXT("webSocketEndpoint"), C.WebSocketEndpoint);
        DeviceArray.Add(MakeShared<FJsonValueObject>(Obj));
    }

    Root->SetArrayField(TEXT("devices"), DeviceArray);

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

    const FString Path = ConfigFilePath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    FFileHelper::SaveStringToFile(JsonString, *Path);
}

void UPCAPToolSubsystem::LoadConfig()
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *ConfigFilePath())) return;

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

    const TArray<TSharedPtr<FJsonValue>>* DeviceArray;
    if (!Root->TryGetArrayField(TEXT("devices"), DeviceArray)) return;

    for (const TSharedPtr<FJsonValue>& Val : *DeviceArray)
    {
        TSharedPtr<FJsonObject> Obj = Val->AsObject();
        if (!Obj.IsValid()) continue;

        FHMCDeviceConfig Config;
        Obj->TryGetStringField(TEXT("deviceName"),        Config.DeviceName);
        Obj->TryGetStringField(TEXT("ipAddress"),         Config.IPAddress);
        Obj->TryGetStringField(TEXT("actorName"),         Config.ActorName);
        Obj->TryGetStringField(TEXT("webSocketEndpoint"), Config.WebSocketEndpoint);

        if (!Config.DeviceName.IsEmpty())
            RegisterDevice(Config);
    }
}
