#include "MapInteraction.hpp"
#include "MapPinLayout.hpp"
#include "RmlRenderer.hpp"
#include "../game/MatchSimulation.hpp"
#include "../game/ZoneControl.hpp"
#include "../world/FacePaint.hpp"
#include "../world/ReferenceData.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <GLFW/glfw3.h>

namespace restoration {
namespace {
std::string Escape(const std::string& text) {
    std::string out;
    for(char c:text) switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;default:out+=c;}
    return out;
}
std::string Number(double n,int decimals=2){char out[32];std::snprintf(out,sizeof(out),decimals==0?"%.0f":"%.2f",n);return out;}
std::string Bool(bool value){return value?"true":"false";}
std::string DeathMarkIcon(const PlayerProfile& profile,int team){
    const int palette=profile.settings.colorblind?5:(profile.settings.palette>=0&&profile.settings.palette<5?profile.settings.palette:0);
    return "markers/death-"+std::to_string(palette)+"-"+std::to_string(team==0?0:1)+".png";
}
Rml::Element* Find(Rml::ElementDocument* doc,const std::string& id){
    auto* element=doc?doc->GetElementById(id):nullptr;
    if(!element)throw std::runtime_error("Missing UI element: "+id);
    return element;
}
void Hidden(Rml::ElementDocument* doc,const std::string& id,bool hidden){
    auto* element=Find(doc,id);element->SetClass("hidden",hidden);
    if(hidden)element->SetProperty("display","none");else element->RemoveProperty("display");
}
void SetText(Rml::ElementDocument* doc,std::map<std::string,std::string>& cache,const std::string& id,const std::string& value){
    const std::string key=doc->GetId()+"/"+id;
    if(cache[key]!=value){Find(doc,id)->SetInnerRML(Escape(value));cache[key]=value;}
}
bool TargetAvailable(const std::vector<UiJumpMapTarget>& targets,int key){
    const auto found=std::find_if(targets.begin(),targets.end(),[&](const auto& target){return target.key==key;});
    return found!=targets.end()&&found->available;
}
}

struct MapInteraction::Impl {
    UiMapFrame frame;
    int hoverKey=-1,selection=-1;
    std::string minimapCache;
    double nextUpdate=0.0;
    std::map<std::string,std::string> textCache;
    const MatchSimulation* match=nullptr;
    std::uint64_t hudEpoch=0;
    bool clearClosedMarkup=false;

    void ResetTransient(){
        minimapCache.clear();nextUpdate=0;hoverKey=-1;selection=-1;
    }
};

MapInteraction::MapInteraction():impl_(std::make_unique<Impl>()){}
MapInteraction::~MapInteraction()=default;

void MapInteraction::SetFrame(UiMapFrame frame,RmlRenderer& renderer){
    auto& p=*impl_;
    const bool openChanged=p.frame.open!=frame.open;
    if(openChanged)p.ResetTransient();
    p.clearClosedMarkup=openChanged&&!frame.open;
    if(!frame.haveWorldViewProjection){
        frame.worldViewProjection=p.frame.worldViewProjection;
        frame.haveWorldViewProjection=p.frame.haveWorldViewProjection;
    }
    p.frame=std::move(frame);
    if(!p.frame.open){p.hoverKey=-1;p.selection=-1;}
    else{
        if(p.hoverKey>=0&&!TargetAvailable(p.frame.targets,p.hoverKey))p.hoverKey=-1;
        if(p.selection>=0&&!TargetAvailable(p.frame.targets,p.selection))p.selection=-1;
    }
    if(openChanged){p.minimapCache.clear();p.nextUpdate=0;}
    renderer.SetNativeMapTexture(static_cast<GLuint>(p.frame.nativeMapTexture));
}
void MapInteraction::SetWorldView(const glm::mat4& viewProjection){impl_->frame.worldViewProjection=viewProjection;impl_->frame.haveWorldViewProjection=true;}
void MapInteraction::Invalidate(){impl_->minimapCache.clear();impl_->nextUpdate=0;}
bool MapInteraction::open()const{return impl_->frame.open;}
int MapInteraction::queuedKey()const{return impl_->frame.queuedKey;}
const std::string& MapInteraction::queuedLabel()const{return impl_->frame.queuedLabel;}
bool MapInteraction::SelectTarget(int key){auto& p=*impl_;if(!p.frame.open||!TargetAvailable(p.frame.targets,key))return false;p.selection=key;return true;}
bool MapInteraction::SelectHoveredTarget(){return SelectTarget(impl_->hoverKey);}
int MapInteraction::TakeSelection(){
    auto& p=*impl_;const int selected=p.selection;p.selection=-1;
    return p.frame.open&&TargetAvailable(p.frame.targets,selected)?selected:-1;
}

