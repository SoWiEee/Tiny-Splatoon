#include "GnsTransport.hpp"

#include <steam/isteamnetworkingutils.h>

#include <algorithm>
#include <array>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace restoration::network {
namespace {
constexpr std::size_t MaxQueuedEvents = 256;
constexpr std::size_t MaxQueuedBytes = 1024 * 1024;
constexpr std::size_t ReservedControlBytes = 64 * 1024;
constexpr std::size_t MaxPendingBytes = 256 * 1024;
constexpr std::size_t MaxConnections = 8;
constexpr int MaxMessagesPerPoll = 64;
constexpr int MessageBatchSize = 16;

std::mutex runtimeMutex;
GnsRuntime* activeRuntime = nullptr;

std::string EndDetail(const SteamNetConnectionInfo_t& info) {
    if (info.m_szEndDebug[0]) return info.m_szEndDebug;
    return "GameNetworkingSockets connection ended (reason " + std::to_string(info.m_eEndReason) + ")";
}
}

struct GnsEndpoint::Impl {
    GnsRuntime* runtime = nullptr;
    HSteamListenSocket listenSocket = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup pollGroup = k_HSteamNetPollGroup_Invalid;
    HSteamNetConnection clientConnection = k_HSteamNetConnection_Invalid;
    std::uint16_t port = 0;
    bool listening = false;
    std::unordered_set<HSteamNetConnection> connections;
    std::deque<GnsEvent> events;
    std::size_t queuedBytes = 0;
};

struct GnsRuntime::Impl {
    ISteamNetworkingSockets* sockets = nullptr;
    ISteamNetworkingUtils* utils = nullptr;
    std::thread::id ownerThread;
    std::unordered_set<GnsEndpoint*> endpoints;
    std::unordered_map<HSteamNetConnection, GnsEndpoint*> connections;
    std::unordered_map<HSteamListenSocket, GnsEndpoint*> listeners;
    bool initialized = false;
};

GnsEndpoint::GnsEndpoint(GnsRuntime& runtime) : impl_(std::make_unique<Impl>()) {
    impl_->runtime = &runtime;
}

GnsEndpoint::~GnsEndpoint() {
    if (impl_ && impl_->runtime && impl_->runtime->impl_->initialized &&
        impl_->runtime->impl_->ownerThread != std::this_thread::get_id()) std::terminate();
    Detach();
}

std::uint16_t GnsEndpoint::port() const { return impl_->port; }

HSteamNetConnection GnsEndpoint::connection() const { return impl_->clientConnection; }

void GnsEndpoint::QueueControl(GnsEvent event) {
    const auto bytes = event.data.size() + event.detail.size();
    if (bytes > MaxQueuedBytes) return;
    while (impl_->events.size() >= MaxQueuedEvents || impl_->queuedBytes > MaxQueuedBytes ||
           bytes > MaxQueuedBytes - impl_->queuedBytes) {
        auto discard = std::find_if(impl_->events.begin(), impl_->events.end(), [](const GnsEvent& queued) {
            return queued.type == GnsEvent::Type::Disconnected || queued.type == GnsEvent::Type::Error;
        });
        if (discard == impl_->events.end())
            discard = std::find_if(impl_->events.begin(), impl_->events.end(), [](const GnsEvent& queued) {
                return queued.type == GnsEvent::Type::Connected;
            });
        if (discard == impl_->events.end()) return;
        impl_->queuedBytes -= discard->data.size() + discard->detail.size();
        impl_->events.erase(discard);
    }
    impl_->queuedBytes += bytes;
    impl_->events.push_back(std::move(event));
}

bool GnsEndpoint::QueueMessage(GnsEvent event) {
    const auto bytes = event.data.size() + event.detail.size();
    constexpr auto messageByteBudget = MaxQueuedBytes - ReservedControlBytes;
    if (impl_->events.size() >= MaxQueuedEvents - 2 * MaxConnections ||
        impl_->queuedBytes > messageByteBudget || bytes > messageByteBudget - impl_->queuedBytes) return false;
    impl_->queuedBytes += bytes;
    impl_->events.push_back(std::move(event));
    return true;
}

