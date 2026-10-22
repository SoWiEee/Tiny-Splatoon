#include "../src/app/OnlinePlay.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace restoration;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Check>void waitFor(OnlinePlay& online,Check check){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(std::chrono::steady_clock::now()<end){
        const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        online.Update(now);if(check())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("online menu flow timed out: "+online.view().error);
}
}
int main(int argc,char** argv){
    try{
        require(argc==2,"server address required");
        OnlinePlay online(std::filesystem::absolute(argv[0]));PlayerProfile profile;
        profile.name="連線測試";profile.weapon="shooter";profile.style.hair=2;
        UiOnlineRequest request;request.kind=UiOnlineRequest::Kind::Create;request.address="invalid-address";
        online.Handle(request,profile);
        require(online.view().phase==network::RoomPhase::Error&&!online.view().error.empty(),"invalid address needs a visible recoverable error");
        request.address=argv[1];online.Handle(request,profile);
        waitFor(online,[&]{return online.view().phase==network::RoomPhase::Lobby;});
        require(online.view().code.size()==5&&online.view().members.size()==1,"create must receive a real five-character room code");
        require(online.view().members[0].name==profile.name&&online.view().members[0].style.hair==2,"room must carry the public profile and style");
        profile.name="Mint Ice";online.ProfileChanged(profile);
        waitFor(online,[&]{return online.view().members[0].name==profile.name;});
        request.kind=UiOnlineRequest::Kind::Ready;request.ready=true;online.Handle(request,profile);
        waitFor(online,[&]{return online.view().members[0].ready;});
        request.kind=UiOnlineRequest::Kind::Start;online.Handle(request,profile);
        waitFor(online,[&]{return online.view().phase==network::RoomPhase::Starting;});
        const auto code=online.view().code;const auto epoch=online.view().matchEpoch;
        require(online.session()->startRoster().size()==8,"default match roster must fill seven bots");
        online.session()->MarkLoaded();waitFor(online,[&]{return online.view().phase==network::RoomPhase::Match;});
        online.session()->ReturnToLobby();waitFor(online,[&]{return online.view().phase==network::RoomPhase::Lobby;});
        require(online.view().code==code&&online.view().matchEpoch==epoch&&!online.view().members[0].ready,"return must keep the same room and clear ready");
        online.Leave();require(online.view().phase==network::RoomPhase::Offline,"leave must restore the offline menu state");
        request.kind=UiOnlineRequest::Kind::Join;request.code="ZZZZZ";online.Handle(request,profile);
        waitFor(online,[&]{return online.view().phase==network::RoomPhase::Error;});
        require(!online.view().error.empty(),"unknown room must expose an actionable error");
        std::cout<<"Online menu adapter create/profile/ready/start/load/return/leave flow passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
