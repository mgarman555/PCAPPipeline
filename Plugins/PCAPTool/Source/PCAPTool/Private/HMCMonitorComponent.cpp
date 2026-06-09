#include "HMCMonitorComponent.h"
#include "PCAPToolStatics.h"
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
#include "Engine/Texture2D.h"
#include "RHI.h"
#include "Engine/World.h"
#include "TimerManager.h"
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

void UHMCMonitorComponent::AssignActor(const FString& DeviceName, const FString& NewActorName)
{
    FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    const FString OldActorName = Config->ActorName;
    if (OldActorName == NewActorName) return;   // nothing to do

    // 1. Config + status both carry the actor name.
    Config->ActorName = NewActorName;
    if (FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName))
    {
        Status->ActorName = NewActorName;
    }

    // 2. CameraFeeds is keyed by ActorName — move this device's feeds to the new key.
    TArray<FHMCCameraFeed>& NewGroup = CameraFeeds.FindOrAdd(NewActorName);
    if (TArray<FHMCCameraFeed>* OldGroup = CameraFeeds.Find(OldActorName))
    {
        for (int32 i = OldGroup->Num() - 1; i >= 0; --i)
        {
            if ((*OldGroup)[i].DeviceName == DeviceName)
            {
                FHMCCameraFeed Feed = (*OldGroup)[i];
                Feed.ActorName = NewActorName;
                NewGroup.Add(Feed);
                OldGroup->RemoveAt(i);
            }
        }
        // Drop the old actor's group if it no longer holds any feeds.
        if (OldGroup->Num() == 0)
        {
            CameraFeeds.Remove(OldActorName);
        }
    }

    SaveConfig();

    // 3. Refresh any bound UI (Preview regroups by actor, OperatorPanel card relabels).
    if (FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName))
    {
        OnStatusUpdated.Broadcast(DeviceName, *Status);
    }
}

TArray<FHMCDeviceConfig> UHMCMonitorComponent::GetRegisteredDevices() const
{
    TArray<FHMCDeviceConfig> Out;
    RegisteredConfigs.GenerateValueArray(Out);
    return Out;
}

// ─── Connection (HTTP polling) ──────────────────────────────────────────────────

void UHMCMonitorComponent::ConnectDevice(const FString& DeviceName)
{
    // Already polling — skip
    if (PollTimers.Contains(DeviceName)) return;

    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    SetConnectionState(DeviceName, EHMCConnectionState::Connected);

    // Mark feeds as Clear (standby) on "connect"
    for (auto& Pair : CameraFeeds)
        for (FHMCCameraFeed& Feed : Pair.Value)
            if (Feed.DeviceName == DeviceName &&
                Feed.FeedState == EHMCFeedState::Disconnected)
                Feed.FeedState = EHMCFeedState::Clear;

    // Fire first poll immediately, then on timer
    PollDevice(DeviceName);

    if (UWorld* World = GetWorld())
    {
        FTimerHandle Handle;
        World->GetTimerManager().SetTimer(
            Handle,
            FTimerDelegate::CreateUObject(this, &UHMCMonitorComponent::PollDevice, DeviceName),
            PollIntervalSeconds,
            true);   // looping
        PollTimers.Add(DeviceName, Handle);
    }

    // Start the continuous frame chains (run off HTTP completions, not the timer —
    // so video streams even though this actor's world timer can't tick in-editor).
    // One per camera, concurrent.
    FrameStreamDevices.Add(DeviceName);
    PumpFrameCam(DeviceName, 0);
    PumpFrameCam(DeviceName, 1);
}

void UHMCMonitorComponent::ConnectAll()
{
    for (const auto& Pair : RegisteredConfigs)
    {
        ConnectDevice(Pair.Key);
    }
}

void UHMCMonitorComponent::PollAllDevicesNow()
{
    // PollTimers holds exactly the set of connected devices.
    TArray<FString> DeviceNames;
    PollTimers.GenerateKeyArray(DeviceNames);
    for (const FString& Name : DeviceNames)
    {
        PollDevice(Name);
    }
}

