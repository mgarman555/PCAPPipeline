

#if defined(_WIN32)

  #include "LiveLinkViconDataStreamNSDBrowserWin.h"
  #include "ViconAsioThrowException.hpp"

  #include <asio/post.hpp>

  // Load the native Windows DNS API library
  #pragma comment(lib, "dnsapi.lib")

// Service type and domain we’re browsing for
static const std::wstring MAIN_SERVICE_TYPE = L"_vicondatastream._tcp";
static const std::wstring SERVICE_DOMAIN = L"local";

FLiveLinkViconDataStreamNSDBrowser::FLiveLinkViconDataStreamNSDBrowser(asio::io_context& i_IOContext, const FString& Subtype)
  : m_IOContext(i_IOContext)
{
  m_Subtype = Subtype;
  m_Query = std::wstring(TCHAR_TO_WCHAR(*m_Subtype)) + L"._sub." + MAIN_SERVICE_TYPE + L"." + SERVICE_DOMAIN;
}

FLiveLinkViconDataStreamNSDBrowser::~FLiveLinkViconDataStreamNSDBrowser()
{
  // Ensure we’ve torn down the browse
  if (m_CancelToken.reserved)
  {
    DnsServiceBrowseCancel(&m_CancelToken);
    m_CancelToken = {};
  }
}

void FLiveLinkViconDataStreamNSDBrowser::Start()
{
  // Fire off OnStart() on the asio thread
  m_IOContext.post([Self = AsShared()]()
  {
    Self->ServiceStart();
  });
}

void FLiveLinkViconDataStreamNSDBrowser::Stop()
{
  // Fire off OnStop() on the asio thread
  m_IOContext.post([Self = AsShared()]()
  {
    Self->ServiceStop();
  });
}

void FLiveLinkViconDataStreamNSDBrowser::ServiceStart()
{
  DNS_SERVICE_BROWSE_REQUEST Request{};
  Request.Version = DNS_QUERY_REQUEST_VERSION1;
  Request.InterfaceIndex = 0;  // all interfaces
  Request.QueryName = m_Query.c_str();
  Request.pBrowseCallback = WindowsBrowseCallback;
  Request.pQueryContext = this;

  DNS_STATUS Status = DnsServiceBrowse(&Request, &m_CancelToken);
  if (Status != DNS_REQUEST_PENDING)
  {
    UE_LOG(LogWindowsDNSBrowser, Error, TEXT("DnsServiceBrowse failed: %u"), Status);
  }
}

void FLiveLinkViconDataStreamNSDBrowser::ServiceStop()
{
  std::scoped_lock Guard(m_ServicesMutex);
  if (m_CancelToken.reserved)
  {
    DnsServiceBrowseCancel(&m_CancelToken);
    m_CancelToken = {};
    m_Services.Empty();
  }
}

void FLiveLinkViconDataStreamNSDBrowser::ServiceServicesChanged()
{
  // Clone under lock and fire the delegate
  TArray<FNSDService> Snapshot;
  {
    std::scoped_lock Guard(m_ServicesMutex);
    Snapshot = m_Services;
  }
  ServiceUpdateDelegate.ExecuteIfBound(Snapshot);
}

VOID CALLBACK FLiveLinkViconDataStreamNSDBrowser::WindowsBrowseCallback(DWORD i_Status, PVOID i_pQueryContext, PDNS_RECORD i_pDnsRecord)
{
  auto* Browser = static_cast<FLiveLinkViconDataStreamNSDBrowser*>(i_pQueryContext);

  if (i_Status == ERROR_CANCELLED)
  {
    DnsRecordListFree(i_pDnsRecord, DnsFreeRecordListDeep);
    return;
  }

  if (i_Status != ERROR_SUCCESS)
  {
    UE_LOG(LogWindowsDNSBrowser, Error, TEXT("DNS browse callback error: %u"), i_Status);
    DnsRecordListFree(i_pDnsRecord, DnsFreeRecordListDeep);
    return;
  }

  // Update the services array
  bool bServicesChanged = false;
  {
    std::scoped_lock Guard(Browser->m_ServicesMutex);

    for (PDNS_RECORD ptr = i_pDnsRecord; ptr; ptr = ptr->pNext)
    {
      if (ptr->wType != DNS_TYPE_PTR)
      {
        // Skip non-PTR records
        continue;
      }

      // Extract <InstanceName> from e.g. "ShogunLive 1.16 on HOSTPC (Default)._vicondatastream._tcp.local"
      FString FullName(ptr->Data.PTR.pNameHost);
      FString FullService(ptr->pName);

      FString Instance = FullName;
      FString Suffix = FullService;

      // Services registered with Bonjour does not have the subtype in the FullName. Those registered
      // with Windowds DNS-SD do have it. Both have the full subtype and main service in the pName field.

      if (FullName.EndsWith(FullService, ESearchCase::IgnoreCase))
      {
        // +1 to drop the dot before the suffix
        const int32 ChopCount = FullService.Len() + 1;
        Instance = FullName.LeftChop(ChopCount);
      }
      else
      {
        // Strip the susbtype from the suffix
        FString LeftPart, RightPart;
        if (Suffix.Split(TEXT("._sub."), &LeftPart, &RightPart, ESearchCase::IgnoreCase, ESearchDir::FromStart))
        {
          // RightPart is now "_vicondatastream._tcp.local"
          Suffix = RightPart;
        }

        // +1 to drop the dot before the suffix
        const int32 ChopCount = Suffix.Len() + 1;
        Instance = FullName.LeftChop(ChopCount);
      }

      // Split the suffix into service type and domain
      int32 LastDot = INDEX_NONE;
      Suffix.FindLastChar(TEXT('.'), LastDot);
      FString ServiceType = (LastDot != INDEX_NONE ? Suffix.Left(LastDot) : Suffix);
      FString Domain = (LastDot != INDEX_NONE ? Suffix.Mid(LastDot + 1) : FString());

      FNSDService Service(Instance, ServiceType, Domain);

      if (ptr->dwTtl > 0)
      {
        // Service is present
        if (!Browser->m_Services.Contains(Service))
        {
          Browser->m_Services.Add(Service);
          UE_LOG(LogWindowsDNSBrowser, Display, TEXT("Found new service: '%s' (%s) in %s"), *Service.m_Name, *Service.m_Type, *Service.m_Domain);
          bServicesChanged = true;
        }
      }
      else
      {
        // Service is being removed (TTL is 0)
        if (Browser->m_Services.Remove(Service) > 0)
        {
          UE_LOG(LogWindowsDNSBrowser, Display, TEXT("Service removed: '%s'"), *Service.m_Name);
          bServicesChanged = true;
        }
      }
    }
  }

  // Notify on the asio thread
  if (bServicesChanged)
  {
    asio::post(Browser->m_IOContext, [Self = Browser->AsShared()]()
    {
      Self->ServiceServicesChanged();
    });
  }

  DnsRecordListFree(i_pDnsRecord, DnsFreeRecordListDeep);
}


#endif