void GnsEndpoint::RemoveQueued(HSteamNetConnection connection) {
    for (auto event = impl_->events.begin(); event != impl_->events.end();) {
        if (event->connection == connection &&
            (event->type == GnsEvent::Type::Connected || event->type == GnsEvent::Type::Message)) {
            impl_->queuedBytes -= event->data.size() + event->detail.size();
            event = impl_->events.erase(event);
        } else {
            ++event;
        }
    }
}

void GnsEndpoint::Reject(HSteamNetConnection connection, std::string detail) {
    auto* runtime = impl_->runtime;
    if (runtime && impl_->connections.erase(connection)) {
        runtime->impl_->connections.erase(connection);
        RemoveQueued(connection);
        if (impl_->clientConnection == connection) impl_->clientConnection = k_HSteamNetConnection_Invalid;
        runtime->impl_->sockets->CloseConnection(connection, 0, detail.c_str(), false);
    }
    QueueControl({GnsEvent::Type::Error, connection, {}, std::move(detail)});
}

bool GnsEndpoint::Send(HSteamNetConnection connection, const void* data, std::size_t size) {
    auto* runtime = impl_->runtime;
    if (!runtime) return false;
    runtime->CheckThread();
    if (!impl_->connections.count(connection)) {
        QueueControl({GnsEvent::Type::Error, connection, {}, "connection does not belong to this endpoint"});
        return false;
    }
    if (size > MaxMessageBytes) {
        QueueControl({GnsEvent::Type::Error, connection, {}, "outgoing message exceeds 64 KiB"});
        return false;
    }
    if (size && !data) {
        QueueControl({GnsEvent::Type::Error, connection, {}, "outgoing message data is null"});
        return false;
    }

    SteamNetConnectionRealTimeStatus_t status{};
    if (runtime->impl_->sockets->GetConnectionRealTimeStatus(connection, &status, 0, nullptr) != k_EResultOK) {
        QueueControl({GnsEvent::Type::Error, connection, {}, "could not read pending connection status"});
        return false;
    }
    const auto pending = static_cast<std::size_t>(std::max(0, status.m_cbPendingReliable)) +
        static_cast<std::size_t>(std::max(0, status.m_cbPendingUnreliable)) +
        static_cast<std::size_t>(std::max(0, status.m_cbSentUnackedReliable));
    if (pending > MaxPendingBytes || size > MaxPendingBytes - pending) {
        QueueControl({GnsEvent::Type::Error, connection, {}, "outgoing pending byte limit exceeded"});
        return false;
    }

    const auto result = runtime->impl_->sockets->SendMessageToConnection(
        connection, data, static_cast<uint32>(size), k_nSteamNetworkingSend_ReliableNoNagle, nullptr);
    if (result != k_EResultOK) {
        QueueControl({GnsEvent::Type::Error, connection, {}, "GameNetworkingSockets rejected the send"});
        return false;
    }
    return true;
}

bool GnsEndpoint::Disconnect(HSteamNetConnection connection) {
    auto* runtime = impl_->runtime;
    if (!runtime) return false;
    runtime->CheckThread();
    if (!impl_->connections.erase(connection)) return false;
    runtime->impl_->connections.erase(connection);
    RemoveQueued(connection);
    if (impl_->clientConnection == connection) impl_->clientConnection = k_HSteamNetConnection_Invalid;
    runtime->impl_->sockets->CloseConnection(connection, 0, "application disconnected", false);
    QueueControl({GnsEvent::Type::Disconnected, connection, {}, "application disconnected"});
    return true;
}

std::vector<GnsEvent> GnsEndpoint::TakeEvents() {
    if (impl_->runtime) impl_->runtime->CheckThread();
    std::vector<GnsEvent> events;
    events.reserve(impl_->events.size());
    while (!impl_->events.empty()) {
        events.push_back(std::move(impl_->events.front()));
        impl_->events.pop_front();
    }
    impl_->queuedBytes = 0;
    return events;
}

