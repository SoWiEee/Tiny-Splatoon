// Included inside restoration after WorldEffects::Update.
void WorldEffects::weaponHooks(double dt,CombatWorld& combat,glm::dvec3 camera){
    if(!(dt>0))return;
    const auto near=[&](glm::dvec3 p,double r){return glm::length2(p-camera)<r*r;};
    WorldFxRippleSink ripple;if(paint_)ripple=[this](glm::dvec3 p,double a,double w,double s,double l){paint_->requestVisualRipple({p,a,w,s,l});};
    const auto emit=[&](std::string_view name,glm::dvec3 p,glm::dvec3 n,int team,double k=0,glm::dvec3 direction=glm::dvec3(0,0,1)){
        auto particles=BuildWorldFxRecipe(name,p,n,ink(team),k,random_,-1.6,camera,ripple,{},direction,time_,dt);
        for(auto& particle:particles)spawn(std::move(particle));
    };
    for(auto it=weaponActors_.begin();it!=weaponActors_.end();){
        if(std::find(combat.actors().begin(),combat.actors().end(),it->first)==combat.actors().end())it=weaponActors_.erase(it);else ++it;
    }
    for(const auto* actor:combat.actors()){
        if(!actor)continue;auto& old=weaponActors_[actor];const auto& state=actor->state;
        if(!state.alive||state.squid){old.full=old.spinFull=false;continue;}
        const auto pose=actor->combatPose();const auto kind=actor->weapon().kind;
        const auto muzzle=actor->muzzle();
        if(pose.charging&&(kind=="charger"||kind=="spinner")&&near(state.pos,36)){
            emit("chargeGlow",muzzle,{0,1,0},actor->team(),pose.charge);
            if(pose.charge>=.999&&!old.full){old.full=true;emit("chargeFull",muzzle,{0,1,0},actor->team());}
        }else old.full=false;
        if(kind=="charger"&&pose.charging&&near(muzzle,50)){
            const auto fallback=actor->aimDirection();const auto delta=actor->input.aimPoint-muzzle;
            const auto direction=actor->input.hasAimPoint&&glm::length(delta)>=2&&glm::dot(delta,fallback)>=0?glm::normalize(delta):fallback;
            const double range=actor->weapon().number("rangeMin")+(actor->weapon().number("rangeMax")-actor->weapon().number("rangeMin"))*pose.charge;
            const auto hit=combat.geometry().raycast(muzzle,direction,range,true);
            if(hit.hit)emit("laserDot",hit.point,hit.normal,actor->team(),pose.charge);
        }
    }
    int budget=48;
    const auto& shots=combat.projectiles();
    std::vector<std::uint64_t> liveIds;liveIds.reserve(shots.size());
    for(const auto& p:shots){
        if(!p.owner||p.delay>p.age||(p.kind!="shot"&&p.kind!="blast"&&p.kind!="drop"&&p.kind!="slosh"))continue;
        liveIds.push_back(p.id);auto& distance=weaponTrailDistance_[p.id];distance+=glm::length(p.vel)*dt;
        const double step=p.kind=="blast"?.55:p.kind=="drop"?1.1:p.kind=="slosh"?1.:.8;
        if(distance>=step){distance=0;
            if(budget>0&&p.age>.03&&near(p.pos,30)){
                --budget;emit(p.kind=="slosh"?"sloshTrail":"shotTrail",p.pos,p.vel,p.owner->team(),p.kind=="blast"?1:0,p.vel);
            }
        }
    }
    for(auto it=weaponTrailDistance_.begin();it!=weaponTrailDistance_.end();){if(std::find(liveIds.begin(),liveIds.end(),it->first)==liveIds.end())it=weaponTrailDistance_.erase(it);else ++it;}
    for(const auto& e:lastEvents_)if(e.kind=="beam"){
        const auto end=e.pos+e.normal*e.radius;
        if(!near(e.pos,60)&&!near(end,40))continue;
        emit("beamTrail",e.pos,end,e.team,e.charge);
        const auto hit=combat.geometry().raycast(end-e.normal*.3,e.normal,.6,true);
        if(hit.hit)emit("beamImpact",hit.point,hit.normal,e.team,e.charge);
    }
}
