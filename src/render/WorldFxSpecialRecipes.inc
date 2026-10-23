bool buildSpecialRecipe(std::vector<WorldFxParticle>& out,std::string_view name,
    glm::dvec3 pos,glm::dvec3 normal,glm::dvec3 color,double radius,std::uint32_t& rng,
    glm::dvec3 direction,double time,double dt,glm::dvec3 camera,const WorldFxRippleSink& ripple){
    const glm::dvec3 white(1.0),up(0.0,1.0,0.0);
    if(name=="rainSheet"){
        const auto rainColor=mix(color,white,0.35);
        int count=static_cast<int>(std::floor(radius*radius*3.2*std::clamp(dt,0.0,0.05)+rand01(rng)));
        while(count-->0){
            const double angle=rand01(rng)*tau,r=std::sqrt(rand01(rng))*radius*0.95;
            const double atY=pos.y-0.4-rand01(rng)*0.5;
            const glm::dvec3 at(pos.x+std::cos(angle)*r,atY,pos.z+std::sin(angle)*r);
            const double velocityY=-19.0-rand01(rng)*4.0;
            const double size=0.022+rand01(rng)*0.012;
            drop(out,at,{0.35,velocityY,0.12},rainColor,size,1.0,1.0,5.5,F_QUIET,rng);
        }
        return true;
    }
    if(name=="stormPuddle"){
        const auto n=normalized(normal);
        const auto at=pos+n*0.01;
        const auto bright=mix(color,white,0.15);
        const double rippleAmp=0.004+rand01(rng)*0.003;
        if(ripple)ripple(at,rippleAmp,0.09,0.8,0.5);
        else ring(out,at,n,bright,0.8*0.5*0.8+0.1,0.5*0.7,1,0.45,1,rng);
        if(rand01(rng)<0.35){
            const auto tangent=normalized(glm::cross(n,std::abs(n.y)<0.95?up:glm::dvec3(1,0,0)));
            const auto bitangent=glm::cross(n,tangent);
            for(int i=0;i<6;i++){
                const double angle=tau*double(i)/6.0+rand01(rng)*0.12;
                const auto radial=tangent*std::cos(angle)+bitangent*std::sin(angle);
                const double speed=0.8+rand01(rng)*0.6;
                const double velocityUp=0.35+rand01(rng)*0.35;
                const double size=0.012+rand01(rng)*0.01;
                drop(out,at+n*0.02+radial*0.08,radial*speed+n*velocityUp,color,
                    size,0.34,0.65,1.0,F_QUIET,rng);
            }
        }
        return true;
    }
    if(name=="stormFlash"){
        const auto glowColor=mix(color,white,0.5)*2.5;
        const double atX=pos.x+(rand01(rng)-0.5)*radius;
        const double atY=pos.y+(rand01(rng)-0.3)*0.8;
        const double atZ=pos.z+(rand01(rng)-0.5)*radius;
        const glm::dvec3 at(atX,atY,atZ);
        sprite(out,WorldFxShape::Glow,at,{},glowColor,radius*0.4,radius*0.7,0.12,1.0,rng,0,0,1,0);
        return true;
    }
    if(name=="wailRing"){
        ring(out,pos,normalized(normal),color,radius,0.3,2,0.9,1.0,rng);
        return true;
    }
    if(name=="bubblePop"||name=="shieldPop"||name=="jetpackBoost"||name=="jetpackExhaust"){
        int count=10;double speed=3.0,size=0.08,spread=0.9;
        if(name=="shieldPop"){count=12;size=0.07;}
        else if(name=="jetpackBoost"){count=8;speed=7.0;size=0.09;}
        else if(name=="jetpackExhaust"){count=2;speed=4.0;size=0.07;}
        const auto n=normalized(normal);
        const auto at=pos+n*0.04;
        for(int i=0;i<count;i++){
            const auto dir=cone(n,std::max(0.05,spread)*pi*0.5,rng);
            const double velocity=speed*(0.5+rand01(rng)*0.8),dropSize=size*(0.35+rand01(rng)*0.6);
            drop(out,at,dir*velocity,color,dropSize,1.6+rand01(rng)*0.6,1.0,1.3,0,rng);
            if(rand01(rng)<0.45){
                const double satellite=velocity*(0.7+rand01(rng)*0.5);
                const double noiseX=(rand01(rng)-0.5)*0.8;
                const double noiseY=(rand01(rng)-0.5)*0.8;
                const double noiseZ=(rand01(rng)-0.5)*0.8;
                const glm::dvec3 noise(noiseX,noiseY,noiseZ);
                drop(out,at,dir*satellite+noise,color,dropSize*0.4,1.2,1.0,1.3,0,rng);
            }
        }
        const double splash=size*(1.6+0.5*std::sqrt(double(count)));
        if(count>=4&&glm::distance(pos,camera)<24.0){
            shell(out,pos+n*splash*0.12,color,splash*0.35,splash*1.1,0.07,0.17+splash*0.12,0.95,0,0,0.2,n,1,rng);
        }
        if(ripple)ripple(pos,0.0035+size*0.03,0.1,1.0,0.5);
        return true;
    }
    (void)direction;(void)time;
    return false;
}
