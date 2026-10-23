void WorldEffects::kitHooks(double seconds,CombatWorld& combat,glm::dvec3 camera){
    const double dt=std::clamp(seconds,0.0,0.05);
    const double waterY=PlayerTuning().number("waterY",-1.6);
    const auto near=[&](glm::dvec3 pos,double distance){return glm::length2(pos-camera)<distance*distance;};
    WorldFxRippleSink ripple;
    if(paint_)ripple=[this](glm::dvec3 pos,double amplitude,double width,double speed,double life){
        paint_->requestVisualRipple({pos,amplitude,width,speed,life});
    };
    const auto recipe=[&](std::string_view name,glm::dvec3 pos,glm::dvec3 normal,glm::dvec3 color,
                          double radius,glm::dvec3 direction,const WorldFxRecipeContext& context){
        auto particles=BuildWorldFxRecipe(name,pos,normal,color,radius,random_,waterY,camera,ripple,context,direction,time_,dt);
        for(auto& particle:particles)spawn(std::move(particle));
    };
    const auto simpleRecipe=[&](std::string_view name,glm::dvec3 pos,glm::dvec3 normal,glm::dvec3 color,double radius){
        recipe(name,pos,normal,color,radius,glm::dvec3(0,0,1),WorldFxRecipeContext{});
    };
    const auto directedRecipe=[&](std::string_view name,glm::dvec3 pos,glm::dvec3 normal,glm::dvec3 color,
                                  double radius,glm::dvec3 direction){
        recipe(name,pos,normal,color,radius,direction,WorldFxRecipeContext{});
    };
    const auto burst=[&](glm::dvec3 pos,glm::dvec3 normal,glm::dvec3 color,int count,double speed,double size,
                         double spread,bool sheet,bool ringEffect,bool mistEffect){
        WorldFxRecipeContext options;options.count=count;options.speed=speed;options.size=size;options.spread=spread;
        options.paint=false;options.sheet=sheet;options.ring=ringEffect;options.mist=mistEffect;
        recipe("burst",pos,normal,color,size,glm::dvec3(0,0,1),options);
    };
    const auto canopyBreak=[&](glm::dvec3 pos,glm::dvec3 normal,glm::dvec3 color){
        burst(pos,normal,color,22,5.2,0.11,1.6,true,false,false);
        burst(pos,-normal,glm::dvec3(0.9568627451,0.937254902,0.8941176471),10,3.5,0.08,1.4,true,false,false);
        simpleRecipe("canopyBreakRing",pos,normal,color,0.84);
    };
    const auto findWallNormal=[&](glm::dvec3 point,glm::dvec3 fallback){
        const glm::dvec3 origin=point+glm::dvec3(0,1,0);
        double best=1.0e9;
        glm::dvec3 result=fallback;
        for(int i=0;i<16;++i){
            const double angle=(double(i)/16.0)*6.2831853071795864769;
            const glm::dvec3 direction(std::sin(angle),0,std::cos(angle));
            const auto hit=combat.geometry().raycast(origin,direction,1.25,true);
            if(hit.hit&&std::abs(hit.normal.y)<0.5&&hit.dist<best){best=hit.dist;result=hit.normal;}
        }
        return result;
    };

    bool heldBrollyBroke=false;
    const auto& actors=combat.actors();
    const auto shields=combat.shields();
    for(const auto* actor:actors){
        if(!actor)continue;
        const auto pose=actor->combatPose();
        const auto color=glm::dvec3(ink(actor->team()));
        auto& state=kitActors_[actor];

        if(actor->mainId()=="mitts"){
            if(state.mittsLeapEvents<0){
                state.mittsLeapEvents=pose.mittsLeapEvents;
                state.mittsLeaping=pose.leaping;
                state.mittsLandTime=pose.mittsLandTime;
                state.mittsWasClimbing=actor->state.climbing;
                if(actor->state.climbing)state.mittsWallNormal=findWallNormal(actor->state.pos,state.mittsWallNormal);
            }else{
                if(pose.charging&&near(actor->state.pos,26)){
                    const double k=std::clamp(pose.charge,0.0,1.0);
                    simpleRecipe("chargeGlow",actor->muzzle(0),glm::dvec3(0,1,0),color,0.3+0.7*k);
                    simpleRecipe("chargeGlow",actor->muzzle(1),glm::dvec3(0,1,0),color,0.3+0.7*k);
                }
                if(pose.mittsLeapEvents!=state.mittsLeapEvents){
                    const bool fromWall=state.mittsWasClimbing;
                    if(near(actor->state.pos,40)){
                        if(fromWall){
                            const auto normal=state.mittsWallNormal;
                            burst(actor->state.pos+glm::dvec3(0,1,0),normal,color,12,4,0.08,0.9,true,false,false);
                        }else{
                            burst(actor->state.pos+glm::dvec3(0,0.08,0),glm::dvec3(0,1,0),color,14,5,0.08,0.7,true,false,false);
                            simpleRecipe("leapRing",actor->state.pos,glm::dvec3(0,1,0),color,1.3);
                        }
                    }
                }
                if(pose.leaping&&near(actor->state.pos,34))
                    directedRecipe("superJumpTrail",actor->state.pos+glm::dvec3(0,0.8,0),glm::dvec3(0,1,0),color,0,actor->state.vel);
                if(actor->state.climbing){
                    state.mittsWallNormal=findWallNormal(actor->state.pos,state.mittsWallNormal);
                    if(near(actor->state.pos,20)&&random()<dt*5.0){
                        const glm::dvec3 clingPoint=actor->state.pos-state.mittsWallNormal*0.38+glm::dvec3(0,1,0);
                        simpleRecipe("climbDrip",clingPoint,state.mittsWallNormal,color,0.035);
                    }
                }
                const bool landed=pose.mittsLandTime+1e-6<state.mittsLandTime&&pose.mittsLandTime<0.08;
                if(landed&&near(actor->state.pos,40)){
                    glm::dvec3 at=actor->state.pos,normal=actor->state.grounded?glm::dvec3(0,1,0):state.mittsWallNormal;
                    if(actor->state.grounded){
                        const auto hit=combat.geometry().raycast(at+glm::dvec3(0,0.2,0),glm::dvec3(0,-1,0),0.7,true);
                        if(hit.hit){at=hit.point;normal=hit.normal;}
                    }else at=actor->state.pos-state.mittsWallNormal*0.38+glm::dvec3(0,1,0);
                    const auto center=at+normal*0.35;
                    simpleRecipe("explosion",center,normal,color,1.9);
                    if(normal.y>0.7)simpleRecipe("superJumpLand",at,normal,color,0);
                    else directedRecipe("ring",at,normal,color,GetEquipment(EquipmentCategory::Main,"mitts").number("landRadius"),glm::dvec3(0,0.45,0));
                }
                state.mittsLeapEvents=pose.mittsLeapEvents;
                state.mittsLeaping=pose.leaping;
                state.mittsLandTime=pose.mittsLandTime;
                state.mittsWasClimbing=actor->state.climbing;
            }
        }else{
            state.mittsLeapEvents=pose.mittsLeapEvents;
            state.mittsLeaping=pose.leaping;
            state.mittsLandTime=pose.mittsLandTime;
            state.mittsWasClimbing=actor->state.climbing;
        }

        if(actor->mainId()=="brolly"){
            bool hasHeldShield=false;
            for(const auto& shield:shields){
                if(shield.owner==actor&&shield.held&&shield.live){
                    state.canopyCenter=shield.C;state.canopyNormal=shield.N;hasHeldShield=true;break;
                }
            }
            const bool broke=state.canopyHp>0&&pose.canopyHp<=0&&pose.canopyState=="broken";
            if(broke&&!hasHeldShield){
                const glm::dvec3 normal=actor->aimDirection();
                state.canopyCenter=actor->state.pos+glm::dvec3(0,actor->state.smoothY+0.9,0)+normal*0.62;
                state.canopyNormal=normal;
            }
            if(broke&&near(state.canopyCenter,40)){
                canopyBreak(state.canopyCenter,state.canopyNormal,color);
                heldBrollyBroke=true;
            }
            if(!hasHeldShield&&state.canopyHp<=0&&pose.canopyState=="broken"){
                const glm::dvec3 normal=actor->aimDirection();
                state.canopyCenter=actor->state.pos+glm::dvec3(0,actor->state.smoothY+0.9,0)+normal*0.62;
                state.canopyNormal=normal;
            }
            state.canopyHp=pose.canopyHp;
            state.canopyState=pose.canopyState;
        }else{
            state.canopyHp=pose.canopyHp;
            state.canopyState=pose.canopyState;
        }
    }

    for(const auto& event:lastEvents_){
        if(!near(event.pos,45))continue;
        const auto color=glm::dvec3(ink(event.team));
        if(event.kind=="bow_lodge"){
            burst(event.pos,event.normal,color,5,2.4,0.06,0.9,false,false,false);
        }else if(event.kind=="bow_impact"){
            const bool wall=event.charge<=0;
            burst(event.pos,event.normal,color,wall?5:6,3,0.07,0.9,false,false,false);
        }else if(event.kind=="bow_burst"){
            simpleRecipe("explosion",event.pos,event.normal,color,event.radius);
        }else if(event.kind=="blade_wave_hit"){
            burst(event.pos,event.normal,color,12,4.5,0.09,0.9,true,false,false);
        }else if(event.kind=="blade_wave_wall"){
            burst(event.pos,event.normal,color,16,5,0.1,0.9,true,false,true);
        }else if(event.kind=="device_hit"&&(event.source=="tracer"||event.source=="tracer_trail")){
            if(event.sourceVictimLocal&&event.pos.x==0&&event.pos.y==0&&event.pos.z==0)continue;
            simpleRecipe("glint",event.pos,event.normal,color,event.source=="tracer_trail"?0.1:0.14);
        }else if(event.kind=="device_destroy"&&event.source=="torpedo"){
            if(event.pos.y<=waterY+0.2)
                simpleRecipe("waterPlop",glm::dvec3(event.pos.x,waterY-0.02,event.pos.z),glm::dvec3(0,1,0),glm::dvec3(1),0.4);
            else burst(event.pos,glm::dvec3(0,1,0),color,12,4,0.08,0.9,true,false,true);
        }else if(event.kind=="device_destroy"&&event.source=="brolly"&&event.id!=0&&!heldBrollyBroke){
            canopyBreak(event.pos,event.normal,color);
        }
    }

    const auto& devices=combat.devices();
    for(const auto& device:devices){
        if(!device.live)continue;
        const auto key=device.id^(std::uint64_t(device.networkOwner)<<32);
        auto& state=kitDevices_[key];
        if(device.kind=="torpedo"&&device.state=="launch"&&near(device.pos,26)){
            const auto direction=glm::length(device.vel)>1e-8?glm::normalize(device.vel):device.normal;
            const auto at=device.pos-direction*0.45;
            directedRecipe("bombTrail",at,-direction*device.speed,glm::dvec3(ink(device.team)),0,-direction*device.speed);
        }else if(device.kind=="brolly"&&device.state=="launched"&&near(device.pos,28)&&time_>=state.nextFxTime){
            state.nextFxTime=time_+0.22;
            const auto direction=glm::length(device.N)>1e-8?glm::normalize(device.N):glm::dvec3(0,0,1);
            const glm::dvec3 canopyNormal=direction+glm::dvec3(0,0.8,0);
            const glm::dvec3 normal=glm::length2(canopyNormal)>1e-12?glm::normalize(canopyNormal):glm::dvec3(0,1,0);
            burst(device.pos+direction*0.05,normal,glm::dvec3(ink(device.team)),3,1.8,0.05,0.9,false,false,false);
        }
        state.previousPos=device.pos;state.initialized=true;
    }

    for(auto it=kitActors_.begin();it!=kitActors_.end();){
        if(std::find(actors.begin(),actors.end(),it->first)==actors.end())it=kitActors_.erase(it);else ++it;
    }
    for(auto it=kitDevices_.begin();it!=kitDevices_.end();){
        const auto found=std::find_if(devices.begin(),devices.end(),[&](const auto& device){
            return device.live&&(device.id^(std::uint64_t(device.networkOwner)<<32))==it->first;
        });
        if(found==devices.end())it=kitDevices_.erase(it);else ++it;
    }
}
