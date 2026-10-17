#include "PCAPToolSubsystem.h"
#include "PCAPToolStatics.h"
#include "Devices/TechnopropsFacialDevice.h"
#include "Devices/MarkerlessBodyCamera.h"
#include "PCAPDeviceRegistry.h"
#include "PCAPToolSettings.h"
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
    DisconnectBodyCamera();
    DisconnectAll();
    Super::Deinitialize();
}

// ─── Registration ─────────────────────────────────────────────────────────────

void UPCAPToolSubsystem::RegisterDevice(const FHMCDeviceConfig& Config)
{
    RegisteredConfigs.Add(Config.DeviceName, Config);

    // The transport addresses the device by the config's IP (re-registering updates it).
    TSharedPtr<FTechnopropsFacialDevice>& Transport = Transports.FindOrAdd(Config.DeviceName);
    if (!Transport.IsValid()) Transport = MakeShared<FTechnopropsFacialDevice>();
    Transport->Connect(FPCAPDeviceAddress(Config.IPAddress));

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
    Transports.Remove(DeviceName);

    for (auto& Pair : CameraFeeds)
    {
        Pair.Value.RemoveAll([&DeviceName](const FHMCCameraFeed& F){
            return F.DeviceName == DeviceName;
        });
    }

    // Prune per-camera Capture Monitor state so it doesn't accumulate across
    // register/unregister cycles (keyed "DeviceName_Cam"); the agents are dropped below.
    for (int32 Cam = 0; Cam < 2; ++Cam)
    {
        const FString CamKey = FString::Printf(TEXT("%s_%d"), *DeviceName, Cam);
        ImageMetrics.Remove(CamKey);
        LastAnalyzeTime.Remove(CamKey);
        FrameNoFace.Remove(CamKey);
        LastFrameBGRA.Remove(CamKey);
        LastFrameDims.Remove(CamKey);
    }
    AgentOrchestrator.Remove(DeviceName);   // drop both cameras' agents (debounce / stability / board)

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

    // 1b. The framing reference + stability history were captured for the PREVIOUS
    // performer's face/mount — clear them so they don't mis-fire for the new actor
    // (Preview shows "framing reference not set" until it's re-captured in Setup).
    Config->FramingRef0 = FHMCFramingRef();
    Config->FramingRef1 = FHMCFramingRef();
    AgentOrchestrator.Remove(DeviceName);   // reset the agents' stability/debounce for the new actor

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

void UPCAPToolSubsystem::RefreshDevice(const FString& DeviceName)
{
    if (!RegisteredConfigs.Contains(DeviceName)) return;

    // Clear the failure streak so a dropped/offline device retries on this refresh.
    PollFailCount.Remove(DeviceName);

    if (PollTimers.Contains(DeviceName))
    {
        // Already connected — pull fresh data now without a disconnect flicker:
        PollDevice(DeviceName);                  // immediate status fetch
        FrameStreamDevices.Add(DeviceName);      // ensure the feed chain is armed
        // Drop any stale in-flight guard so a stalled feed re-kicks, then pump both cams.
        FrameInFlight.Remove(FString::Printf(TEXT("%s_0"), *DeviceName));
        FrameInFlight.Remove(FString::Printf(TEXT("%s_1"), *DeviceName));
        PumpFrameCam(DeviceName, 0);
        PumpFrameCam(DeviceName, 1);
    }
    else
    {
        // Disconnected/dropped — (re)connect, which fires a poll + starts the feeds.
        ConnectDevice(DeviceName);
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
    FTechnopropsFacialDevice* Transport = FindTransport(DeviceName);
    if (!Transport) return;

    Transport->PollStatus(FTechnopropsFacialDevice::FOnStatusPolled::CreateUObject(
        this, &UPCAPToolSubsystem::OnPollResponse, DeviceName));
}

void UPCAPToolSubsystem::OnPollResponse(bool bOk, const FString& Body, FString DeviceName)
{
    FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName);
    if (!Status) return;

    if (!bOk)
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

    // control.json -> telemetry fields (vendor parse lives in the adapter).
    if (!FTechnopropsFacialDevice::ParseControlJson(Body, *Status)) return;

    Status->LastUpdateTime = FDateTime::UtcNow();
    Status->StatusMessage  = GenerateStatusQuip(*Status);
    float TargetFPS = 30.f;
    {
        ECaptureConfiguration CapCfg = ECaptureConfiguration::StereoHeadMount;
        if (const FHMCDeviceConfig* DC = RegisteredConfigs.Find(DeviceName)) CapCfg = DC->CaptureConfig;
        TargetFPS = UPCAPToolStatics::GetDefinition(GetDevicePipeline(DeviceName), CapCfg).TargetFPS;
    }
    Status->IssueFlags0    = UPCAPToolStatics::EvaluateCameraIssues(*Status, 0, TargetFPS);
    Status->IssueFlags1    = UPCAPToolStatics::EvaluateCameraIssues(*Status, 1, TargetFPS);
    UpdateDeviceHealth(DeviceName);

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

TSharedPtr<IPCAPFacialDevice> UPCAPToolSubsystem::GetFacialDevice(const FString& DeviceName) const
{
    const TSharedPtr<FTechnopropsFacialDevice>* Found = Transports.Find(DeviceName);
    return Found ? TSharedPtr<IPCAPFacialDevice>(*Found) : nullptr;
}

FTechnopropsFacialDevice* UPCAPToolSubsystem::FindTransport(const FString& DeviceName) const
{
    const TSharedPtr<FTechnopropsFacialDevice>* Found = Transports.Find(DeviceName);
    return Found ? Found->Get() : nullptr;
}

void UPCAPToolSubsystem::UpdateDeviceHealth(const FString& DeviceName)
{
    FTechnopropsFacialDevice* Transport = FindTransport(DeviceName);
    const FHMCDeviceStatus* Status = DeviceStatuses.Find(DeviceName);
    if (Transport && Status) Transport->UpdateHealth(*Status);
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
    if (FTechnopropsFacialDevice* Transport = FindTransport(DeviceName))
        Transport->SendCommand(Command);
}

void UPCAPToolSubsystem::SendDeviceCommand(const FString& DeviceName, const FString& Cmd,
                                           const FString& Param, const FString& ExtraKey,
                                           const FString& ExtraVal)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;

    // GET /control?cmd=..&param=..[&k=v]&_=<ticks> — fire and forget, confirms on next poll.
    if (FTechnopropsFacialDevice* Transport = FindTransport(DeviceName))
        Transport->SendDeviceCommand(Cmd, Param, ExtraKey, ExtraVal);
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

    // Debounced automatic image-analysis flags from this camera's agent (focus /
    // exposure / lighting / framing / stability, incl. the latched bump).
    const int32 AutoFlags = AgentOrchestrator.GetStableFlags(DeviceName, CameraIndex);

    return Hardware | GetManualIssueFlags(DeviceName) | FrameFlags | AutoFlags;
}

// ─── Video Frames ─────────────────────────────────────────────────────────────

void UPCAPToolSubsystem::RequestVideoFrame(const FString& DeviceName, int32 CameraIndex)
{
    const FHMCDeviceConfig* Config = RegisteredConfigs.Find(DeviceName);
    if (!Config) return;
    FTechnopropsFacialDevice* Transport = FindTransport(DeviceName);
    if (!Transport) return;

    // Device cameras are 0-based: cam=0 (Top), cam=1 (Bot).
    static_cast<IPCAPFacialDevice*>(Transport)->RequestPreviewFrame(CameraIndex,
        FOnPCAPPreviewFrame::CreateUObject(this, &UPCAPToolSubsystem::OnVideoFrameResponse,
                                           DeviceName, CameraIndex));
}

UTexture2D* UPCAPToolSubsystem::GetLastFrame(const FString& DeviceName, int32 CameraIndex) const
{
    const FString Key = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const TObjectPtr<UTexture2D>* Found = FrameTextureCache.Find(Key);
    return Found ? Found->Get() : nullptr;
}

void UPCAPToolSubsystem::OnVideoFrameResponse(bool bOk, int32 StatusCode, const TArray<uint8>& RawBytes,
                                               FString DeviceName, int32 CameraIndex)
{
    const FString CamKey = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    FrameInFlight.Remove(CamKey);

    // Re-arm this camera's NEXT request immediately, BEFORE decoding — its network
    // round-trip then overlaps this frame's decode (pipelines network with decode).
    PumpFrameCam(DeviceName, CameraIndex);

    UTexture2D* Texture = nullptr;
    bool bSubject = false;
    if (bOk)
    {
        const double DecodeT0 = FPlatformTime::Seconds();
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

                // Resolve this device's definition (pipeline x configuration) first so
                // the focus measure is weighted to the pipeline's nasolabial band.
                const FHMCDeviceConfig* DCfg = RegisteredConfigs.Find(DeviceName);
                const ECaptureConfiguration CaptureCfg =
                    DCfg ? DCfg->CaptureConfig : ECaptureConfiguration::StereoHeadMount;
                FPipelineCheckProfile Profile =
                    UPCAPToolStatics::GetDefinition(GetDevicePipeline(DeviceName), CaptureCfg);
                if (DCfg && DCfg->FocusMinOverride >= 0.f) Profile.FocusMin = DCfg->FocusMinOverride;

                const FHMCImageMetrics M = UPCAPToolStatics::AnalyzeFrameBGRA(
                    Uncompressed, IW->GetWidth(), IW->GetHeight(),
                    Profile.FocusRegionCenter, Profile.FocusRegionExtent);
                ImageMetrics.Add(CamKey, M);
                LastFrameBGRA.Add(CamKey, Uncompressed);
                LastFrameDims.Add(CamKey, FIntPoint(IW->GetWidth(), IW->GetHeight()));

                // Per-camera agent: run the pipeline×config checklist (focus / exposure /
                // lighting / framing / stability / board) on this frame's metrics. The
                // agent owns the debounce, the centroid history + bump latch, and board state.
                FHMCSkillContext Ctx;
                Ctx.Metrics     = M;
                Ctx.Profile     = Profile;
                Ctx.FramingRef  = GetFramingRef(DeviceName, CameraIndex);
                Ctx.CameraIndex = CameraIndex;
                Ctx.NowSeconds  = NowT;
                AgentOrchestrator.OnFrame(DeviceName, CameraIndex,
                    GetDevicePipeline(DeviceName), CaptureCfg, Ctx);
            }
        }
        LogFrameRate(CamKey, (FPlatformTime::Seconds() - DecodeT0) * 1000.0, RawBytes.Num());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] HMC %s cam%d frame request failed (HTTP %d)"),
            *DeviceName, CameraIndex, StatusCode);
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
        // Append the latest auto-analysis metrics so thresholds can be tuned from the
        // Output Log against the real feed (focus has no default threshold until tuned).
        const FHMCImageMetrics* M = ImageMetrics.Find(Key);
        UE_LOG(LogTemp, Log, TEXT("[PCAPTool] HMC %s: %.1f fps | avg decode %.1f ms | %d KB/frame%s"),
            *Key, Fps, DecodeAccum / FMath::Max(1, Count), FrameBytes / 1024,
            (M && M->bValid)
                ? *FString::Printf(TEXT(" | focus %.3f luma %.2f blown %.0f%% spread %.2f size %.2f"),
                      M->FocusScore, M->MeanLuma, M->BlownFrac * 100.f, M->RegionSpread, M->SubjectSize)
                : TEXT(""));
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
    UpdateDeviceHealth(DeviceName);
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
        Obj->SetNumberField(TEXT("captureConfig"), (double)(uint8)C.CaptureConfig);
        Obj->SetBoolField(TEXT("preppedForPreview"), C.bPreppedForPreview);
        Obj->SetBoolField(TEXT("performerPrepConfirmed"), C.bPerformerPrepConfirmed);
        Obj->SetBoolField(TEXT("neutralCaptured"), C.bNeutralCaptured);
        Obj->SetBoolField(TEXT("teethCaptured"), C.bTeethCaptured);
        Obj->SetBoolField(TEXT("romCaptured"), C.bROMCaptured);
        Obj->SetStringField(TEXT("neutralStillPath"), C.NeutralStillPath);
        Obj->SetStringField(TEXT("teethStillPath"), C.TeethStillPath);
        Obj->SetStringField(TEXT("romTakeLabel"), C.ROMTakeLabel);
        Obj->SetNumberField(TEXT("focusMinOverride"), C.FocusMinOverride);
        Obj->SetBoolField(TEXT("calibStartCaptured"), C.bCalibStartCaptured);
        Obj->SetBoolField(TEXT("calibEndCaptured"), C.bCalibEndCaptured);
        Obj->SetStringField(TEXT("calibStartStillPath"), C.CalibStartStillPath);
        Obj->SetStringField(TEXT("calibEndStillPath"), C.CalibEndStillPath);
        Obj->SetStringField(TEXT("sourceRig"), C.SourceRig.ToSoftObjectPath().ToString());

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
        int32 CapCfg = 0;
        if (Obj->TryGetNumberField(TEXT("captureConfig"), CapCfg))
            Config.CaptureConfig = (ECaptureConfiguration)(uint8)CapCfg;
        Obj->TryGetBoolField(TEXT("preppedForPreview"), Config.bPreppedForPreview);
        Obj->TryGetBoolField(TEXT("performerPrepConfirmed"), Config.bPerformerPrepConfirmed);
        Obj->TryGetBoolField(TEXT("neutralCaptured"), Config.bNeutralCaptured);
        Obj->TryGetBoolField(TEXT("teethCaptured"), Config.bTeethCaptured);
        Obj->TryGetBoolField(TEXT("romCaptured"), Config.bROMCaptured);
        Obj->TryGetStringField(TEXT("neutralStillPath"), Config.NeutralStillPath);
        Obj->TryGetStringField(TEXT("teethStillPath"), Config.TeethStillPath);
        Obj->TryGetStringField(TEXT("romTakeLabel"), Config.ROMTakeLabel);
        double FMO = -1.0;
        if (Obj->TryGetNumberField(TEXT("focusMinOverride"), FMO)) Config.FocusMinOverride = (float)FMO;
        Obj->TryGetBoolField(TEXT("calibStartCaptured"), Config.bCalibStartCaptured);
        Obj->TryGetBoolField(TEXT("calibEndCaptured"), Config.bCalibEndCaptured);
        Obj->TryGetStringField(TEXT("calibStartStillPath"), Config.CalibStartStillPath);
        Obj->TryGetStringField(TEXT("calibEndStillPath"), Config.CalibEndStillPath);
        FString SR;
        if (Obj->TryGetStringField(TEXT("sourceRig"), SR) && !SR.IsEmpty())
            Config.SourceRig = TSoftObjectPtr<UHMCRigEntry>(FSoftObjectPath(SR));

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

