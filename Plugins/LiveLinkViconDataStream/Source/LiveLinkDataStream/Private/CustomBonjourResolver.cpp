#include "CustomBonjourResolver.h"
#include "BonjourResolver.h"
#include "NSDCommon.h"

#if defined(_WIN32)

  #include "Windows/AllowWindowsPlatformTypes.h"
  #include <WinSock2.h>
  #include <WS2tcpip.h>
  #include <Iphlpapi.h>
  #include <windns.h>
  #include "Windows/HideWindowsPlatformTypes.h"

  #include <future>
  #include <thread>
  #include <vector>
  #include <string>
  #include <string_view>
  #include <atomic>
  #include <utility>

  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "iphlpapi.lib")
  #pragma comment(lib, "dnsapi.lib")

namespace MDNS
{
  // Apple's Bonjour DNS-SD Open Source implementation
  // https://github.com/apple-oss-distributions/mDNSResponder

  // --- Protocol Constants ---
  constexpr int BONJOUR_PORT = 5353;
  constexpr std::string_view BONJOUR_IPV4 = "224.0.0.251";
  constexpr std::string_view BONJOUR_IPV6 = "ff02::fb";
  constexpr uint16_t DNS_CLASS_IN = 1;
  constexpr uint16_t DNS_FLAG_RESPONSE = 0x8000;
  constexpr uint8_t DNS_NAME_COMPRESSION_MASK = 0xC0;
  constexpr uint16_t MDNS_MULTICAST_TTL = 255;

  // --- DNS Packet Structures (On-the-Wire Format) ---
  // The pragma pack directives ensure that the structures are packed tightly without padding.
  #pragma pack(push, 1)
  struct MDNS_WIRE_QUESTION
  {
    WORD QType;
    WORD QClass;
  };

  struct MDNS_WIRE_RR_HEADER
  {
    WORD Type;
    WORD Class;
    DWORD TTL;
    WORD DataLength;
  };

  struct MDNS_HEADER
  {
    WORD TransactionId;
    WORD Flags;
    WORD QuestionCount;
    WORD AnswerCount;
    WORD AuthorityCount;
    WORD AdditionalCount;
  };
  #pragma pack(pop)

  // RAII wrapper to manage a SOCKET handle.
  // Ensures closesocket is called automatically when the object goes out of scope.
  class ScopedSocket
  {
  public:
    explicit ScopedSocket(SOCKET sock = INVALID_SOCKET)
      : m_Socket(sock)
    {}

    ~ScopedSocket()
    {
      if (m_Socket != INVALID_SOCKET)
      {
        closesocket(m_Socket);
      }
    }

    // Non-copyable
    ScopedSocket(const ScopedSocket&) = delete;
    ScopedSocket& operator=(const ScopedSocket&) = delete;

    // Movable
    ScopedSocket(ScopedSocket&& other) noexcept
      : m_Socket(other.m_Socket)
    {
      other.m_Socket = INVALID_SOCKET;
    }

    ScopedSocket& operator=(ScopedSocket&& other) noexcept
    {
      if (this != &other)
      {
        // Ensure we close the current socket before taking ownership of the new one
        if (m_Socket != INVALID_SOCKET)
        {
          closesocket(m_Socket);
        }
        m_Socket = other.m_Socket;
        other.m_Socket = INVALID_SOCKET;
      }
      return *this;
    }

    SOCKET get() const { return m_Socket; }

  private:
    SOCKET m_Socket;
  };


  // RAII wrapper to manage the lifetime of the Winsock library.
  class FWsaManager
  {
  public:
    FWsaManager()
    {
      WSADATA WsaData;
      if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
      {
        UE_LOG(LogBonjourResolver, Error, TEXT("WSAStartup failed."));
      }
    }

    ~FWsaManager()
    {
      WSACleanup();
    }

    FWsaManager(const FWsaManager&) = delete;
    FWsaManager& operator=(const FWsaManager&) = delete;
  };

