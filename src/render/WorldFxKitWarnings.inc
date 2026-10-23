// Active kit warnings are independent of their solid models.
void WorldEffects::warningHooks(double dt,CombatWorld& combat,glm::dvec3 camera){
    if(!(dt>0))return;
    const auto near=[&](glm::dvec3 p,double r){return glm::length2(p-camera)<r*r;};
    WorldFxRippleSink ripple;if(paint_)ripple=[this](glm::dvec3 p,double a,double w,double s,double l){paint_->requestVisualRipple({p,a,w,s,l});};
    const auto emit=[&](std::string_view name,glm::dvec3 p,glm::dvec3 n,int team,double radius,WorldFxRecipeContext context=WorldFxRecipeContext{}){
        auto particles=BuildWorldFxRecipe(name,p,n,ink(team),radius,random_,-1.6,camera,ripple,context,n,time_,dt);
        for(auto& item:particles)spawn(std::move(item));
    };
    const auto drop=[&](glm::dvec3 p,glm::dvec3 v,glm::vec3 color,double size,double life){
        Item item;item.shape=Shape::Drop;item.pos=glm::vec3(p);item.vel=glm::vec3(v);item.color=color;item.radius=float(size);item.life=float(life);item.gravity=1;item.stretch=1;item.flags=8;item.seed=random();item.renderRadius=item.radius;spawn(item);
    };
    const auto danger=[&](glm::dvec3 p,glm::dvec3 n,int team,double radius,double progress){
        if(world_.size()<2048)world_.push_back({Shape::Ring,glm::vec3(p),{},glm::vec3(n),ink(team),float(radius),0,float(std::clamp(progress,0.,1.)),1,.95f,3,.37f});
    };
    for(auto it=warningActors_.begin();it!=warningActors_.end();){if(std::find(combat.actors().begin(),combat.actors().end(),it->first)==combat.actors().end())it=warningActors_.erase(it);else ++it;}
    for(const auto* actor:combat.actors()){
        if(!actor||actor->subId()!="shaker")continue;
        auto& old=warningActors_[actor];const auto pose=actor->combatPose();const auto p=actor->subNozzle(),n=actor->subNozzleDirection();
        const auto level=[](double f){return f>=1?3:f>=.5?2:1;};
        if(pose.shakerCharging&&actor->state.alive&&!actor->state.squid){
            if(near(p,36)||actor->isLocal()){
                const double f=pose.shakerCharge;const auto color=glm::mix(ink(actor->team()),glm::vec3(1),.3f);
                if(level(f)>level(old.charge)){
                    if(level(f)>=3)emit("chargeFull",p,n,actor->team(),1);
                    else {WorldFxRecipeContext c;c.count=5;c.speed=2.2;c.size=.035;c.sheet=false;emit("burst",p,n,actor->team(),0,c);}
                }
                old.fizz+=dt*(4+26*f+(f>=1?22:0));
                if(old.fizz>=1){
                    while(old.fizz>=1){old.fizz-=1;const double speed=1.6+random()*(1.5+2*f);
                        glm::dvec3 v(n.x*.7+(random()-.5)*.8,1.3+random()*.5,n.z*.7+(random()-.5)*.8);v=glm::normalize(v)*speed;
                        drop(p,v,color,.004+random()*.006,.5);
                    }
                    if(f>.3&&random()<dt*(3+8*f)){
                        WorldFxRecipeContext c;c.factor=.2;
                        auto items=BuildWorldFxRecipe("mist",p,{0,1,0},color,.04+.04*f,random_,-1.6,camera,ripple,c,{n.x*.5,.9,n.z*.5},time_,dt);
                        for(auto& item:items)spawn(std::move(item));
                    }
                }
            }
        }else{
            if(old.charging&&!actor->state.alive&&near(p,36)){
                WorldFxRecipeContext c;c.count=7;c.speed=1.6;c.size=.04;c.sheet=false;emit("burst",p,{0,1,0},actor->team(),0,c);
                c.factor=.3;auto items=BuildWorldFxRecipe("mist",p,{0,1,0},glm::mix(ink(actor->team()),glm::vec3(1),.4f),.12,random_,-1.6,camera,ripple,c,{0,.8,0},time_,dt);
                for(auto& item:items)spawn(std::move(item));
            }
            old.fizz=0;
        }
        old.charging=pose.shakerCharging;old.charge=pose.shakerCharge;
    }
    std::vector<DeviceFxKey> active;
    for(const auto& d:combat.devices()){
        if(!d.live||!near(d.pos,40)||(d.kind!="shaker"&&d.kind!="boomerang"))continue;
        const DeviceFxKey key{d.networkOwner,d.id};active.push_back(key);auto& old=warningObjects_[key];
        const auto& spec=GetEquipment(EquipmentCategory::Sub,d.kind);
        if(d.kind=="shaker"&&d.held){
            const auto ground=combat.geometry().raycast(d.pos+glm::dvec3(0,.3,0),{0,-1,0},3.5,true);
            if(ground.hit){const double k=d.fuse>=0?1-d.fuse/spec.number("fuse"):1-d.timer/spec.number("gap");danger(ground.point+ground.normal*.02,ground.normal,d.team,spec.number("radius"),k);}
        }
        if(d.kind=="boomerang"){
            if(d.state=="fly"||d.state=="return")emit("bombTrail",d.pos,d.vel,d.team,0);
            if(d.state=="hover"||d.state=="armed"||d.state=="orbit"){
                old.timer-=dt;if(old.timer<=0){old.timer=d.state=="orbit"?.09:.045;
                    const double angle=random()*2*pi,r=(models_?models_->VisualRadius("boomerang"):0)*.92,speed=4+random()*3;
                    const glm::dvec3 p=d.pos+glm::dvec3(std::cos(angle)*r,0,std::sin(angle)*r);
                    const glm::dvec3 v(-std::sin(angle)*speed,.6+random(),std::cos(angle)*speed);
                    drop(p,v,ink(d.team),.035+random()*.025,.5);
                }
            }
            if(d.state=="hover"||d.state=="armed"){
                const auto ground=combat.geometry().raycast(d.pos,{0,-1,0},3.4);
                if(ground.hit){const double k=d.state=="armed"?d.age/spec.number("hitFuse"):.3+.2*std::sin(d.age*10);
                    danger(ground.point+ground.normal*.03,ground.normal,d.team,spec.number(d.state=="armed"?"hitRadius":"hoverRadius"),k);}
            }
        }
    }
    for(const auto& p:combat.projectiles())if(p.owner&&p.kind=="arrow"&&p.state=="lodged"){
        const DeviceFxKey key{p.networkOwner,p.id};active.push_back(key);auto& old=warningObjects_[key];
        if(near(p.hitPoint,36))danger(p.hitPoint+p.hitNormal*.02,p.hitNormal,p.owner->team(),p.burstRadius,p.age/std::max(.001,p.fuse));
        constexpr double thresholds[]={.42,.24,.1};
        if(old.ticks<3&&p.fuse-p.age<thresholds[old.ticks]){
            ++old.ticks;if(near(p.hitPoint,30)){WorldFxRecipeContext c;c.factor=p.age/std::max(.001,p.fuse);c.hasPoint=false;emit("beepPulse",p.hitPoint+p.hitNormal*.1,p.hitNormal,p.owner->team(),p.burstRadius,c);}
        }
    }
    for(auto it=warningObjects_.begin();it!=warningObjects_.end();){if(std::find(active.begin(),active.end(),it->first)==active.end())it=warningObjects_.erase(it);else ++it;}
}