ECaptureConfiguration UPCAPToolSubsystem::GetDeviceCaptureConfig(const FString& DeviceName) const
{
    const FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName);
    return C ? C->CaptureConfig : ECaptureConfiguration::StereoHeadMount;
}

void UPCAPToolSubsystem::SetDeviceCaptureConfig(const FString& DeviceName, ECaptureConfiguration Config)
{
    if (FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName))
    {
        C->CaptureConfig = Config;
        SaveConfig();
    }
}

FHMCDeviceConfig UPCAPToolSubsystem::GetDeviceConfig(const FString& DeviceName) const
{
    const FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName);
    return C ? *C : FHMCDeviceConfig();
}

void UPCAPToolSubsystem::SetPerformerPrepConfirmed(const FString& DeviceName, bool bConfirmed)
{
    if (FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName)) { C->bPerformerPrepConfirmed = bConfirmed; SaveConfig(); }
}

void UPCAPToolSubsystem::MarkROMCaptured(const FString& DeviceName, const FString& TakeLabel)
{
    if (FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName)) { C->bROMCaptured = true; C->ROMTakeLabel = TakeLabel; SaveConfig(); }
}

void UPCAPToolSubsystem::ClearScanReadiness(const FString& DeviceName)
{
    if (FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName))
    {
        C->bPerformerPrepConfirmed = false;
        C->bNeutralCaptured = false;
        C->bTeethCaptured   = false;
        C->bROMCaptured     = false;
        C->NeutralStillPath.Empty();
        C->TeethStillPath.Empty();
        C->ROMTakeLabel.Empty();
        SaveConfig();
    }
}

