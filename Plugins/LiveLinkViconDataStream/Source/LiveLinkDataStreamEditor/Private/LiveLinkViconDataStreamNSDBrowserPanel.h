#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "LiveLinkViconDataStreamNSDBrowserWin.h"

class FListedService
{
public:
  FNSDService Service;
  FString Subtype;
  FListedService(const FNSDService& Service, const FString& Subtype)
    : Service(Service)
    , Subtype(Subtype)
  {
  }
  bool operator==(const FListedService& Other) const
  {
    return Service == Other.Service && Subtype == Other.Subtype;
  }
};

class SLiveLinkViconDataStreamNSDBrowserPanel : public SCompoundWidget
{
public:
  DECLARE_DELEGATE_TwoParams(FOnListSelectionChanged, TSharedPtr<FListedService>, ESelectInfo::Type);
  SLATE_BEGIN_ARGS(SLiveLinkViconDataStreamNSDBrowserPanel) {}
  SLATE_EVENT(FOnListSelectionChanged, OnListSelectionChanged)
  SLATE_END_ARGS()

  ~SLiveLinkViconDataStreamNSDBrowserPanel();
  void Construct(const FArguments& args, asio::io_context& IoContext);
  // To be called by Browser's delgate
  void ReceiveUpdate(const TArray<FNSDService>& Services, const FString& Subtype);
  // Get selected service
  TOptional<FNSDService> GetSelectedService();
  // Called when selection changed
  FOnListSelectionChanged OnListSelectionChanged;

private:
  void OnListWidgetSelectionChanged(TSharedPtr<FListedService> service, ESelectInfo::Type type);

  // browser - started in second stage constructer `Construct(const FArguments&))`
  TMap<FString, TSharedPtr<FLiveLinkViconDataStreamNSDBrowser>> BrowserBySubtype;
  // Actual UI list widget
  TArray<TSharedPtr<FListedService>> Items;
  // Actual UI list widget
  TMap<FString, TArray<TSharedPtr<FListedService>>> ItemsBySubtype;
  // actual list widget
  TSharedPtr<SListView<TSharedPtr<FListedService>>> ListViewWidget;
  // callback to generate list item widgets
  TSharedRef<ITableRow> OnGenerateRowForList(
    TSharedPtr<FListedService> Item, const TSharedRef<STableViewBase>& OwnerTable);
};