void MapInteraction::Update(Rml::Context* context,Rml::ElementDocument* hud,
                            const StageData& stage,const FacePaint& paint,MatchSimulation& match,
                            const PlayerProfile& player,const std::vector<MapDeathMark>& deathMarks,
                            double deathClock){
    auto& p=*impl_;
    const auto epoch=match.world().hudEventEpoch();
    if(p.match!=&match||p.hudEpoch!=epoch){
        p.match=&match;p.hudEpoch=epoch;p.ResetTransient();
    }

    SetText(hud,p.textCache,"jump-map-help",p.frame.open?"點擊地圖上的隊友，或按 1–0 選擇跳躍目標　·　Tab / M 關閉":"");
    SetText(hud,p.textCache,"jump-map-queue",p.frame.queuedKey>=0?std::to_string(p.frame.queuedKey%10)+"　"+p.frame.queuedLabel:"");
    Find(hud,"jump-map-queue")->SetClass("queued",p.frame.queuedKey>=0);
    Find(hud,"minimap")->SetClass("jump-map-open",p.frame.open);
    if(!player.settings.minimap&&!p.frame.open){
        if(p.clearClosedMarkup){
            Find(hud,"minimap-map")->SetInnerRML("");p.textCache["hud/minimap-map"].clear();
            p.clearClosedMarkup=false;
        }
        Hidden(hud,"minimap",true);return;
    }
    p.clearClosedMarkup=false;Hidden(hud,"minimap",false);
    const auto& state=match.local().state;const auto palette=ProfilePaletteColors(player.settings.palette,player.settings.colorblind,match.boss()!=nullptr);
    const double now=glfwGetTime();
    const std::string stageKey=std::string(stage.id)+":"+std::string(stage.mode);
    const auto dimensions=context->GetDimensions();
    std::string cache=stageKey+":"+std::to_string(paint.version())+":"+std::to_string(player.settings.palette)+":"+Bool(player.settings.colorblind)+":"+std::to_string(dimensions.x)+":"+std::to_string(dimensions.y)+":"+std::to_string(context->GetDensityIndependentPixelRatio());
    cache+=p.frame.open?":jump-map-open:"+std::to_string(static_cast<int>(match.world().time()*10)):std::string(":jump-map-closed");
    cache+=":"+std::to_string(p.frame.queuedKey)+":"+std::to_string(p.hoverKey);
    for(const auto& target:p.frame.targets)cache+=":"+std::to_string(target.key)+","+std::to_string(static_cast<int>(std::round(target.x*4)))+","+std::to_string(static_cast<int>(std::round(target.y*4)))+","+std::to_string(static_cast<int>(std::round(target.z*4)))+","+Bool(target.available)+","+target.label+","+target.weapon;
    if(const auto* zones=match.zones()){cache+=":"+zones->objective().id;for(const int id:zones->activeIndices())cache+=":"+std::to_string(zones->zones()[id].owner);}
    for(const auto& actor:match.actors())cache+=":"+std::to_string(static_cast<int>(std::round(actor->state.pos.x*4)))+","+std::to_string(static_cast<int>(std::round(actor->state.pos.y*4)))+","+std::to_string(static_cast<int>(std::round(actor->state.pos.z*4)))+","+std::to_string(actor->state.alive)+","+std::to_string(actor->state.track>0&&actor->state.trackTeam==match.local().team());
    cache+=":"+std::to_string(deathMarks.size())+","+std::to_string(deathMarks.empty()?0:deathMarks.back().serial)+","+std::to_string(static_cast<int>(deathClock*10));
    const glm::mat4& mapView=p.frame.open||!p.frame.haveNativeMapView?p.frame.worldViewProjection:p.frame.nativeMapViewProjection;
    for(int column=0;column<4;++column)for(int row=0;row<4;++row)
        cache+=":"+std::to_string(static_cast<int>(std::round(mapView[column][row]*1000.0f)));
    const auto project=[&](glm::dvec3 point,double& left,double& top){
        const glm::vec4 clip=mapView*glm::vec4(glm::vec3(point),1.0f);
        if(clip.w<=1e-4f)return false;
        const double x=clip.x/clip.w,y=clip.y/clip.w;
        if(std::abs(x)>1.04||std::abs(y)>1.04)return false;
        left=(x*.5+.5)*100.0;top=(.5-y*.5)*100.0;return true;
    };
    if(p.minimapCache!=cache&&now>=p.nextUpdate){
        auto* map=Find(hud,"minimap-map");const double mapWidth=p.frame.open?context->GetDimensions().x:map->GetClientWidth(),mapHeight=p.frame.open?context->GetDimensions().y:map->GetClientHeight();
        std::string markup,targetLegend;
        if(const auto* zones=match.zones()){
            for(const int id:zones->activeIndices()){
                const auto& zone=zones->zones()[id];const std::string color=zone.owner>=0?std::string(palette[zone.owner]):"#f4cb62";
                for(const auto& polygon:zone.region.polygons)for(std::size_t i=0;i<polygon.size();++i){
                    const auto& a=polygon[i];const auto& b=polygon[(i+1)%polygon.size()];double x0=0,y0=0,x1=0,y1=0;
                    if(!project({a.x,.15,a.y},x0,y0)||!project({b.x,.15,b.y},x1,y1))continue;
                    const double dx=(x1-x0)*mapWidth/100.0,dy=(y1-y0)*mapHeight/100.0;
                    markup+="<div class=\"map-zone-edge\" style=\"left:"+Number(x0,3)+"%;top:"+Number(y0,3)+"%;width:"+Number(std::hypot(dx,dy),3)+"px;background-color:"+color+";transform:rotate("+Number(std::atan2(dy,dx)*180/3.141592653589793,3)+"deg);\"></div>";
                }
            }
        }
        for(std::size_t i=0;i<match.actors().size();++i){
            const auto& actor=match.actors()[i];if(!match.roster()[i].active||!actor->state.alive)continue;
            const bool local=actor->isLocal(),ally=actor->team()==match.local().team();
            const bool tracked=!ally&&actor->state.track>0&&actor->state.trackTeam==match.local().team();
            if(!local&&!ally&&!tracked)continue;
            if(p.frame.open&&!local)continue;
            const auto position=actor->visualPos();double left=0,top=0;if(!project(position,left,top))continue;
            const std::string color(actor->team()==0?palette[0]:palette[1]);
            std::string contents;
            if(local){
                double facingX=0,facingY=0,rotation=0;
                const auto facing=position+glm::dvec3(std::sin(actor->state.yaw),0,std::cos(actor->state.yaw))*2.0;
                if(mapWidth>0&&mapHeight>0&&project(facing,facingX,facingY)){
                    const double dx=(facingX-left)*mapWidth/100.0,dy=(facingY-top)*mapHeight/100.0;
                    if(dx*dx+dy*dy>1e-8)rotation=std::atan2(dx,-dy)*180.0/3.141592653589793;
                }
                contents="<img class=\"map-self-arrow\" src=\"markers/arrow-"+std::to_string(player.settings.colorblind?5:std::clamp(player.settings.palette,0,4))+"-"+std::to_string(actor->team())+".png\" style=\"transform:rotate("+Number(rotation,2)+"deg);\"/>";
                if(p.frame.open)contents+="<span class=\"map-you-label\">自己</span>";
            }
            const std::string actorClass=local?"map-actor-local map-you ":ally?"map-actor-ally ":"map-actor-tracked ";
            markup+="<div class=\"map-actor "+actorClass+"map-actor-"+(actor->team()==0?"a":"b")+"\" style=\"left:"+Number(left,3)+"%;top:"+Number(top,3)+"%;background-color:"+(local&&!p.frame.open?"transparent":color)+";\">"+contents+"</div>";
        }
        for(const auto& mark:deathMarks){
            double left=0,top=0;if(!project(mark.pos,left,top))continue;
            const double alpha=std::clamp((mark.born+5.0-deathClock)/1.3,0.0,1.0);
            markup+="<div class=\"map-death-mark\" style=\"left:"+Number(left,3)+"%;top:"+Number(top,3)+"%;opacity:"+Number(alpha)+";\"><img src=\""+DeathMarkIcon(player,mark.team)+"\"/></div>";
        }
        std::vector<glm::dvec2> pinAnchors;std::vector<int> pinKeys;
        if(p.frame.open)for(const auto& target:p.frame.targets){double x=0,y=0;if(project({target.x,target.y,target.z},x,y)){pinAnchors.push_back({x*mapWidth/100,y*mapHeight/100});pinKeys.push_back(target.key);}}
        const auto pinPositions=SpreadMapPins(pinAnchors,84*context->GetDensityIndependentPixelRatio());
        for(const auto& target:p.frame.targets){
            const std::string stateClass=target.available?"available":"unavailable";
            const std::string disabled=target.available&&p.frame.open?"":" disabled=\"disabled\"";
            const std::string icon=!target.weapon.empty()?"<img src=\"weapons/"+Escape(target.weapon)+".png\"/>":target.key==4?"<span class=\"map-base-icon\"><i></i><b></b><em><u></u></em></span>":(target.key==0||target.key>4)?"<img src=\"equipment/sub-beacon.png\"/>":std::string();
            targetLegend+="<button id=\"jump-target-"+std::to_string(target.key)+"\" class=\"jump-target "+stateClass+"\""+disabled+">"+icon+"<strong>"+std::to_string(target.key%10)+"</strong><span>"+Escape(target.label)+"</span><small>"+(target.available?"可用":"暫不可用")+"</small></button>";
            if(!p.frame.open)continue;
            double left=0,top=0;if(!project({target.x,target.y,target.z},left,top))continue;
            const double anchorLeft=left,anchorTop=top;
            const auto pin=std::find(pinKeys.begin(),pinKeys.end(),target.key);
            if(pin!=pinKeys.end()&&mapWidth>0&&mapHeight>0){const auto point=pinPositions[static_cast<std::size_t>(pin-pinKeys.begin())];left=std::clamp(point.x,48.,mapWidth-48)*100/mapWidth;top=std::clamp(point.y,135.,mapHeight-25)*100/mapHeight;}
            const double linkX=(anchorLeft-left)*mapWidth/100,linkY=(anchorTop-top)*mapHeight/100;
            const std::string link=std::hypot(linkX,linkY)>3?"<i class=\"map-pin-link\" style=\"width:"+Number(std::hypot(linkX,linkY))+"px;transform:rotate("+Number(std::atan2(linkY,linkX)*180/3.141592653589793)+"deg);\"></i>":"";
            std::string displayLabel=target.label;
            std::string mapIcon=icon;
            if(target.key==4){displayLabel="BASE";mapIcon="<span class=\"map-base-icon\"><i></i><b></b><em><u></u></em></span>";}
            else if(target.key==0||target.key>4){displayLabel="訊標 "+std::to_string(target.key==0?6:target.key-4);mapIcon="<img src=\"equipment/sub-beacon.png\"/>";}
            else if(displayLabel.rfind("隊友 ",0)==0)displayLabel.erase(0,std::string("隊友 ").size());
            const std::string teamColor(palette[static_cast<std::size_t>(match.local().team())]);
            const std::string style=" style=\"left:"+Number(left,3)+"%;top:"+Number(top,3)+"%;\"";
            markup+="<div class=\"map-marker\" data-anchor-left=\""+Number(anchorLeft,3)+"\" data-anchor-top=\""+Number(anchorTop,3)+"\""+style+">"+link+"<div class=\"map-pin-stem\"></div><div class=\"map-pin-ground\" style=\"background-color:"+teamColor+";\"></div><button id=\"jump-map-marker-"+std::to_string(target.key)+"\" class=\"map-target "+stateClass+(p.frame.queuedKey==target.key?" queued":"")+"\" style=\"background-color:"+teamColor+";\""+disabled+" title=\""+Escape(target.label)+"\">"+mapIcon+"<strong>"+std::to_string(target.key%10)+"</strong></button><span class=\"map-target-label\">"+Escape(displayLabel)+"</span></div>";
        }
        if(p.frame.open){
            const auto arc=[&](glm::dvec3 from,glm::dvec3 to,double start,double end,bool self){
                double x0,y0,x1,y1;if(!project(from,x0,y0)||!project(to,x1,y1))return;
                const glm::dvec2 a(x0*mapWidth/100,y0*mapHeight/100),b(x1*mapWidth/100,y1*mapHeight/100),control=MapArcControl(a,b);
                auto previous=MapArcPoint(a,control,b,start);
                for(int i=1;i<=24;++i){const auto next=MapArcPoint(a,control,b,start+(end-start)*i/24);const auto delta=next-previous;
                    markup+="<i class=\"map-jump-arc\" style=\"left:"+Number(previous.x)+"px;top:"+Number(previous.y)+"px;width:"+Number(glm::length(delta)+1)+"px;background-color:"+std::string(palette[match.local().team()])+";opacity:"+(self?"1":".55")+";transform:rotate("+Number(std::atan2(delta.y,delta.x)*180/3.141592653589793)+"deg);\"></i>";previous=next;}
                markup+="<i class=\"map-landing-ring\" style=\"left:"+Number(b.x)+"px;top:"+Number(b.y)+"px;border-color:"+std::string(palette[match.local().team()])+";\"></i>";
            };
            const int previewKey=p.hoverKey>=0?p.hoverKey:p.frame.queuedKey;
            for(const auto& target:p.frame.targets)if(target.key==previewKey&&target.available)arc(match.local().state.alive?match.local().state.pos:stage.spawnPads[match.local().team()],{target.x,target.y,target.z},0,1,true);
            for(const auto& actor:match.actors())if(actor->team()==match.local().team()&&actor->superJumpState()){
                const auto& jump=*actor->superJumpState();const double done=jump.flight?std::clamp(jump.elapsed/std::max(.01,jump.duration),0.,1.):0;
                const double reach=jump.flight?1:1-std::pow(1-std::clamp(jump.elapsed/.75,0.,1.),3);
                arc(jump.flight?jump.from:actor->state.pos,jump.flight?jump.to:jump.target,done,reach,actor->isLocal());
            }
        }
        if(p.frame.open)markup+="<div id=\"map-cursor\" class=\"map-cursor\"><i></i></div>";
        const auto legendKey=std::string("hud/jump-map-legend");if(p.textCache[legendKey]!=targetLegend){Find(hud,"jump-map-legend")->SetInnerRML(targetLegend);p.textCache[legendKey]=targetLegend;}
        const auto key=std::string("hud/minimap-map");if(p.textCache[key]!=markup){Find(hud,"minimap-map")->SetInnerRML(markup);p.textCache[key]=markup;}
        p.minimapCache=cache;p.nextUpdate=now+.1;
    }

}

