#include "RoomServer.hpp"

#include <charconv>
#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
struct Options {
    std::string bind;
    std::uint16_t port = 0;
    std::optional<double> runFor;
    std::optional<double> idleExit;
};

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bind" && i + 1 < argc) options.bind = argv[++i];
        else if (arg == "--port" && i + 1 < argc) {
            unsigned port = 0;
            const std::string value = argv[++i];
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
            if (error != std::errc{} || end != value.data() + value.size() || port == 0 || port > 65535)
                throw std::invalid_argument("--port must be an integer from 1 to 65535");
            options.port = static_cast<std::uint16_t>(port);
        } else if (arg == "--run-for" && i + 1 < argc) {
            double seconds = 0.0;
            const std::string value = argv[++i];
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), seconds);
            if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(seconds) || seconds <= 0)
                throw std::invalid_argument("--run-for must be a positive number of seconds");
            options.runFor = seconds;
        } else if (arg == "--idle-exit" && i + 1 < argc) {
            double seconds = 0.0;
            const std::string value = argv[++i];
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), seconds);
            if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(seconds) || seconds <= 0)
                throw std::invalid_argument("--idle-exit must be a positive number of seconds");
            options.idleExit = seconds;
        } else {
            throw std::invalid_argument("Usage: Tiny-Splatoon-Server --bind numericIP --port port [--run-for seconds] [--idle-exit seconds]");
        }
    }
    if (options.bind.empty() || !options.port)
        throw std::invalid_argument("Usage: Tiny-Splatoon-Server --bind numericIP --port port [--run-for seconds] [--idle-exit seconds]");
    return options;
}
}

int main(int argc, char** argv) {
    try {
        const auto options = ParseOptions(argc, argv);
        std::string addressText = options.bind;
        if (addressText.find(':') != std::string::npos &&
            !(addressText.size() >= 2 && addressText.front() == '[' && addressText.back() == ']'))
            addressText = "[" + addressText + "]";
        addressText += ":" + std::to_string(options.port);
        SteamNetworkingIPAddr address{};
        if (!address.ParseString(addressText.c_str()))
            throw std::invalid_argument("--bind must be a numeric IPv4 or IPv6 address");

        restoration::network::GnsRuntime runtime;
        restoration::network::RoomServer server(runtime, address);
        std::cout << "Room server listening on port " << server.port() << std::endl;
        const auto start = std::chrono::steady_clock::now();
        std::optional<double> idleSince;
        for (;;) {
            runtime.Poll();
            const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            server.Update(now);
            if (options.idleExit) {
                if (server.idle()) {
                    if (!idleSince) idleSince = now;
                    else if (now - *idleSince >= *options.idleExit) break;
                } else idleSince.reset();
            }
            if (options.runFor && now >= *options.runFor) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
