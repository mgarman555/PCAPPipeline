#pragma once

#include "CoreMinimal.h"
#include "PCAPMarkerSource.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/CriticalSection.h"

class FRunnableThread;

// Phase 2: the real Vicon marker cloud (labeled + unlabeled) via the DataStream SDK.
// GetFrame() blocks, so the SDK client runs on a worker thread that buffers the latest
// frame; the game thread's Poll() just copies the buffer (never blocks the editor).
// When WITH_VICON_SDK == 0 the SDK calls compile out and this acts as an empty source.
class FViconSDKMarkerSource : public IMarkerSource, public FRunnable
{
public:
    FViconSDKMarkerSource();
    virtual ~FViconSDKMarkerSource();

    // IMarkerSource
    virtual bool IsAvailable() const override;
    virtual bool Connect(const FString& Host) override;
    virtual void Disconnect() override;
    virtual void Poll(FVizFrame& OutFrame) override;

    // FRunnable
    virtual uint32 Run() override;
    virtual void Stop() override;

private:
    FString HostName;
    FThreadSafeBool bConnected;
    FThreadSafeBool bStopRequested;
    FRunnableThread* Thread = nullptr;
    mutable FCriticalSection FrameLock;
    FVizFrame LatestFrame;   // written by the worker thread, read by the game thread
};
