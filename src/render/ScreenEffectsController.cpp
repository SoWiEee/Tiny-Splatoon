#include "ScreenEffectsController.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace restoration {
namespace {
double clamp(double v,double a=0,double b=1){return std::clamp(v,a,b);}
double damp(double value,double target,double rate,double dt){return target+(value-target)*std::exp(-rate*dt);}
double easeOut(double t){return 1-std::pow(1-clamp(t),3);}
double easeInOut(double t){t=clamp(t);return t<.5?4*t*t*t:1-std::pow(-2*t+2,3)/2;}
glm::vec4 rgba(glm::dvec3 color,double alpha){return {float(color.x),float(color.y),float(color.z),float(alpha)};}
}
ScreenEffectsController::ScreenEffectsController(std::function<double()> random):random_(std::move(random)),lens_([this]{return Random();}){}
double ScreenEffectsController::Random(){if(random_)return random_();seed_=seed_*1664525u+1013904223u;return seed_/4294967296.;}
double ScreenEffectsController::Between(double a,double b){return a+Random()*(b-a);}
void ScreenEffectsController::Reset(){
    auto& s=state_;for(double* value:{&s.speed,&s.stretch,&s.punch,&s.punchV,&s.blast,&s.chroma,&s.edge,&s.aura,&s.auraPulse,&s.heart,&s.hurt,&s.urgency,&s.urgencyBase,&s.swim,&s.focus,&s.shimmer,&s.kill,&s.flash,&s.desaturation,&s.saturationPop,&s.flood,&s.floodDrip,&s.hole,&s.damage,&s.damageTime})*value=0;
    s.saturation=1;s.floodMode.clear();lens_.clear();
}
void ScreenEffectsController::Punch(double v){state_.punchV+=v*22;}
void ScreenEffectsController::Damage(double amount,const glm::dvec3* attacker){
    if(amount<=0)return;auto& s=state_;s.damage+=amount;if(attacker){s.attacker=*attacker;s.hasAttacker=true;}if(s.damageTime<=0)s.damageTime=.06;
    const double k=clamp(amount/60,.1,1);s.chroma=std::min(.9,s.chroma+.12+k*.4);if(amount>=40)Punch(-.005-.008*k);
}
void ScreenEffectsController::Kill(){state_.kill=1;state_.chroma=std::min(1.,state_.chroma+.3);Punch(.012);}
void ScreenEffectsController::Splat(bool water){
    auto& s=state_;output_.flood=rgba(water?glm::dvec3(.06,.34,.62):input_.enemyColor,s.flood);output_.floodClear=water?1:0;s.floodMode="in";s.floodTime=0;s.hole=0;s.chroma=std::min(1.4,s.chroma+.9);Punch(-.03);
    if(water)for(int i=0;i<34;i++){const double x=Random(),y=Between(.05,1),r=Between(.01,.034),slide=Between(.3,1.2),life=Between(1.8,3.6);lens_.droplet(2,x,y,r,slide,life);}
}
void ScreenEffectsController::Respawn(){auto& s=state_;output_.flood=rgba(input_.ownColor,1);output_.floodClear=0;output_.holeRim=glm::mix(input_.ownColor,glm::dvec3(1),.55)*1.6;s.flood=1;s.floodMode="reveal";s.floodTime=0;s.hole=.0001;s.floodDrip=.05;lens_.clear();s.desaturation=std::min(s.desaturation,.3);s.shimmer=1;}
void ScreenEffectsController::Jump(const std::string& phase){if(phase=="flight"){state_.flash=.5;state_.chroma=std::min(1.2,state_.chroma+.6);Punch(-.04);}}
void ScreenEffectsController::Land(){auto& s=state_;if(s.time-s.landTime<.3)return;s.landTime=s.time;output_.punchPos={.5f,.3f};output_.blastPos={.5f,.18f};output_.blastColor=glm::mix(input_.ownColor,glm::dvec3(1),.3);s.blast=std::max(s.blast,.75);s.blastTime=0;s.chroma=std::min(1.3,s.chroma+.7);Punch(.045);s.flash=std::max(s.flash,.35);}
void ScreenEffectsController::Special(const std::string& id){auto& s=state_;s.auraPulse=1;s.chroma=std::min(1.,s.chroma+.25);if(id=="storm")s.aura=std::max(s.aura,.9);}
void ScreenEffectsController::Countdown(int n){state_.urgency=1;state_.urgencyBase=clamp((11.-n)/10)*.35;}
void ScreenEffectsController::MatchState(const std::string& state){
    if(state=="intro")Reset();else if(state=="playing"){state_.saturationPop=1;state_.chroma=std::min(1.,state_.chroma+.35);Punch(.015);}
    else if(state=="judge"||state=="results"){state_.floodMode=state_.flood>0?"fadeout":"";state_.floodTime=0;lens_.clear();}
}
void ScreenEffectsController::Blast(glm::dvec3 p,double amount,glm::dvec3 color){
    auto& s=state_;if(s.time-s.lastBlastTime<.05&&glm::dot(glm::abs(s.lastBlast-p),glm::dvec3(1))<.8){output_.blastColor=glm::mix(color,glm::dvec3(1),.35);return;}
    s.lastBlastTime=s.time;s.lastBlast=p;const double k=amount*clamp(1-(glm::distance(p,input_.cameraPosition)-3)/24);if(k<.04)return;
    const auto clip=input_.viewProjection*glm::dvec4(p,1);if(std::abs(clip.w)>1e-9){const auto ndc=glm::dvec3(clip)/clip.w;if(ndc.z<1&&std::abs(ndc.x)<1.25&&std::abs(ndc.y)<1.25){output_.blastPos=glm::dvec2(ndc)*.5+.5;output_.punchPos=output_.blastPos;s.blast=std::min(1.2,std::max(s.blast,k*1.1));s.blastTime=0;output_.blastColor=glm::mix(color,glm::dvec3(1),.35);}}
    s.chroma=std::min(1.3,s.chroma+k*.9);Punch(.02*k);
}
void ScreenEffectsController::Impact(glm::dvec3 position,bool ownTeam,bool roller){
    if(!input_.alive||ownTeam||roller)return;
    const double distance=glm::dot(position-input_.cameraPosition,position-input_.cameraPosition);if(distance>2.4*2.4)return;
    const auto clip=input_.viewProjection*glm::dvec4(position,1);if(std::abs(clip.w)<1e-9)return;const auto p=glm::dvec3(clip)/clip.w;if(p.z>1)return;
    const double u=clamp(p.x*.5+.5,.03,.97),v=clamp(p.y*.5+.5,.05,.97);const int count=distance<1.2?3:1+int(Random()*2);
    for(int i=0;i<count;i++){const double x=clamp(u+Between(-.06,.06),.02,.98),y=clamp(v+Between(-.06,.06),.04,.97),r=Between(.008,.02),slide=Between(.1,.35),life=Between(.5,1);lens_.droplet(0,x,y,r,slide,life);}
}
glm::dvec2 ScreenEffectsController::EdgePoint(double angle,double inset){
    const double aspect=double(input_.width)/input_.height,cx=std::cos(angle),cy=std::sin(angle),t=std::min((aspect/2)/std::max(.001,std::abs(cx)),.5/std::max(.001,std::abs(cy)));
    const auto sign=[](double x){return (x>0)-(x<0);};double x=aspect/2+cx*t,y=.5+cy*t;x-=sign(cx)*inset*(std::abs(cx)>.25?1:.3);y-=sign(cy)*inset*(std::abs(cy)>.25?1:.3);
    double u=x/aspect,v=y;if(v<.5&&std::abs(u-.5)<.2){const double direction=u==.5?Random()-.5:u-.5;u=.5+sign(direction)*Between(.22,.3);}return {clamp(u,.02,.98),clamp(v,.03,.97)};
}
void ScreenEffectsController::DamageSplat(double amount){
    const auto& s=state_;const double aspect=double(input_.width)/input_.height,k=clamp(amount/70,.18,1.2);double angle=0;bool projected=false;
    if(s.hasAttacker){const auto clip=input_.viewProjection*glm::dvec4(s.attacker+glm::dvec3(0,1,0),1);if(std::abs(clip.w)>1e-9){auto v=glm::dvec3(clip)/clip.w;const bool behind=v.z>1;if(behind)v.x=-v.x,v.y=-v.y;if(!behind&&std::abs(v.x)<1&&std::abs(v.y)<1)angle=(v.x>=0?0:3.141592653589793)+Between(-.35,.35);else angle=std::atan2(v.y,v.x*aspect);projected=true;}}
    if(!projected)angle=Random()*6.283185307179586;
    const int count=k>.7?2:1;for(int i=0;i<count;i++){const double a=angle+(i?Between(-.7,.7):Between(-.18,.18)),size=(.045+.05*k)*(i?.6:1)*Between(.85,1.15),inset=size*Between(.2,.9);const auto p=EdgePoint(a,inset);const int arms=6+int(Random()*4),sats=5+int(Random()*5);lens_.splat(0,p.x,p.y,size,aspect,arms,sats,1.5+k*.9);}
}
void ScreenEffectsController::Flood(double dt){
    auto& s=state_;if(s.floodMode.empty()){s.flood=std::max(0.,s.flood-dt*2);s.hole=0;return;}s.floodTime+=dt;const double t=s.floodTime;
    if(s.floodMode=="in"){s.flood=t<.24?easeInOut(t/.24):t<.5?1:1+(.17-1)*easeOut((t-.5)/.8);s.floodDrip=std::min(.4,t<.5?.05:.05+(t-.5)*.08);}
    else if(s.floodMode=="reveal"){s.flood=1;s.floodDrip=.05;const double k=clamp((t-.1)/.72);s.hole=t<.1?.0001:(std::pow(k,1.7)*.75+easeOut(k)*.25)*1.32;if(t>.86){s.floodMode.clear();s.flood=0;s.hole=0;}}
    else if(s.floodMode=="fadeout"){s.flood=std::max(0.,s.flood-dt*2.5);if(s.flood<=0)s.floodMode.clear();}
}
ScreenEffects ScreenEffectsController::Update(double dt,const ScreenEffectsInput& input){
    if(!std::isfinite(dt)||dt<0||dt>.25||input.width<=0||input.height<=0)throw std::invalid_argument("Invalid screen FX step");
    input_=input;auto& s=state_;const double aspect=double(input.width)/input.height;
    if(!input.paused){
        s.time+=dt;const bool alive=input.alive;
        if(s.damageTime>0){s.damageTime-=dt;if(s.damageTime<=0){if(s.damage>0&&alive)DamageSplat(s.damage);s.damage=0;s.hasAttacker=false;}}
        const double hs=alive?input.horizontalSpeed:0;const std::string form=alive?input.form:"kid";const bool swimming=form=="swim"||form=="climb";
        double speed=swimming?clamp((hs-6.5)/5.3)*.55:0,stretch=swimming?clamp((hs-7)/4.8)*.045:0;
        const bool flight=alive&&input.jumpPhase=="flight",chargeJump=alive&&input.jumpPhase=="charge";
        if(flight){const double k=clamp(input.jumpTime/(input.jumpDuration>0?input.jumpDuration:1.2)),c=std::abs(std::cos(k*3.141592653589793));speed=.55+.45*c;stretch=.06*(.4+.6*c);}
        s.speed=damp(s.speed,speed,speed>s.speed?5:3,dt);s.stretch=damp(s.stretch,stretch,4,dt);if(s.wasJump&&!flight&&alive)Land();s.wasJump=flight;s.jumpCharge=chargeJump?std::min(1.,s.jumpCharge+dt/.75):std::max(0.,s.jumpCharge-dt*3);
        s.swim=damp(s.swim,alive&&form=="swim"?1:0,7,dt);
        if(alive&&s.lastForm=="swim"&&form!="swim"&&form!="climb"&&(hs>7||input.verticalSpeed>4)&&s.emergeTime<=0){s.emergeTime=.6;const int n=2+int(Random()*3);for(int i=0;i<n;i++){const double x=Random()<.5?Between(.04,.3):Between(.7,.96),y=Between(.03,.22),r=Between(.008,.016),slide=Between(.05,.2),life=Between(.45,.8);lens_.droplet(1,x,y,r,slide,life);}}
        s.emergeTime-=dt;s.lastForm=form;const bool enemy=alive&&input.onEnemy;s.edge=damp(s.edge,enemy?1:0,enemy?9:3,dt);
        if(enemy&&hs>.8){s.stepTime-=dt*(.6+hs/3);if(s.stepTime<=0){s.stepTime=Between(.22,.4);const double x=Random()<.5?Between(.03,.34):Between(.66,.97),y=Between(.02,.14),r=Between(.012,.024),slide=Between(.1,.4),life=Between(.6,1.1);lens_.droplet(0,x,y,r,slide,life);}}
        if(alive)for(const auto& cloud:input.clouds){const auto d=cloud.position-input.actorPosition;if(d.x*d.x+d.z*d.z<3.6*3.6&&cloud.live){s.rainTime-=dt;if(s.rainTime<=0){s.rainTime=Between(.05,.12);double u=Random(),v=Between(.15,1);if(std::abs(u-.5)<.16&&std::abs(v-.5)<.2)u+=.3*(u>=.5?1:-1);const double r=Between(.008,.02),slide=Between(.02,.2),life=Between(.5,1.1);lens_.droplet(cloud.own?1:0,clamp(u,.02,.98),v,r,slide,life);}break;}}
        const double hp=alive?clamp(input.hp/100):1,low=alive?clamp((.5-hp)/.38):0;s.hurt=damp(s.hurt,low,4,dt);
        if(low>0){s.heartPhase=std::fmod(s.heartPhase+dt*(80+70*low)/60,1);s.heart=low*(std::exp(-std::pow((s.heartPhase-.04)/.05,2))+.7*std::exp(-std::pow((s.heartPhase-.24)/.055,2)));}else s.heart=damp(s.heart,0,6,dt);
        s.auraPulse=std::max(0.,s.auraPulse-dt*1.4);const double aura=std::max({alive&&input.specialActive?.75:0,s.jumpCharge*.9,s.auraPulse*.9});s.aura=damp(s.aura,aura,aura>s.aura?10:2.5,dt);
        const double charge=alive&&input.charging?input.charge:0;s.focus=damp(s.focus,charge>0?.25+.75*charge:0,8,dt);if(charge>=.999&&!s.fullCharge){s.fullCharge=true;s.chargePulse=1;}if(charge<.999)s.fullCharge=false;s.chargePulse=std::max(0.,s.chargePulse-dt*2.4);
        s.shimmer=damp(s.shimmer,alive&&input.invulnerability>0&&input.phase=="playing"?.7:0,5,dt);s.urgency=std::max(input.phase=="playing"&&input.remaining<=10.2?s.urgencyBase:0,s.urgency-dt*1.6);if(input.phase!="playing")s.urgencyBase=0;
        const double desat=input.phase=="finish"?.45:!s.floodMode.empty()&&s.floodMode!="reveal"&&s.floodMode!="fadeout"?.62:0;s.desaturation=damp(s.desaturation,desat,5,dt);s.saturation=damp(s.saturation,input.phase=="finish"?.92:1,4,dt);s.saturationPop=std::max(0.,s.saturationPop-dt*2.2);
        s.kill=std::max(0.,s.kill-dt*2.4);s.flash=std::max(0.,s.flash-dt*(s.flash>.6?4.5:2.6));s.chroma=std::max(0.,s.chroma-dt*3.2);s.blast=std::max(0.,s.blast-dt*2.4);s.blastTime+=dt;
        const int steps=std::max(1,int(std::ceil(16*dt/.12)));const double h=dt/steps;for(int i=0;i<steps;i++){s.punchV+=(-256*s.punch-32*s.punchV)*h;s.punch+=s.punchV*h;}if(s.jumpCharge>0)s.punch+=( -.012*s.jumpCharge-s.punch)*.2;
        Flood(dt);lens_.update(dt,aspect);
    }
    auto& f=output_;const double intensity=clamp(input.cameraShake)*(input.reducedMotion?.35:1);
    f.enabled=!lens_.particles().empty()||s.speed>.002||s.stretch>.0005||std::abs(s.punch)>.0004||s.blast>.002||s.chroma>.004||s.edge>.002||s.aura>.002||s.heart>.002||s.hurt>.002||s.urgency>.002||s.swim>.002||s.focus>.002||s.chargePulse>.002||s.shimmer>.002||s.kill>.002||s.flash>.002||s.desaturation>.002||std::abs(s.saturation-1)>.002||s.saturationPop>.002||s.flood>.001;
    f.time=float(s.time);f.speed=float(s.speed*intensity);f.stretch=float(s.stretch*intensity);f.punch=float(clamp(s.punch,-.08,.08)*intensity);f.blast=float(s.blast*intensity);f.blastRing=float(easeOut(s.blastTime/.55)*.9);f.chroma=float(std::min(1.5,s.chroma)*intensity);
    f.edgeInk=rgba(input.enemyColor,s.edge);f.aura=rgba(input.ownColor*1.6,s.aura*(.55+.45*intensity));f.heart=rgba(glm::mix(input.enemyColor,glm::dvec3(.55,0,.04),.55),s.heart);f.hurt=float(s.hurt);f.urgency=rgba({1,.16,.05},s.urgency);f.swim=rgba(input.ownColor,s.swim);f.focus=float(s.focus);f.charge=rgba(input.ownColor+.5,s.chargePulse*intensity);f.shimmer=rgba(input.ownColor+.3,s.shimmer);f.kill=rgba(input.ownColor*1.3,s.kill*(.5+.5*intensity));f.flash=rgba({1,1,1},s.flash*(.3+.7*intensity));f.desaturation=float(s.desaturation);f.saturation=float(s.saturation+s.saturationPop*.28);f.flood.w=float(s.flood);f.floodDrip=float(s.floodDrip);f.hole=float(s.hole);f.lensColorA=input.enemyColor;f.lensColorB=input.ownColor;f.speedTint=glm::mix(input.ownColor,glm::dvec3(1),.6);return f;
}
}
