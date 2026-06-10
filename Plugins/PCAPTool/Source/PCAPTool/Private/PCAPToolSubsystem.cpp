#include "PCAPToolSubsystem.h"
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
#include "TimerManager.h"
#include "Editor.h"
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
    Status.ActorID       = Config.ActorID;
    Status.ConnectionState = EHMCConnectionState::Disconnected;

    TArray<FHMCCameraFeed>& Feeds = CameraFeeds.FindOrAdd(Config.ActorID);
    bool bExists = Feeds.ContainsByPredicate(
        [&](const FHMCCameraFeed& F){ return F.DeviceName == Config.DeviceName; });

    if (!bExists)
    {
        FHMCCameraFeed Top;
        Top.DeviceName  = Config.DeviceName;
        Top.ActorID   = Config.ActorID;
        Top.Role        = EHMCCameraRole::Top;
        Top.FeedState   = EHMCFeedState::Disconnected;
        Top.CameraIndex = 0;
        Feeds.Add(Top);

        FHMCCameraFeed Bot;
        Bot.DeviceName  = Config.DeviceName;
        Bot.ActorID   = Config.ActorID;
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

void UPCAPToolSubsystem::AssignActor(const FString& DeviceName, const FString& NewActorID)
{
    FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    const FString OldActorID = Config->ActorID;
    if (OldActorID == NewActorID) return;   // nothing to do

    // 1. Config + status both carry the actor name.
    Config->ActorID = NewActorID;
    if (FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName))
    {
        Status->ActorID = NewActorID;
    }

    // 2. CameraFeeds is keyed by ActorID — move this device's feeds to the new key.
    TArray<FHMCCameraFeed>& NewGroup = CameraFeeds.FindOrAdd(NewActorID);
    if (TArray<FHMCCameraFeed>* OldGroup = CameraFeeds.Find(OldActorID))
    {
        for (int32 i = OldGroup->Num() - 1; i >= 0; --i)
        {
            if ((*OldGroup)[i].DeviceName == DeviceName)
            {
                FHMCCameraFeed Feed = (*OldGroup)[i];
                Feed.ActorID = NewActorID;
                NewGroup.Add(Feed);
                OldGroup->RemoveAt(i);
            }
        }
        if (OldGroup->Num() == 0)
        {
            CameraFeeds.Remove(OldActorID);
        }
    }

    SaveConfig();

    // 3. Refresh bound UI (Preview regroups by actor, Setup relabels the dropdown).
    if (FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName))
    {
        OnStatusUpdated.Broadcast(DeviceName, *Status);
    }
}

// ─── Connection (HTTP polling) ──────────────────────────────────────────────────

void UPCAPToolSubsystem::ConnectDevice(const FString& DeviceName)
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

    // EngineSubsystem has no world — use the editor's timer manager.
    if (GEditor)
    {
        FTimerHandle Handle;
        GEditor->GetTimerManager()->SetTimer(
            Handle,
            FTimerDelegate::CreateUObject(this, &UPCAPToolSubsystem::PollDevice, DeviceName),
            PollIntervalSeconds,
            true);   // looping
        PollTimers.Add(DeviceName, Handle);
    }

    // Start the continuous, self-sustaining frame chains (independent of the poll) —
    // one per camera, running concurrently.
    FrameStreamDevices.Add(DeviceName);
    PumpFrameCam(DeviceName, 0);
    PumpFrameCam(DeviceName, 1);
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
    if (FTimerHandle* Handle = PollTimers.Find(DeviceName))
    {
        if (GEditor)
        {
            GEditor->GetTimerManager()->ClearTimer(*Handle);
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

void UPCAPToolSubsystem::DisconnectAll()
{
    TArray<FString> Names;
    PollTimers.GenerateKeyArray(Names);
    for (const FString& Name : Names)
    {
        DisconnectDevice(Name);
    }
}

// ─── Status Poll ────────────────────────────────────────────────────────────────

void UPCAPToolSubsystem::PollDevice(FString DeviceName)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    // Exact endpoint the Technoprops MugShot web UI uses
    const FString URL = FString::Printf(
        TEXT("http://%s/control?cmd=no&param="), *Config->IPAddress);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(2.5f);   // generous — status competes with the video pulls
    Request->OnProcessRequestComplete().BindUObject(
        this, &UPCAPToolSubsystem::OnPollResponse, DeviceName);
    Request->ProcessRequest();
}

void UPCAPToolSubsystem::OnPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                                        bool bWasSuccessful, FString DeviceName)
{
    FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName);
    if (!Status) return;

    if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
    {
        // Debounce: under heavy video load the status poll can occasionally time out.
        // A single miss must NOT blank the feed — only declare Offline after several
        // consecutive failures (~6s). The video stream keeps running until then.
        int32& Fails = PollFailCount.FindOrAdd(DeviceName);
        if (++Fails >= 3)
        {
            SetConnectionState(DeviceName, EHMCConnectionState::Offline);
            MarkFeedsDisconnected(DeviceName);
        }
        OnStatusUpdated.Broadcast(DeviceName, *Status);
        return;
    }

    PollFailCount.Remove(DeviceName);   // a success resets the failure streak

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
    Val = 0.0; JsonObj->TryGetNumberField(TEXT("boomPos"),        Val); Status->BoomPos        = (int32)Val;

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

