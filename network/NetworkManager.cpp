#include "NetworkManager.hpp"
#include <iostream>
#include <cassert>
#include <algorithm>
#include <cctype>

#include <cstring>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
#endif

// ��@ Singleton
NetworkManager& NetworkManager::Instance() {
    static NetworkManager instance;
    return instance;
}

// �R�A Callback ��o������
void NetworkManager::OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo) {
    NetworkManager::Instance().OnConnectionStatusChangedHelper(pInfo);
}

bool NetworkManager::Initialize() {
#ifdef _WIN32
    if (!m_WsaInited) {
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            return false;
        }
        m_WsaInited = true;
    }
#endif

    SteamDatagramErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
        std::cerr << "GameNetworkingSockets init failed: " << errMsg << std::endl;
        return false;
    }

    m_pInterface = SteamNetworkingSockets();
    return true;
}

void NetworkManager::Shutdown() {
    if (m_IsServer && m_hListenSock != k_HSteamListenSocket_Invalid) {
        m_pInterface->CloseListenSocket(m_hListenSock);
        m_hListenSock = k_HSteamListenSocket_Invalid;
    }
    if (m_hConnection != k_HSteamNetConnection_Invalid) {
        m_pInterface->CloseConnection(m_hConnection, 0, "Shutdown", true);
        m_hConnection = k_HSteamNetConnection_Invalid;
    }

    GameNetworkingSockets_Kill();

#ifdef _WIN32
    if (m_WsaInited) {
        WSACleanup();
        m_WsaInited = false;
    }
#endif
}

bool NetworkManager::StartServer(int port) {
    m_IsServer = true;
    m_ClientConnections.clear();
    connectedPlayerIDs.clear();
    playerWeaponMap.clear();
    connectionPlayerIDs.clear();

    SteamNetworkingIPAddr serverAddr;
    serverAddr.Clear();
    serverAddr.m_port = (uint16_t)port; // listen on all interfaces

    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)OnConnectionStatusChanged);

    m_hListenSock = m_pInterface->CreateListenSocketIP(serverAddr, 1, &opt);

    if (m_hListenSock == k_HSteamListenSocket_Invalid) {
        std::cerr << "Failed to listen on port " << port << std::endl;
        return false;
    }

    std::cout << "GNS Server started on port " << port << std::endl;
    m_MyID = 0;
    m_IsConnected = true;
    m_IsServer = true;
    return true;
}

bool NetworkManager::ParseHostPort(const std::string& input, std::string& outHost, int& outPort, int defaultPort) {
    outHost.clear();
    outPort = defaultPort;

    if (input.empty()) return false;

    // [ipv6]:port
    if (!input.empty() && input.front() == '[') {
        const auto rb = input.find(']');
        if (rb == std::string::npos) return false;
        outHost = input.substr(1, rb - 1);
        if (rb + 1 < input.size() && input[rb + 1] == ':') {
            try {
                outPort = std::stoi(input.substr(rb + 2));
            } catch (...) {
                return false;
            }
        }
        return true;
    }

    // host:port (use last ':' so we don't break on extra ':' in other contexts)
    const auto pos = input.rfind(':');
    if (pos != std::string::npos) {
        const std::string maybePort = input.substr(pos + 1);
        const bool allDigits = !maybePort.empty() && std::all_of(maybePort.begin(), maybePort.end(), [](unsigned char c) { return std::isdigit(c); });
        if (allDigits) {
            outHost = input.substr(0, pos);
            try {
                outPort = std::stoi(maybePort);
            } catch (...) {
                return false;
            }
            return true;
        }
    }

    outHost = input;
    return true;
}

