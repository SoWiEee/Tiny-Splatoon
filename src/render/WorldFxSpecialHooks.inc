// Included inside restoration after WorldEffects::Update.
void WorldEffects::specialHooks(double dt,CombatWorld& combat,glm::dvec3 camera){
    if(!(dt>0))return;
    const auto near=[&](glm::dvec3 p,double r){return glm::length2(p-camera)<r*r;};
    WorldFxRippleSink ripple;
    if(paint_)ripple=[this](glm::dvec3 p,double a,double w,double s,double l){paint_->requestVisualRipple({p,a,w,s,l});};
    const auto emit=[&](std::string_view name,glm::dvec3 p,glm::dvec3 n,int team,double radius=0.0,glm::dvec3 direction=glm::dvec3(0,0,1)){
        auto particles=BuildWorldFxRecipe(name,p,n,ink(team),radius,random_,-1.6,camera,ripple,{},direction,time_,dt);
        for(auto& item:particles)spawn(std::move(item));
    };
    const auto actorKey=[](const PlayerSimulation& actor){return DeviceFxKey{actor.networkOwner()^0x80000000u,actor.networkOwner()?actor.networkNid():static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&actor))};};
    std::vector<DeviceFxKey> active;
    active.reserve(combat.visualObjects().size()+combat.actors().size());

    // Ink Tempest cloud callbacks are source-phase driven. The cloud mesh and
    // its ordinary rain drops are owned by WorldSpecials/DeviceRenderer.
    for(const auto& object:combat.visualObjects()){
        if(object.kind!="stormCloud"&&object.kind!="speaker")continue;
        const DeviceFxKey key{object.ownerNid,object.id};active.push_back(key);
        if(!near(object.pos,object.kind=="stormCloud"?45.0:40.0))continue;
        auto& state=specialFx_[key];
        if(!state.initialized){state.initialized=true;state.puddleT=0;state.flashT=0.5;state.pulseT=0;}
        if(object.kind=="stormCloud"){
            const double fade=std::clamp((object.timer-object.age)/0.6,0.0,1.0);
            const auto radius=GetEquipment(EquipmentCategory::Special,"storm").number("radius");
            if(object.age>0.35&&fade>0.3)emit("rainSheet",object.pos,{0,1,0},object.team,radius,std::abs(object.direction.y)>0.01?object.direction:glm::dvec3(0,0,1));
            state.puddleT+=dt*16.0*fade;
            while(state.puddleT>=1.0){
                state.puddleT-=1.0;
                const double angle=random()*2*pi,r=std::sqrt(double(random()))*radius*0.95;
                const glm::dvec3 origin(object.pos.x+std::cos(angle)*r,object.pos.y-0.6,object.pos.z+std::sin(angle)*r);
                const auto ground=combat.geometry().raycast(origin,{0,-1,0},14,true);
                if(ground.hit)emit("stormPuddle",ground.point,ground.normal,object.team);
            }
            state.flashT-=dt;
            if(state.flashT<=0){state.flashT=0.5+random()*1.2;emit("stormFlash",object.pos,{0,1,0},object.team,radius);}
        }else if(object.phase=="blast"&&random()<dt*30.0){
            const auto at=object.mouth+object.direction*(1.0+random()*6.0);
            emit("wailRing",at,object.direction,object.team,object.radius*1.3,object.direction);
        }
    }

    // Jetpack has two source nozzle bursts on a boost edge and a restrained
    // two-particle exhaust at 20 Hz. Use the animated tank attachment and the
    // source prop's exact local nozzle coordinates.
    const auto eachNozzle=[](const PlayerSimulation& actor,const auto& fn){
        const auto tank=actor.tankWorld();
        fn(glm::dvec3(tank*glm::dvec4(-0.138,-0.224,-0.012,1.0)));
        fn(glm::dvec3(tank*glm::dvec4( 0.138,-0.224,-0.012,1.0)));
    };
    for(const auto* actor:combat.actors()){
        if(!actor)continue;
        const auto pose=actor->specialPose();
        if(!pose.active||pose.kind!="jetpack")continue;
        const auto key=actorKey(*actor);active.push_back(key);
        auto& state=specialFx_[key];
        if(!state.initialized){state.initialized=true;state.puddleT=0;state.flashT=0.5;state.pulseT=0;}
        if(near(actor->state.pos,30)){
            state.pulseT-=dt;
            if(state.pulseT<=0){
                state.pulseT+=0.05;
                eachNozzle(*actor,[&](const auto& at){emit("jetpackExhaust",at,{0,-1,0},actor->team());});
            }
        }
    }

    for(const auto& event:lastEvents_){
        if(event.kind=="bubble_pop"&&event.source=="blower"&&near(event.pos,40)){
            emit("bubblePop",event.pos,{0,1,0},event.team,event.radius);
        }else if(event.kind=="special_audio"&&event.source=="shield_pop"&&near(event.pos,40)){
            emit("shieldPop",event.pos+glm::dvec3(0,0.9,0),{0,1,0},event.team);
        }else if(event.kind=="special_audio"&&event.source=="zip_pull"&&near(event.pos,40)){
            const PlayerSimulation* owner=nullptr;double best=2.25;
            for(const auto* actor:combat.actors()){
                if(!actor||actor->team()!=event.team)continue;
                const auto pose=actor->specialPose();if(!pose.active||pose.kind!="jetpack")continue;
                const double d2=glm::length2(actor->state.pos-event.pos);if(d2<best){best=d2;owner=actor;}
            }
            if(owner)eachNozzle(*owner,[&](const auto& at){emit("jetpackBoost",at,{0,-1,0},owner->team());});
        }
    }
    for(auto it=specialFx_.begin();it!=specialFx_.end();){
        if(std::find(active.begin(),active.end(),it->first)==active.end())it=specialFx_.erase(it);else ++it;
    }
}
