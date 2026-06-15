#pragma once

#if defined(_WIN32)

  #include "NSDCommon.h"
  #include "Delegates/Delegate.h"

  #include <mutex>


  #include "Windows/AllowWindowsPlatformTypes.h"
  #include <Winsock2.h>  // must come before any Win32 network headers
  #include <Ws2tcpip.h>
  #include <windns.h>  // defines DNS_SERVICE_BROWSE_REQUEST, DNS_SERVICE_CANCEL, DnsServiceBrowse, etc.
  #include "Windows/AllowWindowsPlatformAtomics.h"
  #include <asio/io_context.hpp>
  #include "Windows/HideWindowsPlatformAtomics.h"
  #include "Windows/HideWindowsPlatformTypes.h"


// Log category for Windows DNS browser
DECLARE_LOG_CATEGORY_CLASS(LogWindowsDNSBrowser, Display, All);

// Delegate to report new/removed services
DECLARE_DELEGATE_OneParam(FServiceUpdate, TArray<FNSDService>&);

class FLiveLinkViconDataStreamNSDBrowser : public TSharedFromThis<FLiveLinkViconDataStreamNSDBrowser>
{
public:
  // Subtype is the string after the comma, e.g. "Default"
  FLiveLinkViconDataStreamNSDBrowser(asio::io_context& i_IOContext, const FString& Subtype);
  ~FLiveLinkViconDataStreamNSDBrowser();

  // Called whenever the set of services changes
  FServiceUpdate ServiceUpdateDelegate;

  // Start/stop browsing
  void Start();
  void Stop();

private:
  // Internal start/stop on the asio thread
  void ServiceStart();
  void ServiceStop();

  // Marshals m_Services out to the delegate
  void ServiceServicesChanged();

  // Windows‐callback for each incoming PTR record
  static VOID CALLBACK WindowsBrowseCallback(DWORD i_Status, PVOID i_pQueryContext, PDNS_RECORD i_pDnsRecord);

  // Function to match subtype from instance name as Windows DNS-SD does not support subtype queries
  bool MatchesSubtype(const FString& InstanceName) const;

  asio::io_context& m_IOContext;
  std::wstring m_Query;
  FString m_Subtype;
  DNS_SERVICE_CANCEL m_CancelToken{};
  std::mutex m_ServicesMutex;
  TArray<FNSDService> m_Services;
};

#endif