  // Encodes a single DNS label.
  void EncodeLabel(std::vector<char>& o_rBuffer, const std::string_view i_rLabel)
  {
    if (i_rLabel.length() > DNS_MAX_LABEL_BUFFER_LENGTH)
    {
      UE_LOG(LogBonjourResolver, Error, TEXT("Label '%s' exceeds max length of 63 characters"), *FString(i_rLabel.data(), i_rLabel.length()));
      return;  // DNS label length limit
    }
    o_rBuffer.push_back(static_cast<char>(i_rLabel.length()));
    o_rBuffer.insert(o_rBuffer.end(), i_rLabel.begin(), i_rLabel.end());
  }


  // Encodes a dotted name (e.g., "_service._tcp.local") into DNS label format.
  void EncodeDnsNamePart(std::vector<char>& o_rBuffer, const std::string_view i_rNamePart)
  {
    size_t Start = 0;
    while (Start < i_rNamePart.length())
    {
      size_t Pos = i_rNamePart.find('.', Start);
      if (Pos == std::string_view::npos)
      {
        EncodeLabel(o_rBuffer, i_rNamePart.substr(Start));
        break;
      }
      EncodeLabel(o_rBuffer, i_rNamePart.substr(Start, Pos - Start));
      Start = Pos + 1;
    }
  }

  // Encodes the full service name for an SRV query.
  // Don't split on dots in the name part
  void EncodeSrvQueryName(std::vector<char>& o_rBuffer, const FNSDService& i_rService)
  {
    EncodeLabel(o_rBuffer, TCHAR_TO_UTF8(*i_rService.m_Name));
    EncodeDnsNamePart(o_rBuffer, TCHAR_TO_UTF8(*i_rService.m_Type));
    EncodeDnsNamePart(o_rBuffer, TCHAR_TO_UTF8(*i_rService.m_Domain));
    o_rBuffer.push_back(0);  // Null terminator for the full name.
  }

  // Safely reads a POD type from a buffer view and advances the view.
  // Returns a pointer to the data in the buffer, or nullptr if out of bounds.
  template<typename T>
  const T* ReadFromBuffer(std::string_view& Buffer)
  {
    if (Buffer.size() < sizeof(T))
    {
      return nullptr;
    }
    const T* Ptr = reinterpret_cast<const T*>(Buffer.data());
    Buffer.remove_prefix(sizeof(T));
    return Ptr;
  }

  // Parses a DNS name from a response buffer, handling name compression.
  // io_rData: The view of the data at the current parsing offset. Will be advanced past the name.
  // i_rFullPacket: The view of the entire original packet, for handling compression jumps.
  // Returns the parsed DNS name as a UTF-8 string.
  std::string ParseDnsName(std::string_view& io_rData, const std::string_view i_rFullPacket)
  {
    std::string Result;
    std::string_view CurrentView = io_rData;
    bool bJumped = false;

    while (!CurrentView.empty() && CurrentView.front() != 0)
    {
      uint8_t FirstByte = static_cast<uint8_t>(CurrentView.front());
      if ((FirstByte & DNS_NAME_COMPRESSION_MASK) == DNS_NAME_COMPRESSION_MASK)
      {
        if (CurrentView.size() < 2)
        {
          return "";  // Invalid pointer
        }

        int JumpOffset = ((FirstByte & 0x3F) << 8) | static_cast<uint8_t>(CurrentView[1]);
        if (JumpOffset >= i_rFullPacket.size())
        {
          return "";  // Invalid jump
        }

        if (!bJumped)
        {
          io_rData.remove_prefix(2);
          bJumped = true;
        }
        CurrentView = i_rFullPacket.substr(JumpOffset);
      }
      else
      {
        uint8_t Len = FirstByte;
        CurrentView.remove_prefix(1);
        if (CurrentView.size() < Len)
        {
          return "";  // Invalid length
        }

        if (!Result.empty())
        {
          Result += '.';
        }
        Result.append(CurrentView.data(), Len);

        CurrentView.remove_prefix(Len);
        if (!bJumped)
        {
          io_rData = CurrentView;
        }
      }
    }

    if (!bJumped && !io_rData.empty())
    {
      io_rData.remove_prefix(1);  // Move past the null terminator
    }
    return Result;
  }