TArray<FHMCCameraFeed> UPCAPToolSubsystem::GetFeedsForActor(const FString& ActorID) const
{
    const TArray<FHMCCameraFeed>* Feeds = CameraFeeds.Find(ActorID);
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

// ─── Commands (HTTP) ──────────────────────────────────────────────────────────

void UPCAPToolSubsystem::SendCommand(const FString& DeviceName, const FString& Command)
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

void UPCAPToolSubsystem::SendDeviceCommand(const FString& DeviceName, const FString& Cmd,
                                           const FString& Param, const FString& ExtraKey,
                                           const FString& ExtraVal)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    // Mirrors the proven poll endpoint (GET query on /control). The MugShot web UI
    // uses the same pattern; if firmware rejects GET, switch SetVerb to POST and
    // move the query into the body.
    FString URL = FString::Printf(TEXT("http://%s/control?cmd=%s&param=%s"),
        *Config->IPAddress, *Cmd, *Param);
    if (!ExtraKey.IsEmpty())
        URL += FString::Printf(TEXT("&%s=%s"), *ExtraKey, *ExtraVal);
    // Cache-buster (as the device's own web client appends) so repeated/identical
    // commands aren't served from cache and actually re-fire on the device.
    URL += FString::Printf(TEXT("&_=%lld"), (long long)FDateTime::UtcNow().GetTicks());

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(1.5f);
    Request->ProcessRequest();   // fire and forget — value confirms on next poll
}

// ─── Issue flags ───────────────────────────────────────────────────────────────

void UPCAPToolSubsystem::SetManualIssue(const FString& DeviceName, EHMCManualIssue Issue, bool bSet)
{
    const int32 FlagBit = 1 << (16 + static_cast<int32>(Issue));   // maps to HMC_Manual_*
    int32& Flags = ManualIssueFlags.FindOrAdd(DeviceName);
    if (bSet) Flags |= FlagBit;
    else      Flags &= ~FlagBit;

    // Repaint borders/banners immediately — manual flags don't wait for a poll.
    if (FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName))
        OnStatusUpdated.Broadcast(DeviceName, *Status);
}

bool UPCAPToolSubsystem::GetManualIssue(const FString& DeviceName, EHMCManualIssue Issue) const
{
    const int32 FlagBit = 1 << (16 + static_cast<int32>(Issue));
    return (GetManualIssueFlags(DeviceName) & FlagBit) != 0;
}

int32 UPCAPToolSubsystem::GetManualIssueFlags(const FString& DeviceName) const
{
    const int32* Flags = ManualIssueFlags.Find(DeviceName);
    return Flags ? *Flags : 0;
}