void UPCAPToolSubsystem::SetFocusMinOverride(const FString& DeviceName, float Value)
{
    if (FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName)) { C->FocusMinOverride = Value; SaveConfig(); }
}

bool UPCAPToolSubsystem::SaveCameraStillPng(const FString& DeviceName, int32 CameraIndex, const FString& FullPath)
{
    const FString CamKey = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const TArray<uint8>* BGRA = LastFrameBGRA.Find(CamKey);
    const FIntPoint*     Dim  = LastFrameDims.Find(CamKey);
    if (!BGRA || !Dim || Dim->X <= 0 || Dim->Y <= 0 || BGRA->Num() < Dim->X * Dim->Y * 4)
        return false;   // no cached frame for this camera yet

    IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> PNG = IWM.CreateImageWrapper(EImageFormat::PNG);
    if (!PNG.IsValid() ||
        !PNG->SetRaw(BGRA->GetData(), BGRA->Num(), Dim->X, Dim->Y, ERGBFormat::BGRA, 8))
        return false;

    const TArray64<uint8>& Png = PNG->GetCompressed();
    TArray<uint8> PngData;
    PngData.Append(Png.GetData(), (int32)Png.Num());

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), true);
    return FFileHelper::SaveArrayToFile(PngData, *FullPath);
}

