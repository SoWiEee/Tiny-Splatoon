void kitCrown(std::vector<WorldFxParticle>& out,glm::dvec3 pos,glm::dvec3 normal,glm::dvec3 color,
              double count,double speed,double size,int flags,std::uint32_t& rng){
    normal=normalized(normal);
    const glm::dvec3 reference=std::abs(normal.y)<0.95?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
    const glm::dvec3 tangent=normalized(glm::cross(reference,normal));
    const glm::dvec3 bitangent=glm::cross(normal,tangent);
    const int total=std::max(1,static_cast<int>(std::round(count)));
    const double angle0=rand01(rng)*tau;
    for(int i=0;i<total;++i){
        const double angle=angle0+(double(i)/total)*tau+(rand01(rng)-0.5)*0.5;
        const double c=std::cos(angle),s=std::sin(angle);
        const double upAmount=0.7+rand01(rng)*0.9,outward=0.6+rand01(rng)*0.6;
        const double velocityScale=speed*(0.6+rand01(rng)*0.6);
        const glm::dvec3 velocity=((tangent*c+bitangent*s)*outward+normal*upAmount)*velocityScale;
        const double particleSize=size*(0.55+rand01(rng)*0.8);
        drop(out,pos+normal*0.04,velocity,color,particleSize,1.2,1,1,flags,rng);
    }
}

