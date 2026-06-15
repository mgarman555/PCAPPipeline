#include "BonjourResolver.h"
#include "CustomBonjourResolver.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <windns.h>
#include "Windows/HideWindowsPlatformTypes.h"

#include <future>

#pragma comment(lib, "dnsapi.lib")


TOptional<THostNameAndPort> ResolveServerAddress(const FNSDService& Service)
{
  // Prepare a context to shuttle data between callback and this thread
  struct ResolveContext
  {
    DNS_SERVICE_CANCEL CancelToken{};
    std::wstring FullName;
    std::wstring Hostname;
    uint16 Port = 0;
    DWORD Status = ERROR_SUCCESS;

    // Blocks calling thread until the resolve callback is called or timed out
    std::promise<void> Promise;
    std::future<void> Future;

    ResolveContext()
      : Future(Promise.get_future())
    {
    }

  } Context;

  //Build "Instance._service._tcp.local" query
  FString FullName = FString::Printf(
    TEXT("%s.%s.%s"),
    *Service.m_Name,
    *Service.m_Type,
    *Service.m_Domain);

  Context.FullName = std::wstring(*FullName, FullName.Len());

  // Fill out the async resolve request
  DNS_SERVICE_RESOLVE_REQUEST Request{};
  Request.Version = DNS_QUERY_REQUEST_VERSION1;
  Request.InterfaceIndex = 0;  // all interfaces
  Request.QueryName = Context.FullName.data();
  Request.pQueryContext = &Context;
  Request.pResolveCompletionCallback = [](DWORD i_Status, PVOID i_pQueryContext, PDNS_SERVICE_INSTANCE i_pInstance)
  {
    auto* ctx = static_cast<ResolveContext*>(i_pQueryContext);
    ctx->Status = i_Status;
    if (i_Status == ERROR_SUCCESS && i_pInstance)
    {
      ctx->Port = i_pInstance->wPort;
      ctx->Hostname.assign(i_pInstance->pszHostName);

      DnsServiceFreeInstance(i_pInstance);
    }

    // Wake up the waiting thread
    ctx->Promise.set_value();
  };

  // Start the resolve
  DNS_STATUS Start = DnsServiceResolve(&Request, &Context.CancelToken);
  if (Start != DNS_REQUEST_PENDING)
  {
    UE_LOG(LogBonjourResolver, Error, TEXT("DnsServiceResolve failed: %u"), Start);
    return {};
  }

  // Block until the callback fires or timeout
  if (Context.Future.wait_for(std::chrono::milliseconds(1000)) != std::future_status::ready)
  {
    DnsServiceResolveCancel(&Context.CancelToken);

    // If we fail to resolve the service, it is likely because it is served by a legacy Bonjour driven
    // server app. These handle '.' in the service name differently than Windows DNS-SD and we have to
    // do a custom resolve that doesn't split the name at .
    return MDNS::ResolveDottedServerAddress(Service);
  }

  // Check result status
  if (Context.Status != ERROR_SUCCESS)
  {
    UE_LOG(LogBonjourResolver, Error, TEXT("Resolve failed: %u"), Context.Status);
    return {};
  }

  // Successfully resolved host
  return THostNameAndPort(Context.Hostname.c_str(), Context.Port);
}