void UHMCMonitorComponent::DisconnectDevice(const FString& DeviceName)
{
    if (FTimerHandle* Handle = PollTimers.Find(DeviceName))
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(*Handle);
        }
        PollTimers.Remove(DeviceName);
    }
    // Stop both frame chains (any in-flight requests just finish unused).
    FrameStreamDevices.Remove(DeviceName);
    FrameInFlight.Remove(FString::Printf(TEXT("%s_0"), *DeviceName));
    FrameInFlight.Remove(FString::Printf(TEXT("%s_1"), *DeviceName));
    FrameNoFace.Remove(FString::Printf(TEXT("%s_0"), *DeviceName));
    FrameNoFace.Remove(FString::Printf(TEXT("%s_1"), *DeviceName));
    SetConnectionState(DeviceName, EHMCConnectionState::Disconnected);
    MarkFeedsDisconnected(DeviceName);
}

void UHMCMonitorComponent::DisconnectAll()
{
    TArray<FString> DeviceNames;
    PollTimers.GenerateKeyArray(DeviceNames);
    for (const FString& Name : DeviceNames)
    {
        DisconnectDevice(Name);
    }
}

// ─── Status Poll ────────────────────────────────────────────────────────────────

void UHMCMonitorComponent::PollDevice(FString DeviceName)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    // Exact endpoint the Technoprops MugShot web UI uses
    const FString URL = FString::Printf(
        TEXT("http://%s/control?cmd=no&param="), *Config->IPAddress);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(1.5f);
    Request->OnProcessRequestComplete().BindUObject(
        this, &UHMCMonitorComponent::OnPollResponse, DeviceName);
    Request->ProcessRequest();
}

void UHMCMonitorComponent::OnPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                                          bool bWasSuccessful, FString DeviceName)
{
    FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName);
    if (!Status) return;

    if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
    {
        SetConnectionState(DeviceName, EHMCConnectionState::Offline);
        MarkFeedsDisconnected(DeviceName);
        OnStatusUpdated.Broadcast(DeviceName, *Status);
        return;
    }

    // Back online after offline
    if (Status->ConnectionState == EHMCConnectionState::Offline)
        SetConnectionState(DeviceName, EHMCConnectionState::Connected);

    TSharedPtr<FJsonObject> JsonObj;
    TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(Response->GetContentAsString());
    if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid()) return;

    double Val = 0.0;

    JsonObj->TryGetNumberField(TEXT("nRecording"),            Val);
    Status->bIsRecording = Val != 0.0;

    Val = 0.0;
    JsonObj->TryGetNumberField(TEXT("batteryVoltage"),        Val);
    Status->BatteryVoltage = static_cast<float>(Val);

    Val = 0.0;
    JsonObj->TryGetNumberField(TEXT("availableStorageInMB"),  Val);
    Status->AvailableStorageMB = static_cast<float>(Val);

    Val = 0.0;
    JsonObj->TryGetNumberField(TEXT("cpuUsage"),              Val);
    Status->CPUUsagePercent = static_cast<float>(Val);

    Val = 0.0;
    JsonObj->TryGetNumberField(TEXT("cpuTemp"),               Val);
    Status->TemperatureCelsius = static_cast<float>(Val);

    Val = 0.0;
    JsonObj->TryGetNumberField(TEXT("frameRate"),             Val);
    Status->FPS = static_cast<float>(Val);

    // Per-camera telemetry
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("skippedFrames0"), Val); Status->DroppedFrames0 = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("skippedFrames1"), Val); Status->DroppedFrames1 = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("exposure0"),      Val); Status->Exposure0      = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("exposure1"),      Val); Status->Exposure1      = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("gain0"),          Val); Status->Gain0          = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("gain1"),          Val); Status->Gain1          = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("topLights"),      Val); Status->TopLights      = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("bottomLights"),   Val); Status->BottomLights   = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("streaming0"),     Val); Status->bStreaming0    = Val != 0.0;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("streaming1"),     Val); Status->bStreaming1    = Val != 0.0;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("width"),          Val); Status->FrameWidth     = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("height"),         Val); Status->FrameHeight    = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("rotation0"),      Val); Status->Rotation0      = (int32)Val;
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("rotation1"),      Val); Status->Rotation1      = (int32)Val;

    JsonObj->TryGetStringField(TEXT("takename"),                 Status->CurrentTakeName);
    JsonObj->TryGetStringField(TEXT("lastMovieIntegrityStatus"), Status->LastClipStatus);

    Status->LastUpdateTime = FDateTime::UtcNow();
    Status->StatusMessage  = GenerateStatusQuip(*Status);
    Status->IssueFlags0    = UPCAPToolStatics::EvaluateCameraIssues(*Status, 0);
    Status->IssueFlags1    = UPCAPToolStatics::EvaluateCameraIssues(*Status, 1);

    // HTTP completes on the game thread in UE5 — no AsyncTask needed
    OnStatusUpdated.Broadcast(DeviceName, *Status);

    // Re-arm both camera chains in case one stalled (e.g. after a reconnect).
    // Idempotent — no-ops while that camera already has a request in flight.
    PumpFrameCam(DeviceName, 0);
    PumpFrameCam(DeviceName, 1);
}