int32 UPCAPToolSubsystem::GetEffectiveIssueFlags(const FString& DeviceName, int32 CameraIndex) const
{
    int32 Hardware = 0;
    if (const FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName))
        Hardware = (CameraIndex == 0) ? Status->IssueFlags0 : Status->IssueFlags1;

    // Frame-derived: no subject detected (or no signal) → red NoFace flag.
    const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const int32 FrameFlags = FrameNoFace.Contains(Key) ? HMC_Issue_NoFace : 0;

    // Debounced automatic image-analysis flags (focus / exposure / lighting / framing).
    const int32 AutoFlags = StableAutoFlags.Contains(Key) ? StableAutoFlags[Key] : 0;

    return Hardware | GetManualIssueFlags(DeviceName) | FrameFlags | AutoFlags;
}

// ─── Video Frames ─────────────────────────────────────────────────────────────

void UPCAPToolSubsystem::RequestVideoFrame(const FString& DeviceName, int32 CameraIndex)
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
    const FString CamKey = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    FrameInFlight.Remove(CamKey);

    // Re-arm this camera's NEXT request immediately, BEFORE decoding — its network
    // round-trip then overlaps this frame's decode (pipelines network with decode).
    PumpFrameCam(DeviceName, CameraIndex);

    UTexture2D* Texture = nullptr;
    bool bSubject = false;
    if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() == 200)
    {
        const double DecodeT0 = FPlatformTime::Seconds();
        const TArray<uint8>& RawBytes = Response->GetContent();
        IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
        TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(EImageFormat::JPEG);

        TArray<uint8> Uncompressed;
        if (IW.IsValid() && IW->SetCompressed(RawBytes.GetData(), RawBytes.Num())
            && IW->GetRaw(ERGBFormat::BGRA, 8, Uncompressed))
        {
            Texture = UpdateFrameTexture(CamKey, Uncompressed, IW->GetWidth(), IW->GetHeight());
            bSubject = UPCAPToolStatics::FrameHasSubject(Uncompressed, IW->GetWidth(), IW->GetHeight());

            // Throttled automatic image analysis (~5Hz) for this device's pipeline.
            const double NowT = FPlatformTime::Seconds();
            double& LastA = LastAnalyzeTime.FindOrAdd(CamKey);
            if (NowT - LastA >= 0.2)
            {
                LastA = NowT;
                const FHMCImageMetrics M = UPCAPToolStatics::AnalyzeFrameBGRA(
                    Uncompressed, IW->GetWidth(), IW->GetHeight());
                ImageMetrics.Add(CamKey, M);
                const FPipelineCheckProfile Profile =
                    UPCAPToolStatics::GetPipelineProfile(GetDevicePipeline(DeviceName));
                const int32 RawAuto = UPCAPToolStatics::MapMetricsToAutoFlags(
                    M, Profile, GetFramingRef(DeviceName, CameraIndex));
                UpdateAutoFlagHysteresis(CamKey, RawAuto);
            }
        }
        LogFrameRate(CamKey, (FPlatformTime::Seconds() - DecodeT0) * 1000.0, RawBytes.Num());
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

void UPCAPToolSubsystem::LogFrameRate(const FString& Key, double DecodeMs, int32 FrameBytes)
{
    int32&  Count       = FrameRateCount.FindOrAdd(Key);
    double& WindowStart = FrameRateWindowStart.FindOrAdd(Key);
    double& DecodeAccum = FrameRateDecodeAccum.FindOrAdd(Key);

    const double Now = FPlatformTime::Seconds();
    if (Count == 0) WindowStart = Now;
    ++Count;
    DecodeAccum += DecodeMs;

    if (Count >= 30)
    {
        const double Elapsed = Now - WindowStart;
        const double Fps = Elapsed > 0.0 ? Count / Elapsed : 0.0;
        UE_LOG(LogTemp, Log, TEXT("[PCAPTool] HMC %s: %.1f fps | avg decode %.1f ms | %d KB/frame"),
            *Key, Fps, DecodeAccum / FMath::Max(1, Count), FrameBytes / 1024);
        Count = 0;
        DecodeAccum = 0.0;
    }
}

void UPCAPToolSubsystem::PumpFrameCam(const FString& DeviceName, int32 CameraIndex)
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

UTexture2D* UPCAPToolSubsystem::UpdateFrameTexture(const FString& Key, const TArray<uint8>& BGRA,
                                                    int32 Width, int32 Height)
{
    if (Width <= 0 || Height <= 0 || BGRA.Num() < Width * Height * 4) return nullptr;

    UTexture2D* Texture = nullptr;
    if (TObjectPtr<UTexture2D>* Found = FrameTextureCache.Find(Key))
        Texture = Found->Get();

    // (Re)create only when missing or the dimensions changed. Fill the pixels
    // synchronously BEFORE UpdateResource so a freshly-created texture is never
    // shown blank — otherwise it flashes on every recreation if frame sizes vary.
    if (!Texture || Texture->GetSizeX() != Width || Texture->GetSizeY() != Height)
    {
        Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
        if (!Texture) return nullptr;
        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Data, BGRA.GetData(), Width * Height * 4);
        Mip.BulkData.Unlock();
        Texture->UpdateResource();
        FrameTextureCache.Add(Key, Texture);
        UE_LOG(LogTemp, Log, TEXT("[PCAPTool] HMC %s texture created %dx%d (if this repeats, frame sizes vary)"),
            *Key, Width, Height);
        return Texture;   // already filled — done
    }

    // Same size — fast in-place GPU update, no reallocation, no UpdateResource.
    // The source copy is freed on the render thread once the upload completes.
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

void UPCAPToolSubsystem::SetConnectionState(const FString& DeviceName, EHMCConnectionState NewState)
{
    FHMCDeviceStatus* S = DeviceStatuses.Find(DeviceName);
    const bool bChanged = !S || S->ConnectionState != NewState;
    if (S) S->ConnectionState = NewState;
    if (bChanged)
    {
        const TCHAR* StateStr =
            NewState == EHMCConnectionState::Connected ? TEXT("Connected") :
            NewState == EHMCConnectionState::Offline   ? TEXT("Offline")   : TEXT("Disconnected");
        UE_LOG(LogTemp, Log, TEXT("[PCAPTool] HMC %s -> %s"), *DeviceName, StateStr);
    }
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
        Obj->SetStringField(TEXT("actorID"),         C.ActorID);
        Obj->SetStringField(TEXT("webSocketEndpoint"), C.WebSocketEndpoint);
        Obj->SetNumberField(TEXT("pipeline"), (double)(uint8)C.Pipeline);

        auto WriteRef = [](const FHMCFramingRef& R) -> TSharedPtr<FJsonObject>
        {
            TSharedPtr<FJsonObject> RO = MakeShared<FJsonObject>();
            RO->SetBoolField  (TEXT("set"),  R.bSet);
            RO->SetNumberField(TEXT("x"),    R.Center.X);
            RO->SetNumberField(TEXT("y"),    R.Center.Y);
            RO->SetNumberField(TEXT("size"), R.Size);
            return RO;
        };
        Obj->SetObjectField(TEXT("framingRef0"), WriteRef(C.FramingRef0));
        Obj->SetObjectField(TEXT("framingRef1"), WriteRef(C.FramingRef1));

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
        Obj->TryGetStringField(TEXT("actorID"),         Config.ActorID);
        Obj->TryGetStringField(TEXT("webSocketEndpoint"), Config.WebSocketEndpoint);

        int32 Pipe = 0;
        if (Obj->TryGetNumberField(TEXT("pipeline"), Pipe))
            Config.Pipeline = (ECapturePipeline)(uint8)Pipe;

        auto ReadRef = [](const TSharedPtr<FJsonObject>& Parent, const TCHAR* Key, FHMCFramingRef& R)
        {
            const TSharedPtr<FJsonObject>* RO = nullptr;
            if (Parent->TryGetObjectField(Key, RO) && RO && RO->IsValid())
            {
                (*RO)->TryGetBoolField(TEXT("set"), R.bSet);
                double X = 0.5, Y = 0.5, S = 0.0;
                (*RO)->TryGetNumberField(TEXT("x"),    X);
                (*RO)->TryGetNumberField(TEXT("y"),    Y);
                (*RO)->TryGetNumberField(TEXT("size"), S);
                R.Center = FVector2D(X, Y);
                R.Size   = (float)S;
            }
        };
        ReadRef(Obj, TEXT("framingRef0"), Config.FramingRef0);
        ReadRef(Obj, TEXT("framingRef1"), Config.FramingRef1);

        if (!Config.DeviceName.IsEmpty())
            RegisterDevice(Config);
    }
}

