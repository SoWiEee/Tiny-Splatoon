    struct BossFeedbackNumber {std::uint64_t id=0;glm::dvec3 point{0};double sum=0,born=0,last=0,jitter=0;bool weak=false;std::string shown;};
    struct BossFeedbackCrit {std::uint64_t id=0;glm::dvec3 point{0};double born=0;};
    struct BossFeedbackState {
        std::uint64_t matchId=0,epoch=0,hitSerial=0,nextId=0;
        double clock=-1,lastDamage=0,lastCrit=-99,titleUntil=0,phaseUntil=0;int weakHits=0,phase=1;
        bool hasBoss=false,introSeen=false;
        std::vector<BossFeedbackNumber> numbers;std::vector<BossFeedbackCrit> crits;std::string markup;
    } bossFeedback;

    bool ProjectBossFeedback(const glm::dvec3& point,double& x,double& y,bool& behind)const{
        if(!haveWorldView)return false;
        const auto size=context->GetDimensions();if(size.x<=0||size.y<=0)return false;
        const auto clip=worldView*glm::vec4(glm::vec3(point),1.f);
        if(!std::isfinite(clip.x)||!std::isfinite(clip.y)||!std::isfinite(clip.z)||!std::isfinite(clip.w)||std::abs(clip.w)<1e-6f)return false;
        const double inv=1./clip.w;x=(clip.x*inv*.5+.5)*size.x;y=(-clip.y*inv*.5+.5)*size.y;behind=clip.w<=0||clip.z*inv>1;
        return std::isfinite(x)&&std::isfinite(y);
    }
    std::string BossFeedbackMoveLabel(HudBossMove id)const{
        switch(id){case HudBossMove::Slam:return "重鉗衝擊！";case HudBossMove::Barrage:return "貨櫃彈幕！";case HudBossMove::Sweep:return "墨砲掃射！";case HudBossMove::Charge:return "船殼衝撞！";case HudBossMove::Crablets:return "小蟹群來襲！";case HudBossMove::Frenzy:return "狂亂攻勢！";}
        return "";
    }
    void ResetBossFeedback(const HudViewModel& view,const HudBoss& boss){
        bossFeedback={};bossFeedback.matchId=view.matchId;bossFeedback.hasBoss=true;bossFeedback.epoch=view.eventEpoch;bossFeedback.phase=boss.phase;
        if(!view.networkAuthority&&view.localActor>=0&&view.localActor<8){
            const auto slot=static_cast<std::size_t>(view.localActor);bossFeedback.lastDamage=boss.squadDamage[slot];bossFeedback.weakHits=boss.squadWeakHits[slot];
        }
        Find(hud,"boss-feedback-numbers")->SetInnerRML("");
    }
    void AddBossFeedbackHit(const HudBossHit& event,double now){
        if(!std::isfinite(event.damage)||event.damage<=0)return;
        const double at=std::isfinite(event.time)?event.time:now;
        if(!bossFeedback.numbers.empty()&&at>=bossFeedback.numbers.back().last&&at-bossFeedback.numbers.back().last<=.45){
            auto& number=bossFeedback.numbers.back();number.sum+=event.damage;number.last=at;
            number.point+=(event.point-number.point)*.35;number.weak|=event.weak;
        }else{
            BossFeedbackNumber number;number.id=++bossFeedback.nextId;number.point=event.point;number.sum=event.damage;number.born=number.last=at;number.weak=event.weak;number.jitter=(static_cast<int>(number.id%9)-4)*2.0;
            bossFeedback.numbers.push_back(number);if(bossFeedback.numbers.size()>6)bossFeedback.numbers.erase(bossFeedback.numbers.begin());
        }
        if(event.weak&&at-bossFeedback.lastCrit>.22){bossFeedback.lastCrit=at;bossFeedback.crits.push_back({++bossFeedback.nextId,event.point,at});if(bossFeedback.crits.size()>8)bossFeedback.crits.erase(bossFeedback.crits.begin());}
    }
    void UpdateBossFeedbackNumbers(double now,bool hidden){
        auto& s=bossFeedback;
        s.numbers.erase(std::remove_if(s.numbers.begin(),s.numbers.end(),[&](const auto& n){return now-n.last>1.05;}),s.numbers.end());
        s.crits.erase(std::remove_if(s.crits.begin(),s.crits.end(),[&](const auto& n){return now-n.born>.9;}),s.crits.end());
        std::string markup;
        for(const auto& n:s.numbers)markup+="<div id=\"boss-feedback-number-"+std::to_string(n.id)+"\" class=\"boss-feedback-number"+(n.weak?" weak":"")+"\"><b id=\"boss-feedback-number-value-"+std::to_string(n.id)+"\"></b></div>";
        for(const auto& n:s.crits)markup+="<div id=\"boss-feedback-crit-"+std::to_string(n.id)+"\" class=\"boss-feedback-crit\"><i></i><b>弱點命中！</b></div>";
        if(markup!=s.markup){Find(hud,"boss-feedback-numbers")->SetInnerRML(markup);s.markup=std::move(markup);for(auto& n:s.numbers)n.shown.clear();}
        const bool reduced=player.settings.reducedMotion;
        for(auto& n:s.numbers){
            const auto value=Integer(std::round(n.sum));if(value!=n.shown){Find(hud,"boss-feedback-number-value-"+std::to_string(n.id))->SetInnerRML(value);n.shown=value;}
            auto* el=Find(hud,"boss-feedback-number-"+std::to_string(n.id));if(!el)continue;
            double x=0,y=0;bool behind=false;const bool projected=ProjectBossFeedback(n.point,x,y,behind);
            const double age=std::max(0.,now-n.last),alpha=age<=.45?1.:std::clamp((1.05-age)/.6,0.,1.);
            el->SetProperty("opacity",Number(projected&&!behind&&!hidden?alpha:0));
            if(projected){el->SetProperty("left",Number(x*100/context->GetDimensions().x,3)+"%");el->SetProperty("top",Number(y*100/context->GetDimensions().y,3)+"%");}
            const double rise=reduced?0:std::max(0.,now-n.born)*18+(age>.45?(age-.45)*48:0);
            const double scale=std::clamp(.85+std::log10(std::max(1.,n.sum))*.16+(n.weak?.2:0),.85,1.6);
            el->SetProperty("transform","translate(-50%,-50%) translateX("+Number(n.jitter)+"dp) translateY(-"+Number(rise)+"dp) scale("+Number(scale)+")");
        }
        for(const auto& n:s.crits){
            auto* el=Find(hud,"boss-feedback-crit-"+std::to_string(n.id));if(!el)continue;
            double x=0,y=0;bool behind=false;const bool projected=ProjectBossFeedback(n.point,x,y,behind);const double age=std::max(0.,now-n.born);
            el->SetProperty("opacity",Number(projected&&!behind&&!hidden?(reduced?std::clamp((.9-age)/.2,0.,1.):1.):0));
            if(projected){el->SetProperty("left",Number(x*100/context->GetDimensions().x,3)+"%");el->SetProperty("top",Number(y*100/context->GetDimensions().y,3)+"%");}
        }
    }
    void PlaceBossFeedbackMove(const HudBoss& boss,bool moveLive,bool open){
        auto* el=Find(hud,"boss-feedback-move");const auto size=context->GetDimensions();
        double x=size.x*.5,y=size.y*.2,angle=0;bool behind=false,edge=false;
        if(haveWorldView&&size.x>0&&size.y>0){
            if(ProjectBossFeedback(boss.moveAnchor,x,y,behind)){
                const double marginX=std::max(72.,size.x*.05),top=std::max(130.,size.y*.14),bottom=std::max(48.,size.y*.06),cx=size.x*.5,cy=size.y*.5;
                if(behind||x<marginX||x>size.x-marginX||y<top||y>size.y-bottom){
                    edge=true;double dx=x-cx,dy=y-cy;if(behind){dx=-dx;dy=-dy;}if(std::abs(dx)+std::abs(dy)<1)dy=-1;
                    angle=std::atan2(dy,dx);const double ex=size.x*.5-marginX,ey=(size.y-top-bottom)*.5,edgeY=(top+size.y-bottom)*.5;
                    const double k=std::min(ex/std::max(1e-3,std::abs(std::cos(angle))),ey/std::max(1e-3,std::abs(std::sin(angle))));x=cx+std::cos(angle)*k;y=edgeY+std::sin(angle)*k;
                }else{
                    const double rx=size.x*.15,ry=size.y*.105,dx=x-cx,dy=y-cy,r=(dx/rx)*(dx/rx)+(dy/ry)*(dy/ry);
                    if(r<1){if(r<1e-4){x=cx;y=cy-ry;}else{const double k=1/std::sqrt(r);x=cx+dx*k;y=cy+dy*k;}}
                    if(y<top)y=top;
                }
            }else{x=size.x*.5;y=size.y*.2;}
        }
        el->SetProperty("left",Number(x*100/std::max(1.0,static_cast<double>(size.x)),3)+"%");el->SetProperty("top",Number(y*100/std::max(1.0,static_cast<double>(size.y)),3)+"%");
        el->SetClass("edge",edge);Find(hud,"boss-feedback-edge-arrow")->SetProperty("transform","rotate("+Number(angle*180/3.141592653589793)+"deg)");
        auto* direction=Find(hud,"boss-feedback-move-direction");direction->SetClass("hidden",!moveLive||boss.moveId!=HudBossMove::Charge);
        if(moveLive&&boss.moveId==HudBossMove::Charge){
            double x0=0,y0=0,x1=0,y1=0;bool b0=false,b1=false;
            const auto from=boss.pos+glm::dvec3(0,1,0),to=from+glm::dvec3(std::sin(boss.moveYaw)*6,0,std::cos(boss.moveYaw)*6);
            if(ProjectBossFeedback(from,x0,y0,b0)&&ProjectBossFeedback(to,x1,y1,b1)&&!b0&&!b1)angle=std::atan2(y1-y0,x1-x0);
            else angle=boss.moveYaw;
            direction->SetProperty("transform","rotate("+Number(angle*180/3.141592653589793)+"deg)");
        }
        el->SetClass("open",open);el->SetClass("active",boss.hasMove&&boss.movePhase==HudBossMovePhase::Active);
        Text(hud,"boss-feedback-move-label",open?"弱點開啟！":moveLive?BossFeedbackMoveLabel(boss.moveId):std::string());
    }
    void UpdateBossFeedback(const HudViewModel& view,bool paused,bool results){
        const auto* boss=view.boss?&*view.boss:nullptr;auto* layer=Find(hud,"boss-feedback-layer");
        if(!boss){if(bossFeedback.hasBoss){bossFeedback={};Find(hud,"boss-feedback-numbers")->SetInnerRML("");}Hidden(hud,"boss-feedback-layer",true);return;}
        const double now=boss->clock;auto& s=bossFeedback;
        if(s.matchId!=view.matchId||!s.hasBoss||s.epoch!=view.eventEpoch||(s.clock>=0&&now<s.clock))ResetBossFeedback(view,*boss);
        s.clock=now;const bool reduced=player.settings.reducedMotion,obscured=paused||results||mapInteraction.open()||hudOverlay||practiceGear;
        layer->SetClass("reduced-motion",reduced);Hidden(hud,"boss-feedback-layer",obscured);
        if(boss->anim==1&&!s.introSeen){s.introSeen=true;s.titleUntil=now+(reduced?1.5:2.75);}
        if(boss->phase>s.phase){s.phase=boss->phase;if(!obscured)s.phaseUntil=now+(reduced?1.5:2.9);}
        else if(boss->phase<s.phase){s.phase=boss->phase;s.phaseUntil=0;}
        Hidden(hud,"boss-feedback-title",obscured||!s.introSeen||now>=s.titleUntil);
        const bool ending=view.phase==HudMatchPhase::Finish||view.phase==HudMatchPhase::Judge;
        const bool phaseBanner=!obscured&&(ending||(now<s.phaseUntil&&s.phase>=2));
        if(phaseBanner||(!obscured&&s.introSeen&&now<s.titleUntil))Hidden(hud,"battle-banner",true);
        Hidden(hud,"boss-feedback-phase",!phaseBanner);Find(hud,"boss-feedback-phase")->SetClass("rage",s.phase>=3);
        Text(hud,"boss-feedback-phase-title",ending?(boss->dead?"擊破！":"時間到！"):s.phase>=3?"外殼破裂！":"第二階段！");
        Text(hud,"boss-feedback-phase-sub",ending?(boss->dead?"破艦巨獸已沉沒":"破艦巨獸逃脫了"):s.phase>=3?"最終階段・攻擊發光腹部！":"小蟹群來襲・快把牠們打倒！");
        const std::size_t localSlot=view.localActor<0?view.actors.size():static_cast<std::size_t>(view.localActor);
        for(const auto& event:boss->hits)if(event.serial>s.hitSerial){
            s.hitSerial=event.serial;
            if(!obscured&&event.attackerSlot>=0&&static_cast<std::size_t>(event.attackerSlot)==localSlot&&now-event.time<=1.05)AddBossFeedbackHit(event,now);
        }
        if(!view.networkAuthority&&localSlot<8){
            const double damage=boss->squadDamage[localSlot];const int weakHits=boss->squadWeakHits[localSlot];
            if(!obscured&&damage>s.lastDamage+.001){HudBossHit event;event.time=now;event.damage=damage-s.lastDamage;event.weak=weakHits>s.weakHits;event.point=boss->pos+glm::dvec3(0,boss->phase>=3?2.6:4.2,0);AddBossFeedbackHit(event,now);}
            s.lastDamage=damage;s.weakHits=weakHits;
        }
        UpdateBossFeedbackNumbers(now,obscured);
        const bool open=boss->stunned;
        const bool moveLive=boss->hasMove&&(boss->movePhase==HudBossMovePhase::Telegraph||boss->movePhase==HudBossMovePhase::Active)&&now-boss->moveT0<=3.2;
        const bool showMove=!obscured&&!boss->dead&&(open||moveLive);
        Hidden(hud,"boss-feedback-move",!showMove);
        if(showMove)PlaceBossFeedbackMove(*boss,moveLive,open);
    }