  // Worker function to listen for mDNS responses.
  // This function takes ownership of ScopedSocket objects.
  TOptional<THostNameAndPort> ListenForMdnsResponse(std::vector<ScopedSocket> Sockets, WORD TransactionId)
  {
    if (Sockets.empty())
    {
      return {};
    }

    timeval Timeout{3, 0};  // 3 seconds

    // Set up the file descriptor set (a collection of socket handles to monitor).
    fd_set Fds;
    FD_ZERO(&Fds);
    for (const auto& Sock : Sockets)
    {
      FD_SET(Sock.get(), &Fds);
    }

    // Wait for a response using from any of the sockets or timeout (returns 0 on timeout, negative on error).
    int SelectResult = select(0, &Fds, nullptr, nullptr, &Timeout);
    if (SelectResult <= 0)
    {
      if (SelectResult == 0)
      {
        UE_LOG(LogBonjourResolver, Error, TEXT("Resolve timed out waiting for mDNS response."));
      }
      return {};
    }


    for (const auto& Sock : Sockets)
    {
      // Find the socket with incoming data (FD_ISSET)
      if (!FD_ISSET(Sock.get(), &Fds))
      {
        continue;
      }

      // Read the response data from the socket.
      char RecvBuffer[1500];
      int RecvLen = recv(Sock.get(), RecvBuffer, sizeof(RecvBuffer), 0);
      if (RecvLen < sizeof(MDNS_HEADER))
      {
        continue;
      }

      // Parse the response header to check if it matches the transaction ID and is a valid response.
      std::string_view ResponseView(RecvBuffer, RecvLen);
      const auto* pRespHeader = ReadFromBuffer<MDNS_HEADER>(ResponseView);

      if (!pRespHeader || pRespHeader->TransactionId != TransactionId || !(ntohs(pRespHeader->Flags) & DNS_FLAG_RESPONSE))
      {
        continue;
      }

      // Skip over the questions section, we're only interested in answers.
      for (int i = 0; i < ntohs(pRespHeader->QuestionCount); ++i)
      {
        ParseDnsName(ResponseView, {RecvBuffer, (size_t)RecvLen});
        ResponseView.remove_prefix(sizeof(MDNS_WIRE_QUESTION));
      }

      // Iterate through the answers section to find an SRV record.
      for (int i = 0; i < ntohs(pRespHeader->AnswerCount); ++i)
      {
        if (ResponseView.empty())
        {
          break;
        }
        ParseDnsName(ResponseView, {RecvBuffer, (size_t)RecvLen});

        const auto* pRrHeader = ReadFromBuffer<MDNS_WIRE_RR_HEADER>(ResponseView);
        if (!pRrHeader)
        {
          break;
        }

        int DataLength = ntohs(pRrHeader->DataLength);
        if (ResponseView.size() < DataLength)
        {
          break;
        }

        // We're only looking for SRV type records.
        if (ntohs(pRrHeader->Type) == DNS_TYPE_SRV && DataLength >= 6)
        {
          // Skip priority and weight fields (4 bytes), then read the port (2 bytes).
          std::string_view DataView = ResponseView.substr(0, DataLength);
          DataView.remove_prefix(4);  // Skip priority and weight
          const uint16_t Port = ntohs(*reinterpret_cast<const WORD*>(DataView.data()));
          DataView.remove_prefix(2);

          std::string TargetHostStr = ParseDnsName(DataView, {RecvBuffer, (size_t)RecvLen});

          // Found a valid response.
          return THostNameAndPort(UTF8_TO_TCHAR(TargetHostStr.c_str()), Port);
        }

        // Move the response view past the current record.
        ResponseView.remove_prefix(DataLength);
      }
    }
    return {};  // No suitable response found
  }


