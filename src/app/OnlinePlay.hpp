#pragma once

#include "../gui/GameUi.hpp"
#include "../network/RoomSession.hpp"
#include <filesystem>
#include <memory>

namespace restoration {

// The room connection outlives each match and returns to the same menu.
class OnlinePlay {
public:
    explicit OnlinePlay(std::filesystem::path executable);
    void Update(double now);
    void Handle(const UiOnlineRequest& request,const PlayerProfile& profile);
    void ProfileChanged(const PlayerProfile& profile);
    void Leave();
    void Fail(std::string message);
    const network::RoomView& view() const { return view_; }
    network::RoomSession* session() { return session_.get(); }
private:
    network::Member PublicMember(const PlayerProfile& profile) const;
    std::filesystem::path executable_;
    std::unique_ptr<network::GnsRuntime> runtime_;
    std::unique_ptr<network::RoomSession> session_;
    network::RoomView view_;
    std::string error_,notice_;
};
} // namespace restoration
