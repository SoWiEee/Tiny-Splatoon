#pragma once

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingtypes.h>
#include <vector>
#include <queue>
#include <string>
#include <map>
#include "NetworkProtocol.hpp"

struct ReceivedPacket {
    PacketType type;
    std::vector<uint8_t> data;
    int fromID;
    HSteamNetConnection fromConnection; // connection handle
};

template <typename T>
const T* TryGetPacket(const ReceivedPacket& packet) {
    if (packet.data.size() < sizeof(T)) {
        return nullptr;
    }
    return reinterpret_cast<const T*>(packet.data.data());
}

class NetworkManager {
public:
    static NetworkManager& Instance();
    // Utility: parse "host:port" or "[ipv6]:port"
    static bool ParseHostPort(const std::string& input, std::string& outHost, int& outPort, int defaultPort = 7777);
    std::vector<int> connectedPlayerIDs;
    std::map<int, WeaponType> playerWeaponMap;
    std::map<HSteamNetConnection, int> connectionPlayerIDs;

    // GNS init
    bool Initialize();
    void Shutdown();

	// connect/disconnect
    bool StartServer(int port = 7777);
    bool Connect(const std::string& hostOrIp, int port);
    void Disconnect();

    // --- �D�j�� ---
    void Update();

    void Send(HSteamNetConnection conn, const void* data, size_t size, bool reliable = false);

    void SendToServer(const void* data, size_t size, bool reliable = false) {
        if (!m_IsServer && m_hConnection != k_HSteamNetConnection_Invalid) {
            Send(m_hConnection, data, size, reliable);
        }
    }

    // Server �s����
    void Broadcast(const void* data, size_t size, bool reliable = false, HSteamNetConnection except = k_HSteamNetConnection_Invalid);

    // --- ���� ---
    bool HasPackets();
    ReceivedPacket PopPacket();

    // --- ���A ---
    bool IsServer() const { return m_IsServer; }
    bool IsConnected() const { return m_IsConnected; }
    int GetMyPlayerID() const { return m_MyID; }
    void SetMyPlayerID(int id) { m_MyID = id; }
    int GetMyTeamID() const { return m_MyTeamID; }
    void SetMyTeamID(int team) { m_MyTeamID = team; }
    WeaponType GetMyWeaponType() const { return m_MyWeaponType; }
    void SetMyWeaponType(WeaponType type) { m_MyWeaponType = type; }

private:
    NetworkManager() {}
    ~NetworkManager() { Shutdown(); }

    // GNS ��������
    ISteamNetworkingSockets* m_pInterface = nullptr;

    // Server �Ϊ���ť Socket
    HSteamListenSocket m_hListenSock = k_HSteamListenSocket_Invalid;

    // Server �ݪ��s�u�C�� (Client ID -> Connection Handle)
    // �o�̬��F²��A�ڭ̥��u�s Connection Handle
    std::vector<HSteamNetConnection> m_ClientConnections;

    // Client �Ϊ��s�u Handle (�s�� Server �������u)
    HSteamNetConnection m_hConnection = k_HSteamNetConnection_Invalid;

    // ���A�P��C
    WeaponType m_MyWeaponType = WeaponType::SHOOTER;
    bool m_IsServer = false;
    bool m_IsConnected = false;
    int m_MyID = -1; // -1 �N���|�����t
    int m_MyTeamID = 1;
    int m_NextClientID = 1;
    std::queue<ReceivedPacket> m_PacketQueue;

    bool m_WsaInited = false;

    // Resolve IPv4/IPv6 or hostname (DNS) to a SteamNetworkingIPAddr
    bool ResolveHostToAddr(const std::string& hostOrIp, int port, SteamNetworkingIPAddr& outAddr);

    // --- GNS �^�I�禡 (�B�z�s�u���A����) ---
    // �����O static �~��ǵ� GNS
    static void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo);

    // �B�z�s�u���A�������޿�
    void OnConnectionStatusChangedHelper(SteamNetConnectionStatusChangedCallback_t* pInfo);
};