// ─── Capture Monitor ──────────────────────────────────────────────────────────

ECapturePipeline UPCAPToolSubsystem::GetDevicePipeline(const FString& DeviceName) const
{
    const FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName);
    return C ? C->Pipeline : ECapturePipeline::MetaHumanHMC;
}

void UPCAPToolSubsystem::SetDevicePipeline(const FString& DeviceName, ECapturePipeline Pipeline)
{
    if (FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName))
    {
        C->Pipeline = Pipeline;
        SaveConfig();
    }
}

FHMCFramingRef UPCAPToolSubsystem::GetFramingRef(const FString& DeviceName, int32 CameraIndex) const
{
    if (const FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName))
        return (CameraIndex == 0) ? C->FramingRef0 : C->FramingRef1;
    return FHMCFramingRef();
}

bool UPCAPToolSubsystem::SetFramingReferenceFromCurrent(const FString& DeviceName, int32 CameraIndex)
{
    FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName);
    if (!C) return false;

    const FString CamKey = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const FHMCImageMetrics* M = ImageMetrics.Find(CamKey);
    if (!M || !M->bValid || !M->bHasSubject) return false;   // need a live subject to lock onto

    FHMCFramingRef Ref;
    Ref.bSet   = true;
    Ref.Center = M->SubjectCenter;
    Ref.Size   = M->SubjectSize;
    (CameraIndex == 0 ? C->FramingRef0 : C->FramingRef1) = Ref;
    SaveConfig();

    // Within the pipeline's ideal target tolerance?
    const FPipelineCheckProfile P = UPCAPToolStatics::GetPipelineProfile(C->Pipeline);
    return FVector2D::Distance(Ref.Center, P.FramingTargetCenter) <= P.FramingCenterTol
        && Ref.Size >= P.FramingSizeMin && Ref.Size <= P.FramingSizeMax;
}