void MapInteraction::LayoutAndHitTest(Rml::Context* context,Rml::ElementDocument* hud,
                                      const glm::dvec2& pointerPosition,double scale){
    auto& p=*impl_;
    if(!p.frame.open)return;
    context->Update();
    auto* cursor=hud->GetElementById("map-cursor");
    if(!cursor){p.hoverKey=-1;return;}
    const double x=pointerPosition.x,y=pointerPosition.y;
    p.hoverKey=-1;double nearest=60*scale;glm::dvec2 snap(x,y);
    for(const auto& target:p.frame.targets)if(target.available)if(auto* pin=hud->GetElementById("jump-map-marker-"+std::to_string(target.key))){
        const auto offset=pin->GetAbsoluteOffset(Rml::BoxArea::Border);const auto size=pin->GetBox().GetSize(Rml::BoxArea::Border);const glm::dvec2 center(offset.x+size.x*.5,offset.y+size.y*.5);const double distance=glm::length(center-glm::dvec2(x,y));
        pin->SetClass("cursor-snap",false);if(distance<nearest){nearest=distance;p.hoverKey=target.key;snap=center;}
    }
    if(p.hoverKey>=0)Find(hud,"jump-map-marker-"+std::to_string(p.hoverKey))->SetClass("cursor-snap",true);
    cursor->SetClass("snapped",p.hoverKey>=0);cursor->SetProperty("left",Number(snap.x)+"px");cursor->SetProperty("top",Number(snap.y)+"px");
}
} // namespace restoration
