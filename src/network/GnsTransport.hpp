#pragma once

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace restoration::network {

struct GnsEvent {
    enum class Type { Connected, Message, Disconnected, Error };

    Type type = Type::Error;
    HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
    std::vector<std::uint8_t> data;
    std::string detail;
};

class GnsRuntime;

class GnsEndpoint {
public:
    static constexpr std::size_t MaxMessageBytes = 64 * 1024;

    ~GnsEndpoint();
    GnsEndpoint(const GnsEndpoint&) = delete;
    GnsEndpoint& operator=(const GnsEndpoint&) = delete;

    std::uint16_t port() const;
    HSteamNetConnection connection() const;
    bool Send(HSteamNetConnection connection, const void* data, std::size_t size, bool reliable = true);
    bool Disconnect(HSteamNetConnection connection);
    std::vector<GnsEvent> TakeEvents();

private:
    friend class GnsRuntime;
    struct Impl;
    explicit GnsEndpoint(GnsRuntime& runtime);
    void Poll();
    void QueueControl(GnsEvent event);
    bool QueueMessage(GnsEvent event);
    void RemoveQueued(HSteamNetConnection connection);
    void Reject(HSteamNetConnection connection, std::string detail);
    void HandleIncoming(HSteamNetConnection connection);
    void HandleClosed(HSteamNetConnection connection, GnsEvent::Type type, std::string detail);
    void Detach();
    std::unique_ptr<Impl> impl_;
};

// One runtime may be active per process. It must be polled and shut down on its
// construction thread; endpoints detach safely when the runtime shuts down.
class GnsRuntime {
public:
    GnsRuntime();
    ~GnsRuntime();
    GnsRuntime(const GnsRuntime&) = delete;
    GnsRuntime& operator=(const GnsRuntime&) = delete;

    void Init();
    void Shutdown();
    std::unique_ptr<GnsEndpoint> Listen(const SteamNetworkingIPAddr& address, std::size_t maxConnections = 8);
    std::unique_ptr<GnsEndpoint> Connect(const SteamNetworkingIPAddr& address);
    void Poll();

private:
    friend class GnsEndpoint;
    struct Impl;
    static void StatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    void DispatchStatus(SteamNetConnectionStatusChangedCallback_t* info);
    void CheckThread() const;
    void Unregister(GnsEndpoint* endpoint);
    std::unique_ptr<Impl> impl_;
};

} // namespace restoration::network
