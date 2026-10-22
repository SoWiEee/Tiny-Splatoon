#pragma once

#include "GnsTransport.hpp"
#include "RoomTypes.hpp"

#include <memory>

namespace restoration::network {

class RoomServer {
public:
    RoomServer(GnsRuntime& runtime, const SteamNetworkingIPAddr& address);
    ~RoomServer();
    RoomServer(const RoomServer&) = delete;
    RoomServer& operator=(const RoomServer&) = delete;

    std::uint16_t port() const;
    bool idle() const;
    void Update(double now);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace restoration::network