bool buildKitRecipe(std::vector<WorldFxParticle>& out,std::string_view name,glm::dvec3 pos,
                    glm::dvec3 normal,glm::dvec3 color,double radius,std::uint32_t& rng,
                    glm::dvec3 direction,double time,double dt,glm::dvec3 camera,
                    const WorldFxRippleSink& ripple,const WorldFxRecipeContext& context){
    (void)camera;
    const glm::dvec3 up(0,1,0),white(1);
    const glm::dvec3 sourceNormal=normal;
    normal=normalized(normal);
    const double frameDt=std::clamp(dt,0.0,0.05);
    if(name=="glint"){
        const glm::dvec3 glintColor=mix(color,white,0.6)*2.6;
        sprite(out,WorldFxShape::Glow,pos,glm::dvec3(0),glintColor,radius*0.4,radius,
            0.35+rand01(rng)*0.2,1,rng,0,0,12,0.12,0.8);
    }else if(name=="drop"){
        drop(out,pos,direction,color,radius>0?radius:0.1,1.2,1,1,0,rng);
    }else if(name=="mist"){
        const double size=radius>0?radius:0.2;
        sprite(out,WorldFxShape::Puff,pos,direction,mix(color,white,0.4),size*0.45,size*1.4,
            0.3+rand01(rng)*0.12,context.factor>0?context.factor:0.25,rng,4.5,0.2);
    }else if(name=="chargeGlow"){
        const double k=std::clamp(radius,0.0,1.0),flick=0.85+0.15*std::sin(time*40.0);
        const glm::dvec3 glow=mix(color,white,0.2+0.4*k)*(1.5+2.5*k);
        const double size=(0.07+0.2*k)*flick;
        sprite(out,WorldFxShape::Glow,pos,glm::dvec3(0),glow,size,size,std::max(0.03,frameDt*1.6),1,rng,0,0,1+3*k,0,0);
        int count=static_cast<int>(std::floor((10+36*k)*frameDt+rand01(rng)));
        const glm::dvec3 sparks=mix(color,white,0.5)*2.2;
        while(count-->0){
            const double y=rand01(rng)*2-1,radial=std::sqrt(std::max(0.0,1-y*y)),phi=rand01(rng)*tau;
            const glm::dvec3 sphereDir(radial*std::cos(phi),y,radial*std::sin(phi));
            const double r=0.3+rand01(rng)*0.25;
            const glm::dvec3 at=pos+sphereDir*r,velocity=-sphereDir*(r/0.17);
            sprite(out,WorldFxShape::Glow,at,velocity,sparks,0.05,0.02,0.17,1,rng,0,0,1,0.03,0);
        }
    }else if(name=="climbDrip"){
        const glm::dvec3 n=sourceNormal;
        const double atX=pos.x+n.x*0.06+(rand01(rng)-0.5)*0.2;
        const double atY=pos.y+(rand01(rng)-0.5)*0.2;
        const double atZ=pos.z+n.z*0.06+(rand01(rng)-0.5)*0.2;
        const glm::dvec3 at(atX,atY,atZ);
        const double velocityX=n.x*0.25+(rand01(rng)-0.5)*0.2;
        const double velocityY=-0.4-rand01(rng)*0.6;
        const double velocityZ=n.z*0.25+(rand01(rng)-0.5)*0.2;
        const glm::dvec3 velocity(velocityX,velocityY,velocityZ);
        drop(out,at,velocity,color,0.024+rand01(rng)*0.022,1.4,0.5,1.3,0,rng);
        const glm::dvec3 point=pos+n*0.03,rippleColor=mix(color,white,0.2);
        if(rand01(rng)<0.5){
            if(ripple)ripple(point,0.004,0.12,0.9,0.55);
            else ring(out,point,n,rippleColor,0.496,0.385,1,0.45,1,rng);
        }
    }else if(name=="superJumpTrail"){
        const glm::dvec3 velocity=direction;
        for(int i=0;i<2;++i){
            const double atX=pos.x+(rand01(rng)-0.5)*0.3;
            const double atY=pos.y+(rand01(rng)-0.5)*0.3;
            const double atZ=pos.z+(rand01(rng)-0.5)*0.3;
            const glm::dvec3 at(atX,atY,atZ);
            const double velocityX=velocity.x*0.15+(rand01(rng)-0.5);
            const double velocityZ=velocity.z*0.15+(rand01(rng)-0.5);
            const glm::dvec3 v(velocityX,velocity.y*0.15,velocityZ);
            drop(out,at,v,color,0.03+rand01(rng)*0.035,1.1,1,1.4,0,rng);
        }
        sprite(out,WorldFxShape::Glow,pos,glm::dvec3(0),mix(color,white,0.2)*2.2,
            0.55,0.3,0.16,0.9,rng,0,0,1.5,0);
        if(rand01(rng)<0.5)
            sprite(out,WorldFxShape::Puff,pos,glm::dvec3(0,0.2,0),mix(color,white,0.35),
                0.2,0.55,0.45,0.3,rng,3);
    }else if(name=="superJumpLand"){
        const glm::dvec3 groundPos(pos.x,pos.y+0.02,pos.z);
        ring(out,groundPos,up,color,2.4,0.45,0,0.95,1.2,rng);
        ring(out,groundPos,up,color,3.0,0.3,7,0.8,1,rng);
        kitCrown(out,groundPos,up,color,26,4.2,0.045,0,rng);
        shell(out,{pos.x,pos.y+0.1,pos.z},color,0.2,0.9,0.08,0.3,0.95,0,0,0.18,up,1,rng);
        if(ripple)ripple(groundPos,0.013,0.24,2.4,1.1);
        else ring(out,groundPos,up,color,2.212,0.77,1,0.45,1,rng);
        sprite(out,WorldFxShape::Glow,{pos.x,pos.y+0.5,pos.z},glm::dvec3(0),color*3.0,
            0.8,1.8,0.18,1,rng,0,0,3,0);
        const glm::dvec3 dust=mix(glm::dvec3(0.4793201830913402,0.3967552307153359,0.2746773120495699),color,0.2);
        const int count=8;const double angle0=rand01(rng)*tau;
        for(int i=0;i<count;++i){
            const double angle=angle0+(double(i)/count)*tau+(rand01(rng)-0.5)*0.4;
            const double speed=3*(0.6+rand01(rng)*0.6);
            const double atY=groundPos.y+0.12+rand01(rng)*0.12;
            const glm::dvec3 at(pos.x+std::cos(angle)*0.25,atY,pos.z+std::sin(angle)*0.25);
            const double velocityY=0.3+rand01(rng)*0.5;
            const glm::dvec3 v(std::cos(angle)*speed,velocityY,std::sin(angle)*speed);
            const double size0=0.16+rand01(rng)*0.1,size1=0.5+rand01(rng)*0.35;
            const double life=0.55+rand01(rng)*0.35;
            sprite(out,WorldFxShape::Puff,at,v,dust,size0,size1,life,0.3,rng,3.2,0.25,4,0.05,1.2);
        }
    }else if(name=="waterPlop"){
        const double size=radius>0?radius:0.3;
        const double waterY=pos.y;
        const glm::dvec3 point(pos.x,waterY+0.02,pos.z);
        const glm::dvec3 foam(0.85499260812423383,0.94730653673319987,1.0);
        const glm::dvec3 water(0.5209955732043543,0.81484657221610124,1.0);
        ring(out,point,up,foam,0.35+size,0.5,1,0.7,1,rng);
        kitCrown(out,point,up,water,4+size*8,1.8+size*2,0.03,F_NOCOL,rng);
    }else if(name=="leapRing"){
        ring(out,pos,normal,color,radius>0?radius:1.3,0.35,0,0.95,1,rng);
    }else if(name=="canopyBreakRing"){
        ring(out,pos,normal,color,radius>0?radius:0.84,0.3,0,0.95,1,rng);
    }else if(name=="ring"){
        ring(out,pos,normal,color,radius,direction.y>0?direction.y:0.35,0,0.95,1,rng);
    }else{
        return false;
    }
    return true;
}
