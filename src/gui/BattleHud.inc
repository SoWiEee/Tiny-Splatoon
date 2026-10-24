    struct BattleNote { std::string text,weapon,color,kind;double born=0,until=0; };
    struct BattleHudState {
        const MatchSimulation* match=nullptr;std::uint64_t epoch=0,serial=0;
        double time=-1,hitAt=-99,killAt=-99,hitDamage=0,callUntil=0,turfBefore=0,turfAcc=0,turfShown=0,turfTick=0,popUntil=0,denyUntil=0,emptyUntil=0,phaseAt=0;
        int chain=0,streak=0,pop=0,lastSeconds=-1;bool first=false,alive=true;
        const PlayerSimulation* lastKiller=nullptr;
        MatchPhase phase=MatchPhase::Intro;
        std::string call,deathName,deathWeapon,deathColor="#2f5bff",deathCause;
        std::vector<BattleNote> cards,feed;
        std::vector<double> kills,slotChanged,respawnTotal;
        std::vector<int> slotState;
        std::map<const PlayerSimulation*,double> dealt;
        std::map<const PlayerSimulation*,int> streaks;
        std::vector<DamageDirection> directions;
    } battle;
    Rml::Colourb HudColor(const std::string& hex)const{
        const auto rgb=std::stoul(hex.substr(1),nullptr,16);
        return {Rml::byte((rgb>>16)&255),Rml::byte((rgb>>8)&255),Rml::byte(rgb&255),255};
    }
    std::string ActorName(const MatchSimulation& match,const PlayerSimulation* actor)const{
        for(std::size_t i=0;i<match.actors().size();++i)if(match.actors()[i].get()==actor)return match.roster()[i].name;
        return "玩家";
    }
    std::string CauseLabel(const std::string& cause)const{
        if(cause=="water")return "落入水中";
        if(cause=="environment"||cause.empty())return "場地危險";
        for(const auto category:{EquipmentCategory::Main,EquipmentCategory::Sub,EquipmentCategory::Special}){
            const auto label=EquipmentLabel(category,cause);if(!label.empty())return std::string(label);
        }
        return "敵方攻擊";
    }
    void BattleCall(std::string text,double now){battle.call=std::move(text);battle.callUntil=now+3.2;}
    void BattleCard(std::string name,std::string weapon,std::string color,bool assist,double now){
        battle.cards.insert(battle.cards.begin(),{std::move(name),std::move(weapon),std::move(color),assist?"assist":"kill",now,now+(assist?1.7:2.2)});
        if(battle.cards.size()>2)battle.cards.resize(2);
    }
    void UpdateBattleHud(MatchSimulation& match,bool paused,bool results,bool practice){
        auto& local=match.local();const auto& state=local.state;auto& world=match.world();const double now=world.time();
        const auto palette=ProfilePaletteColors(player.settings.palette,player.settings.colorblind);
        const auto own=std::string(palette[local.team()]);
        if(battle.match!=&match||battle.epoch!=world.hudEventEpoch()||now<battle.time){
            battle={};battle.match=&match;battle.epoch=world.hudEventEpoch();battle.turfBefore=local.turfPainted();battle.turfShown=battle.turfBefore;battle.alive=state.alive;battle.phase=match.phase();battle.phaseAt=now;
            if(auto* gauge=dynamic_cast<SpecialGaugeElement*>(Find(hud,"special-orb")))gauge->Reset();
        }
        const double dt=battle.time>=0?std::clamp(now-battle.time,0.,.1):0;battle.time=now;
        const bool obscured=paused||results||mapInteraction.open()||hudOverlay||practiceGear;
        Hidden(hud,"battle-hud-layer",obscured);
        bool deniedThisFrame=false;
        for(const auto& event:world.hudEvents()){
            if(event.serial<=battle.serial)continue;battle.serial=event.serial;
            if(now-event.time>4.2)continue;
            if(event.kind==CombatHudEvent::Kind::Denied){
                if(event.victim==&local){deniedThisFrame=true;battle.denyUntil=now+.8;if(event.damage>0)battle.emptyUntil=now+.45;Text(hud,"equipment-denied",event.damage>0?"墨量不足":event.source==local.specialId()?"特殊技能尚未就緒":"道具尚未就緒");}
                continue;
            }
            if(event.kind==CombatHudEvent::Kind::Hit){
                if(event.attacker==&local&&event.victim!=&local){
                    battle.chain=event.time-battle.hitAt<.55?std::min(6,battle.chain+1):0;
                    battle.hitAt=event.time;battle.hitDamage=event.damage;if(event.victim)battle.dealt[event.victim]=event.time;
                }
                if(event.victim==&local&&event.attacker&&event.attacker!=&local){
                    const auto dir=event.attackerPos-event.victimPos;double angle=std::atan2(dir.x,dir.z)-local.input.aimYaw-1.57079632679;
                    if(haveWorldView){const auto clip=worldView*glm::vec4(glm::vec3(event.attackerPos+glm::dvec3(0,1,0)),1);const double sign=clip.w<0?-1:1;angle=std::atan2(-clip.y*sign,clip.x*sign);}
                    battle.directions.push_back({angle,event.time,event.damage/100,HudColor(std::string(palette[event.attackerTeam==1?1:0]))});
                    if(battle.directions.size()>6)battle.directions.erase(battle.directions.begin());
                }
                continue;
            }
            const int victimStreak=battle.streaks[event.victim];battle.streaks[event.victim]=0;
            if(event.attacker&&event.attacker!=event.victim)++battle.streaks[event.attacker];
            const auto victimName=ActorName(match,event.victim),attackerName=ActorName(match,event.attacker);
            if(event.victim==&local){
                battle.lastKiller=event.attacker;battle.streak=0;battle.directions.clear();
                battle.deathName=event.attacker?attackerName:"";battle.deathWeapon=event.source;battle.deathCause=CauseLabel(event.source);
                battle.deathColor=std::string(palette[event.attackerTeam==1?1:0]);
            }else if(event.attacker==&local){
                battle.killAt=event.time;BattleCard(victimName,event.victimWeapon,std::string(palette[event.victimTeam==1?1:0]),false,now);
                battle.kills.erase(std::remove_if(battle.kills.begin(),battle.kills.end(),[&](double t){return event.time-t>=4.2;}),battle.kills.end());
                battle.kills.push_back(event.time);++battle.streak;
                int enemies=0,alive=0;for(std::size_t i=0;i<match.actors().size();++i)if(match.roster()[i].active&&match.actors()[i]->team()!=local.team()){++enemies;if(match.actors()[i]->state.alive)++alive;}
                if(enemies>=4&&alive==0)BattleCall("全隊擊倒！",now);
                else if(battle.kills.size()>=2){const char* names[]={"","","雙重擊倒！","三重擊倒！","四重擊倒！"};BattleCall(names[std::min<std::size_t>(4,battle.kills.size())],now);}
                else if(!battle.first)BattleCall("首次擊倒！",now);
                else if(battle.lastKiller&&event.victim==battle.lastKiller){BattleCall("復仇成功！",now);battle.lastKiller=nullptr;}
                else if(victimStreak>=3)BattleCall("終結連殺！",now);
                else if(battle.streak>=3&&battle.streak%2==1)BattleCall("連續擊倒 ×"+std::to_string(battle.streak),now);
                battle.first=true;
            }else if(event.attacker&&event.attackerTeam==local.team()&&battle.dealt.count(event.victim)&&event.time-battle.dealt[event.victim]<4){
                BattleCard(victimName,event.victimWeapon,std::string(palette[event.victimTeam==1?1:0]),true,now);battle.dealt.erase(event.victim);
            }
            if(event.attacker!=&local){
                const auto text=event.attacker?attackerName+" 擊倒了 "+victimName:victimName+" "+CauseLabel(event.source);
                battle.feed.insert(battle.feed.begin(),{text,event.attackerWeapon,std::string(palette[event.attackerTeam==1?1:0]),"feed",now,now+4.2});
                if(battle.feed.size()>5)battle.feed.resize(5);
            }
        }
        for(auto it=battle.dealt.begin();it!=battle.dealt.end();)if(now-it->second>=4)it=battle.dealt.erase(it);else ++it;
        const auto expired=[&](const BattleNote& note){return now>=note.until;};
        battle.cards.erase(std::remove_if(battle.cards.begin(),battle.cards.end(),expired),battle.cards.end());
        battle.feed.erase(std::remove_if(battle.feed.begin(),battle.feed.end(),expired),battle.feed.end());
        const auto notes=[&](const std::vector<BattleNote>& list,bool cards){std::string markup;for(const auto& note:list){
            const auto kind=note.weapon.empty()?std::string("shooter"):note.weapon;
            markup+="<div class=\"battle-note "+note.kind+"\" style=\"border-color:"+note.color+";\"><img src=\"weapons/"+kind+".png\"/><span>"+(cards?std::string(note.kind=="assist"?"助攻　":"擊倒　"):std::string())+Escape(note.text)+"</span></div>";
        }return markup;};
        for(const auto& pair:{std::make_pair("kill-cards",notes(battle.cards,true)),std::make_pair("kill-feed",notes(battle.feed,false))}){
            const auto key=std::string("hud/")+pair.first;if(textCache[key]!=pair.second){Find(hud,pair.first)->SetInnerRML(pair.second);textCache[key]=pair.second;}
        }
        for(const auto& pair:{std::make_pair("kill-cards",&battle.cards),std::make_pair("kill-feed",&battle.feed)}){
            auto* list=Find(hud,pair.first);for(int i=0;i<list->GetNumChildren();++i){const auto& note=(*pair.second)[i];const double age=now-note.born,enter=player.settings.reducedMotion?1:1-std::exp(-std::max(0.,age)*18),fade=player.settings.reducedMotion?1:std::clamp((note.until-now)/.25,0.,1.);auto* child=list->GetChild(i);child->SetProperty("opacity",Number((.35+.65*enter)*fade));child->SetProperty("transform","translateX("+Number((std::string(pair.first)=="kill-feed"?-24.:12.)*(1-enter))+"dp)");}
        }
        Hidden(hud,"kill-cards",!state.alive);
        Hidden(hud,"battle-callout",!state.alive||now>=battle.callUntil);Text(hud,"battle-callout",battle.call);
        if(now<battle.callUntil){const double age=3.2-(battle.callUntil-now);Find(hud,"battle-callout")->SetProperty("transform","scale("+Number(player.settings.reducedMotion?1:1+.14*std::exp(-age*9)*std::sin(age*18))+")");}
        auto* feedback=dynamic_cast<BattleFeedbackElement*>(Find(hud,"battle-feedback"));
        BattleFeedbackFrame feedbackFrame;feedbackFrame.hitAge=now-battle.hitAt;feedbackFrame.killAge=now-battle.killAt;feedbackFrame.hitDamage=battle.hitDamage;feedbackFrame.hitChain=battle.chain;feedbackFrame.invuln=state.alive&&!state.squid?state.invuln:0;feedbackFrame.time=now;feedbackFrame.color=HudColor(own);feedbackFrame.reducedMotion=player.settings.reducedMotion;
        for(const auto& direction:battle.directions)if(now-direction.age<1.4){auto d=direction;d.age=now-d.age;feedbackFrame.directions.push_back(d);}
        feedback->Update(feedbackFrame);feedback->SetAttribute("data-hit",feedbackFrame.hitAge<.3?"true":"false");feedback->SetAttribute("data-kill",feedbackFrame.killAge<.6?"true":"false");
        Hidden(hud,"battle-feedback",!state.alive);
        if(!state.alive&&battle.alive&&battle.deathCause.empty()){battle.deathName="";battle.deathCause="場地危險";}
        if(state.alive&&!battle.alive){battle.deathName.clear();battle.deathWeapon.clear();battle.deathCause.clear();battle.directions.clear();}
        battle.alive=state.alive;
        const bool respawning=match.phase()==MatchPhase::Playing&&!state.alive;
        Hidden(hud,"death-card",!respawning);Text(hud,"death-by",battle.deathName.empty()?"遭到擊倒":battle.deathName+" 擊倒了你");Text(hud,"death-cause",battle.deathCause);
        Text(hud,"respawn-seconds",Integer(std::ceil(std::max(0.,state.respawnTimer))));Hidden(hud,"respawn-hint",!respawning);Text(hud,"respawn-hint",mapInteraction.queuedKey()>=0?"已安排復活彈射："+mapInteraction.queuedLabel():"按住 Tab／M 規劃復活彈射");
        dynamic_cast<HudRingElement*>(Find(hud,"death-ring"))->Update(std::clamp(1-state.respawnTimer/5.5,0.,1.),HudColor(battle.deathColor),Rml::Colourb(255,255,255,45));
        const auto causeMain=EquipmentLabel(EquipmentCategory::Main,battle.deathWeapon);const auto causeSub=EquipmentLabel(EquipmentCategory::Sub,battle.deathWeapon);const auto causeSpecial=EquipmentLabel(EquipmentCategory::Special,battle.deathWeapon);
        auto* deathArt=Find(hud,"death-weapon");const std::string art=!causeMain.empty()?"weapons/"+battle.deathWeapon+".png":!causeSub.empty()?"equipment/sub-"+battle.deathWeapon+".png":!causeSpecial.empty()?"equipment/special-"+battle.deathWeapon+".png":"weapons/squid.png";deathArt->SetAttribute("src",art);
        battle.turfAcc+=std::max(0.,local.turfPainted()-battle.turfBefore);battle.turfBefore=local.turfPainted();battle.turfTick+=dt;
        if(battle.turfAcc>0&&(battle.turfTick>.4||battle.turfAcc>40)){const int points=static_cast<int>(std::round(battle.turfAcc));if(points>=1){battle.pop=points;battle.popUntil=now+1.1;battle.turfAcc=std::max(0.,battle.turfAcc-points);}battle.turfTick=0;}
        battle.turfShown=std::min(local.turfPainted(),battle.turfShown+std::max(6.,(local.turfPainted()-battle.turfShown)*7)*dt);
        Text(hud,"personal-score",Integer(std::floor(battle.turfShown))+" p");Text(hud,"turf-pop","+"+std::to_string(battle.pop)+"p");Hidden(hud,"turf-pop",now>=battle.popUntil);
        if(now<battle.popUntil){const double age=1.1-(battle.popUntil-now);Find(hud,"turf-pop")->SetProperty("transform","translateY("+Number(player.settings.reducedMotion?0:-age*28)+"dp)");Find(hud,"turf-pop")->SetProperty("opacity",Number(std::clamp((battle.popUntil-now)/.3,0.,1.)));}
        const double cost=local.weapon().number("specialCost",190),subCost=local.subWeapon().number("inkCost",70);
        Hidden(hud,"equipment-denied",now>=battle.denyUntil||!state.alive);
        SpecialGaugeFrame gauge;gauge.charge=state.specialCharge/std::max(1.,cost);gauge.subFill=subCost>0?state.ink/subCost:1;gauge.dt=dt;gauge.time=now;gauge.ready=state.specialCharge>=cost;gauge.active=local.specialPose().active;gauge.rejected=deniedThisFrame;gauge.subKind=local.subId();gauge.color=HudColor(own);gauge.reducedMotion=player.settings.reducedMotion;
        dynamic_cast<SpecialGaugeElement*>(Find(hud,"special-orb"))->Update(gauge);
        Find(hud,"special-art")->SetAttribute("src","equipment/special-"+local.specialId()+".png");Find(hud,"sub-art")->SetAttribute("src","equipment/sub-"+local.subId()+".png");
        Find(hud,"sub-art")->SetProperty("opacity",gauge.subFill>=1?"1":"0.35");
        Text(hud,"special-percent",gauge.active?"發動中":gauge.ready?"就緒 F／Q":Integer(std::clamp(gauge.charge,0.,1.)*100)+"%");
        const std::string abilityName(EquipmentLabel(EquipmentCategory::Special,local.specialId()));Find(hud,"special-display")->SetAttribute("title",abilityName);Find(hud,"special-orb")->SetAttribute("aria-label",abilityName);
        if(battle.phase!=match.phase()){battle.phase=match.phase();battle.phaseAt=now;}
        const bool intro=match.phase()==MatchPhase::Intro&&!practice;
        const bool go=match.phase()==MatchPhase::Playing&&now-battle.phaseAt<.8&&!practice;
        const bool finish=match.phase()==MatchPhase::Finish&&!practice;
        Hidden(hud,"battle-banner",(!intro&&!go&&!finish)||(!intro&&!finish&&(battle.callUntil>now||!state.alive)));Text(hud,"battle-banner",intro?"準備好了嗎？":finish?"對戰結束！":"開始！");
        Hidden(hud,"intro-lineup",!intro||now-battle.phaseAt>=3.5);
        Find(hud,"intro-lineup")->SetProperty("opacity",Number(player.settings.reducedMotion?1:std::clamp((3.5-(now-battle.phaseAt))/.6,0.,1.)));
        if(intro){std::string lineup;for(int side=0;side<2;++side){lineup+="<div class=\"lineup-team\"><h2>"+std::string(match.boss()?side==0?"協力小隊":"破艦巨獸":side==0?"我方隊伍":"對方隊伍")+"</h2>";
            for(std::size_t i=0;i<match.actors().size();++i){const auto& a=*match.actors()[i];if(!match.roster()[i].active||a.team()!=(side==0?local.team():1-local.team()))continue;lineup+="<div class=\"lineup-player\"><img src=\"weapons/"+std::string(a.weapon().kind)+".png\"/><strong>"+Escape(match.roster()[i].name)+"</strong></div>";}
            lineup+="</div>";}const auto key="hud/intro-lineup";if(textCache[key]!=lineup){Find(hud,"intro-lineup")->SetInnerRML(lineup);textCache[key]=lineup;}}
        const int seconds=static_cast<int>(std::ceil(match.remaining()));
        const bool countdown=match.phase()==MatchPhase::Playing&&!practice&&seconds>0&&seconds<=10;
        Hidden(hud,"final-countdown",!countdown);Text(hud,"final-countdown",std::to_string(seconds));
        if(seconds!=battle.lastSeconds){battle.lastSeconds=seconds;if(countdown)Find(hud,"final-countdown")->SetProperty("transform","scale(1.08)");}
        if(battle.slotState.size()!=match.actors().size()){battle.slotState.assign(match.actors().size(),-1);battle.slotChanged.assign(match.actors().size(),now-1);battle.respawnTotal.assign(match.actors().size(),5.5);}
        for(std::size_t i=0;i<match.actors().size();++i){const auto& a=*match.actors()[i];auto* slot=hud->GetElementById("squad-player-"+std::to_string(i));if(!slot)continue;
            const int status=!a.state.alive?0:a.state.specialCharge>=a.weapon().number("specialCost",190)?2:1;
            if(status!=battle.slotState[i]){battle.slotState[i]=status;battle.slotChanged[i]=now;battle.respawnTotal[i]=std::max(.01,a.state.respawnTimer);}
            const double age=now-battle.slotChanged[i];const double scale=player.settings.reducedMotion?1:1+.2*std::exp(-age*10)*std::sin(age*16);
            slot->SetProperty("transform","scale("+Number(scale)+")");
            if(auto* ring=dynamic_cast<HudRingElement*>(hud->GetElementById("squad-ring-"+std::to_string(i))))ring->Update(status==0?std::clamp(1-a.state.respawnTimer/battle.respawnTotal[i],0.,1.):0,HudColor(std::string(palette[a.team()])),Rml::Colourb(255,255,255,35));
        }
    }