  TOptional<THostNameAndPort> ResolveDottedServerAddress(const FNSDService& i_rService)
  {
    // Initialize Winsock if not already done
    static FWsaManager TheWsaManager;

    // Transaction ID is only used here, so its scope can be narrowed.
    static std::atomic<uint16_t> GTransactionId{0};

    // Retrieve the list of network adapters.
    ULONG BufferSize = 15000;
    std::vector<char> AdapterBuffer(BufferSize);
    auto pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(AdapterBuffer.data());
    DWORD RetVal = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, pAddresses, &BufferSize);

    std::vector<ScopedSocket> Sockets;
    if (RetVal == ERROR_SUCCESS)
    {
      // Iterate through the adapters and create sockets for each unicast address.
      for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr != nullptr; pCurr = pCurr->Next)
      {
        // Skip adapters that are not active.
        if (pCurr->OperStatus != IfOperStatusUp)
        {
          continue;
        }

        for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurr->FirstUnicastAddress; pUnicast != nullptr; pUnicast = pUnicast->Next)
        {
          // Create a socket for the unicast address (IPv4 case)
          ScopedSocket Sock;
          if (pUnicast->Address.lpSockaddr->sa_family == AF_INET)
          {
            SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (s == INVALID_SOCKET)
            {
              continue;
            }

            Sock = ScopedSocket(s);
            sockaddr_in LocalAddr = *reinterpret_cast<sockaddr_in*>(pUnicast->Address.lpSockaddr);
            LocalAddr.sin_port = htons(0);

            // Bind the socket to the adapter's ip address.
            if (bind(s, reinterpret_cast<sockaddr*>(&LocalAddr), sizeof(LocalAddr)) != 0)
            {
              UE_LOG(LogBonjourResolver, Error, TEXT("Failed to bind IPv4 socket to adapter %s: %d"), pCurr->FriendlyName, WSAGetLastError());
              continue;
            }

            // Join the multicast group for mDNS.
            ip_mreq mreq{};
            inet_pton(AF_INET, BONJOUR_IPV4.data(), &mreq.imr_multiaddr);
            mreq.imr_interface.s_addr = LocalAddr.sin_addr.s_addr;
            if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char*>(&mreq), sizeof(mreq)) != 0)
            {
              UE_LOG(LogBonjourResolver, Error, TEXT("Failed to join IPv4 multicast group on adapter %s: %d"), pCurr->FriendlyName, WSAGetLastError());
              continue;
            }
            DWORD Ttl = MDNS_MULTICAST_TTL;
            setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<char*>(&Ttl), sizeof(Ttl));
          }

          // Do the same for IPv6 addresses.
          else if (pUnicast->Address.lpSockaddr->sa_family == AF_INET6)
          {
            SOCKET s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            if (s == INVALID_SOCKET)
            {
              continue;
            }

            Sock = ScopedSocket(s);
            DWORD Ipv6Only = 0;
            setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&Ipv6Only), sizeof(Ipv6Only));

            sockaddr_in6 LocalAddr6 = *reinterpret_cast<sockaddr_in6*>(pUnicast->Address.lpSockaddr);
            LocalAddr6.sin6_port = htons(0);

            if (bind(s, reinterpret_cast<sockaddr*>(&LocalAddr6), sizeof(LocalAddr6)) != 0)
            {
              UE_LOG(LogBonjourResolver, Error, TEXT("Failed to bind IPv6 socket to adapter %s: %d"), pCurr->FriendlyName, WSAGetLastError());
              continue;
            }

            ipv6_mreq mreq6{};
            inet_pton(AF_INET6, BONJOUR_IPV6.data(), &mreq6.ipv6mr_multiaddr);
            mreq6.ipv6mr_interface = pCurr->Ipv6IfIndex;
            if (setsockopt(s, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, reinterpret_cast<char*>(&mreq6), sizeof(mreq6)) != 0)
            {
              UE_LOG(LogBonjourResolver, Error, TEXT("Failed to join IPv6 multicast group on adapter %s: %d"), pCurr->FriendlyName, WSAGetLastError());
              continue;
            }
            DWORD Hops = MDNS_MULTICAST_TTL;
            setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, reinterpret_cast<char*>(&Hops), sizeof(Hops));
          }

          if (Sock.get() != INVALID_SOCKET)
          {
            // Set the socket to reuse the address as required by mDNS, which is a shared multicast address.
            // This allows multiple applications to bind to the same mDNS port.
            BOOL bReuseAddr = true;
            setsockopt(Sock.get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&bReuseAddr), sizeof(bReuseAddr));
            Sockets.push_back(std::move(Sock));
          }
        }
      }
    }

    if (Sockets.empty())
    {
      UE_LOG(LogBonjourResolver, Error, TEXT("No suitable network adapters found to send mDNS query."));
      return {};
    }

    // --- Construct and send the mDNS query packet ---
    // Bulids the raw network packet to query for a mDNS service.
    std::vector<char> QueryBuffer;

    // Construct the mDNS header with the service to query.
    MDNS_HEADER Header{};
    Header.TransactionId = GTransactionId++;  // Each query must have a unique transaction ID.
    Header.QuestionCount = htons(1);

    QueryBuffer.insert(QueryBuffer.end(), reinterpret_cast<char*>(&Header), reinterpret_cast<char*>(&Header) + sizeof(Header));
    EncodeSrvQueryName(QueryBuffer, i_rService);

    // Specify that we are looking for a SRV record.
    MDNS_WIRE_QUESTION Question{};
    Question.QType = htons(DNS_TYPE_SRV);
    Question.QClass = htons(DNS_CLASS_IN);
    QueryBuffer.insert(QueryBuffer.end(), reinterpret_cast<char*>(&Question), reinterpret_cast<char*>(&Question) + sizeof(Question));

    // Build the destination address for IPv4
    sockaddr_in DestAddrV4{};
    DestAddrV4.sin_family = AF_INET;
    DestAddrV4.sin_port = htons(BONJOUR_PORT);
    inet_pton(AF_INET, BONJOUR_IPV4.data(), &DestAddrV4.sin_addr);  // Convert string to binary form

    // Build the destination address for IPv6
    sockaddr_in6 DestAddrV6{};
    DestAddrV6.sin6_family = AF_INET6;
    DestAddrV6.sin6_port = htons(BONJOUR_PORT);
    inet_pton(AF_INET6, BONJOUR_IPV6.data(), &DestAddrV6.sin6_addr);  // Convert string to binary form

    // Send the query to all sockets that are bound to the multicast address.
    for (const auto& Sock : Sockets)
    {
      sockaddr_storage SockAddr;
      int Len = sizeof(SockAddr);
      if (getsockname(Sock.get(), reinterpret_cast<sockaddr*>(&SockAddr), &Len) == 0)
      {
        if (SockAddr.ss_family == AF_INET)
        {
          sendto(Sock.get(), QueryBuffer.data(), QueryBuffer.size(), 0, reinterpret_cast<sockaddr*>(&DestAddrV4), sizeof(DestAddrV4));
        }
        else if (SockAddr.ss_family == AF_INET6)
        {
          sendto(Sock.get(), QueryBuffer.data(), QueryBuffer.size(), 0, reinterpret_cast<sockaddr*>(&DestAddrV6), sizeof(DestAddrV6));
        }
      }
    }

    // ListenForMdnsResponse blocks until it receives a response or times out
    return ListenForMdnsResponse(std::move(Sockets), Header.TransactionId);
  }

}  // namespace MDNS

#endif  // defined(_WIN32)
