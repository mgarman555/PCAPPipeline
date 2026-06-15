#include "PCAPViconSDKMarkerSource.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"

#if WITH_VICON_SDK
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include "DataStreamClient.h"
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

FViconSDKMarkerSource::FViconSDKMarkerSource() {}

FViconSDKMarkerSource::~FViconSDKMarkerSource()
{
    Disconnect();
}

bool FViconSDKMarkerSource::IsAvailable() const
{
    return bConnected;
}

bool FViconSDKMarkerSource::Connect(const FString& Host)
{
    Disconnect();
    HostName = Host;
    bStopRequested = false;
    Thread = FRunnableThread::Create(this, TEXT("PCAPViconSDKMarkerSource"));
    return Thread != nullptr;
}

void FViconSDKMarkerSource::Disconnect()
{
    bStopRequested = true;
    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
    bConnected = false;
}

void FViconSDKMarkerSource::Stop()
{
    bStopRequested = true;
}

void FViconSDKMarkerSource::Poll(FVizFrame& OutFrame)
{
    FScopeLock Lock(&FrameLock);
    OutFrame = LatestFrame;
}

uint32 FViconSDKMarkerSource::Run()
{
#if WITH_VICON_SDK
    using namespace ViconDataStreamSDK::CPP;
    Client C;

    {
        const std::string H(TCHAR_TO_UTF8(*HostName));
        if (C.Connect(String(H)).Result != Result::Success)
        {
            return 0;   // host unreachable — leave bConnected false; actor shows nothing
        }
    }
    C.SetStreamMode(StreamMode::ServerPush);
    C.EnableMarkerData();
    C.EnableUnlabeledMarkerData();
    bConnected = true;

    while (!bStopRequested)
    {
        if (C.GetFrame().Result != Result::Success)
        {
            FPlatformProcess::Sleep(0.005f);
            continue;
        }

        FVizFrame F;

        // Labeled markers, grouped per subject.
        const unsigned int SubjectCount = C.GetSubjectCount().SubjectCount;
        for (unsigned int s = 0; s < SubjectCount; ++s)
        {
            const String Subject = C.GetSubjectName(s).SubjectName;
            FVizMarkerGroup Group;
            Group.SubjectName = FName(UTF8_TO_TCHAR(((std::string)Subject).c_str()));

            const unsigned int MarkerCount = C.GetMarkerCount(Subject).MarkerCount;
            for (unsigned int m = 0; m < MarkerCount; ++m)
            {
                const String MarkerName = C.GetMarkerName(Subject, m).MarkerName;
                const Output_GetMarkerGlobalTranslation T = C.GetMarkerGlobalTranslation(Subject, MarkerName);
                if (T.Result == Result::Success && !T.Occluded)
                {
                    Group.Points.Add(PCAPViz::ViconMMToUE(T.Translation[0], T.Translation[1], T.Translation[2]));
                }
            }
            F.Labeled.Add(MoveTemp(Group));
        }

        // Unlabeled markers (the loose dots the operator sees floating in the volume).
        const unsigned int UnlabeledCount = C.GetUnlabeledMarkerCount().MarkerCount;
        for (unsigned int u = 0; u < UnlabeledCount; ++u)
        {
            const Output_GetUnlabeledMarkerGlobalTranslation T = C.GetUnlabeledMarkerGlobalTranslation(u);
            if (T.Result == Result::Success)
            {
                F.Unlabeled.Add(PCAPViz::ViconMMToUE(T.Translation[0], T.Translation[1], T.Translation[2]));
            }
        }

        {
            FScopeLock Lock(&FrameLock);
            LatestFrame = MoveTemp(F);
        }
    }

    C.Disconnect();
    bConnected = false;
#endif
    return 0;
}