// ─── Status accessors ───────────────────────────────────────────────────────────

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

// ─── Commands (HTTP) ──────────────────────────────────────────────────────────

void UHMCMonitorComponent::SendCommand(const FString& DeviceName, const FString& Command)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    // Technoprops MugShot control endpoint. TEST ONLY — fire and forget.
    const FString URL = FString::Printf(
        TEXT("http://%s/control?cmd=%s&param="), *Config->IPAddress, *Command);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(1.5f);
    Request->ProcessRequest();
}

void UHMCMonitorComponent::SendDeviceCommand(const FString& DeviceName, const FString& Cmd,
                                             const FString& Param, const FString& ExtraKey,
                                             const FString& ExtraVal)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    // Mirrors the proven poll endpoint (GET query on /control). If firmware rejects
    // GET, switch SetVerb to POST and move the query into the request body.
    FString URL = FString::Printf(TEXT("http://%s/control?cmd=%s&param=%s"),
        *Config->IPAddress, *Cmd, *Param);
    if (!ExtraKey.IsEmpty())
        URL += FString::Printf(TEXT("&%s=%s"), *ExtraKey, *ExtraVal);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(1.5f);
    Request->ProcessRequest();   // fire and forget — value confirms on next poll
}

// ─── Issue flags ───────────────────────────────────────────────────────────────

void UHMCMonitorComponent::SetManualIssue(const FString& DeviceName, EHMCManualIssue Issue, bool bSet)
{
    const int32 FlagBit = 1 << (16 + static_cast<int32>(Issue));   // maps to HMC_Manual_*
    int32& Flags = ManualIssueFlags.FindOrAdd(DeviceName);
    if (bSet) Flags |= FlagBit;
    else      Flags &= ~FlagBit;

    if (FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName))
        OnStatusUpdated.Broadcast(DeviceName, *Status);
}

bool UHMCMonitorComponent::GetManualIssue(const FString& DeviceName, EHMCManualIssue Issue) const
{
    const int32 FlagBit = 1 << (16 + static_cast<int32>(Issue));
    return (GetManualIssueFlags(DeviceName) & FlagBit) != 0;
}

int32 UHMCMonitorComponent::GetManualIssueFlags(const FString& DeviceName) const
{
    const int32* Flags = ManualIssueFlags.Find(DeviceName);
    return Flags ? *Flags : 0;
}

int32 UHMCMonitorComponent::GetEffectiveIssueFlags(const FString& DeviceName, int32 CameraIndex) const
{
    int32 Hardware = 0;
    if (const FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName))
        Hardware = (CameraIndex == 0) ? Status->IssueFlags0 : Status->IssueFlags1;

    // Frame-derived: no subject detected (or no signal) → red NoFace flag.
    const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const int32 FrameFlags = FrameNoFace.Contains(Key) ? HMC_Issue_NoFace : 0;

    return Hardware | GetManualIssueFlags(DeviceName) | FrameFlags;
}

// ─── Video Frames (HTTP pull) ─────────────────────────────────────────────────