bool NetworkManager::ResolveHostToAddr(const std::string& hostOrIp, int port, SteamNetworkingIPAddr& outAddr) {
    outAddr.Clear();

    // 1) Try direct parse first (IPv4/IPv6 in string form)
    if (outAddr.ParseString(hostOrIp.c_str())) {
        outAddr.m_port = (uint16_t)port;
        return true;
    }

    // 2) DNS resolve via getaddrinfo
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM; // any is fine for address resolution

    addrinfo* result = nullptr;
    std::string service = std::to_string(port);
    int rc = getaddrinfo(hostOrIp.c_str(), service.c_str(), &hints, &result);
    if (rc != 0 || !result) {
#ifdef _WIN32
        std::cerr << "DNS resolve failed for host '" << hostOrIp << "': " << rc << std::endl;
#else
        std::cerr << "DNS resolve failed for host '" << hostOrIp << "': " << gai_strerror(rc) << std::endl;
#endif
        return false;
    }

    // Prefer IPv4 first for compatibility, then IPv6
    const addrinfo* chosen = nullptr;
    for (const addrinfo* ai = result; ai; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET) { chosen = ai; break; }
        if (!chosen && ai->ai_family == AF_INET6) chosen = ai;
    }

    bool ok = false;
    if (chosen && chosen->ai_family == AF_INET) {
        const sockaddr_in* sa = (const sockaddr_in*)chosen->ai_addr;
        uint32_t ipHostOrder = ntohl(sa->sin_addr.s_addr);
        outAddr.SetIPv4(ipHostOrder, (uint16_t)port);
        ok = true;
    }
    else if (chosen && chosen->ai_family == AF_INET6) {
        const sockaddr_in6* sa6 = (const sockaddr_in6*)chosen->ai_addr;
        outAddr.SetIPv6(sa6->sin6_addr.s6_addr, (uint16_t)port);
        ok = true;
    }

    freeaddrinfo(result);
    return ok;
}

bool NetworkManager::Connect(const std::string& hostOrIp, int port) {
    m_IsServer = false;

    SteamNetworkingIPAddr serverAddr;
    if (!ResolveHostToAddr(hostOrIp, port, serverAddr)) {
        std::cerr << "Failed to resolve server address: " << hostOrIp << ":" << port << std::endl;
        return false;
    }

    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)OnConnectionStatusChanged);

    m_hConnection = m_pInterface->ConnectByIPAddress(serverAddr, 1, &opt);

    if (m_hConnection == k_HSteamNetConnection_Invalid) {
        std::cerr << "Failed to create connection." << std::endl;
        return false;
    }

    return true;
}

void NetworkManager::Disconnect() {
    if (m_hConnection != k_HSteamNetConnection_Invalid) {
        m_pInterface->CloseConnection(m_hConnection, 0, "User Disconnect", true);
        m_hConnection = k_HSteamNetConnection_Invalid;
    }
    m_IsConnected = false;
}

void NetworkManager::Update() {
    if (!m_pInterface) return;

    // 1. �B�z����^�I (�s�u�B�_�u�ƥ�)
    m_pInterface->RunCallbacks();

    // 2. �����T�� (Polling)
    // �����Ҧ����D�s�u
    std::vector<HSteamNetConnection> connectionsToCheck;
    if (m_IsServer) {
        connectionsToCheck = m_ClientConnections;
    }
    else if (m_hConnection != k_HSteamNetConnection_Invalid) {
        connectionsToCheck.push_back(m_hConnection);
    }

    for (auto conn : connectionsToCheck) {
        ISteamNetworkingMessage* incomingMsgs[32] = {};
        int numMsgs = 0;

        do {
            numMsgs = m_pInterface->ReceiveMessagesOnConnection(conn, incomingMsgs, 32);

            for (int i = 0; i < numMsgs; ++i) {
                ISteamNetworkingMessage* pIncomingMsg = incomingMsgs[i];

                // �B�z�o�h�T��
                if (pIncomingMsg->GetSize() >= sizeof(PacketHeader)) {
                    ReceivedPacket pkt;
                    PacketHeader* header = (PacketHeader*)pIncomingMsg->GetData();
                    pkt.type = header->type;
                    pkt.fromConnection = conn;

                    pkt.data.resize(pIncomingMsg->GetSize());
                    memcpy(pkt.data.data(), pIncomingMsg->GetData(), pIncomingMsg->GetSize());

                    m_PacketQueue.push(pkt);
                }

                // ���� GNS ���T���O����
                pIncomingMsg->Release();
            }
        } while (numMsgs > 0);
    }
}

