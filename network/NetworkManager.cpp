#include "NetworkManager.h"
#include <iostream>
#include <cassert>

// 實作 Singleton
NetworkManager& NetworkManager::Instance() {
    static NetworkManager instance;
    return instance;
}

// 靜態 Callback 轉發給實體
void NetworkManager::OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo) {
    NetworkManager::Instance().OnConnectionStatusChangedHelper(pInfo);
}

bool NetworkManager::Initialize() {
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
}

bool NetworkManager::StartServer(int port) {
    m_IsServer = true;
    m_ClientConnections.clear();

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

bool NetworkManager::Connect(const std::string& ip, int port) {
    m_IsServer = false;

    SteamNetworkingIPAddr serverAddr;
    serverAddr.Clear();
    serverAddr.ParseString(ip.c_str());
    serverAddr.m_port = (uint16_t)port;

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

    // 1. 處理全域回呼 (連線、斷線事件)
    m_pInterface->RunCallbacks();

    // 2. 接收訊息 (Polling)
    // 收集所有活躍連線
    std::vector<HSteamNetConnection> connectionsToCheck;
    if (m_IsServer) {
        connectionsToCheck = m_ClientConnections;
    }
    else if (m_hConnection != k_HSteamNetConnection_Invalid) {
        connectionsToCheck.push_back(m_hConnection);
    }

    for (auto conn : connectionsToCheck) {
        ISteamNetworkingMessage* pIncomingMsg = nullptr;
        int numMsgs = m_pInterface->ReceiveMessagesOnConnection(conn, &pIncomingMsg, 1);

        if (numMsgs > 0) {
            // 處理這則訊息
            if (pIncomingMsg->GetSize() >= sizeof(PacketHeader)) {
                ReceivedPacket pkt;
                PacketHeader* header = (PacketHeader*)pIncomingMsg->GetData();
                pkt.type = header->type;
                pkt.fromConnection = conn;

                pkt.data.resize(pIncomingMsg->GetSize());
                memcpy(pkt.data.data(), pIncomingMsg->GetData(), pIncomingMsg->GetSize());

                m_PacketQueue.push(pkt);
            }
            // 釋放 GNS 的訊息記憶體
            pIncomingMsg->Release();
        }
    }
}

// 處理連線狀態改變
void NetworkManager::OnConnectionStatusChangedHelper(SteamNetConnectionStatusChangedCallback_t* pInfo) {
    switch (pInfo->m_info.m_eState) {
    case k_ESteamNetworkingConnectionState_None:
        // 銷毀中
        break;

    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        // 斷線
        if (m_IsServer) {
            // 從 client list 移除
            auto it = std::remove(m_ClientConnections.begin(), m_ClientConnections.end(), pInfo->m_hConn);
            m_ClientConnections.erase(it, m_ClientConnections.end());
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
                std::cout << "Accepted connection " << pInfo->m_hConn << std::endl;

                // S2C_JOIN_ACCEPT
                PacketJoinAccept pkt;
                pkt.header.type = PacketType::S2C_JOIN_ACCEPT;
                // Connection Handle as Player ID
                pkt.yourPlayerID = (int)pInfo->m_hConn;
                pkt.yourTeamID = (pkt.yourPlayerID % 2) + 1; // team 1/2

                Send(pInfo->m_hConn, &pkt, sizeof(pkt), true);
            }
        }
        break;

    case k_ESteamNetworkingConnectionState_Connected:
        // 連線成功！
        if (m_IsServer) {
            std::cout << "Client connected! Handle: " << pInfo->m_hConn << std::endl;
            int newID = m_NextClientID++;
            m_ClientConnections.push_back(pInfo->m_hConn);
            connectedPlayerIDs.push_back(newID);

            PacketJoinAccept pkt;
            pkt.header.type = PacketType::S2C_JOIN_ACCEPT;
            pkt.yourPlayerID = newID;   // Server=0, Clients=1,2,3...

            // 偶數 ID = Team 1 (紅), 奇數 ID = Team 2 (綠)
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