void GnsEndpoint::HandleIncoming(HSteamNetConnection connection) {
    auto* runtime = impl_->runtime;
    if (!runtime || !impl_->listening) {
        if (runtime) runtime->impl_->sockets->CloseConnection(connection, 0, "endpoint is not listening", false);
        return;
    }
    if (impl_->connections.size() >= MaxConnections) {
        QueueControl({GnsEvent::Type::Error, connection, {}, "server connection limit of 8 reached"});
        runtime->impl_->sockets->CloseConnection(connection, 0, "server connection limit reached", false);
        return;
    }
    if (runtime->impl_->sockets->AcceptConnection(connection) != k_EResultOK) {
        QueueControl({GnsEvent::Type::Error, connection, {}, "could not accept incoming connection"});
        runtime->impl_->sockets->CloseConnection(connection, 0, "connection accept failed", false);
        return;
    }
    if (!runtime->impl_->sockets->SetConnectionPollGroup(connection, impl_->pollGroup)) {
        QueueControl({GnsEvent::Type::Error, connection, {}, "could not assign connection to poll group"});
        runtime->impl_->sockets->CloseConnection(connection, 0, "poll group assignment failed", false);
        return;
    }
    impl_->connections.insert(connection);
    try {
        if (!runtime->impl_->connections.emplace(connection, this).second)
            throw std::runtime_error("duplicate GameNetworkingSockets connection handle");
    } catch (...) {
        impl_->connections.erase(connection);
        runtime->impl_->sockets->CloseConnection(connection, 0, "connection registration failed", false);
        throw;
    }
}

void GnsEndpoint::HandleClosed(HSteamNetConnection connection, GnsEvent::Type type, std::string detail) {
    impl_->connections.erase(connection);
    if (impl_->clientConnection == connection) impl_->clientConnection = k_HSteamNetConnection_Invalid;
    RemoveQueued(connection);
    QueueControl({type, connection, {}, std::move(detail)});
}

void GnsEndpoint::Poll() {
    if (!impl_->runtime || impl_->pollGroup == k_HSteamNetPollGroup_Invalid) return;
    auto* sockets = impl_->runtime->impl_->sockets;
    std::array<SteamNetworkingMessage_t*, MessageBatchSize> messages{};
    int processed = 0;
    while (processed < MaxMessagesPerPoll) {
        const int capacity = std::min(MessageBatchSize, MaxMessagesPerPoll - processed);
        const int count = sockets->ReceiveMessagesOnPollGroup(impl_->pollGroup, messages.data(), capacity);
        if (count < 0) {
            QueueControl({GnsEvent::Type::Error, k_HSteamNetConnection_Invalid, {}, "poll group receive failed"});
            return;
        }
        if (count == 0) return;
        processed += count;
        for (int i = 0; i < count; ++i) {
            SteamNetworkingMessage_t* message = messages[i];
            const auto connection = message->m_conn;
            const auto size = message->m_cbSize;
            if (!impl_->connections.count(connection)) {
                message->Release();
                continue;
            }
            if (size < 0 || static_cast<std::size_t>(size) > MaxMessageBytes || (size && !message->m_pData)) {
                message->Release();
                Reject(connection, "incoming message exceeds transport limits");
                continue;
            }

            GnsEvent event{GnsEvent::Type::Message, connection, {}, {}};
            try {
                const auto* bytes = static_cast<const std::uint8_t*>(message->m_pData);
                if (size) event.data.assign(bytes, bytes + size);
                message->Release();
            } catch (...) {
                message->Release();
                throw;
            }
            if (!QueueMessage(std::move(event)))
                Reject(connection, "incoming event queue limit exceeded");
        }
    }
}

void GnsEndpoint::Detach() {
    if (!impl_ || !impl_->runtime) return;
    auto* runtime = impl_->runtime;
    runtime->Unregister(this);
    if (runtime->impl_->initialized) {
        if (impl_->clientConnection != k_HSteamNetConnection_Invalid &&
            !impl_->connections.count(impl_->clientConnection))
            runtime->impl_->sockets->CloseConnection(impl_->clientConnection, 0, "endpoint destroyed", false);
        for (const auto connection : impl_->connections)
            runtime->impl_->sockets->CloseConnection(connection, 0, "endpoint destroyed", false);
        if (impl_->listenSocket != k_HSteamListenSocket_Invalid)
            runtime->impl_->sockets->CloseListenSocket(impl_->listenSocket);
        if (impl_->pollGroup != k_HSteamNetPollGroup_Invalid)
            runtime->impl_->sockets->DestroyPollGroup(impl_->pollGroup);
    }
    impl_->connections.clear();
    impl_->events.clear();
    impl_->queuedBytes = 0;
    impl_->runtime = nullptr;
    impl_->listenSocket = k_HSteamListenSocket_Invalid;
    impl_->pollGroup = k_HSteamNetPollGroup_Invalid;
    impl_->clientConnection = k_HSteamNetConnection_Invalid;
    impl_->port = 0;
    impl_->listening = false;
}

