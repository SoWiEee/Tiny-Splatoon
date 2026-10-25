// Native section-5 presets; all particles are cosmetic and reuse the existing pools.
bool buildInkBurstRecipe(std::vector<WorldFxParticle>& out,std::string_view name,
    glm::dvec3 pos,glm::dvec3 normal,glm::dvec3 color,double radius,std::uint32_t& rng,
    const WorldFxRecipeContext& context,glm::dvec3 direction){
    const bool slosh=name=="inkSlosh",flick=name=="inkFlick",explosion=name=="inkExplosion",impact=name=="inkImpact",anticipation=name=="inkAnticipation";
    if(!slosh&&!flick&&!explosion&&!impact&&!anticipation)return false;
    if(!rng)rng=0x6d2b79f5u;
    const double factor=std::clamp(std::isfinite(context.factor)&&context.factor>0?context.factor:1.0,.4,1.0);
    const double scale=std::clamp(std::isfinite(radius)&&radius>0?radius:1.0,.4,2.0);
    const auto forward=normalized(direction),right=normalized(glm::cross(forward,std::abs(forward.y)<.9?glm::dvec3(0,1,0):glm::dvec3(1,0,0))),up=normalized(glm::cross(right,forward));
    if(anticipation){
        for(int i=0;i<3;i++){
            const auto at=pos+right*((i-1)*.06);
            sprite(out,WorldFxShape::Puff,at,forward*.4,color,.025,.08,.10,.55,rng,3,0,0,0,0,0,.06);out.back().flags=InkBurstMicro;
        }
        return true;
    }
    const int heroes=explosion?7:3,mids=impact?10:int((explosion?40:28)*factor),micros=impact?24:int((explosion?96:64)*factor);
    const double speed=impact?3.5:explosion?11.0:std::clamp(context.speed,5.0,25.0);
    const auto at=pos+(impact?normalized(normal)*.025:glm::dvec3(0));
    const auto heading=[&](int index,int count,double spread){
        if(explosion){auto d=sphere(rng);d.y=std::abs(d.y)*.8+.2;return normalized(d);}
        if(impact)return cone(normal,.9,rng);
        const double horizontal=(count>1?double(index)/(count-1)*2-1:0)*spread+(rand01(rng)-.5)*.12;
        const double vertical=(rand01(rng)-.2)*(slosh?.30:.18);
        return normalized(forward+right*horizontal+up*vertical);
    };
    for(int i=0;i<heroes;i++){
        const auto dir=heading(i,heroes,flick?.65:.42);
        const double velocity=speed*(.72+rand01(rng)*.35),size=(impact?.09:explosion?.14:.15)*scale*(.85+rand01(rng)*.4);
        const double life=impact?.18+rand01(rng)*.08:.38+rand01(rng)*.18;
        drop(out,at+right*((rand01(rng)-.5)*.1),dir*velocity,color,size,life,impact?.65:1.1,explosion?1.15:1.7,F_VISUAL_ONLY|InkBurstHero,rng);
    }
    for(int i=0;i<mids;i++){
        const auto dir=heading(i,mids,flick?.95:.68);
        const double velocity=speed*(.7+rand01(rng)*.65),size=scale*(.022+rand01(rng)*.035),life=.18+rand01(rng)*.22;
        drop(out,at,dir*velocity,color,size,life,1,1.4,F_VISUAL_ONLY|InkBurstMid,rng);
    }
    for(int i=0;i<micros;i++){
        const auto dir=heading(i,micros,flick?1.25:.9);
        const double velocity=speed*(1.1+rand01(rng)*.8),size=scale*(.008+rand01(rng)*.018),life=.09+rand01(rng)*.12;
        sprite(out,WorldFxShape::Puff,at,dir*velocity,color,size,size*.35,life,.75,rng,2,-4,0,0,3,0,.07);out.back().flags=InkBurstMicro;
    }
    if(impact||explosion)shell(out,at,color,scale*.12,scale*.75,.065,.20,.93,0,0,.12,normal,impact?1:0,rng);
    return true;
}
