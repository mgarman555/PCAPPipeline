#include "LiveLinkViconDataStreamNSDBrowserPanel.h"

#include "Async/Async.h"

#include "Templates/SharedPointer.h"

SLiveLinkViconDataStreamNSDBrowserPanel::~SLiveLinkViconDataStreamNSDBrowserPanel()
{
  // Ensure the browsers are destroyed so they longer refer to the context
  // before destroying the runner. This is guaranteed by destruction order,
  // but best to be safe.
  for (const TPair<FString, TSharedPtr<FLiveLinkViconDataStreamNSDBrowser>>& Browser : BrowserBySubtype)
  {
    Browser.Value->Stop();
  }
  BrowserBySubtype.Empty();
}

void SLiveLinkViconDataStreamNSDBrowserPanel::Construct(const FArguments& args, asio::io_context& IoContext)
{
  TWeakPtr<SWidget> WeakWidgetPtr = AsShared().ToWeakPtr();
  TArray<FString> Subtypes{"_default", "_low-latency", "_always-live"};
  for (const FString& Subtype : Subtypes)
  {
    auto Browser = MakeShared<FLiveLinkViconDataStreamNSDBrowser>(IoContext, Subtype);
    //  Lambdas capture by value as required locals will go out of scope
    Browser->ServiceUpdateDelegate.BindLambda([WeakWidgetPtr, Subtype](TArray<FNSDService>& Services)
    {
      AsyncTask(
        // The reason we're wrapping this up in a task is so that the browser executes
        // the delegate on the game thread. This ensures thread safety.
        ENamedThreads::GameThread,
        [WeakWidgetPtr, Services, Subtype]()
      {
        if (TSharedPtr<SWidget> SharedWidgetPtr = WeakWidgetPtr.Pin())
        {
          // static_cast needed as no dynamic_cast in UE as RTTI is disabled
          // SharedWidgetPtr is guaranteed to be an SLiveLinkViconDataStreamNSDBrowserPanel
          // as it is created from AsShared() above
          auto* Panel = static_cast<SLiveLinkViconDataStreamNSDBrowserPanel*>(SharedWidgetPtr.Get());
          Panel->ReceiveUpdate(Services, Subtype);
        }
      });
    });
    Browser->Start();
    BrowserBySubtype.Add(Subtype, MoveTemp(Browser));
    ItemsBySubtype.Add(Subtype, {});
  }
  ChildSlot[SAssignNew(ListViewWidget, SListView<TSharedPtr<FListedService>>)
              .ListItemsSource(&Items)
              .SelectionMode(ESelectionMode::Type::Single)
              .OnGenerateRow(this, &SLiveLinkViconDataStreamNSDBrowserPanel::OnGenerateRowForList)
              .OnSelectionChanged(this, &SLiveLinkViconDataStreamNSDBrowserPanel::OnListWidgetSelectionChanged)
              .HeaderRow(
                SNew(SHeaderRow) + SHeaderRow::Column("Name").DefaultLabel(FText::FromString("Name")) + SHeaderRow::Column("Type").DefaultLabel(FText::FromString("Type")))];
}

// No need for locking as ReceiveUpdate only ever called using AsyncTask on game thread
void SLiveLinkViconDataStreamNSDBrowserPanel::ReceiveUpdate(const TArray<FNSDService>& Services, const FString& Subtype)
{
  // Replace current list of services for subtype with update
  ItemsBySubtype[Subtype].Empty();
  for (const FNSDService& Service : Services)
  {
    ItemsBySubtype[Subtype].Add(MakeShared<FListedService>(Service, Subtype));
  }
  // Rebuild list
  Items.Empty();
  TArray<TSharedPtr<FListedService>> SavedSelection = ListViewWidget->GetSelectedItems();
  for (const TPair<FString, TArray<TSharedPtr<FListedService>>>& Pair : ItemsBySubtype)
  {
    for (const TSharedPtr<FListedService>& ListedService : Pair.Value)
    {
      Items.Add(ListedService);
    }
  }
  // Restore saved items - maximum of one as selection mode is single
  for (TSharedPtr<FListedService>& SavedItem : SavedSelection)
  {
    for (TSharedPtr<FListedService>& Item : Items)
    {
      if (*Item == *SavedItem)
      {
        ListViewWidget->SetItemSelection(Item, true);
      }
    }
  }
  // Request UI update
  ListViewWidget->RequestListRefresh();
}


TOptional<FNSDService> SLiveLinkViconDataStreamNSDBrowserPanel::GetSelectedService()
{
  TArray<TSharedPtr<FListedService>> SelectedItems;
  int32 Num = ListViewWidget->GetSelectedItems(SelectedItems);
  // Currently it's only dealing with one selection
  // Need to discuss if this panel should support multi-route
  if (Num != 1)
  {
    return TOptional<FNSDService>();
  }
  FListedService ListedService = *SelectedItems[0];
  FNSDService SelectedNSDService(ListedService.Service);

  return SelectedNSDService;
}

TSharedRef<ITableRow> SLiveLinkViconDataStreamNSDBrowserPanel::OnGenerateRowForList(
  TSharedPtr<FListedService> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
  class SModuleItemWidget : public SMultiColumnTableRow<TSharedPtr<FListedService>>
  {
  public:
    SLATE_BEGIN_ARGS(SModuleItemWidget) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedPtr<FListedService> InListItem)
    {
      Item = InListItem;
      SMultiColumnTableRow<TSharedPtr<FListedService>>::Construct(FSuperRowType::FArguments(), InOwnerTable);
    }

    TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName)
    {
      FString Property;
      if (ColumnName == "Name")
      {
        Property = Item->Service.m_Name;
      }
      else if (ColumnName == "Type")
      {
        Property = Item->Subtype;
      }
      return SNew(STextBlock).Text(FText::FromString(Property)).ToolTipText(FText::FromString(Item->Service.m_Name));
    }

    TSharedPtr<FListedService> Item;
  };
  return SNew(SModuleItemWidget, OwnerTable, InItem);
}

void SLiveLinkViconDataStreamNSDBrowserPanel::OnListWidgetSelectionChanged(TSharedPtr<FListedService> service, ESelectInfo::Type type)
{
  OnListSelectionChanged.ExecuteIfBound(service, type);
}

#undef LOCTEXT_NAMESPACE
