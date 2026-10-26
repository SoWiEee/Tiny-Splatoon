// Included inside restoration after WorldEffects::Update.
void WorldEffects::weaponDeviceHooks(double dt,CombatWorld& combat,glm::dvec3 camera,const std::vector<WorldVisualEvent>& events){
    if(!(dt>0))return;
    const auto near=[&](glm::dvec3 p,double range){return glm::length2(p-camera)<range*range;};
    WorldFxRippleSink ripple;
    if(paint_)ripple=[this](glm::dvec3 p,double a,double w,double s,double l){paint_->requestVisualRipple({p,a,w,s,l});};
    const auto emit=[&](std::string_view name,glm::dvec3 pos,glm::dvec3 normal,int team,double radius,
                        const WorldFxRecipeContext& context,glm::dvec3 direction){
        auto particles=BuildWorldFxRecipe(name,pos,normal,ink(team),radius,random_,-1.6,camera,ripple,context,direction,time_,dt);
        for(auto& particle:particles)spawn(std::move(particle));
    };

    // Failed shots are emitted by WeaponRunner::spend, so preserve the source
    // event and its muzzle/aim sample instead of inferring failure from input.
    for(const auto& event:events){
        if(event.kind!="lowink"||!near(event.pos,40))continue;
        const PlayerSimulation* sourceActor=nullptr;
        double closest=std::numeric_limits<double>::infinity();
        for(const auto* actor:combat.actors()){
            if(!actor||!(actor->mainId()==event.source||actor->subId()==event.source))continue;
            const double distance=glm::length2(actor->muzzle()-event.pos);
            if(distance<closest){closest=distance;sourceActor=actor;}
        }
        if(!sourceActor)continue;
        auto& state=weaponActors_[sourceActor];
        if(time_-state.dryT<0.4)continue;
        state.dryT=time_;
        emit("dryFire",event.pos,event.normal,event.team,0,{},event.normal);
    }

    for(const auto* actor:combat.actors()){
        if(!actor)continue;
        auto& state=weaponActors_[actor];
        const int shots=actor->shotsFired();
        int shotDelta=0;
        if(state.shotsFired<0||shots<state.shotsFired)state.shotsFired=shots;
        else {shotDelta=shots-state.shotsFired;state.shotsFired=shots;}

        const auto& player=actor->state;
        const auto& weapon=actor->weapon();
        const std::string& id=actor->mainId();
        const auto aim=actor->aimDirection();
        if(!player.alive||player.squid){state.dodgeTime=0;continue;}

        const int fireEvents=std::min(shotDelta,4);
        if(fireEvents>0&&(weapon.kind=="blaster"||weapon.kind=="charger")&&near(player.pos,38)){
            for(int i=0;i<fireEvents;++i){
                const auto muzzle=actor->muzzle();
                emit(weapon.kind=="blaster"?"muzzleBlaster":"muzzleCharger",muzzle,aim,actor->team(),0,{},aim);
            }
        }
        if(fireEvents>0&&(id=="roller"||id=="bucket"||id=="brush")
           &&time_-state.flickT>=0.25&&near(player.pos,34)){
            state.flickT=time_;
            const auto at=player.pos+glm::dvec3(0,1.05,0);
            const glm::dvec3 forward(std::sin(player.yaw),0,std::cos(player.yaw));
            WorldFxRecipeContext context;context.factor=id=="brush"?.6:1.0;context.speed=weapon.number(id=="bucket"?"throwSpeed":id=="brush"?"swipeSpeed":"flickSpeed",15);
            auto seed=static_cast<std::uint32_t>(shots)*0x9e3779b9u^actor->networkNid()^static_cast<std::uint32_t>(std::abs(at.x*997+at.z*313));if(!seed)seed=0x6d2b79f5u;
            auto particles=BuildWorldFxRecipe(id=="bucket"?"inkSlosh":"inkFlick",at,forward,ink(actor->team()),1,seed,-1.6,camera,{},context,glm::normalize(aim+glm::dvec3(0,id=="bucket"?.22:.10,0)));
            for(auto& particle:particles)spawn(std::move(particle));
        }

        const auto pose=actor->combatPose();
        if((id=="bucket"||id=="roller"||id=="brush")&&pose.firing&&!pose.rolling&&!state.burstFiring&&fireEvents==0&&near(player.pos,34)){
            auto seed=std::uint32_t(shots+1)*0x85ebca6bu;if(!seed)seed=1;
            auto particles=BuildWorldFxRecipe("inkAnticipation",actor->muzzle(),aim,ink(actor->team()),1,seed,-1.6,camera,{}, {},aim);
            for(auto& particle:particles)spawn(std::move(particle));
        }
        state.burstFiring=pose.firing;
        if(weapon.kind=="twins"){
            const glm::dvec3 horizontal(player.vel.x,0,player.vel.z);
            const auto forward=glm::length2(horizontal)>1e-8?glm::normalize(horizontal):glm::dvec3(std::sin(player.yaw),0,std::cos(player.yaw));
            const auto at=actor->visualPos();
            if(pose.dodgeTime>0){
                if(state.dodgeTime<=0)emit("dodgeSplash",at,forward,actor->team(),0,{},forward);
                WorldFxRecipeContext context;
                const double rollTime=weapon.number("rollTime");
                context.factor=rollTime>0?std::clamp(1.0-pose.dodgeTime/rollTime,0.0,1.0):0.0;
                emit("dodgeSkid",at,forward,actor->team(),0,context,forward);
            }else if(state.dodgeTime>0)emit("dodgePlant",at,forward,actor->team(),0,{},forward);
            state.dodgeTime=pose.dodgeTime;
        }else state.dodgeTime=0;
        const double horizontalSpeed=std::hypot(player.vel.x,player.vel.z);
        if(pose.rolling&&player.grounded&&horizontalSpeed>0.8&&near(player.pos,38)){
            const glm::dvec3 forward(std::sin(player.yaw),0,std::cos(player.yaw));
            const auto at=actor->visualPos()+forward*0.85;
            WorldFxRecipeContext context;
            context.factor=std::clamp(horizontalSpeed/weapon.number("rollSpeed",5),0.0,1.0);
            emit("rollerSpray",at,forward,actor->team(),weapon.number("rollWidth",1.1),context,forward);
        }
    }

    std::vector<DeviceFxKey> liveBombs;
    for(const auto& bomb:combat.devices()){
        if(!bomb.live||bomb.kind!="bomb")continue;
        const auto owner=bomb.networkRemote?bomb.networkOwner:(bomb.owner?bomb.owner->networkNid():0u);
        const DeviceFxKey key{owner,bomb.id};
        liveBombs.push_back(key);
        auto& state=bombFx_[key];
        if(bomb.fuse<0){
            if(near(bomb.pos,40))emit("bombTrail",bomb.pos,bomb.vel,bomb.team,0,{},bomb.vel);
            if(state.initialized&&state.previousVy< -1.5&&bomb.vel.y>0.3&&near(bomb.pos,40)){
                emit("bounceSplash",bomb.pos-glm::dvec3(0,0.2,0),{0,1,0},bomb.team,0,{},glm::dvec3(0,1,0));
            }
            state.armed=false;
        }else{
            const auto& sub=GetEquipment(EquipmentCategory::Sub,"bomb");
            if(!state.armed){
                const auto hit=combat.geometry().raycast(bomb.pos+glm::dvec3(0,0.3,0),{0,-1,0},2.5,true);
                if(hit.hit){state.ground=hit.point;state.normal=hit.normal;}
                else {state.ground=bomb.pos-glm::dvec3(0,0.2,0);state.normal={0,1,0};}
                if(near(state.ground,65))emit("bounceSplash",state.ground,state.normal,bomb.team,0,{},glm::dvec3(0,1,0));
                state.armed=true;
                state.beepT=0;
            }
            const double k=std::clamp(1.0-bomb.fuse/sub.number("fuse"),0.0,1.0);
            state.beepT-=dt;
            if(state.beepT<=0&&near(bomb.pos,65)){
                WorldFxRecipeContext context;context.factor=k;context.point=state.ground;context.hasPoint=true;
                emit("beepPulse",bomb.pos,state.normal,bomb.team,sub.number("radius"),context,glm::dvec3(0,0,1));
                state.beepT=0.3-k*0.2;
            }
        }
        state.previousVy=bomb.vel.y;
        state.initialized=true;
    }
    for(const auto& bomb:combat.visualObjects()){
        if(bomb.kind!="stormBomb")continue;
        const DeviceFxKey key{bomb.ownerNid,bomb.id};
        liveBombs.push_back(key);
        auto& state=bombFx_[key];
        if(near(bomb.pos,40))emit("bombTrail",bomb.pos,bomb.vel,bomb.team,0,{},bomb.vel);
        if(state.initialized&&state.previousVy< -1.5&&bomb.vel.y>0.3&&near(bomb.pos,40))
            emit("bounceSplash",bomb.pos-glm::dvec3(0,0.2,0),{0,1,0},bomb.team,0,{},glm::dvec3(0,1,0));
        state.armed=false;state.previousVy=bomb.vel.y;state.initialized=true;
    }
    for(auto it=bombFx_.begin();it!=bombFx_.end();){
        if(std::find(liveBombs.begin(),liveBombs.end(),it->first)==liveBombs.end())it=bombFx_.erase(it);
        else ++it;
    }
}
