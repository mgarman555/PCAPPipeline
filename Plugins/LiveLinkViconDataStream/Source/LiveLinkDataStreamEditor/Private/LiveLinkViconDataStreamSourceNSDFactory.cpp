// Copyright (c) 2021 Vicon Motion Systems Ltd. All Rights Reserved.


#include "LiveLinkViconDataStreamSourceNSDFactory.h"
#include "ILiveLinkDataStreamModule.h"
#include "LiveLinkViconDataStreamSource.h"
#include "LiveLinkViconDataStreamBlueprint.h"
#include "LiveLinkViconDataStreamSourceNSDEditor.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include "IoContextRunner.h"
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

#define LOCTEXT_NAMESPACE "LiveLinkViconDataStreamSourceNSDFactory"

FText ULiveLinkViconDataStreamSourceNSDFactory::GetSourceDisplayName() const
{
  return LOCTEXT("SourceDisplayName", "Vicon Data Stream Source (Auto-Discover)");
}

FText ULiveLinkViconDataStreamSourceNSDFactory::GetSourceTooltip() const
{
  return LOCTEXT("SourceTooltip", "Creates a connection to a Vicon Data Stream based Live Link Source (Auto-Discover)");
}

TSharedPtr<SWidget> ULiveLinkViconDataStreamSourceNSDFactory::BuildCreationPanel(FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const
{
  // Runner for browser - started in browser panel constructor
  // Kept here so it doesn't block game thread when waiting for the thread to be finished.
  // The factory is a uclass, which means if we keep it as a member, a new runner instance ( on top of the Class Default Object) will be created everytime
  // a livelink panel is opened and they are only garbaged collected at shutdown.
  static FIoContextRunner IoContextRunner;
  return SNew(SLiveLinkViconDataStreamSourceNSDEditor, IoContextRunner.m_IoContext)
    .OnPropertiesSelected(FOnDataStreamPropertiesSelected::CreateUObject(this, &ULiveLinkViconDataStreamSourceNSDFactory::OnPropertiesSelected, OnLiveLinkSourceCreated));
}

/** Create a new source from a ConnectionString */
TSharedPtr<ILiveLinkSource> ULiveLinkViconDataStreamSourceNSDFactory::CreateSource(const FString& ConnectionString) const
{
  // Extract the properties from the string
  ViconStreamProperties Props = ViconStreamProperties::FromString(ConnectionString);
  const FText SourceType = FText::FromString(FString(ULiveLinkViconDataStreamBlueprint::SOURCE_TYPE.c_str()));
  TSharedPtr<FLiveLinkViconDataStreamSource> NewSource = MakeShareable(new FLiveLinkViconDataStreamSource(SourceType, Props));
  return NewSource;
}

void ULiveLinkViconDataStreamSourceNSDFactory::OnPropertiesSelected(ViconStreamProperties StreamProperties, FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const
{
  FString PropertiesString = StreamProperties.ToString();
  const FText SourceType = FText::FromString(FString(ULiveLinkViconDataStreamBlueprint::SOURCE_TYPE.c_str()));
  TSharedPtr<FLiveLinkViconDataStreamSource> SharedPtr = MakeShared<FLiveLinkViconDataStreamSource>(SourceType, StreamProperties);
  OnLiveLinkSourceCreated.ExecuteIfBound(SharedPtr, MoveTemp(PropertiesString));
}
#undef LOCTEXT_NAMESPACE