bool UPCAPToolSubsystem::CaptureIdentityStill(const FString& DeviceName, int32 CameraIndex, bool bTeeth)
{
    FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName);
    if (!C) return false;

    const FString Path = FPaths::ProjectSavedDir() / TEXT("PCAPTool/Identity")
        / FString::Printf(TEXT("%s_%s.png"), *DeviceName, bTeeth ? TEXT("teeth") : TEXT("neutral"));
    if (!SaveCameraStillPng(DeviceName, CameraIndex, Path)) return false;

    if (bTeeth) { C->TeethStillPath   = Path; C->bTeethCaptured   = true; }
    else        { C->NeutralStillPath = Path; C->bNeutralCaptured = true; }
    SaveConfig();
    return true;
}

bool UPCAPToolSubsystem::CaptureCalibrationStill(const FString& DeviceName, int32 CameraIndex, bool bEnd)
{
    FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName);
    if (!C) return false;

    const FString Path = FPaths::ProjectSavedDir() / TEXT("PCAPTool/Identity")
        / FString::Printf(TEXT("%s_calib_%s.png"), *DeviceName, bEnd ? TEXT("end") : TEXT("start"));
    if (!SaveCameraStillPng(DeviceName, CameraIndex, Path)) return false;

    if (bEnd) { C->CalibEndStillPath   = Path; C->bCalibEndCaptured   = true; }
    else      { C->CalibStartStillPath = Path; C->bCalibStartCaptured = true; }
    SaveConfig();
    return true;
}

