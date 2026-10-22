#include "OnlinePlay.hpp"
#include "ArsenalData.hpp"
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace restoration {
namespace {
SteamNetworkingIPAddr ServerAddress(const std::string& text){
    SteamNetworkingIPAddr address{};
    if(text.empty()||text.size()>64||!address.ParseString(text.c_str())||!address.m_port||address.IsIPv6AllZeros())
        throw std::invalid_argument("請輸入有效的伺服器位址與連接埠，例如 127.0.0.1:27020。");
    return address;
}

void LaunchLocalService(const std::filesystem::path& game,const SteamNetworkingIPAddr& address){
    if(!address.IsLocalHost())throw std::invalid_argument("啟動本機服務時，請使用 127.0.0.1 或 ::1 的本機位址。");
    const auto server=game.parent_path()/"Tiny-Splatoon-Server.exe";
    if(!std::filesystem::is_regular_file(server))throw std::runtime_error("找不到房間服務程式，請確認遊戲檔案完整。");
#ifdef _WIN32
    char ip[SteamNetworkingIPAddr::k_cchMaxString]{};address.ToString(ip,sizeof(ip),false);
    const std::string host(ip);
    std::wstring command=L"\""+server.wstring()+L"\" --bind "+std::wstring(host.begin(),host.end())+L" --port "+std::to_wstring(address.m_port)+L" --idle-exit 180";
    std::vector<wchar_t> arguments(command.begin(),command.end());arguments.push_back(L'\0');
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(server.c_str(),arguments.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,server.parent_path().c_str(),&startup,&process))
        throw std::runtime_error("無法啟動本機房間服務，請確認程式可執行。");
    // Keep the service independent so another player can take over the room.
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
#else
    throw std::runtime_error("此平台請先啟動房間服務，再使用伺服器位址連線。");
#endif
}
}

OnlinePlay::OnlinePlay(std::filesystem::path executable):executable_(std::move(executable)){}

network::Member OnlinePlay::PublicMember(const PlayerProfile& profile)const{
    network::Member member;
    for(const auto& current:view_.members)if(current.id==view_.selfId){member=current;break;}
    member.name=profile.name;member.main=profile.weapon;member.style=profile.style;
    const auto& main=GetEquipment(EquipmentCategory::Main,profile.weapon);
    member.sub=profile.sub.empty()?std::string(main.sub):profile.sub;
    member.special=profile.special.empty()?std::string(main.special):profile.special;
    return member;
}

void OnlinePlay::Update(double now){
    try{
        if(runtime_)runtime_->Poll();
        if(session_){session_->Update(now);view_=session_->view();}
    }catch(const std::exception& error){Fail(error.what());}
    if(!error_.empty()){view_.error=error_;if(view_.phase==network::RoomPhase::Offline)view_.phase=network::RoomPhase::Error;}
    if(!notice_.empty())view_.status=notice_;
}

void OnlinePlay::Handle(const UiOnlineRequest& request,const PlayerProfile& profile){
    error_.clear();notice_.clear();
    try{
        using Kind=UiOnlineRequest::Kind;
        if(request.kind==Kind::Create||request.kind==Kind::Join){
            const auto address=ServerAddress(request.address);
            if(!runtime_)runtime_=std::make_unique<network::GnsRuntime>();
            if(!session_)session_=std::make_unique<network::RoomSession>(*runtime_);
            if(request.kind==Kind::Create)session_->Create(address,PublicMember(profile));
            else session_->Join(address,request.code,PublicMember(profile));
        }else if(request.kind==Kind::LocalService){
            LaunchLocalService(executable_,ServerAddress(request.address));notice_="本機服務已啟動，可以建立房間。";
        }else if(request.kind==Kind::Leave)Leave();
        else if(session_){
            switch(request.kind){
                case Kind::Ready:session_->SetReady(request.ready);break;
                case Kind::Team:session_->SetTeam(request.team);break;
                case Kind::Settings:session_->SetSettings(request.settings);break;
                case Kind::Start:session_->StartMatch();break;
                case Kind::Emote:session_->Emote(request.emote);break;
                default:break;
            }
        }
        if(session_)view_=session_->view();
    }catch(const std::exception& error){error_=error.what();view_.error=error_;if(view_.phase==network::RoomPhase::Offline)view_.phase=network::RoomPhase::Error;}
}

void OnlinePlay::ProfileChanged(const PlayerProfile& profile){
    if(session_&&view_.phase==network::RoomPhase::Lobby){
        try{session_->SetMe(PublicMember(profile));}catch(const std::exception& error){error_=error.what();}
    }
}
void OnlinePlay::Leave(){if(session_)session_->Leave();view_={};error_.clear();notice_.clear();}
void OnlinePlay::Fail(std::string message){Leave();error_=std::move(message);view_.phase=network::RoomPhase::Error;view_.error=error_;}
} // namespace restoration