void UPCAPToolSubsystem::ClearFramingReference(const FString& DeviceName, int32 CameraIndex)
{
    if (FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName))
    {
        (CameraIndex == 0 ? C->FramingRef0 : C->FramingRef1) = FHMCFramingRef();
        SaveConfig();
    }
}

FHMCImageMetrics UPCAPToolSubsystem::GetImageMetrics(const FString& DeviceName, int32 CameraIndex) const
{
    const FString CamKey = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const FHMCImageMetrics* M = ImageMetrics.Find(CamKey);
    return M ? *M : FHMCImageMetrics();
}

int32 UPCAPToolSubsystem::UpdateAutoFlagHysteresis(const FString& CamKey, int32 RawFlags)
{
    static const int32 Bits[] = {
        HMC_Issue_Overexposed, HMC_Issue_Underexposed,
        HMC_Issue_OutOfFocus,  HMC_Issue_UnevenLight, HMC_Issue_FramingDrift
    };
    const int32 HOLD = 5;   // ~1s at the ~5Hz analysis rate

    int32& Stable = StableAutoFlags.FindOrAdd(CamKey);
    for (int32 Bit : Bits)
    {
        int32& Hold = AutoFlagHold.FindOrAdd(FString::Printf(TEXT("%s|%d"), *CamKey, Bit));
        if (RawFlags & Bit) Hold = FMath::Min(HOLD, Hold + 1);
        else                Hold = FMath::Max(0, Hold - 1);
        if      (Hold >= HOLD) Stable |= Bit;
        else if (Hold <= 0)    Stable &= ~Bit;
    }
    return Stable;
}