GnsRuntime::GnsRuntime() : impl_(std::make_unique<Impl>()) { Init(); }

GnsRuntime::~GnsRuntime() {
    if (impl_->initialized && impl_->ownerThread != std::this_thread::get_id()) std::terminate();
    Shutdown();
}

void GnsRuntime::Init() {
    if (impl_->initialized) {
        CheckThread();
        return;
    }
    std::lock_guard<std::mutex> lock(runtimeMutex);
    if (activeRuntime) throw std::logic_error("GameNetworkingSockets runtime already active");
    impl_->ownerThread = std::this_thread::get_id();
    SteamNetworkingErrMsg error{};
    if (!GameNetworkingSockets_Init(nullptr, error)) throw std::runtime_error(error);
    impl_->sockets = SteamNetworkingSockets();
    impl_->utils = SteamNetworkingUtils();
    if (!impl_->sockets || !impl_->utils) {
        GameNetworkingSockets_Kill();
        impl_->sockets = nullptr;
        impl_->utils = nullptr;
        throw std::runtime_error("GameNetworkingSockets interfaces unavailable");
    }
    if (!impl_->utils->SetGlobalCallback_SteamNetConnectionStatusChanged(&GnsRuntime::StatusChanged)) {
        GameNetworkingSockets_Kill();
        impl_->sockets = nullptr;
        impl_->utils = nullptr;
        throw std::runtime_error("could not register GameNetworkingSockets callback");
    }
    impl_->initialized = true;
    activeRuntime = this;
}

void GnsRuntime::Shutdown() {
    if (!impl_->initialized) return;
    CheckThread();
    std::lock_guard<std::mutex> lock(runtimeMutex);
    const std::vector<GnsEndpoint*> endpoints(impl_->endpoints.begin(), impl_->endpoints.end());
    for (auto* endpoint : endpoints) endpoint->Detach();
    impl_->utils->SetGlobalCallback_SteamNetConnectionStatusChanged(nullptr);
    activeRuntime = nullptr;
    GameNetworkingSockets_Kill();
    impl_->sockets = nullptr;
    impl_->utils = nullptr;
    impl_->initialized = false;
}

std::unique_ptr<GnsEndpoint> GnsRuntime::Listen(const SteamNetworkingIPAddr& address) {
    CheckThread();
    if (!address.m_port) throw std::invalid_argument("listen address requires a nonzero port");
    auto endpoint = std::unique_ptr<GnsEndpoint>(new GnsEndpoint(*this));
    endpoint->impl_->listening = true;
    endpoint->impl_->pollGroup = impl_->sockets->CreatePollGroup();
    if (endpoint->impl_->pollGroup == k_HSteamNetPollGroup_Invalid)
        throw std::runtime_error("could not create GameNetworkingSockets poll group");
    endpoint->impl_->listenSocket = impl_->sockets->CreateListenSocketIP(address, 0, nullptr);
    if (endpoint->impl_->listenSocket == k_HSteamListenSocket_Invalid)
        throw std::runtime_error("could not create GameNetworkingSockets listen socket");
    SteamNetworkingIPAddr bound{};
    if (!impl_->sockets->GetListenSocketAddress(endpoint->impl_->listenSocket, &bound))
        throw std::runtime_error("could not query GameNetworkingSockets listen address");
    endpoint->impl_->port = bound.m_port;
    impl_->endpoints.insert(endpoint.get());
    impl_->listeners.emplace(endpoint->impl_->listenSocket, endpoint.get());
    return endpoint;
}

