#pragma once

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingtypes.h>
#include <vector>
#include <queue>
#include <string>
#include <map>
#include "NetworkProtocol.h"

struct ReceivedPacket {
    PacketType type;
    std::vector<uint8_t> data;
    int fromID;
    HSteamNetConnection fromConnection; // connection handle
};

class NetworkManager {
public:
    static NetworkManager& Instance();
    std::vector<int> connectedPlayerIDs;

    // GNS init
    bool Initialize();
    void Shutdown();

	// connect/disconnect
    bool StartServer(int port = 7777);
    bool Connect(const std::string& ip, int port = 7777);
    void Disconnect();

    // --- 主迴圈 ---
    void Update();

    void Send(HSteamNetConnection conn, const void* data, size_t size, bool reliable = false);

    void SendToServer(const void* data, size_t size, bool reliable = false) {
        if (!m_IsServer && m_hConnection != k_HSteamNetConnection_Invalid) {
            Send(m_hConnection, data, size, reliable);
        }
    }

    // Server 廣播用
    void Broadcast(const void* data, size_t size, bool reliable = false, HSteamNetConnection except = k_HSteamNetConnection_Invalid);

    // --- 接收 ---
    bool HasPackets();
    ReceivedPacket PopPacket();

    // --- 狀態 ---
    bool IsServer() const { return m_IsServer; }
    bool IsConnected() const { return m_IsConnected; }
    int GetMyPlayerID() const { return m_MyID; }
    void SetMyPlayerID(int id) { m_MyID = id; }
    int GetMyTeamID() const { return m_MyTeamID; }
    void SetMyTeamID(int team) { m_MyTeamID = team; }

private:
    NetworkManager() {}
    ~NetworkManager() { Shutdown(); }

    // GNS 介面指標
    ISteamNetworkingSockets* m_pInterface = nullptr;

    // Server 用的監聽 Socket
    HSteamListenSocket m_hListenSock = k_HSteamListenSocket_Invalid;

    // Server 端的連線列表 (Client ID -> Connection Handle)
    // 這裡為了簡單，我們先只存 Connection Handle
    std::vector<HSteamNetConnection> m_ClientConnections;

    // Client 用的連線 Handle (連到 Server 的那條線)
    HSteamNetConnection m_hConnection = k_HSteamNetConnection_Invalid;

    // 狀態與佇列
    bool m_IsServer = false;
    bool m_IsConnected = false;
    int m_MyID = -1; // -1 代表尚未分配
    int m_MyTeamID = 1;
    int m_NextClientID = 1;
    std::queue<ReceivedPacket> m_PacketQueue;

    // --- GNS 回呼函式 (處理連線狀態改變) ---
    // 必須是 static 才能傳給 GNS
    static void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo);

    // 處理連線狀態的內部邏輯
    void OnConnectionStatusChangedHelper(SteamNetConnectionStatusChangedCallback_t* pInfo);
};