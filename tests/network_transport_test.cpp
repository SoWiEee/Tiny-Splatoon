#include "../src/network/GnsTransport.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
using restoration::network::GnsEndpoint;
using restoration::network::GnsEvent;
using restoration::network::GnsRuntime;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

SteamNetworkingIPAddr loopback(std::uint16_t port) {
    SteamNetworkingIPAddr address{};
    address.SetIPv4(0x7f000001u, port);
    return address;
}

std::unique_ptr<GnsEndpoint> listenAvailablePort(GnsRuntime& runtime) {
    const auto seed = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        try {
            return runtime.Listen(loopback(static_cast<std::uint16_t>(30000 + (seed + attempt) % 10000)));
        } catch (const std::runtime_error&) {
            if (attempt == 31) throw;
        }
    }
    throw std::runtime_error("no local test port available");
}

template<class Check>
void waitFor(GnsRuntime& runtime, Check check, std::chrono::milliseconds timeout = std::chrono::seconds(6)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.Poll();
        if (check()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("timed out while waiting for local GameNetworkingSockets events");
}

void discardEvents(GnsEndpoint& endpoint) { (void)endpoint.TakeEvents(); }
}

int main(int argc, char** argv) {
    try {
        const std::size_t clientCount = argc > 1 ? static_cast<std::size_t>(std::stoul(argv[1])) : 8;
        require(clientCount == 2 || clientCount == 3 || clientCount == 8,
            "test client count must be 2, 3, or 8");
        GnsRuntime runtime;
        runtime.Init();
        runtime.Init();
        bool zeroPortRejected = false;
        try { (void)runtime.Listen(loopback(0)); }
        catch (const std::invalid_argument&) { zeroPortRejected = true; }
        require(zeroPortRejected, "GNS requires a specific nonzero listen port");
        auto server = listenAvailablePort(runtime);
        require(server->port() != 0, "listener must report its actual bound port");
        const auto serverAddress = loopback(server->port());

        std::vector<std::unique_ptr<GnsEndpoint>> clients;
        clients.reserve(clientCount);
        for (std::size_t i = 0; i < clientCount; ++i) clients.push_back(runtime.Connect(serverAddress));

        std::vector<bool> connected(clientCount), sent(clientCount), replied(clientCount);
        std::unordered_map<HSteamNetConnection, std::size_t> serverPeers;
        std::unordered_set<HSteamNetConnection> distinctServerConnections;
        waitFor(runtime, [&] {
            for (std::size_t i = 0; i < clients.size(); ++i) {
                for (auto& event : clients[i]->TakeEvents()) {
                    if (event.type == GnsEvent::Type::Connected) {
                        connected[i] = true;
                        require(event.connection == clients[i]->connection(), "client connection handle must match its connected event");
                        if (!sent[i]) {
                            const auto token = static_cast<std::uint8_t>(i);
                            require(clients[i]->Send(event.connection, &token, sizeof(token)), "client handshake send failed");
                            sent[i] = true;
                        }
                    } else if (event.type == GnsEvent::Type::Message) {
                        require(event.data.size() == 2, "client reply must include a server marker and its client token");
                        require(event.data[0] == static_cast<std::uint8_t>(0x80 + i) &&
                                event.data[1] == static_cast<std::uint8_t>(i),
                            "server replies must return to the correct client connection");
                        replied[i] = true;
                    } else {
                        throw std::runtime_error("unexpected client error or disconnect during client exchange");
                    }
                }
            }
            for (auto& event : server->TakeEvents()) {
                if (event.type == GnsEvent::Type::Connected) {
                    distinctServerConnections.insert(event.connection);
                } else if (event.type == GnsEvent::Type::Message) {
                    require(event.data.size() == 1 && event.data[0] < clientCount,
                        "server must receive each client's one-byte identity token");
                    const auto client = static_cast<std::size_t>(event.data[0]);
                    const auto inserted = serverPeers.emplace(event.connection, client);
                    require(inserted.second || inserted.first->second == client,
                        "a server connection must keep the same client identity");
                    const std::uint8_t response[2] = {static_cast<std::uint8_t>(0x80 + client), static_cast<std::uint8_t>(client)};
                    require(server->Send(event.connection, response, sizeof(response)), "server reply send failed");
                } else {
                    throw std::runtime_error("unexpected server error or disconnect during client exchange");
                }
            }
            for (const bool value : replied) if (!value) return false;
            return true;
        });
        require(std::all_of(connected.begin(), connected.end(), [](bool value) { return value; }),
            "all clients must connect");
        require(serverPeers.size() == clientCount && distinctServerConnections.size() == clientCount,
            "server must retain distinct client connection identities");

        if (clientCount == 8) {
            auto ninth = runtime.Connect(serverAddress);
            bool refusedAtServer = false, refusedAtClient = false;
            waitFor(runtime, [&] {
                for (auto& event : server->TakeEvents())
                    refusedAtServer |= event.type == GnsEvent::Type::Error &&
                        event.detail.find("connection limit of 8") != std::string::npos;
                for (auto& event : ninth->TakeEvents())
                    refusedAtClient |= event.type == GnsEvent::Type::Disconnected || event.type == GnsEvent::Type::Error;
                return refusedAtServer && refusedAtClient;
            });
        }

        // The server queues at most 240 message events and closes a peer whose traffic cannot fit.
        std::uint8_t floodByte = 0x33;
        for (int i = 0; i < 300; ++i)
            require(clients[1]->Send(clients[1]->connection(), &floodByte, sizeof(floodByte)),
                "small event-queue saturation messages must stay within the sender budget");
        bool queueLimitClosedClient = false;
        const auto floodDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        while (std::chrono::steady_clock::now() < floodDeadline && !queueLimitClosedClient) {
            runtime.Poll();
            for (auto& event : clients[1]->TakeEvents())
                queueLimitClosedClient |= event.type == GnsEvent::Type::Disconnected || event.type == GnsEvent::Type::Error;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        bool queueLimitVisible = false;
        for (auto& event : server->TakeEvents())
            queueLimitVisible |= event.type == GnsEvent::Type::Error &&
                event.detail.find("incoming event queue limit") != std::string::npos;
        require(queueLimitClosedClient && queueLimitVisible,
            "event queue overflow must close the source connection and report an error");

        std::vector<std::uint8_t> boundary(GnsEndpoint::MaxMessageBytes, 0x5a);
        require(clients[0]->Send(clients[0]->connection(), boundary.data(), boundary.size()),
            "64 KiB boundary message must be accepted");
        bool boundaryReceived = false, boundaryAck = false;
        waitFor(runtime, [&] {
            for (auto& event : server->TakeEvents()) {
                if (event.type == GnsEvent::Type::Message) {
                    require(event.connection == std::find_if(serverPeers.begin(), serverPeers.end(), [](const auto& peer) {
                        return peer.second == 0;
                    })->first, "64 KiB message must arrive on the original client connection");
                    require(event.data.size() == GnsEndpoint::MaxMessageBytes &&
                            std::all_of(event.data.begin(), event.data.end(), [](std::uint8_t byte) { return byte == 0x5a; }),
                        "64 KiB payload must arrive without truncation");
                    boundaryReceived = true;
                    const std::uint8_t ack = 0xb0;
                    require(server->Send(event.connection, &ack, 1), "boundary acknowledgement send failed");
                } else if (event.type == GnsEvent::Type::Error || event.type == GnsEvent::Type::Disconnected) {
                    throw std::runtime_error("boundary message unexpectedly closed its connection");
                }
            }
            for (auto& event : clients[0]->TakeEvents()) {
                if (event.type == GnsEvent::Type::Message) {
                    require(event.data == std::vector<std::uint8_t>{0xb0}, "boundary acknowledgement mismatch");
                    boundaryAck = true;
                }
            }
            return boundaryReceived && boundaryAck;
        });

        std::vector<std::uint8_t> tooLarge(GnsEndpoint::MaxMessageBytes + 1, 0x2a);
        require(!clients[0]->Send(clients[0]->connection(), tooLarge.data(), tooLarge.size()),
            "outgoing messages over 64 KiB must be rejected");
        bool outgoingRejected = false;
        for (auto& event : clients[0]->TakeEvents())
            outgoingRejected |= event.type == GnsEvent::Type::Error && event.detail.find("64 KiB") != std::string::npos;
        require(outgoingRejected, "outgoing size rejection must be visible as an error event");

        const auto serverConnection = std::find_if(serverPeers.begin(), serverPeers.end(), [](const auto& peer) {
            return peer.second == 0;
        })->first;
        require(SteamNetworkingSockets()->SendMessageToConnection(clients[0]->connection(), tooLarge.data(),
                static_cast<uint32>(tooLarge.size()), k_nSteamNetworkingSend_ReliableNoNagle, nullptr) == k_EResultOK,
            "raw test peer must be able to send an oversized frame past the endpoint send guard");
        bool inboundRejected = false, clientDisconnected = false;
        waitFor(runtime, [&] {
            for (auto& event : server->TakeEvents())
                if (event.connection == serverConnection && event.type == GnsEvent::Type::Error &&
                    event.detail.find("incoming message") != std::string::npos) inboundRejected = true;
            for (auto& event : clients[0]->TakeEvents())
                if (event.type == GnsEvent::Type::Disconnected || event.type == GnsEvent::Type::Error)
                    clientDisconnected = true;
            return inboundRejected && clientDisconnected;
        });

        // Destroy with a connection callback still pending, then let GNS deliver and retire it.
        auto pending = runtime.Connect(serverAddress);
        pending.reset();
        const auto cleanupDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        while (std::chrono::steady_clock::now() < cleanupDeadline) {
            runtime.Poll();
            discardEvents(*server);
            for (std::size_t i = 1; i < clients.size(); ++i) discardEvents(*clients[i]);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        auto replacement = runtime.Connect(serverAddress);
        bool replacementConnected = false, replacementReplied = false, replacementSent = false;
        HSteamNetConnection replacementServerConnection = k_HSteamNetConnection_Invalid;
        waitFor(runtime, [&] {
            for (auto& event : replacement->TakeEvents()) {
                if (event.type == GnsEvent::Type::Connected) {
                    replacementConnected = true;
                    const std::uint8_t token = 42;
                    require(replacement->Send(event.connection, &token, 1), "replacement handshake send failed");
                    replacementSent = true;
                } else if (event.type == GnsEvent::Type::Message) {
                    require(event.data == std::vector<std::uint8_t>{0xda}, "replacement reply must come from its matching server peer");
                    replacementReplied = true;
                } else {
                    throw std::runtime_error("replacement client failed to connect");
                }
            }
            for (auto& event : server->TakeEvents()) {
                if (event.type == GnsEvent::Type::Message && event.data == std::vector<std::uint8_t>{42}) {
                    replacementServerConnection = event.connection;
                    const std::uint8_t response = 0xda;
                    require(server->Send(event.connection, &response, 1), "replacement response send failed");
                }
            }
            return replacementConnected && replacementSent && replacementReplied;
        });

        const auto replacementConnection = replacement->connection();
        require(replacementServerConnection != k_HSteamNetConnection_Invalid,
            "replacement handshake must be associated with a server-side connection handle");
        require(replacement->Disconnect(replacementConnection), "local disconnect must close a live connection");
        bool remoteDisconnectDelivered = false;
        waitFor(runtime, [&] {
            for (auto& event : server->TakeEvents())
                remoteDisconnectDelivered |= event.connection == replacementServerConnection &&
                    event.type == GnsEvent::Type::Disconnected;
            discardEvents(*replacement);
            return remoteDisconnectDelivered;
        });

        runtime.Shutdown();
        runtime.Shutdown();
        runtime.Init();
        runtime.Init();
        auto secondServer = listenAvailablePort(runtime);
        auto secondClient = runtime.Connect(loopback(secondServer->port()));
        bool secondLifecycleConnected = false;
        waitFor(runtime, [&] {
            for (auto& event : secondClient->TakeEvents())
                if (event.type == GnsEvent::Type::Connected) secondLifecycleConnected = true;
            return secondLifecycleConnected;
        });
        runtime.Shutdown();
        runtime.Shutdown();
        std::cout << "GameNetworkingSockets transport lifecycle and loopback tests passed for "
                  << clientCount << " clients\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