bool UPCAPToolSubsystem::IsReadyToScan(const FString& DeviceName) const
{
    const FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName);
    if (!C) return false;

    const int32 F0 = GetEffectiveIssueFlags(DeviceName, 0);
    const int32 F1 = GetEffectiveIssueFlags(DeviceName, 1);
    const bool bChecksClear =
        UPCAPToolStatics::GetIssueSeverity(F0) == EHMCIssueSeverity::None &&
        UPCAPToolStatics::GetIssueSeverity(F1) == EHMCIssueSeverity::None;

    // Teeth is recommended but not required (docs: "optional but highly recommended").
    return C->bPerformerPrepConfirmed && C->bNeutralCaptured && C->bROMCaptured && bChecksClear;
}

FHMCFramingRef UPCAPToolSubsystem::GetFramingRef(const FString& DeviceName, int32 CameraIndex) const
{
    if (const FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName))
        return (CameraIndex == 0) ? C->FramingRef0 : C->FramingRef1;
    return FHMCFramingRef();
}

FString UPCAPToolSubsystem::GetFramingHint(const FString& DeviceName, int32 CameraIndex) const
{
    const FHMCFramingRef Ref = GetFramingRef(DeviceName, CameraIndex);
    if (!Ref.bSet) return FString();

    const FString CamKey = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const FHMCImageMetrics* M = ImageMetrics.Find(CamKey);
    if (!M || !M->bValid || !M->bHasSubject) return FString();

    const double Tol = 0.04;   // dead-zone so tiny motion isn't called drift
    const double dx = M->SubjectCenter.X - Ref.Center.X;
    const double dy = M->SubjectCenter.Y - Ref.Center.Y;

    FString V, H;
    if (dy >  Tol)      V = TEXT("low");    // face sits lower in frame than the reference
    else if (dy < -Tol) V = TEXT("high");
    if (dx >  Tol)      H = TEXT("right");
    else if (dx < -Tol) H = TEXT("left");

    if (!V.IsEmpty() && !H.IsEmpty()) return FString::Printf(TEXT("%s & %s"), *V, *H);
    if (!V.IsEmpty())                 return V;
    if (!H.IsEmpty())                 return TEXT("off-axis ") + H;
    return FString();
}

FString UPCAPToolSubsystem::GetLightingHint(const FString& DeviceName, int32 CameraIndex) const
{
    const FString CamKey = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const FHMCImageMetrics* M = ImageMetrics.Find(CamKey);
    if (!M || !M->bValid || !M->bHasSubject) return FString();
    return UPCAPToolStatics::GetLightingHintText(M->LightDir);
}

EHMCBoardState UPCAPToolSubsystem::GetAgentBoardState(const FString& DeviceName, int32 CameraIndex) const
{
    return AgentOrchestrator.GetBoardState(DeviceName, CameraIndex);
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

    // Within the pipeline's ideal target tolerance? (honor the capture configuration)
    const FPipelineCheckProfile P = UPCAPToolStatics::GetDefinition(C->Pipeline, C->CaptureConfig);
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

bool UPCAPToolSubsystem::IsPreppedForPreview(const FString& DeviceName) const
{
    const FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName);
    return C && C->bPreppedForPreview;
}

