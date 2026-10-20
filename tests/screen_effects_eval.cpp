#include "../src/render/ScreenEffectsController.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
int main(){
 using namespace restoration;std::cout<<std::setprecision(17);std::string line;
 while(std::getline(std::cin,line)){
  if(line=="reset"){continue;}
  unsigned seed=0x195016;ScreenEffectsController fx([&]{seed=seed*1664525u+1013904223u;return seed/4294967296.;});
  do{
   if(line=="end")break;
   ScreenEffectsInput in;double dt;std::string action;int alive,enemy,special,charging,paused,reduced,rain;std::istringstream row(line);
   if(!(row>>dt>>alive>>in.hp>>in.horizontalSpeed>>in.verticalSpeed>>in.form>>enemy>>special>>charging>>in.charge>>in.invulnerability>>in.remaining>>in.phase>>in.jumpPhase>>in.jumpTime>>in.jumpDuration>>paused>>reduced>>rain>>action))return 2;
   if(in.jumpPhase=="-")in.jumpPhase.clear();in.alive=alive;in.onEnemy=enemy;in.specialActive=special;in.charging=charging;in.paused=paused;in.reducedMotion=reduced;in.cameraPosition={0,0,8};in.viewProjection=glm::perspective(glm::radians(60.),16./9,.1,1000.)*glm::lookAt(in.cameraPosition,glm::dvec3(0),glm::dvec3(0,1,0));
   if(rain)in.clouds.push_back({{0,4,0},false,true});
   // Prime non-advancing context before source events (camera and palette are read by handlers).
   const bool wasPaused=in.paused;in.paused=true;fx.Update(0,in);in.paused=wasPaused;
   if(action=="damage")fx.Damage(55);if(action=="kill")fx.Kill();if(action=="water")fx.Splat(true);if(action=="flood")fx.Splat();if(action=="respawn")fx.Respawn();if(action=="special")fx.Special("storm");if(action=="flight")fx.Jump("flight");if(action=="blast")fx.Blast({0,0,0},1,in.ownColor);if(action=="count")fx.Countdown(9);
   if(action.rfind("impact",0)==0)fx.Impact({0,0,6.9},action=="impact_own",action=="impact_roll");if(action=="reset")fx.Reset();if(action=="go")fx.MatchState("playing");
   const auto v=fx.Update(dt,in);std::cout<<'[';bool comma=false;auto number=[&](double x){if(comma)std::cout<<',';std::cout<<x;comma=true;};auto vector=[&](glm::vec4 x){for(int i=0;i<4;i++)number(x[i]);};
   for(double x:{double(v.speed),double(v.stretch),double(v.punch),double(v.blast),double(v.blastRing),double(v.chroma)})number(x);
   vector(v.edgeInk);vector(v.aura);vector(v.heart);number(v.hurt);vector(v.urgency);vector(v.swim);number(v.focus);vector(v.charge);vector(v.shimmer);vector(v.kill);vector(v.flash);number(v.desaturation);number(v.saturation);vector(v.flood);number(v.floodDrip);number(v.hole);number(v.floodClear);
   std::cout<<"],"<<'[';bool separator=false;for(const auto* p:fx.lens().particles()){if(separator)std::cout<<',';separator=true;std::cout<<'[';comma=false;number(int(p->kind));number(p->channel);for(double x:{p->x,p->y,p->r,p->rx,p->ry,p->rotation,p->intensity,p->initialIntensity,p->life,p->stick,p->pop,p->grow,p->vx,p->vy,p->age,p->trail,p->hold,p->seed})number(x);std::cout<<']';}std::cout<<"]\n";
  }while(std::getline(std::cin,line));
 }
}