// �B�z�s�u���A����
void NetworkManager::OnConnectionStatusChangedHelper(SteamNetConnectionStatusChangedCallback_t* pInfo) {
    switch (pInfo->m_info.m_eState) {
    case k_ESteamNetworkingConnectionState_None:
        // �P����
        break;

    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        // �_�u
        if (m_IsServer) {
            // �q client list ����
            auto it = std::remove(m_ClientConnections.begin(), m_ClientConnections.end(), pInfo->m_hConn);
            m_ClientConnections.erase(it, m_ClientConnections.end());

            auto idIt = connectionPlayerIDs.find(pInfo->m_hConn);
            if (idIt != connectionPlayerIDs.end()) {
                int playerID = idIt->second;
                connectionPlayerIDs.erase(idIt);
                connectedPlayerIDs.erase(std::remove(connectedPlayerIDs.begin(), connectedPlayerIDs.end(), playerID), connectedPlayerIDs.end());
                playerWeaponMap.erase(playerID);
            }
        }
        else {
            m_IsConnected = false;
            m_hConnection = k_HSteamNetConnection_Invalid;
        }
        std::cout << "Connection closed: " << pInfo->m_info.m_szEndDebug << std::endl;

        m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
        break;

    case k_ESteamNetworkingConnectionState_Connecting:
        if (m_IsServer) {
            std::cout << "Incoming connection request..." << std::endl;
            if (m_pInterface->AcceptConnection(pInfo->m_hConn) != k_EResultOK) {
                std::cerr << "Failed to accept connection!" << std::endl;
                m_pInterface->CloseConnection(pInfo->m_hConn, 0, "Accept Failed", false);
                return;
            }

            std::cout << "Accepted connection " << pInfo->m_hConn << std::endl;
        }
        break;

    case k_ESteamNetworkingConnectionState_Connected:
        // �s�u���\�I
        if (m_IsServer) {
            std::cout << "Client connected! Handle: " << pInfo->m_hConn << std::endl;
            int newID = m_NextClientID++;
            m_ClientConnections.push_back(pInfo->m_hConn);
            connectedPlayerIDs.push_back(newID);
            connectionPlayerIDs[pInfo->m_hConn] = newID;

            PacketJoinAccept pkt;
            pkt.header.type = PacketType::S2C_JOIN_ACCEPT;
            pkt.yourPlayerID = newID;   // Server=0, Clients=1,2,3...

            // ���� ID = Team 1 (��), �_�� ID = Team 2 (��)
            pkt.yourTeamID = (pkt.yourPlayerID % 2 == 0) ? 1 : 2;

            m_pInterface->SendMessageToConnection(pInfo->m_hConn, &pkt, sizeof(pkt), k_nSteamNetworkingSend_Reliable, nullptr);
            std::cout << ">> Sent Welcome Packet to ID: " << pkt.yourPlayerID << std::endl;
        }
        else {
            std::cout << "Connected to server!" << std::endl;
            m_IsConnected = true;
        }
        break;
    }
}

void NetworkManager::Send(HSteamNetConnection conn, const void* data, size_t size, bool reliable) {
    if (!m_pInterface) return;

    int flags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
    m_pInterface->SendMessageToConnection(conn, data, (uint32_t)size, flags, nullptr);
}

void NetworkManager::Broadcast(const void* data, size_t size, bool reliable, HSteamNetConnection except) {
    if (!m_IsServer) return;

    int flags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;

    for (auto conn : m_ClientConnections) {
        if (conn != except) {
            m_pInterface->SendMessageToConnection(conn, data, (uint32_t)size, flags, nullptr);
        }
    }
}

bool NetworkManager::HasPackets() { return !m_PacketQueue.empty(); }

ReceivedPacket NetworkManager::PopPacket() {
    if (m_PacketQueue.empty()) return {};
    ReceivedPacket pkt = m_PacketQueue.front();
    m_PacketQueue.pop();
    return pkt;
}