void UPCAPToolSubsystem::SetPreppedForPreview(const FString& DeviceName, bool bPrepped)
{
    if (FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName))
    {
        C->bPreppedForPreview = bPrepped;
        SaveConfig();
    }
}

void UPCAPToolSubsystem::MarkAllPreppedForPreview()
{
    for (auto& Pair : RegisteredConfigs)
        Pair.Value.bPreppedForPreview = true;
    SaveConfig();
}

// (UpdateAutoFlagHysteresis removed — per-flag debounce now lives in FHMCCameraAgent::Tick.)

// ─── Body camera (markerless) ───────────────────────────────────────────────

void UPCAPToolSubsystem::ConnectBodyCamera()
{
    DisconnectBodyCamera();
    const UPCAPToolSettings* S = GetDefault<UPCAPToolSettings>();
    BodyCamera = UPCAPDeviceRegistry::CreateBodySource(FMarkerlessBodyCamera::StaticVendorId());
    if (!BodyCamera.IsValid()) return;
    BodyCamera->Connect(FPCAPDeviceAddress(S->BodyCameraDevice));
    // Preview arrives at ~10 fps; decode at the same rate into the reused feed texture.
    BodyPumpHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UPCAPToolSubsystem::PumpBodyFrame), 0.1f);
}

void UPCAPToolSubsystem::DisconnectBodyCamera()
{
    if (BodyPumpHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(BodyPumpHandle);
        BodyPumpHandle.Reset();
    }
    if (BodyCamera.IsValid())
    {
        if (BodyCamera->VendorId() == FMarkerlessBodyCamera::StaticVendorId())
            static_cast<FMarkerlessBodyCamera*>(BodyCamera.Get())->Disconnect();
        BodyCamera.Reset();
    }
    FrameTextureCache.Remove(FString::Printf(TEXT("%s_0"), BodyCameraKey()));
}

bool UPCAPToolSubsystem::PumpBodyFrame(float)
{
    if (!BodyCamera.IsValid()) return true;
    BodyCamera->RequestPreviewFrame(0, FOnPCAPPreviewFrame::CreateWeakLambda(this,
        [this](bool bOk, int32, const TArray<uint8>& Jpeg)
    {
        if (!bOk) return;
        IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
        TSharedPtr<IImageWrapper> IW = IWM.CreateImageWrapper(EImageFormat::JPEG);
        TArray<uint8> BGRA;
        if (IW.IsValid() && IW->SetCompressed(Jpeg.GetData(), Jpeg.Num()) && IW->GetRaw(ERGBFormat::BGRA, 8, BGRA))
            UpdateFrameTexture(FString::Printf(TEXT("%s_0"), BodyCameraKey()), BGRA, IW->GetWidth(), IW->GetHeight());
    }));
    return true;
}

FString UPCAPToolSubsystem::GetBodyCameraStatusText() const
{
    if (!BodyCamera.IsValid()) return TEXT("Not connected");
    const FPCAPDeviceHealth H = BodyCamera->GetHealth();
    FString Out;
    if (H.bRecording)
        Out = FString::Printf(TEXT("RECORDING  %.1f fps  %d dropped"), H.FPS, H.DroppedFrames);
    else
        Out = H.bConnected ? TEXT("Live") : TEXT("Starting...");
    if (!H.LastError.IsEmpty()) Out += TEXT("  |  ") + H.LastError;
    return Out;
}

FString UPCAPToolSubsystem::GetBodyCameraWarning() const
{
    const int32 Fps = GetDefault<UPCAPToolSettings>()->BodyCameraFps;
    FString Out = Fps < 60
        ? FString::Printf(TEXT("%d fps: below Epic's 60 fps target for markerless body video"), Fps)
        : FString();
    // The C920 halves its frame rate in low light (body-camera.md §8); say so while recording.
    if (BodyCamera.IsValid())
    {
        const FPCAPDeviceHealth H = BodyCamera->GetHealth();
        if (H.bRecording && H.FPS > 0.f && H.FPS < Fps * 0.9f)
            Out += FString::Printf(TEXT("%sLOW FRAME RATE: %.1f of %d fps. Add light on the performer."),
                Out.IsEmpty() ? TEXT("") : TEXT("\n"), H.FPS, Fps);
    }
    return Out;
}
