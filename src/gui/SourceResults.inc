    void UpdateResults(MatchSimulation& match,const std::array<double,2>& coverage){
        if(!match.result())return;
        (void)coverage;
        const auto& result=*match.result();
        const auto& local=match.local();
        const bool win=result.boss?result.boss->win:result.winner==local.team();
        const double now=glfwGetTime();
        if(resultXpMatch!=&match){
            resultXpMatch=&match;resultXpStartedAt=now;resultXpNextTick=now+.065;
            resultSkipped=false;resultDone=false;resultFocused=false;
        }
        const double elapsed=std::max(0.0,now-resultXpStartedAt);
        constexpr double intro=2.3;
        resultIntro=!player.settings.reducedMotion&&!resultSkipped&&elapsed<intro;
        const double showTime=player.settings.reducedMotion?10.0:resultSkipped?10.0:std::max(0.0,elapsed-intro);
        resultDone=player.settings.reducedMotion||resultSkipped||showTime>=7.0;
        Find(hud,"results")->SetClass("results-intro",resultIntro);
        Find(hud,"results")->SetClass("results-full",!resultIntro);
        const double stamp=player.settings.reducedMotion||resultSkipped?1:std::clamp((showTime-1.35)/.35,0.,1.);
        Find(hud,"result-boss-stamp")->SetProperty("transform","rotate(-8deg) scale("+Number(1+(1-stamp)*1.2)+")");
        Find(hud,"result-boss-stamp")->SetProperty("opacity",Number(stamp));

        if(!resultIntro&&!resultFocused){
            auto* target=Find(hud,"results-rematch");
            if(!target->IsVisible())target=Find(hud,"results-menu");
            target->Focus();resultFocused=true;
        }
        Text(hud,"result-title",win?"VICTORY!":"DEFEAT");
        Find(hud,"result-title")->GetParentNode()->SetClass("defeat",!win);
        const auto stageLabel=StageLabel(match.stage().id);
        Text(hud,"result-detail",(stageLabel.empty()?std::string(match.stage().name):std::string(stageLabel))+" · "+(result.boss?"破艦巨獸":result.zones?"區域爭奪":"塗地對戰"));
        const auto palette=ProfilePaletteColors(player.settings.palette,player.settings.colorblind,match.boss()!=nullptr);
        static constexpr std::array<std::array<std::string_view,2>,5> names={{{"橘紅","鈷藍"},{"泡泡糖","薄荷"},{"檸檬","葡萄"},{"水藍","櫻桃"},{"萊姆","洋紅"}}};
        const auto teamNames=player.settings.colorblind?std::array<std::string_view,2>{"陽光","海洋"}:names[std::clamp(player.settings.palette,0,4)];
        auto* splat=Find(hud,"result-splat");
        splat->SetProperty("background-color",std::string(palette[result.boss&&!win?1:0]));
        for(auto* dot=splat->GetFirstChild();dot;dot=dot->GetNextSibling())dot->SetProperty("background-color",std::string(palette[result.boss&&!win?1:0]));
        for(int team=0;team<2;++team){
            Find(hud,team?"result-score-b-name":"result-score-a-name")->SetProperty("color",std::string(palette[team]));
            Find(hud,team?"result-score-b-fill":"result-score-a-fill")->SetProperty("background-color",std::string(palette[team]));
        }
        const double scoreProgress=resultDone?1.0:1.0-std::pow(1.0-std::clamp((showTime-.45)/.9,0.0,1.0),3.0);
        Hidden(hud,"result-boss",!result.boss);Hidden(hud,"result-score",result.boss.has_value());
        Find(hud,"results")->SetClass("boss-results",result.boss.has_value());
        if(result.boss){
            const auto& b=*result.boss;
            const double hp=std::clamp(b.hp/std::max(1.0,b.maxHp),0.0,1.0);
            const double shownHp=1+(hp-1)*scoreProgress;
            Text(hud,"result-boss-phase","階段 "+std::string("◆")+(b.phase>=2?"◆":"◇")+(b.phase>=3?"◆":"◇"));
            Text(hud,"result-boss-hp",Integer(shownHp*100)+"%");
            Find(hud,"result-boss-fill")->SetProperty("width",Number(shownHp*100)+"%");
            const int seconds=static_cast<int>(std::floor(b.time*scoreProgress));
            Text(hud,"result-boss-time",std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60));
            Text(hud,"result-boss-stamp",b.win?"擊破！":"逃脫");
            Hidden(hud,"result-boss-stamp",scoreProgress<1);
            Find(hud,"result-boss-emblem")->SetAttribute("src",b.win||b.phase>=3?"markers/boss-cracked.png":"markers/boss.png");
        }else if(result.zones){
            const auto& z=*result.zones;
            Text(hud,"result-score-a-name",std::string(teamNames[0])+"  "+ZoneCount(z.counts[0])+(z.penalty[0]>0?"  +"+ZoneCount(z.penalty[0]):""));
            Text(hud,"result-score-b-name",ZoneCount(z.counts[1])+(z.penalty[1]>0?"  +"+ZoneCount(z.penalty[1]):"")+"  "+std::string(teamNames[1]));
            Find(hud,"result-score-a-fill")->SetProperty("width",Number(std::clamp(100.0-z.counts[0],0.0,100.0)*.5*scoreProgress)+"%");
            Find(hud,"result-score-b-fill")->SetProperty("width",Number(std::clamp(100.0-z.counts[1],0.0,100.0)*.5*scoreProgress)+"%");
        }else{
            Text(hud,"result-score-a-name",std::string(result.winner==0?"♛  ":"")+std::string(teamNames[0])+"  "+Percent(result.coverage[0]*scoreProgress));
            Text(hud,"result-score-b-name",Percent(result.coverage[1]*scoreProgress)+"  "+std::string(teamNames[1])+(result.winner==1?"  ♛":""));
            Find(hud,"result-score-a-fill")->SetProperty("width",Number(std::clamp(result.coverage[0],0.0,1.0)*100*scoreProgress)+"%");
            Find(hud,"result-score-b-fill")->SetProperty("width",Number(std::clamp(result.coverage[1],0.0,1.0)*100*scoreProgress)+"%");
        }
        const auto medals=ResultMedals(match,result);
        const auto medalKey="hud/result-medals";
        if(textCache[medalKey]!=medals){Find(hud,"result-medals")->SetInnerRML(medals);textCache[medalKey]=medals;}
        Hidden(hud,"result-medals",resultIntro||showTime<2.05);
        std::vector<std::size_t> order;
        for(std::size_t i=0;i<match.actors().size();++i)if(match.roster()[i].active)order.push_back(i);
        const auto* boss=match.boss();
        std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){
            if(boss&&boss->squadDamage()[a]!=boss->squadDamage()[b])return boss->squadDamage()[a]>boss->squadDamage()[b];
            if(!boss&&match.roster()[a].team!=match.roster()[b].team)return match.roster()[a].team<match.roster()[b].team;
            return match.actors()[a]->turfPainted()>match.actors()[b]->turfPainted();
        });
        double bestTurf=0,bestDamage=0;int bestSplats=0;
        for(auto i:order){bestTurf=std::max(bestTurf,match.actors()[i]->turfPainted());bestSplats=std::max(bestSplats,match.actors()[i]->splats());if(boss)bestDamage=std::max(bestDamage,boss->squadDamage()[i]);}
        std::string rows;
        for(int team=0;team<2;++team){
            rows+="<div class=\"result-team"+std::string(boss?" boss-team":result.zones?" zones":"")+"\">";
            rows+="<div class=\"result-team-head\"><span class=\"result-team-name\" style=\"color:"+std::string(palette[boss?local.team():team])+";\">● "+(boss?std::string(team?"協力小隊 · 5–8":"協力小隊 · 前四名"):std::string(teamNames[team]))+(boss?(win&&!team?" <strong>★ 擊破</strong>":""):(result.winner==team?" <strong>★ 勝利</strong>":""))+"</span><span class=\"result-col turf\">"+(boss?"傷害":"塗地")+"</span>";
            if(result.zones&&!boss)rows+="<span class=\"result-col zone\">區域</span>";
            if(boss)rows+="<span class=\"result-col small\">弱點</span>";
            rows+="<span class=\"result-col small\">擊倒</span><span class=\"result-col small\">陣亡</span>";
            if(boss)rows+="<span class=\"result-col turf\">塗地</span>";
            rows+="</div>";
            int rowNumber=0;
            for(std::size_t position=0;position<order.size();++position){
                const auto i=order[position];
                if(boss?position/4!=static_cast<std::size_t>(team):match.roster()[i].team!=team)continue;
                const auto& entry=match.roster()[i];const auto& actor=*match.actors()[i];
                const double rowProgress=resultDone?1.0:1.0-std::pow(1.0-std::clamp((showTime-.8-rowNumber*.13)/.75,0.0,1.0),3.0);
                const bool mvp=boss?bestDamage>0&&boss->squadDamage()[i]==bestDamage:entry.local&&medals.find("MVP")!=std::string::npos;
                rows+="<div class=\"result-player"+std::string(entry.local?" result-self":"")+(mvp?" result-mvp":"")+"\"><span class=\"result-identity\"><img class=\"result-weapon\" src=\"weapons/"+std::string(actor.weapon().kind)+".png\"/><span class=\"result-name\">"+(boss?std::to_string(position+1)+"  ":"")+Escape(entry.name)+"</span>"+(entry.local?" <em>自己</em>":entry.bot||actor.botControlled()?" <em>BOT</em>":"");
                if(resultDone||showTime>=2.05){if(mvp)rows+="<span class=\"result-award\" title=\"MVP\">★</span>";if(!boss&&bestTurf>0&&actor.turfPainted()==bestTurf)rows+="<span class=\"result-award\" title=\"Turf\">✦</span>";if(!boss&&bestSplats>0&&actor.splats()==bestSplats)rows+="<span class=\"result-award\" title=\"Splats\">♛</span>";}
                rows+="</span><span class=\"result-stat turf\">"+Integer((boss?boss->squadDamage()[i]:actor.turfPainted())*rowProgress)+(boss?"":"<small>p</small>")+"</span>";
                if(result.zones&&!boss)rows+="<span class=\"result-stat zone\">"+Integer(actor.zoneTurf()*rowProgress)+"</span>";
                if(boss)rows+="<span class=\"result-stat small\">"+std::to_string(boss->squadWeakHits()[i])+"</span>";
                rows+="<span class=\"result-stat small\">"+std::to_string(boss?boss->squadSplats()[i]:actor.splats())+"</span><span class=\"result-stat small\">"+std::to_string(actor.deaths())+"</span>";
                if(boss)rows+="<span class=\"result-stat turf\">"+Integer(actor.turfPainted()*rowProgress)+"<small>p</small></span>";
                rows+="</div>";
                ++rowNumber;
            }
            rows+="</div>";
        }
        const auto tableKey="hud/results-table";
        if(textCache[tableKey]!=rows){Find(hud,"results-table")->SetInnerRML(rows);textCache[tableKey]=rows;}
        const double xpProgress=resultDone?1.0:std::clamp((showTime-3.6)/1.7,0.0,1.0);
        const int shownXp=reward.applied?static_cast<int>(std::round(reward.gained*xpProgress)):0;
        Text(hud,"result-xp-label","+"+std::to_string(shownXp)+" XP"+(reward.applied&&reward.levelAfter>reward.levelBefore&&xpProgress>=1.0?"  升級！":""));
        const int level=reward.applied&&xpProgress>=1.0?reward.levelAfter:reward.applied?reward.levelBefore:player.level;
        const int currentXp=reward.applied?(xpProgress>=1.0?reward.xpAfter:std::min(reward.xpToNextBefore,reward.xpBefore+shownXp)):player.xp;
        const int targetXp=reward.applied?(xpProgress>=1.0?reward.xpToNextAfter:reward.xpToNextBefore):XpForLevel(player.level);
        Text(hud,"result-level-value",std::to_string(level));
        Text(hud,"result-xp-next",std::to_string(std::max(0,targetXp-currentXp))+" XP 升到下一級");
        Find(hud,"result-xp-fill")->SetProperty("width",Number(100.0*currentXp/std::max(1,targetXp))+"%");
        Find(hud,"result-xp-fill")->SetProperty("background-color",std::string(palette[0]));
        std::string breakdown;
        if(reward.applied&&xpProgress>0){
            const int base=win?1200:500;
            const int turf=static_cast<int>(std::llround(local.turfPainted()*(result.zones?0.6:1.0)));
            const int splats=local.splats()*40;
            const int zone=result.zones?static_cast<int>(std::llround(local.zoneTurf())):0;
            std::size_t self=match.actors().size();for(std::size_t i=0;i<match.actors().size();++i)if(match.actors()[i].get()==&local){self=i;break;}
            const int damage=boss&&self<match.actors().size()?static_cast<int>(std::llround(boss->squadDamage()[self]*.04)):0;
            const int knockout=result.zones&&win&&result.zones->reason=="knockout"?300:0;
            if(std::abs(base+turf+splats+zone+damage+knockout-reward.gained)<=2){
                breakdown=(win?"勝利獎勵 +":"對戰獎勵 +")+std::to_string(base)+"　 塗地 +"+std::to_string(turf);
                if(zone)breakdown+="　 區域塗地 +"+std::to_string(zone);
                if(splats)breakdown+="　 擊倒 +"+std::to_string(splats);
                if(damage)breakdown+="　 傷害 +"+std::to_string(damage);
                if(knockout)breakdown+="　 擊破加成 +"+std::to_string(knockout);
            }
        }
        Text(hud,"result-xp-breakdown",breakdown);
        if(reward.applied&&xpProgress>0&&xpProgress<1.0&&now>=resultXpNextTick){Play("xp_tick",.05,1);resultXpNextTick=now+.065;}
    }