std::unique_ptr<GnsEndpoint> GnsRuntime::Connect(const SteamNetworkingIPAddr& address) {
    CheckThread();
    if (!address.m_port || address.IsIPv6AllZeros()) throw std::invalid_argument("connect address needs a numeric IP and nonzero port");
    auto endpoint = std::unique_ptr<GnsEndpoint>(new GnsEndpoint(*this));
    endpoint->impl_->pollGroup = impl_->sockets->CreatePollGroup();
    if (endpoint->impl_->pollGroup == k_HSteamNetPollGroup_Invalid)
        throw std::runtime_error("could not create GameNetworkingSockets poll group");
    endpoint->impl_->clientConnection = impl_->sockets->ConnectByIPAddress(address, 0, nullptr);
    if (endpoint->impl_->clientConnection == k_HSteamNetConnection_Invalid)
        throw std::runtime_error("could not create GameNetworkingSockets connection");
    const auto connection = endpoint->impl_->clientConnection;
    endpoint->impl_->connections.insert(connection);
    if (!impl_->sockets->SetConnectionPollGroup(connection, endpoint->impl_->pollGroup))
        throw std::runtime_error("could not assign client connection to poll group");
    try {
        if (!impl_->connections.emplace(connection, endpoint.get()).second)
            throw std::runtime_error("duplicate GameNetworkingSockets connection handle");
    } catch (...) {
        endpoint->impl_->connections.erase(connection);
        throw;
    }
    impl_->endpoints.insert(endpoint.get());
    return endpoint;
}

void GnsRuntime::Poll() {
    CheckThread();
    impl_->sockets->RunCallbacks();
    for (auto* endpoint : impl_->endpoints) endpoint->Poll();
}

void GnsRuntime::CheckThread() const {
    if (!impl_->initialized) throw std::logic_error("GameNetworkingSockets runtime is shut down");
    if (impl_->ownerThread != std::this_thread::get_id())
        throw std::logic_error("GameNetworkingSockets must be polled and used on its owner thread");
}

void GnsRuntime::Unregister(GnsEndpoint* endpoint) {
    impl_->endpoints.erase(endpoint);
    for (auto connection = impl_->connections.begin(); connection != impl_->connections.end();) {
        if (connection->second == endpoint) connection = impl_->connections.erase(connection);
        else ++connection;
    }
    for (auto listener = impl_->listeners.begin(); listener != impl_->listeners.end();) {
        if (listener->second == endpoint) listener = impl_->listeners.erase(listener);
        else ++listener;
    }
}

void GnsRuntime::StatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    std::lock_guard<std::mutex> lock(runtimeMutex);
    if (!activeRuntime || !info) return;
    try {
        activeRuntime->DispatchStatus(info);
    } catch (...) {
        activeRuntime->impl_->sockets->CloseConnection(info->m_hConn, 0, "callback processing failed", false);
    }
}

void GnsRuntime::DispatchStatus(SteamNetConnectionStatusChangedCallback_t* info) {
    const auto state = info->m_info.m_eState;
    if (state == k_ESteamNetworkingConnectionState_Connecting &&
        info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid) {
        const auto listener = impl_->listeners.find(info->m_info.m_hListenSocket);
        if (listener == impl_->listeners.end()) {
            impl_->sockets->CloseConnection(info->m_hConn, 0, "listen endpoint no longer exists", false);
            return;
        }
        listener->second->HandleIncoming(info->m_hConn);
        return;
    }

    const auto registered = impl_->connections.find(info->m_hConn);
    if (registered == impl_->connections.end()) return;
    auto* endpoint = registered->second;
    if (state == k_ESteamNetworkingConnectionState_Connected) {
        if (endpoint->impl_->events.size() >= MaxQueuedEvents) {
            endpoint->Reject(info->m_hConn, "incoming event queue limit exceeded");
            return;
        }
        endpoint->QueueControl({GnsEvent::Type::Connected, info->m_hConn, {}, {}});
    } else if (state == k_ESteamNetworkingConnectionState_ClosedByPeer ||
               state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
        impl_->connections.erase(registered);
        const auto type = state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally
            ? GnsEvent::Type::Error : GnsEvent::Type::Disconnected;
        endpoint->HandleClosed(info->m_hConn, type, EndDetail(info->m_info));
        impl_->sockets->CloseConnection(info->m_hConn, 0, nullptr, false);
    }
}

} // namespace restoration::network
