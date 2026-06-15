// Copyright (c) 2021 Vicon Motion Systems Ltd. All Rights Reserved.

// =========================================================================
// UI for LiveLink connection to a Vicon server
//
// =========================================================================

#pragma once

#include "Misc/Guid.h"
#include "Widgets/SCompoundWidget.h"

#include "ViconStreamFrameReader.h"

namespace asio
{
  class io_context;
}

DECLARE_LOG_CATEGORY_CLASS(LogNSDEditor, Display, All)
DECLARE_DELEGATE_OneParam(FOnDataStreamPropertiesSelected, ViconStreamProperties);

class SLiveLinkViconDataStreamNSDBrowserPanel;
class SLiveLinkViconDataStreamSourcePropEditor;
class SButton;
class FListedService;

class SLiveLinkViconDataStreamSourceNSDEditor : public SCompoundWidget
{
  SLATE_BEGIN_ARGS(SLiveLinkViconDataStreamSourceNSDEditor) {}
  SLATE_EVENT(FOnDataStreamPropertiesSelected, OnPropertiesSelected)
  SLATE_END_ARGS()

  ~SLiveLinkViconDataStreamSourceNSDEditor();

  void Construct(const FArguments& args, asio::io_context& IoContextRunner);

private:
  TSharedPtr<SLiveLinkViconDataStreamNSDBrowserPanel> Browser;
  TSharedPtr<SLiveLinkViconDataStreamSourcePropEditor> ConnectionProperties;
  TSharedPtr<SButton> CreateButton;

  FReply CreateSource() const;
  void OnBrowserSelectionChanged(TSharedPtr<FListedService> service, ESelectInfo::Type type);

  FOnDataStreamPropertiesSelected OnPropertiesSelected;
};
