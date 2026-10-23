bool buildWeaponRecipe(std::vector<WorldFxParticle>& out,std::string_view name,
    glm::dvec3 pos,glm::dvec3 normal,glm::dvec3 color,double radius,std::uint32_t& rng,
    glm::dvec3 direction,double time,double dt,glm::dvec3 camera,const WorldFxRippleSink& ripple){
    const glm::dvec3 white(1.0);
    if(name=="shotTrail"){
        const bool big=radius>0.5;
        const auto puffColor=mix(color,white,0.42);
        const double size=big?0.3:0.14;
        sprite(out,WorldFxShape::Puff,pos,direction*0.06+glm::dvec3(0,0.1,0),puffColor,
            size*0.5,size*1.5,big?0.4:0.26,big?0.3:0.2,rng,5,0.1);
        if(rand01(rng)<(big?0.8:0.35)){
            const double vx=direction.x*0.25+(rand01(rng)-0.5);
            const double vz=direction.z*0.25+(rand01(rng)-0.5);
            const double dropSize=big?0.03+rand01(rng)*0.02:0.016+rand01(rng)*0.012;
            drop(out,pos,{vx,direction.y*0.25,vz},color,dropSize,0.8,1,1.4,F_QUIET,rng);
        }
        return true;
    }
    if(name=="sloshTrail"){
        const bool head=radius>0.5;
        int count=head?2:(rand01(rng)<0.6?1:0);
        while(count-->0){
            const double px=pos.x+(rand01(rng)-0.5)*0.08;
            const double pz=pos.z+(rand01(rng)-0.5)*0.08;
            const double vx=direction.x*0.25+(rand01(rng)-0.5)*0.8;
            const double vz=direction.z*0.25+(rand01(rng)-0.5)*0.8;
            const double size=(head?0.026:0.018)+rand01(rng)*0.016;
            drop(out,{px,pos.y-0.04,pz},{vx,direction.y*0.2-0.5,vz},color,size,1.1,1,1.5,0,rng);
        }
        return true;
    }
    if(name=="sloshImpact"){
        const glm::dvec3 n=glm::length(normal)>0?normal:glm::dvec3(0,1,0);
        const glm::dvec3 at=pos+n*0.03;
        shell(out,at,color,0.3,1.05,0.08,0.3,0.97,0,0,0.22,n,1,rng);
        const double horizontal=std::hypot(direction.x,direction.z);
        const double fx=horizontal>0?direction.x/horizontal:0;
        const double fz=horizontal>0?direction.z/horizontal:0;
        const double rx=fz,rz=-fx;
        constexpr int paint=F_PAINT;
        for(int i=0;i<18;i++){
            const double side=(rand01(rng)-0.5)*2;
            const double speed=3.5+rand01(rng)*3.5;
            const glm::dvec3 p(at.x+rx*side*0.3,at.y+0.05,at.z+rz*side*0.3);
            const glm::dvec3 v(fx*speed+rx*side*2.2,1.2+rand01(rng)*2.2,fz*speed+rz*side*2.2);
            const double size=0.02+rand01(rng)*0.03;
            const int flags=rand01(rng)<0.3?paint:0;
            drop(out,p,v,color,size,1.2,1,1.5,flags,rng);
        }
        for(int i=0;i<5;i++){
            const double angle=rand01(rng)*tau;
            const double speed=1.6+rand01(rng)*1.8;
            const double vy=3+rand01(rng)*2.2;
            const double size=0.06+rand01(rng)*0.03;
            drop(out,{at.x,at.y+0.1,at.z},
                {std::cos(angle)*speed+fx*1.2,vy,std::sin(angle)*speed+fz*1.2},
                color,size,1.8,1,1,paint,rng);
        }
        if(ripple)ripple(at,0.014,0.26,2.3,1.05);
        else ring(out,at,n,color,2.032,0.735,1,0.45,1,rng);
        return true;
    }
    if(name=="chargeGlow"){
        const double k=radius;
        const double flick=0.85+0.15*std::sin(time*40);
        const auto glowColor=mix(color,white,0.2+0.4*k)*(1.5+2.5*k);
        const double size=(0.07+0.2*k)*flick;
        sprite(out,WorldFxShape::Glow,pos,{0,0,0},glowColor,size,size,std::max(0.03,dt*1.6),1,rng,0,0,k*3+1,0,0);
        int count=static_cast<int>(std::floor((10+36*k)*dt*std::max(0.5,1.0)+rand01(rng)));
        const auto sparkColor=mix(color,white,0.5)*2.2;
        while(count-->0){
            const auto d=sphere(rng);
            const double r=0.3+rand01(rng)*0.25;
            sprite(out,WorldFxShape::Glow,pos+d*r,-d*(r/0.17),sparkColor,0.05,0.02,0.17,1,rng,0,0,1,0.03,0);
        }
        return true;
    }
    if(name=="chargeFull"){
        const auto glowColor=mix(color,white,0.5)*3.2;
        sprite(out,WorldFxShape::Glow,pos,{0,0,0},glowColor,1.0,0.5,0.32,1,rng,0,0,13,0,0.6);
        const auto face=normalized(camera-pos);
        ring(out,pos,face,mix(color,white,0.35),0.6,0.2,7,0.9,1,rng);
        const auto sparkColor=mix(color,white,0.5)*2.0;
        for(int i=0;i<8;i++){
            const auto d=sphere(rng);
            sprite(out,WorldFxShape::Glow,pos,d*2.2,sparkColor,0.12,0.05,0.3,1,rng,3,0,11,0,2);
        }
        return true;
    }
    if(name=="laserDot"){
        const double k=radius;
        const double pulse=0.8+0.2*std::sin(time*22);
        const auto glowColor=mix(color,white,0.35)*(2+2*k);
        const auto at=pos+normal*0.03;
        const double size=(0.08+0.1*k)*pulse;
        sprite(out,WorldFxShape::Glow,at,{0,0,0},glowColor,size,size,std::max(0.03,dt*1.6),1,rng,0,0,11+2*k,0,0);
        return true;
    }
    if(name=="beamImpact"){
        const double charge=std::clamp(radius,0.0,1.0);
        sprite(out,WorldFxShape::Glow,pos,{0,0,0},color*4.0,0.4+0.5*charge,0.9+0.6*charge,0.12,1,rng,0,0,4,0);
        sprite(out,WorldFxShape::Glow,pos,{0,0,0},mix(color,white,0.5)*2.6,0.9,0.4,0.22,1,rng,0,0,13,0,1);
        const auto at=pos+normal*0.02;
        ring(out,at,normal,color,0.55+0.6*charge,0.3,0,0.95,1,rng);
        ring(out,at,normal,color,0.9+charge,0.2,7,0.8,1,rng);
        const int count=std::max(3,static_cast<int>(std::round(8+10*charge)));
        for(int i=0;i<count;i++){
            const auto d=cone(normal,0.95,rng);
            const double speed=3+rand01(rng)*3.5;
            const double size=0.03+rand01(rng)*0.04;
            drop(out,at,d*speed,color,size,1.1,1,1,0,rng);
        }
        return true;
    }
    if(name=="beamTrail"){
        const glm::dvec3 from=pos,to=normal;
        const glm::dvec3 delta=to-from;
        const double length=glm::length(delta);
        if(length<0.5)return true;
        const glm::dvec3 axis=delta/length;
        const int count=std::min(40,static_cast<int>(std::floor(length/0.75)));
        const auto mistColor=mix(color,white,0.45);
        for(int i=1;i<=count;i++){
            const double distance=i*0.75-rand01(rng)*0.3;
            const auto at=from+axis*distance;
            const double vx=(rand01(rng)-0.5)*0.3;
            const double vz=(rand01(rng)-0.5)*0.3;
            const double puffLife=0.3+rand01(rng)*0.2;
            sprite(out,WorldFxShape::Puff,at,{vx,0.15,vz},mistColor,0.06,0.2+0.1*radius,puffLife,0.28,rng,4,0.1);
            if(rand01(rng)<0.45){
                const double dx=(rand01(rng)-0.5)*0.6;
                const double dy=-0.5-rand01(rng);
                const double dz=(rand01(rng)-0.5)*0.6;
                const double size=0.016+rand01(rng)*0.018;
                drop(out,at,{dx,dy,dz},color,size,0.9,1,1.4,F_QUIET,rng);
            }
        }
        return true;
    }
    if(name=="spinUp"||name=="spinStream"){
        const bool streaming=name=="spinStream";
        const double k=std::clamp(radius,0.0,1.0);
        glm::dvec3 axis=direction;
        if(glm::dot(axis,axis)<1e-6)axis={0,0,1};else axis=glm::normalize(axis);
        const double glowSize=(0.045+0.1*k)*(1+0.06*k*k*std::sin(time*12));
        const auto glowColor=mix(color,white,0.2+0.4*k)*(1.1+3.4*k*k);
        sprite(out,WorldFxShape::Glow,pos+axis*0.03,{0,0,0},glowColor,glowSize,glowSize,std::max(0.03,dt*1.6),1,rng,0,0,1+3*k,0,0);
        int count=static_cast<int>(std::floor((streaming?30:3+38*k*k)*dt*std::max(0.5,1.0)+rand01(rng)));
        if(count<=0)return true;
        const glm::dvec3 reference=std::abs(axis.y)<0.95?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
        const glm::dvec3 tangent=normalized(glm::cross(reference,axis));
        const glm::dvec3 bitangent=glm::cross(axis,tangent);
        while(count-->0){
            const double angle=rand01(rng)*tau,c=std::cos(angle),s=std::sin(angle);
            const glm::dvec3 radial=tangent*c+bitangent*s;
            const glm::dvec3 spin=-tangent*s+bitangent*c;
            const double back=0.06+rand01(rng)*0.16;
            const double speed=(1.2+3.6*k)*(0.7+rand01(rng)*0.6);
            const double forward=streaming?2+rand01(rng)*2:0.2+rand01(rng)*0.5;
            const glm::dvec3 at=pos-axis*back+radial*0.065;
            const glm::dvec3 velocity=spin*speed+radial*(speed*0.35)+axis*forward+glm::dvec3(0,0.5,0);
            const double size=0.011+rand01(rng)*0.013+0.008*k;
            const int flags=rand01(rng)<0.55?F_QUIET:0;
            drop(out,at,velocity,color,size,0.75,1,1.5,flags,rng);
        }
        return true;
    }
    if(name=="spinFull"){
        glm::dvec3 axis=direction;
        if(glm::dot(axis,axis)<1e-6)axis={0,0,1};else axis=glm::normalize(axis);
        sprite(out,WorldFxShape::Glow,pos,{0,0,0},mix(color,white,0.55)*3.2,0.34,0.12,0.16,1,rng,0,0,3,0);
        ring(out,pos-axis*0.08,axis,mix(color,white,0.35),0.46,0.2,7,0.85,0.8,rng);
        const glm::dvec3 reference=std::abs(axis.y)<0.95?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
        const glm::dvec3 tangent=normalized(glm::cross(reference,axis));
        const glm::dvec3 bitangent=glm::cross(axis,tangent);
        constexpr int count=16;
        for(int i=0;i<count;i++){
            const double angle=(double(i)/count)*tau+rand01(rng)*0.3,c=std::cos(angle),s=std::sin(angle);
            const glm::dvec3 radial=tangent*c+bitangent*s;
            const glm::dvec3 spin=-tangent*s+bitangent*c;
            const double speed=3.2+rand01(rng)*1.6;
            const glm::dvec3 at=pos-axis*0.1+radial*0.07;
            const glm::dvec3 velocity=(radial*0.8+spin*0.6)*speed+glm::dvec3(0,0.8,0);
            const double size=0.016+rand01(rng)*0.014;
            drop(out,at,velocity,color,size,0.8,1,1.5,0,rng);
        }
        return true;
    }
    return false;
}