void UHMCMonitorComponent::RequestVideoFrame(const FString& DeviceName, int32 CameraIndex)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    // Device cameras are 0-based: cam=0 (Top), cam=1 (Bot). cam=2 returns HTTP 400.
    const FString URL = FString::Printf(TEXT("http://%s/video?cam=%d"),
        *Config->IPAddress, CameraIndex);

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
    const FString CamKey = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    FrameInFlight.Remove(CamKey);

    // Re-arm the next request BEFORE decoding so its network round-trip overlaps
    // this frame's decode (pipelines network with decode).
    PumpFrameCam(DeviceName, CameraIndex);

    UTexture2D* Texture = nullptr;
    bool bSubject = false;
    if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() == 200)
    {
        // Decode is thread-safe; texture creation happens here since HTTP callbacks
        // fire on the game thread in UE5.
        const TArray<uint8>& RawBytes = Response->GetContent();
        IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
        TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(EImageFormat::JPEG);

        TArray<uint8> Uncompressed;
        if (IW.IsValid() && IW->SetCompressed(RawBytes.GetData(), RawBytes.Num())
            && IW->GetRaw(ERGBFormat::BGRA, 8, Uncompressed))
        {
            Texture = UpdateFrameTexture(CamKey, Uncompressed, IW->GetWidth(), IW->GetHeight());
            bSubject = UPCAPToolStatics::FrameHasSubject(Uncompressed, IW->GetWidth(), IW->GetHeight());
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] HMC %s cam%d frame request failed (HTTP %d)"),
            *DeviceName, CameraIndex, Response.IsValid() ? Response->GetResponseCode() : -1);
    }

    // No subject in frame (or no signal at all) → mark so the feed border goes red.
    if (bSubject) FrameNoFace.Remove(CamKey); else FrameNoFace.Add(CamKey);

    OnFrameReceived.Broadcast(DeviceName, CameraIndex, Texture);
}

void UHMCMonitorComponent::PumpFrameCam(const FString& DeviceName, int32 CameraIndex)
{
    if (!FrameStreamDevices.Contains(DeviceName)) return;   // not streaming

    // One chain PER CAMERA — the device serves cam0 and cam1 concurrently, so both
    // stream at full rate in parallel instead of alternating.
    const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    if (FrameInFlight.Contains(Key)) return;                // this camera already in flight

    const FHMCDeviceStatus* S = DeviceStatuses.Find(DeviceName);
    if (!S || S->ConnectionState != EHMCConnectionState::Connected) return;

    FrameInFlight.Add(Key);
    RequestVideoFrame(DeviceName, CameraIndex);
}

// ─── Utilities ────────────────────────────────────────────────────────────────

UTexture2D* UHMCMonitorComponent::UpdateFrameTexture(const FString& Key, const TArray<uint8>& BGRA,
                                                      int32 Width, int32 Height)
{
    if (Width <= 0 || Height <= 0 || BGRA.Num() < Width * Height * 4) return nullptr;

    UTexture2D* Texture = nullptr;
    if (TObjectPtr<UTexture2D>* Found = FrameTextureCache.Find(Key))
        Texture = Found->Get();

    // Allocate once per camera (or if the dimensions change); reuse thereafter.
    if (!Texture || Texture->GetSizeX() != Width || Texture->GetSizeY() != Height)
    {
        Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
        if (!Texture) return nullptr;
        Texture->UpdateResource();
        FrameTextureCache.Add(Key, Texture);
    }

    // Update GPU pixels in place — no reallocation, no per-frame UpdateResource.
    if (Texture->GetResource())
    {
        const int32 DataSize = Width * Height * 4;
        uint8* Buffer = static_cast<uint8*>(FMemory::Malloc(DataSize));
        FMemory::Memcpy(Buffer, BGRA.GetData(), DataSize);

        FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
        Texture->UpdateTextureRegions(0, 1, Region, Width * 4, 4, Buffer,
            [](uint8* InData, const FUpdateTextureRegion2D* InRegions)
            {
                delete InRegions;
                FMemory::Free(InData);
            });
    }
    return Texture;
}

void UHMCMonitorComponent::SetConnectionState(const FString& DeviceName, EHMCConnectionState NewState)
{
    FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName);
    const bool bChanged = !Status || Status->ConnectionState != NewState;
    if (Status)
    {
        Status->ConnectionState = NewState;
    }
    if (bChanged)
    {
        const TCHAR* StateStr =
            NewState == EHMCConnectionState::Connected ? TEXT("Connected") :
            NewState == EHMCConnectionState::Offline   ? TEXT("Offline")   : TEXT("Disconnected");
        UE_LOG(LogTemp, Log, TEXT("[PCAPTool] HMC %s -> %s"), *DeviceName, StateStr);
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
