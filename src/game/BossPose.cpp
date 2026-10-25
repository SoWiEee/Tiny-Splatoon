#include "BossPose.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace restoration {
namespace {
constexpr double pi=3.14159265358979323846, tau=2*pi;
double clamp(double x,double lo,double hi){return std::max(lo,std::min(hi,x));}
double mix(double a,double b,double t){return a+(b-a)*t;}
double k01(double t,double a,double b){return clamp((t-a)/(b-a),0,1);}
double sm(double x){return x*x*(3-2*x);}
double eo(double x){return 1-std::pow(1-x,3);}
double ei(double x){return x*x*x;}
double eob(double x){constexpr double c1=1.9,c3=c1+1;return 1+c3*std::pow(x-1,3)+c1*std::pow(x-1,2);}
double bump(double t,double a,double b){return t<=a||t>=b?0:std::sin(((t-a)/(b-a))*pi);}
double dampK(double l,double dt){return 1-std::exp(-l*dt);}
double fract(double x){return x-std::floor(x);}
double hashN(double i){const double s=std::sin(i*127.1+311.7)*43758.5453;return fract(s);}
double vnoise(double t,double seed=0){const double i=std::floor(t),f=t-i,u=f*f*(3-2*f);return mix(hashN(i+seed*57),hashN(i+1+seed*57),u)*2-1;}
glm::dquat quatYXZ(double x,double y,double z){return glm::normalize(glm::angleAxis(y,glm::dvec3(0,1,0))*glm::angleAxis(x,glm::dvec3(1,0,0))*glm::angleAxis(z,glm::dvec3(0,0,1)));}
glm::dvec3 normalizeThree(glm::dvec3 v){const double length=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);const double inverse=1.0/(length>0?length:1.0);return v*inverse;}
glm::dquat normalizeThree(glm::dquat q){const double length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);const double inverse=1.0/(length>0?length:1.0);return q*inverse;}
glm::dquat multiplyThreeQuat(const glm::dquat& a,const glm::dquat& b){return glm::dquat(a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.x*b.w+a.w*b.x+a.y*b.z-a.z*b.y,a.y*b.w+a.w*b.y+a.z*b.x-a.x*b.z,a.z*b.w+a.w*b.z+a.x*b.y-a.y*b.x);}
glm::dquat conjugateThree(const glm::dquat& q){return glm::dquat(q.w,-q.x,-q.y,-q.z);}
glm::dvec3 applyThreeQuat(const glm::dquat& q,glm::dvec3 v){const double ix=q.w*v.x+q.y*v.z-q.z*v.y,iy=q.w*v.y+q.z*v.x-q.x*v.z,iz=q.w*v.z+q.x*v.y-q.y*v.x,iw=-q.x*v.x-q.y*v.y-q.z*v.z;return glm::dvec3(ix*q.w+iw*-q.x+iy*-q.z-iz*-q.y,iy*q.w+iw*-q.y+iz*-q.x-ix*-q.z,iz*q.w+iw*-q.z+ix*-q.y-iy*-q.x);}
glm::dquat unitQuat(glm::dvec3 from,glm::dvec3 to){
    double r=from.x*to.x+from.y*to.y+from.z*to.z+1;
    if(r<1e-8){r=0;if(std::abs(from.x)>std::abs(from.z))return normalizeThree(glm::dquat(r,-from.y,from.x,0));return normalizeThree(glm::dquat(r,0,-from.z,from.y));}
    const glm::dquat q(r,from.y*to.z-from.z*to.y,from.z*to.x-from.x*to.z,from.x*to.y-from.y*to.x);return normalizeThree(q);
}
glm::dquat frameQuat(glm::dvec3 a0,glm::dvec3 s0,glm::dvec3 a1,glm::dvec3 s1){
    const auto frame=[](glm::dvec3 a,glm::dvec3 s){
        a=glm::normalize(a);s-=a*glm::dot(s,a);
        if(glm::dot(s,s)<1e-10){s=glm::dvec3(0,1,0)-a*a.y;}s=glm::normalize(s);
        return glm::dmat3(a,s,glm::cross(a,s));
    };
    return glm::normalize(glm::quat_cast(frame(a1,s1)*glm::transpose(frame(a0,s0))));
}
double ik2(glm::dvec3 h,glm::dvec3 a,double l1,double l2,glm::dvec3 pole,glm::dvec3& k,glm::dvec3* outA=nullptr){
    glm::dvec3 d=a-h;double dist=glm::length(d);const double reach=dist/(l1+l2),mx=(l1+l2)*.9995,mn=std::abs(l1-l2)+1e-3;
    if(dist<1e-6){d={0,-1,0};dist=mn;}d/=dist;const double dc=clamp(dist,mn,mx);if(outA)*outA=h+d*dc;
    const double along=(l1*l1-l2*l2+dc*dc)/(2*dc),height=std::sqrt(std::max(0.0,l1*l1-along*along));
    glm::dvec3 bend=pole-d*glm::dot(pole,d);if(glm::dot(bend,bend)<1e-8)bend=glm::dvec3(0,1,0)-d*d.y;bend=glm::normalize(bend);
    k=h+d*along+bend*height;return reach;
}
glm::dmat4 multiplyThree(const glm::dmat4& a,const glm::dmat4& b){glm::dmat4 result(0);for(int column=0;column<4;++column)for(int row=0;row<4;++row)result[column][row]=a[0][row]*b[column][0]+a[1][row]*b[column][1]+a[2][row]*b[column][2]+a[3][row]*b[column][3];return result;}
glm::dmat4 trs(glm::dvec3 p,glm::dquat q,glm::dvec3 s){const double x=q.x,y=q.y,z=q.z,w=q.w,x2=x+x,y2=y+y,z2=z+z,xx=x*x2,xy=x*y2,xz=x*z2,yy=y*y2,yz=y*z2,zz=z*z2,wx=w*x2,wy=w*y2,wz=w*z2;glm::dmat4 result(1);result[0][0]=(1-(yy+zz))*s.x;result[0][1]=(xy+wz)*s.x;result[0][2]=(xz-wy)*s.x;result[1][0]=(xy-wz)*s.y;result[1][1]=(1-(xx+zz))*s.y;result[1][2]=(yz+wx)*s.y;result[2][0]=(xz+wy)*s.z;result[2][1]=(yz-wx)*s.z;result[2][2]=(1-(xx+yy))*s.z;result[3][0]=p.x;result[3][1]=p.y;result[3][2]=p.z;return result;}
glm::dquat decomposeThree(const glm::dmat4& matrix){const double sx=std::sqrt(matrix[0][0]*matrix[0][0]+matrix[0][1]*matrix[0][1]+matrix[0][2]*matrix[0][2]),sy=std::sqrt(matrix[1][0]*matrix[1][0]+matrix[1][1]*matrix[1][1]+matrix[1][2]*matrix[1][2]),sz=std::sqrt(matrix[2][0]*matrix[2][0]+matrix[2][1]*matrix[2][1]+matrix[2][2]*matrix[2][2]);const double m11=matrix[0][0]/sx,m12=matrix[1][0]/sy,m13=matrix[2][0]/sz,m21=matrix[0][1]/sx,m22=matrix[1][1]/sy,m23=matrix[2][1]/sz,m31=matrix[0][2]/sx,m32=matrix[1][2]/sy,m33=matrix[2][2]/sz,trace=m11+m22+m33;double x,y,z,w;if(trace>0){const double k=.5/std::sqrt(trace+1);w=.25/k;x=(m32-m23)*k;y=(m13-m31)*k;z=(m21-m12)*k;}else if(m11>m22&&m11>m33){const double k=2*std::sqrt(1+m11-m22-m33);w=(m32-m23)/k;x=.25*k;y=(m12+m21)/k;z=(m13+m31)/k;}else if(m22>m33){const double k=2*std::sqrt(1+m22-m11-m33);w=(m13-m31)/k;x=(m12+m21)/k;y=.25*k;z=(m23+m32)/k;}else{const double k=2*std::sqrt(1+m33-m11-m22);w=(m21-m12)/k;x=(m13+m31)/k;y=(m23+m32)/k;z=.25*k;}return glm::dquat(w,x,y,z);}
glm::dvec3 point(const glm::dmat4& m,glm::dvec3 p){const double w=1.0/(m[0][3]*p.x+m[1][3]*p.y+m[2][3]*p.z+m[3][3]);return glm::dvec3((m[0][0]*p.x+m[1][0]*p.y+m[2][0]*p.z+m[3][0])*w,(m[0][1]*p.x+m[1][1]*p.y+m[2][1]*p.z+m[3][1])*w,(m[0][2]*p.x+m[1][2]*p.y+m[2][2]*p.z+m[3][2])*w);}
glm::dvec3 dir(const glm::dquat& q,glm::dvec3 v){return q*v;}

enum Channel { BX,BY,BZ,BP,BR,BYAW,SP,SR,SY,HP,HY,HR,LX,LY,LZ,LP,LYW,LR,LO,RX,RY,RZ,RP,RYW,RR,RO,EY,EP,ES,ED,MOUTH,SIPP,SIPY,CANNON,GLOWL,HA,HB,HC,SPLAY,CURL,BELLY,STEAM,PAW,SLIDE,MAND,TUCK,BELLYOUT,NC };
struct Channels {std::array<float,NC> v{};float& operator[](int i){return v[static_cast<std::size_t>(i)];}float operator[](int i)const{return v[static_cast<std::size_t>(i)];}};
void add(Channels& p,int i,double x){p[i]=static_cast<float>(static_cast<double>(p[i])+x);}
void assign(Channels& p,int i,double x){p[i]=static_cast<float>(x);}
void maxAssign(Channels& p,int i,double x){p[i]=static_cast<float>(std::max<double>(p[i],x));}

struct Spring {
    double k=0,d=0,x=0,v=0;
    Spring()=default;Spring(double stiffness,double damping):k(stiffness),d(damping){}
    double step(double dt,double target=0){const int n=dt>1.0/90?2:1;const double h=dt/n;for(int i=0;i<n;++i){v+=(k*(target-x)-d*v)*h;x+=v*h;}return x;}
};
struct Foot {
    glm::dvec3 foot{0},from{0},to{0},home{0};
    bool swing=false;double s=0,off=0,err=0;glm::dvec2 restXZ{0};
};
struct LegFrame {glm::dvec3 n{0},dm{0},dt{0},dd{0},coxOff{0},hipOff{0};double maxYaw=.8;};
struct ArmFrame {glm::dvec3 n{0},du{0},df{0},sOff{0},wOff{0},side{0},D{0},U{0};};
struct ChainState {std::vector<glm::dvec3> p,q;bool init=false;};
}

struct BossPose::Rig {
    std::vector<int> parent;
    std::vector<glm::dvec3> localPosition,localScale;
    std::vector<glm::dquat> localRotation;
    std::vector<glm::dmat4> restGlobal,inverseRest,currentGlobal,skin;
    std::unordered_map<std::string,std::size_t> byName;
};

struct BossPose::Animator {
    explicit Animator(const BossPoseRigData& source,const BossModelAsset& hull,const BossModelAsset& crab):source(source){
        const auto& legs=source.legs;
        for(std::size_t i=0;i<legs.size();++i){
            const auto& l=legs[i];const auto pole=glm::normalize(glm::dvec3(0,1,0)+l.out*.35);
            legFrames[i].n=glm::normalize(glm::cross(l.ankle-l.cox,pole));
            legFrames[i].dm=glm::normalize(l.knee-l.cox);legFrames[i].dt=glm::normalize(l.ankle-l.knee);legFrames[i].dd=glm::normalize(l.foot-l.ankle);
            legFrames[i].coxOff=l.cox-l.hip;legFrames[i].hipOff=l.hip-source.bodyPivot;legFrames[i].maxYaw=(i%3)?.45:.8;
            feet[i].restXZ={l.foot.x-source.bodyPivot.x,l.foot.z-source.bodyPivot.z};
            feet[i].off=(i%3==1?.5:0)+(i<3?0:.5);feet[i].off=fract(feet[i].off);
        }
        for(std::size_t i=0;i<source.arms.size();++i){
            const auto& a=source.arms[i];auto& f=armFrames[i];
            f.n=glm::normalize(glm::cross(a.wrist-a.shoulder,a.pole));f.du=glm::normalize(a.elbow-a.shoulder);f.df=glm::normalize(a.wrist-a.elbow);
            f.sOff=a.shoulder-source.bodyPivot;f.wOff=a.wrist-source.bodyPivot;f.side=glm::normalize(glm::cross(a.up,a.direction));
            f.D=a.direction;f.U=glm::normalize(a.up-a.direction*glm::dot(a.up,a.direction));
        }
        chains.resize(source.chains.size());
        for(std::size_t c=0;c<chains.size();++c){chains[c].p.resize(source.chains[c].points.size());chains[c].q.resize(source.chains[c].points.size());}
        // The source meshes share these ink colors and immutable shader constants.
        uniforms={{"uInk",{.0258,.102,1}}, {"uWeak",{1,.258,.0037}}, {"uDrip",{.6}}, {"uAO",{1}}, {"uEyeSpin",{0}}};
        (void)hull;(void)crab;
    }
    const BossPoseRigData& source;
    Channels p,pb,px,pn;
    std::string key,lastMoveKey,lastMove,oneShot;
    double clipT=0,prevT=-1,curT=0,xf=1,xfDur=.2,time=0,phaseNow=1,teleDur=0,recoverT=99,deadT=0,autoRoarT=99,prevHurt=0;
    int prevPhase=0,phase=1;
    bool wasStunned=false,stunBonk=false,first=true,resetFeet=true,gaitOn=false,hasAim=false;
    glm::dvec3 rootPos{0},rootPrev{0},vel{0},aimLocal{0},bodyPos{0};double yaw=0,yawPrev=0,yawRate=0,phi=0,cad=0,aimYaw=0,aimPitch=0;
    glm::dquat rootQ{1,0,0,0},rootQi{1,0,0,0},bodyQ{1,0,0,0};
    std::array<Foot,6> feet{};std::array<LegFrame,6> legFrames{};std::array<ArmFrame,2> armFrames{};
    glm::dvec3 prevHead{0},headPrev{0},headPrev2{0};bool haveHead=false;
    std::array<Spring,14> springs{{Spring(260,11),Spring(260,11),Spring(320,12),Spring(40,9),Spring(40,9),Spring(90,10),Spring(90,10),Spring(70,9),Spring(14,1.3),Spring(110,5),Spring(110,5),Spring(110,5),Spring(110,5),Spring(60,7)}};
    std::vector<ChainState> chains;double chainAcc=0;
    double flash=0,weakFlash=0,lastFlashT=-9,crack=0,enrage=0,modelTime=0,lvx=0,lvz=0;
    std::map<std::string,std::vector<double>> uniforms;
    std::map<int,double> crabPhi,crabSeed;
    std::vector<BossPoseAudioEvent> audioEvents;

    void footstep(glm::dvec3 position,double strength,int leg){
        BossPoseAudioEvent event;event.kind=BossPoseAudioEvent::Kind::Footstep;event.position=position;
        event.strength=strength;event.time=modelTime;event.leg=leg;audioEvents.push_back(std::move(event));
    }
    void impact(std::string socket,double strength){
        BossPoseAudioEvent event;event.kind=BossPoseAudioEvent::Kind::Impact;event.strength=strength;
        event.time=modelTime;event.socket=std::move(socket);audioEvents.push_back(std::move(event));
    }
    void fx(std::string name,std::string socket){
        BossPoseAudioEvent event;event.kind=BossPoseAudioEvent::Kind::Fx;event.time=modelTime;
        event.name=std::move(name);event.socket=std::move(socket);audioEvents.push_back(std::move(event));
    }
    void intro(){
        BossPoseAudioEvent event;event.kind=BossPoseAudioEvent::Kind::Intro;event.time=modelTime;event.position=rootPos+glm::dvec3(0,3,0);
        audioEvents.push_back(std::move(event));
    }

    void update(BossPose& owner,Rig& rig,const BossPoseInput& input);
    void animateCrablet(Rig& rig,const BossCrablet& crab,double dt,double t,double seed,double& phi);
    void loco(Channels& out,double dt,const BossPoseInput& s);
    void clip(Channels& out,const std::string& clipName,const std::string& movePhase,double t,double d,const BossPoseInput& s);
    void apply(Rig& rig,double dt,const BossPoseInput& s);
    void gait(double dt,const BossPoseInput& s);
    void chainsStep(Rig& rig,double dt,const BossPoseInput& s);
    glm::dvec3 home(const Foot& foot,std::size_t i,const Channels& pose,double ahead=0)const;
    void kickShell(double a){springs[2].v+=-1.6*a;springs[0].v+=(std::sin(time*13.1)>0?1:-1)*1.1*a;springs[1].v+=(std::sin(time*7.7)>0?1:-1)*1.3*a;springs[8].v+=2.5*a;}
    void flinch(double a){springs[5].v+=-3.6*a;springs[6].v+=(std::sin(time*91.7)>0?1:-1)*1.6*a;springs[7].v+=6*a;springs[2].v+=-.8*a;}
};

BossPose::BossPose(const std::filesystem::path& path)
    :assets_(LoadBossModels(path)),sourceRig_(LoadBossPoseRig(path.parent_path()/"pose-rig.bin")){
    rigs_.resize(assets_.size());
    for(std::size_t r=0;r<rigs_.size();++r){auto& rig=rigs_[r];const auto& asset=assets_[r];const auto n=asset.bones.size();if(r==0&&sourceRig_.boneRest.size()!=n)throw std::runtime_error("Boss pose-rig rest bones do not match source model");
        rig.parent.reserve(n);rig.localPosition.resize(n);rig.localScale.assign(n,glm::dvec3(1));rig.localRotation.assign(n,glm::dquat(1,0,0,0));
        rig.restGlobal.resize(n);rig.inverseRest.resize(n);rig.currentGlobal.resize(n);rig.skin.resize(n);
        for(std::size_t i=0;i<n;++i){const auto& bone=asset.bones[i];rig.byName.emplace(bone.name,i);rig.parent.push_back(bone.parent);if(r==0&&sourceRig_.boneRest[i].name!=bone.name)throw std::runtime_error("Boss pose-rig rest bone order differs from source model");rig.localPosition[i]=r==0?sourceRig_.boneRest[i].position:glm::dvec3(bone.position);
            const auto local=glm::translate(glm::dmat4(1),rig.localPosition[i]);rig.restGlobal[i]=bone.parent<0?local:rig.restGlobal[static_cast<std::size_t>(bone.parent)]*local;rig.inverseRest[i]=glm::inverse(rig.restGlobal[i]);}
        Evaluate(rig);
    }
    animator_=std::make_unique<Animator>(sourceRig_,assets_.at(0),assets_.at(1));
    BossPoseInput initial;initial.dt=0;Update(initial);animator_->prevPhase=0;animator_->phase=0;
}

BossPose::~BossPose()=default;

void BossPose::Evaluate(Rig& rig){
    for(std::size_t i=0;i<rig.restGlobal.size();++i){const auto local=trs(rig.localPosition[i],rig.localRotation[i],rig.localScale[i]);rig.currentGlobal[i]=rig.parent[i]<0?local:multiplyThree(rig.currentGlobal[static_cast<std::size_t>(rig.parent[i])],local);rig.skin[i]=multiplyThree(rig.currentGlobal[i],rig.inverseRest[i]);}
}

void BossPose::AnimateHull(Rig& rig,const BossPoseInput& input){animator_->update(*this,rig,input);}

void BossPose::Animator::loco(Channels& o,double dt,const BossPoseInput& s){
    o.v.fill(0);const double v=std::hypot(vel.x,vel.z),yr=yawRate,wv=clamp(v/3,0,1),gal=clamp((v-5)/6,0,1),t=time;
    const double br=t*(1.55+.4*(s.phase>=3?1:0));add(o,BY,std::sin(br)*.04*(1-wv*.5));add(o,SP,std::sin(br-.7)*.012);add(o,HP,std::sin(br-.3)*.015);
    const double ph=phi*tau,gw=gaitOn?1:0,bobA=(.05+.05*wv+.2*gal)*clamp(cad/.8,0,1)*gw;
    add(o,BY,-bobA*(.5-.5*std::cos(ph*2))+.015*wv);add(o,BR,std::sin(ph)*(.025+.02*gal)*wv);add(o,BP,.05*wv+.1*gal+std::sin(ph*2+.6)*.02*wv);add(o,SP,-std::sin(ph*2+1.2)*.012*wv);
    add(o,BR,clamp(-yr*.035,-.08,.08));add(o,HY,clamp(yr*.12,-.25,.25));add(o,EY,clamp(yr*.18,-.4,.4));
    add(o,LZ,-.55+std::sin(ph+1.1)*.08*wv);add(o,LY,.12+std::cos(ph*2+.5)*.04*wv);add(o,LX,-.15);add(o,LP,.12);add(o,LYW,-.12);
    add(o,RZ,-.5+std::sin(ph+4.2)*.08*wv);add(o,RY,.08+std::cos(ph*2+2)*.04*wv);add(o,RX,.12);add(o,RP,.05);add(o,RYW,.1);
    const double idle=1-wv;assign(o,LO,.14+.06*std::sin(t*.9)+.35*bump(std::fmod(t,5.3),0,.35)*idle);assign(o,RO,.18+.08*std::sin(t*1.3+1)+.4*bump(std::fmod(t+2.1,3.7),0,.25)*idle);
    add(o,LR,std::sin(t*.7)*.05);add(o,RR,std::sin(t*.9+1)*.06);add(o,EY,vnoise(t*.45,1)*.3);add(o,EP,vnoise(t*.4,2)*.12);add(o,ES,vnoise(t*.3,3)*.1);add(o,MAND,(std::sin(t*7)*.5+.5)*.15);
    if(hasAim){add(o,EY,clamp(aimYaw*.5,-.5,.5));add(o,HY,clamp(aimYaw*.12,-.12,.12));add(o,EP,clamp(aimPitch*.5,-.3,.3));}
    if(s.phase>=3){assign(o,STEAM,.55);assign(o,BELLY,1);}
    (void)dt;
}

void BossPose::Animator::clip(Channels& P,const std::string& name,const std::string& ph,double t,double D,const BossPoseInput& s){
    const auto cross=[&](double at){return prevT<at&&curT>=at;};
    const auto slamUp=[&](double u){add(P,LX,.55*u);add(P,LY,2.45*u);add(P,LZ,-1.25*u);add(P,LP,1.65*u);add(P,LYW,.35*u);add(P,LR,-.5*u);P[LO]=static_cast<float>(mix(P[LO],.95,u));add(P,BP,-.13*u);add(P,BR,.07*u);add(P,BY,.2*u);add(P,BZ,-.28*u);add(P,BYAW,-.14*u);add(P,HP,-.08*u);add(P,RX,-.5*u);add(P,RY,.35*u);add(P,RZ,-.4*u);P[RO]=static_cast<float>(mix(P[RO],.55,u));add(P,RP,.3*u);add(P,SPLAY,.06*u);add(P,EP,.12*u);add(P,EY,.1*u);};
    const auto slamHit=[&](double f){add(P,LX,-.95*f);add(P,LY,-.62*f);add(P,LZ,.35*f);add(P,LP,-.32*f);add(P,LYW,-.42*f);P[LO]=static_cast<float>(mix(P[LO],.08,f));add(P,BP,.15*f);add(P,BR,-.06*f);add(P,BY,-.3*f);add(P,BZ,.32*f);add(P,BYAW,.16*f);add(P,HP,.1*f);add(P,RX,-.2*f);add(P,RY,-.35*f);add(P,RZ,.25*f);add(P,SPLAY,.1*f);};
    const auto aimBlend=[&](double w){if(!hasAim)return;const double by=clamp(aimYaw*.22,-.28,.28),hy=clamp((aimYaw-by)*.45,-.45,.45),sy=clamp(aimYaw-by-hy,-.6,.6);add(P,BYAW,by*w);add(P,HY,hy*w);add(P,SIPY,sy*w);add(P,SIPP,clamp(aimPitch,-.5,.35)*w);add(P,EY,aimYaw*.3*w);};
    if(name=="slam"){
        if(ph=="tele"){
            const double down0=std::max(.35,D-.13),u=eo(k01(t,0,std::min(.62,down0*.85))),d=ei(k01(t,down0,D));slamUp(u*(1-d));slamHit(d);
            const double tr=u*(1-d)*k01(t,.5,down0);add(P,LX,std::sin(t*47)*.03*tr);add(P,LY,std::sin(t*41+1)*.03*tr);add(P,SY,std::sin(t*33)*.012*tr);
            P[GLOWL]=static_cast<float>(std::max<double>(P[GLOWL],sm(k01(t,.1,down0))*(.85+.15*std::sin(t*20))));add(P,ED,.25*d);
        }else if(ph=="act"){
            static const std::array<std::vector<double>,4> fallback{{{0},{0},{0,.9},{0,.6}}};const auto& R=s.hasParams&&!s.rings.empty()?s.rings:fallback[static_cast<std::size_t>(std::clamp(phaseNow,0.0,3.0))];
            std::size_t k=0;while(k+1<R.size()&&t>=R[k+1]-.4)++k;for(const auto r:R)if(cross(r)){impact("clawL",1);kickShell(1);}
            const double dt0=t-R[k];double f;
            if(dt0>=0)f=1-.3*sm(k01(dt0,.05,.45));else{const double up=eo(k01(dt0,-.4,-.16))*(1-ei(k01(dt0,-.13,0)));f=.7*(1-up);slamUp(up*.75);}
            slamHit(f);add(P,BP,bump(dt0,0,.35)*.08);add(P,SY,-bump(dt0,0,.18)*.06);
            assign(P,GLOWL,dt0<0?sm(k01(dt0,-.4,0)):std::max(0.0,1.6*(1-k01(dt0,0,.45))));add(P,ED,.5*bump(dt0,-.02,.4));add(P,MOUTH,.5*bump(dt0,0,.5));
        }else if(ph=="rec"){
            const double u=sm(k01(t,.05,std::min(.8,D*.85)));slamHit(.7*(1-u));add(P,LZ,-.25*bump(t,0,.6));add(P,LY,.35*bump(t,.1,.7));add(P,LR,std::sin(t*26)*.18*bump(t,.3,.8));add(P,LO,.3*bump(t,.3,.8));assign(P,GLOWL,0);
        }
    }else if(name=="barrage"){
        if(ph=="tele"){
            const double u=eo(k01(t,0,.5));add(P,BY,-.22*u);add(P,SP,-.12*u);add(P,BP,-.06*u);add(P,BZ,-.1*u);const double rat=u*k01(t,.15,D);
            add(P,HA,.07*std::abs(std::sin(t*31))*rat);add(P,HB,.07*std::abs(std::sin(t*29+1))*rat);add(P,SY,std::sin(t*37)*.012*rat);
            add(P,RX,.1*u);add(P,RY,-.35*u);add(P,RZ,-.9*u);add(P,RP,-.4*u);P[RO]=static_cast<float>(mix(P[RO],.7,u));add(P,LX,-.1*u);add(P,LY,-.35*u);add(P,LZ,.2*u);add(P,LP,-.2*u);add(P,EP,.3*u);add(P,SPLAY,.1*u);
        }else if(ph=="act"){
            const double tele=teleDur>0?teleDur:.85;const bool has=s.hasParams&&!s.barrels.empty();const std::size_t nt=has?s.barrels.size():static_cast<std::size_t>((std::array<int,4>{0,3,4,6})[static_cast<std::size_t>(std::clamp(phaseNow,0.0,3.0))]);
            std::size_t n=0;double last=-1;for(std::size_t i=0;i<nt;++i){const double ti=has?s.barrels[i][3]-tele:i*.26;if(cross(ti))fx("hatch",i%2?"hatchB":"hatchA");if(t>=ti-.05){n=i+1;last=ti;}}
            const double endT=has?s.barrels[nt-1][3]-tele:(nt-1)*.26,tl=last<0?0:t-last,pump=bump(tl,0,.24)*(n>0&&t<endT+.3?1:0),hold=1-sm(k01(t,endT+.2,endT+.9));
            add(P,BY,-.22*hold+.18*pump);add(P,SP,-.12*hold-.05*pump);add(P,BP,-.06*hold-.05*pump);add(P,BZ,-.1*hold);add(P,SY,.03*pump);double lidA=0,lidB=0;
            for(std::size_t i=0;i<nt;++i){const double ti=has?s.barrels[i][3]-tele:i*.26,dt0=t-ti;if(dt0<-.02||dt0>.7)continue;const double o=dt0<.05?eo(k01(dt0,-.02,.05)):1-sm(k01(dt0,.1,.55));if(i%2)lidB=std::max(lidB,o);else lidA=std::max(lidA,o);}
            add(P,HA,lidA);add(P,HB,lidB);const double lob=sm(k01(t,0,.28))*hold;add(P,RX,mix(.1,.35,lob)*hold);add(P,RY,mix(-.35,1.9,lob)*hold);add(P,RZ,mix(-.9,.5,lob)*hold);add(P,RP,mix(-.4,1.0,lob)*hold);P[RO]=static_cast<float>(mix(P[RO],.8-.5*pump,hold));add(P,RY,-.35*pump);add(P,RZ,.55*pump);add(P,RP,-.75*pump);add(P,RYW,-.2*pump);add(P,LX,-.1*hold);add(P,LY,-.35*hold+.1*pump);add(P,LZ,.2*hold);add(P,LP,-.2*hold);add(P,EP,.35*hold);add(P,SPLAY,.1*hold);add(P,MOUTH,.3*pump);
        }else if(ph=="rec"){
            const double u=1-sm(k01(t,0,std::min(.6,D)));add(P,BY,-.05*u);add(P,SY,-.04*bump(t,0,.2));if(cross(.02))kickShell(.4);
        }
    }else if(name=="sweep"){
        if(ph=="tele"){
            const double u=eo(k01(t,0,.6));add(P,BY,-.28*u);add(P,BZ,-.35*u);add(P,BP,.05*u);add(P,HP,.1*u);add(P,LX,.55*u);add(P,LZ,-.55*u);add(P,LYW,.4*u);P[LO]=static_cast<float>(mix(P[LO],.4,u));add(P,RX,-.55*u);add(P,RZ,-.5*u);add(P,RYW,-.4*u);P[RO]=static_cast<float>(mix(P[RO],.4,u));const double c=sm(k01(t,.05,D));maxAssign(P,CANNON,c*(.8+.2*std::sin(t*40)));add(P,SIPP,-.08*u+std::sin(t*50)*.02*c);aimBlend(.7*u);add(P,SPLAY,.16*u);add(P,ED,.2*u);maxAssign(P,STEAM,.3*c);
        }else if(ph=="act"){
            add(P,BY,-.28);add(P,BZ,-.35+.25*eo(k01(t,0,.2)));add(P,BP,.05);add(P,HP,.1);add(P,LX,.55);add(P,LZ,-.55);add(P,LYW,.4);add(P,RX,-.55);add(P,RZ,-.5);add(P,RYW,-.4);assign(P,LO,.4);assign(P,RO,.4);assign(P,CANNON,1);add(P,HP,std::sin(t*61)*.012);add(P,HY,std::sin(t*53)*.01);add(P,SY,std::sin(t*47)*.006);aimBlend(1);add(P,SPLAY,.16);add(P,ED,.25);add(P,MOUTH,.35);
        }else if(ph=="rec"){
            const double u=1-sm(k01(t,0,std::min(.55,D)));add(P,BY,-.28*u);add(P,BZ,-.1*u);add(P,LX,.55*u);add(P,LZ,-.55*u);add(P,RX,-.55*u);add(P,RZ,-.5*u);assign(P,CANNON,std::max(0.0,1-k01(t,0,.35)));aimBlend(u);add(P,HY,std::sin(t*22)*.14*bump(t,.15,.75));add(P,SIPP,.25*bump(t,.1,.7));maxAssign(P,STEAM,bump(t,0,.8));
        }
    }else if(name=="charge"){
        if(ph=="tele"){
            const double u=eo(k01(t,0,.5));add(P,BY,-.55*u);add(P,BP,.2*u);add(P,BZ,-.3*u);add(P,HP,.1*u);add(P,LX,-u);add(P,LY,-.2*u);add(P,LZ,.2*u);add(P,LYW,-.5*u);add(P,LP,-.1*u);P[LO]=static_cast<float>(mix(P[LO],.05,u));add(P,RX,.7*u);add(P,RY,-.2*u);P[RZ]+=0;add(P,RYW,.45*u);P[RO]=static_cast<float>(mix(P[RO],.05,u));add(P,ED,.35*u);add(P,ES,-.15*u);assign(P,PAW,sm(k01(t,.25,.45))*(1-sm(k01(t,D-.25,D))));add(P,SY,std::sin(t*45)*.01*k01(t,.4,D));add(P,SR,std::sin(t*17)*.015*k01(t,.4,D));add(P,SPLAY,.15*u);
        }else if(ph=="act"){
            add(P,BY,-.35);add(P,BP,.2);add(P,HP,.12);add(P,LX,-1);add(P,LY,-.1);add(P,LZ,.35);add(P,LYW,-.5);assign(P,LO,.05);add(P,RX,.7);add(P,RY,-.1);add(P,RZ,.2);add(P,RYW,.45);assign(P,RO,.05);add(P,ED,.3);add(P,MOUTH,.4);
        }else if(ph=="rec"){
            const double ss=bump(t,0,.5),u=1-sm(k01(t,.1,std::min(.8,D)));add(P,BY,-.35*u);add(P,BP,.2*u-.22*ss);add(P,BZ,-.35*ss);add(P,LX,-u);add(P,LZ,.35*u);add(P,LYW,-.5*u);add(P,RX,.7*u);add(P,RZ,.2*u);add(P,RYW,.45*u);add(P,SPLAY,.2*ss);maxAssign(P,SLIDE,ss*.5);if(cross(.02))kickShell(.6);
        }
    }else if(name=="crablets"){
        if(ph=="tele"){
            const double u=eo(k01(t,0,.5)),r=u*k01(t,.2,D);add(P,BP,-.2*u);add(P,BY,-.1*u);add(P,BZ,.1*u);add(P,SP,.06*u);add(P,HC,.09*std::abs(std::sin(t*27))*r);add(P,SY,std::sin(t*39)*.015*r);add(P,SR,std::sin(t*23)*.02*r);add(P,LY,.35*u);add(P,RY,.35*u);P[LO]=static_cast<float>(mix(P[LO],.6,u));P[RO]=static_cast<float>(mix(P[RO],.6,u));add(P,EP,-.1*u);add(P,EY,std::sin(t*5)*.2*u);
        }else if(ph=="act"){
            if(cross(.02))kickShell(.8);const double o=eob(k01(t,0,.14));add(P,HC,o*(1-.1*bump(t,.2,.5)));const double sh=bump(t,.08,D);add(P,SR,std::sin(t*34)*.1*sh);add(P,BR,std::sin(t*34+.6)*.05*sh);add(P,SP,.1+std::sin(t*29)*.04*sh);add(P,BP,-.28);add(P,BY,-.12+.08*sh);add(P,BZ,.1);add(P,LY,.35);add(P,RY,.35);assign(P,LO,.6);assign(P,RO,.6);add(P,MOUTH,.4*sh);
        }else if(ph=="rec"){
            const double c=ei(k01(t,0,.18));add(P,HC,1-c);if(cross(.18))kickShell(.6);const double u=1-sm(k01(t,.1,std::min(.6,D)));add(P,BP,-.28*u);add(P,BY,-.12*u);add(P,LY,.35*u);add(P,RY,.35*u);
        }
    }else if(name=="frenzy"){
        if(ph=="tele"){
            const double u=eo(k01(t,0,.7));add(P,BYAW,-.55*u);add(P,BY,-.35*u);add(P,BR,.05*u);add(P,BP,.05*u);add(P,LX,u);add(P,LY,.5*u);add(P,LZ,-.9*u);add(P,LYW,.9*u);P[LO]=static_cast<float>(mix(P[LO],1,u));add(P,RX,-u);add(P,RY,.5*u);add(P,RZ,-.9*u);add(P,RYW,-.9*u);P[RO]=static_cast<float>(mix(P[RO],1,u));maxAssign(P,CANNON,sm(k01(t,.2,D))*.7);add(P,SY,std::sin(t*40)*.015*k01(t,.3,D));add(P,ED,.2*u);
        }else if(ph=="act"){
            const double u=1-sm(k01(t,D-.3,D))*.5;const bool rootSpin=std::abs(yawRate)>1.2;const double unwind=1-eo(k01(t,0,.25));add(P,BYAW,-.55*unwind+(rootSpin?0:std::sin(t*7.5)*.9*k01(t,0,.3)));add(P,BY,-.3+std::abs(std::sin(t*9))*.08);add(P,BR,(rootSpin?-.09*(yawRate>0?1:yawRate<0?-1:0):0)+std::sin(t*9)*.03);add(P,LX,1.1*u);add(P,LY,.4*u+std::sin(t*13)*.2);add(P,LZ,-.8*u);add(P,LYW,.9*u);assign(P,LO,.7+.3*std::sin(t*17));add(P,RX,-1.1*u);add(P,RY,.4*u+std::sin(t*13+2)*.2);add(P,RZ,-.8*u);add(P,RYW,-.9*u);assign(P,RO,.7+.3*std::sin(t*15));assign(P,CANNON,1);add(P,MOUTH,.6);add(P,SIPY,std::sin(t*11)*.3);add(P,SY,std::sin(t*43)*.012);
        }else if(ph=="rec"){
            const double u=1-sm(k01(t,0,.6));add(P,BY,-.3*u);add(P,LX,1.1*u);add(P,LZ,-.8*u);add(P,RX,-1.1*u);add(P,RZ,-.8*u);assign(P,CANNON,std::max(0.0,1-k01(t,0,.3)));
        }
    }else if(name=="stun"){
        const bool bonk=stunBonk;const double t0=bonk?.34:.12;
        if(bonk){if(cross(0)){impact("mouth",1.2);fx("bonk","mouth");kickShell(1.4);}const double j=bump(t,0,.42);add(P,BZ,-.75*j);add(P,BP,-.3*j);add(P,BY,.25*j);add(P,ED,.8*j);add(P,MOUTH,.6*j);}
        const double c=eo(k01(t,t0,t0+.55));if(cross(t0+.5)){impact("belly",.7);kickShell(.7);}add(P,BY,-1.05*c);add(P,BP,-.28*c);add(P,BZ,-.25*c);add(P,SPLAY,.45*c);maxAssign(P,SLIDE,c<1?1:.2);
        add(P,LX,.35*c);add(P,LY,-.2*c);add(P,LZ,-.25*c);add(P,LP,-.3*c);P[LO]=static_cast<float>(mix(P[LO],.45,c));add(P,RX,-.35*c);add(P,RY,-.35*c);add(P,RZ,-.3*c);add(P,RP,-.25*c);P[RO]=static_cast<float>(mix(P[RO],.5,c));
        const double w=k01(t,t0+.3,t0+.9),ss=t*2.6;add(P,BR,std::sin(ss)*.05*w);add(P,BP,std::cos(ss)*.035*w);add(P,HR,std::sin(ss+1)*.08*w);add(P,EY,std::sin(ss*1.3)*.35*w);add(P,EP,std::cos(ss*1.3)*.25*w-.1*c);add(P,ES,.25*c);add(P,ED,.35*c);add(P,MOUTH,.45*c+.1*std::sin(t*3));maxAssign(P,BELLY,c);add(P,BELLYOUT,c);add(P,SIPP,.3*c);
    }else if(name=="recover"){
        const double u=1-eo(k01(t,0,.7));add(P,BY,-.7*u);add(P,BP,-.15*u);add(P,SPLAY,.3*u);maxAssign(P,SLIDE,u);const double sh=bump(t,.35,1);add(P,HY,std::sin(t*24)*.2*sh);add(P,BR,std::sin(t*24+.8)*.06*sh);add(P,SR,std::sin(t*24+1.6)*.06*sh);add(P,EY,std::sin(t*24+.4)*.25*sh);maxAssign(P,BELLY,u);
    }else if(name=="roar"){
        const double k=D>0?D/1.9:1,T=t/k,an=eo(k01(T,0,.32))*(1-k01(T,.32,.5)),up=eob(k01(T,.35,.72))*(1-sm(k01(T,1.45,1.9)));
        if(cross(.38*k))kickShell(.8);add(P,BY,-.3*an+.38*up);add(P,BP,.08*an-.36*up);add(P,BZ,-.15*up);add(P,HP,.15*an-.25*up);add(P,LX,.6*up-.3*an);add(P,LY,1.9*up-.3*an);add(P,LZ,-up);add(P,LP,1.2*up);add(P,LYW,.6*up);P[LO]=static_cast<float>(mix(P[LO],1,up));add(P,RX,-.6*up+.3*an);add(P,RY,1.8*up-.3*an);add(P,RZ,-.9*up);add(P,RP,1.2*up);add(P,RYW,-.6*up);P[RO]=static_cast<float>(mix(P[RO],1,up));add(P,MOUTH,up);add(P,MAND,up);add(P,EP,.45*up);add(P,ES,.35*up);const double vib=up*k01(T,.45,.6);add(P,SY,std::sin(t*57)*.02*vib);add(P,SR,std::sin(t*43)*.02*vib);add(P,LR,std::sin(t*31)*.1*vib);add(P,RR,std::sin(t*29)*.1*vib);add(P,SPLAY,.1*up);maxAssign(P,STEAM,bump(T,.4,1.6));maxAssign(P,CANNON,.2*up);
    }else if(name=="intro"){
        const double k=D>0?D/3.4:1,T=t/k;double by;if(T<.5)by=-6.5;else if(T<.86)by=mix(-6.5,.7,eo(k01(T,.5,.86)));else if(T<1.08)by=mix(.7,-.5,ei(k01(T,.86,1.08)));else by=-.5*(1-eo(k01(T,1.08,1.6)));
        if(cross(0))intro();if(cross(1.08*k)){impact("body",1.2);kickShell(1.2);resetFeet=true;}if(cross(2.15*k))kickShell(.6);
        add(P,BY,by);const double air=k01(T,.5,.6)*(1-k01(T,1,1.12));add(P,BP,-.45*air+.12*bump(T,1.05,1.5));maxAssign(P,CURL,T<1?1:1-k01(T,1,1.1));P[LO]=static_cast<float>(mix(P[LO],1,air));P[RO]=static_cast<float>(mix(P[RO],1,air));add(P,LY,.8*air);add(P,RY,.8*air);add(P,LX,.4*air);add(P,RX,-.4*air);
        const double sh=bump(T,1.2,2.05),ss=T*30;add(P,BR,std::sin(ss)*.11*sh);add(P,SR,std::sin(ss-.9)*.13*sh);add(P,BYAW,std::sin(ss+.5)*.06*sh);add(P,HY,std::sin(ss+1.3)*.15*sh);add(P,EY,std::sin(ss+2)*.3*sh);add(P,ED,.4*sh);
        const double up=eob(k01(T,2.05,2.35))*(1-sm(k01(T,2.95,3.4)));add(P,BY,.3*up);add(P,BP,-.3*up);add(P,HP,-.2*up);add(P,LX,.6*up);add(P,LY,1.8*up);add(P,LZ,-up);add(P,LP,1.2*up);add(P,LYW,.6*up);P[LO]=static_cast<float>(mix(P[LO],1,up));add(P,RX,-.6*up);add(P,RY,1.7*up);add(P,RZ,-.9*up);add(P,RP,1.2*up);add(P,RYW,-.6*up);P[RO]=static_cast<float>(mix(P[RO],1,up));add(P,MOUTH,up);add(P,MAND,up);add(P,EP,.4*up);add(P,ES,.3*up);add(P,SY,std::sin(t*57)*.02*up);maxAssign(P,SLIDE,T<1.2?1:0);
    }else if(name=="dead"){
        const double st=bump(t,0,.9);add(P,BZ,-.35*st);add(P,BP,-.2*st);add(P,BR,.1*std::sin(t*7)*st);add(P,BY,.1*st);add(P,LY,1.2*st);add(P,LZ,-.6*st);add(P,LP,.8*st);P[LO]=static_cast<float>(mix(P[LO],1,st));add(P,RY,st);add(P,RP,.6*st);P[RO]=static_cast<float>(mix(P[RO],1,st));add(P,MOUTH,st);add(P,ED,.4*st);
        const double c=eo(k01(t,.75,1.45));if(cross(1.4)){impact("body",1.4);kickShell(1.2);}if(cross(1.9))kickShell(1.6);add(P,BY,-1.55*c);add(P,BP,.12*c);add(P,BR,.14*c);add(P,BZ,-.2*c);add(P,SPLAY,.6*c);maxAssign(P,SLIDE,t>.7?1:0);add(P,LX,.5*c);add(P,LY,-.55*c);add(P,LP,-.4*c);add(P,LYW,.3*c);P[LO]=static_cast<float>(mix(P[LO],.6,c));add(P,RX,-.5*c);add(P,RY,-.6*c);add(P,RP,-.4*c);add(P,RYW,-.3*c);P[RO]=static_cast<float>(mix(P[RO],.6,c));add(P,ED,.9*c);add(P,ES,.5*c);add(P,EP,-.5*c);add(P,HP,.2*c);add(P,HA,eob(k01(t,1.9,2.1))*1.1);add(P,HB,eob(k01(t,1.95,2.15))*1.05);add(P,HC,eob(k01(t,1.92,2.12))*1.1);maxAssign(P,CURL,.5*k01(t,2.2,4));const double tw=k01(t,2.2,3)*(1-k01(t,4.5,6));add(P,LO,std::max(0.0,std::sin(t*9)-.7)*.8*tw);add(P,CURL,std::max(0.0,std::sin(t*5.3+1)-.8)*.6*tw);maxAssign(P,BELLY,c*(1-k01(t,2,4)));
    }
}

glm::dvec3 BossPose::Animator::home(const Foot& f,std::size_t i,const Channels& P,double ahead)const{
    const double sw=(i%3==0?1.0:i%3==1?.75:.55),sp=1+P[SPLAY]*sw,yy=P[BYAW]+yawRate*ahead;
    const double x=f.restXZ.x*sp,z=f.restXZ.y*(1+P[SPLAY]*.3*sw),c=std::cos(yy),s=std::sin(yy);
    glm::dvec3 out(source.bodyPivot.x+P[BX]+x*c+z*s,0,source.bodyPivot.z+P[BZ]-x*s+z*c);
    out=rootPos+rootQ*out;if(ahead)out+=vel*ahead;return out;
}

void BossPose::Animator::gait(double dt,const BossPoseInput& s){
    const double v=glm::length(glm::dvec2(vel.x,vel.z)),yr=std::abs(yawRate),vEff=v+yr*2.8;const bool gallop=s.move=="charge"&&s.movePhase=="act";
    double maxErr=0;bool anySwing=false;
    for(std::size_t i=0;i<feet.size();++i){auto& l=feet[i];l.home=home(l,i,p);l.home.y=rootPos.y;if(!l.swing){l.err=std::hypot(l.foot.x-l.home.x,l.foot.z-l.home.z);maxErr=std::max(maxErr,l.err);}else anySwing=true;}
    if(first||resetFeet){for(auto& l:feet){l.foot=l.home;l.swing=false;}first=false;resetFeet=false;maxErr=0;anySwing=false;}
    const double slide=p[SLIDE];if(slide>.001)for(auto& l:feet)if(!l.swing)l.foot+=(l.home-l.foot)*clamp(slide*dt*8,0,1);
    const bool moving=vEff>.3;for(const auto& l:feet)if(l.swing)anySwing=true;const bool settle=maxErr>.32||anySwing;gaitOn=moving||settle;
    const double duty=gallop?.52:.42,Ls=gallop?mix(2.6,4.6,clamp((v-6)/8,0,1)):mix(1.5,2.8,clamp(v/5,0,1));
    const double f=moving?clamp(vEff/Ls,.9,gallop?3.4:2.4):std::min(2.6,1.5+maxErr*1.2),Tst=(1-duty)/f,arc=gallop?.7:.32+.08*std::min(v,4.0);
    cad=gaitOn?f:0;if(gaitOn)phi=fract(phi+f*dt);
    for(auto& l:feet){
        const double lp=fract(phi+l.off);const bool inSwing=gaitOn&&lp<duty;
        if(inSwing&&!l.swing&&(moving||l.err>.2)){l.swing=true;l.from=l.foot;}
        if(l.swing){const double ss=lp/duty;if(!inSwing||ss<l.s-.5){l.swing=false;l.s=0;l.foot=l.to;footstep(l.foot,std::min(1.5,.35+v*.1+(gallop?.5:0)),static_cast<int>(&l-feet.data()));kickShell(.05+.02*std::min(v,6.0));}
            else{l.s=ss;l.to=home(l,static_cast<std::size_t>(&l-feet.data()),p,Tst*.5+(1-ss)*duty/f);glm::dvec3 delta=l.to-l.home;const double dl=glm::length(delta),mx=1.25+(gallop?.8:0);if(dl>mx)l.to=l.home+delta*(mx/dl);l.to.y=rootPos.y;const double e=sm(ss);l.foot=l.from*(1-e)+l.to*e;l.foot.y=mix(l.from.y,l.to.y,e)+std::sin(ss*pi)*arc*(1-.3*std::pow(ss,3));}}
        if(!l.swing){glm::dvec3 d=l.foot-l.home;d.y=0;const double len=glm::length(d),mx=2.6;if(len>mx){l.foot.x=l.home.x+d.x*mx/len;l.foot.z=l.home.z+d.z*mx/len;}}
    }
    const double paw=p[PAW];if(paw>.001){auto& l=feet[0];const double cyc=fract(time*2.3);glm::dvec3 off(0,std::max(0.0,std::sin(cyc*tau))*.35,.55-cyc*1.1);off=rootQ*off;const glm::dvec3 target=l.home+off;l.foot+=(target-l.foot)*paw;if(cyc<.5&&fract((time-dt)*2.3)>.5)footstep(l.foot,.3,0);}
    (void)s;
}

void BossPose::Animator::apply(Rig& rig,double dt,const BossPoseInput& s){
    for(std::size_t i=0;i<rig.localPosition.size();++i){rig.localRotation[i]=glm::dquat(1,0,0,0);rig.localScale[i]=glm::dvec3(1);}
    const auto id=[&](const std::string& name){return rig.byName.at(name);};const auto setQ=[&](const std::string& name,glm::dquat q){rig.localRotation[id(name)]=glm::normalize(q);};const auto setPos=[&](const std::string& name,glm::dvec3 v){rig.localPosition[id(name)]=v;};const auto setScale=[&](const std::string& name,glm::dvec3 v){rig.localScale[id(name)]=v;};
    const auto& R=rig.localPosition;bodyQ=quatYXZ(p[BP],p[BYAW],-p[BR]);bodyPos=source.bodyPivot+glm::dvec3(p[BX],p[BY],p[BZ]);setPos("body",bodyPos);setQ("body",bodyQ);
    setPos("shell",source.containerPosition-source.bodyPivot+glm::dvec3(0,p[SY],0));setQ("shell",quatYXZ(p[SP],0,-p[SR]));
    setQ("head",quatYXZ(p[HP],p[HY],-p[HR]));
    const double bs=1+std::sin(time*1.55)*.03+p[BELLYOUT]*.08;setScale("belly0",glm::dvec3(bs));setScale("belly1",glm::dvec3(1+std::sin(time*1.55-.5)*.025));setQ("belly0",glm::angleAxis(-.25*double(p[BELLYOUT]),glm::dvec3(1,0,0)));setScale("abdomen",glm::dvec3(1+std::sin(time*1.55-1)*.02+.03*springs[13].x));
    const glm::dvec3 bodyUp=bodyQ*glm::dvec3(0,1,0);const double curl=clamp(p[CURL],0,1);
    const auto rotateRootInv=[&](glm::dvec3 w){return rootQi*(w-rootPos);};
    for(std::size_t i=0;i<source.legs.size();++i){
        const auto& L=source.legs[i];const auto& LR=legFrames[i];const auto& foot=feet[i];const std::string prefix="leg"+std::to_string(i);const std::string c=prefix+"c",m=prefix+"m",t=prefix+"t",d=prefix+"d";
        glm::dvec3 H=bodyQ*LR.hipOff+bodyPos,F=rotateRootInv(foot.foot);
        if(curl>.001){glm::dvec3 v(L.foot.x*.55,L.foot.y+1.3,(L.foot.z-source.bodyPivot.z)*.7+source.bodyPivot.z);v=bodyQ*(v-source.bodyPivot)+bodyPos;F+=(v-F)*curl;}
        glm::dvec3 out=F-H;out-=bodyUp*glm::dot(out,bodyUp);if(glm::dot(out,out)<1e-6)out=L.out;out=glm::normalize(out);
        glm::dvec3 ref=bodyQ*L.out;ref-=bodyUp*glm::dot(ref,bodyUp);ref=glm::normalize(ref);double ang=std::atan2(glm::dot(glm::cross(ref,out),bodyUp),glm::dot(ref,out));ang=clamp(ang,-LR.maxYaw,LR.maxYaw);
        const glm::dquat q1=glm::angleAxis(ang,bodyUp)*bodyQ;setQ(c,glm::inverse(bodyQ)*q1);glm::dvec3 X=q1*LR.coxOff+H;
        out=F-X;out.y=0;if(glm::dot(out,out)<1e-6)out=L.out;out=glm::normalize(out);glm::dvec3 A=F-out*(L.ld*.3)+glm::dvec3(0,L.ld*.954,0),pole=glm::normalize(glm::dvec3(0,1,0)+out*.35),K,reachable;
        ik2(X,A,L.l1,L.l2,pole,K,&reachable);F+=reachable-A;A=reachable;const glm::dvec3 n0=glm::normalize(glm::cross(A-X,pole));
        glm::dquat q2=frameQuat(LR.dm,LR.n,K-X,n0);setQ(m,glm::inverse(q1)*q2);glm::dquat q3=frameQuat(LR.dt,LR.n,A-K,n0);setQ(t,glm::inverse(q2)*q3);glm::dquat q4=frameQuat(LR.dd,LR.n,F-A,n0);setQ(d,glm::inverse(q3)*q4);
    }
    const std::array<std::array<int,7>,2> armCh{{{{LX,LY,LZ,LP,LYW,LR,LO}},{{RX,RY,RZ,RP,RYW,RR,RO}}}};
    for(std::size_t i=0;i<source.arms.size();++i){
        const auto& A=source.arms[i];const auto& AR=armFrames[i];const auto& o=armCh[i];const std::string side=i==0?"L":"R",b0="arm"+side+"0",b1="arm"+side+"1",claw="claw"+side,dact="dact"+side;
        const glm::dvec3 H=bodyQ*AR.sOff+bodyPos;glm::dvec3 wrist(AR.wOff.x+p[o[0]],AR.wOff.y+p[o[1]],AR.wOff.z+p[o[2]]);wrist=bodyQ*wrist+bodyPos;
        const double floorY=i==0?.62:.4;if(wrist.y<floorY)wrist.y=floorY+(wrist.y-floorY)*.15;
        const double raise=clamp((wrist.y-H.y)/1.6,0,1);glm::dvec3 pole=bodyQ*A.pole;pole+=bodyQ*glm::dvec3(0,0,1)*(raise*1.2)+bodyUp*(raise*.6);pole=glm::normalize(pole);
        glm::dvec3 K,reachable;ik2(H,wrist,A.upper,A.fore,pole,K,&reachable);const glm::dvec3 n0=glm::normalize(glm::cross(reachable-H,pole));glm::dquat q1=frameQuat(AR.du,AR.n,K-H,n0);setQ(b0,glm::inverse(bodyQ)*q1);glm::dquat q2=frameQuat(AR.df,AR.n,reachable-K,n0);setQ(b1,glm::inverse(q1)*q2);
        glm::dquat q3=glm::angleAxis(double(p[o[5]]),AR.D),q4=glm::angleAxis(-double(p[o[3]]),AR.side)*q3;q3=glm::angleAxis(double(p[o[4]]),glm::dvec3(0,1,0))*q4;q4=bodyQ*q3;setQ(claw,glm::inverse(q2)*q4);
        const double open=clamp(p[o[6]],0,1)*(i==0?.62:.55);setQ(dact,glm::angleAxis(-open,AR.side));
    }
    const glm::dvec3 headLocal=R[id("head")];const glm::dvec3 hW=rootPos+rootQ*(bodyPos+bodyQ*headLocal);
    glm::dvec3 hw2=headPrev2,hw1=headPrev;headPrev2=headPrev;headPrev=hW;
    if(time>.2){glm::dvec3 accel=(hW-2.0*hw1+hw2)/std::max(dt*dt,1e-5);accel=rootQi*accel;accel.x=clamp(accel.x,-40,40);accel.y=clamp(accel.y,-40,40);accel.z=clamp(accel.z,-40,40);springs[9].v+=-accel.x*.004;springs[11].v+=-accel.x*.004;springs[10].v+=accel.z*.004+accel.y*.003;springs[12].v+=accel.z*.004+accel.y*.0035;}
    springs[9].step(dt);springs[10].step(dt);springs[11].step(dt);springs[12].step(dt);
    for(std::size_t e=0;e<2;++e){const double k=e==0?1:-1,yawJ=springs[e==0?9:11].x,pitJ=springs[e==0?10:12].x,look=p[EY],pit=p[EP],spread=p[ES],droop=clamp(p[ED],0,1);const std::string s=e==0?"L":"R";const std::string b0="stalk"+s+"0",b1="stalk"+s+"1",eye="eye"+s,mand="mand"+s;
        setQ(b0,quatYXZ(-.25*droop+pitJ*.5-pit*.25,look*.35+yawJ*.4,-k*(spread*.35+droop*.18)));
        setQ(b1,quatYXZ(-.35*droop+pitJ-pit*.4,look*.4+yawJ*.6,-k*spread*.2));setQ(eye,quatYXZ(-pit*.35+.15*droop,look*.3,0));setScale(eye,glm::dvec3(1,1-.25*droop,1));
        setQ(mand,glm::angleAxis(-.25*double(p[MOUTH]),glm::dvec3(1,0,0))*glm::angleAxis(k*(.1*double(p[MAND])+.35*double(p[MOUTH])),glm::dvec3(0,0,1)));
    }
    setQ("siphon",quatYXZ(p[SIPP]*.5,p[SIPY]*.5,0));setQ("nozzle",quatYXZ(p[SIPP]*.5,p[SIPY]*.5,0));const double nozzleScale=1+.12*p[CANNON]+.04*std::sin(time*40)*p[CANNON];setScale("nozzle",glm::dvec3(nozzleScale));
    const glm::dquat contQ=glm::normalize(glm::quat_cast(glm::dmat3(source.containerMatrix)));const glm::dvec3 cz=contQ*glm::dvec3(0,0,1),cx=contQ*glm::dvec3(1,0,0),cy=contQ*glm::dvec3(0,1,0);
    setQ("hatchA",glm::angleAxis(-1.95*std::max(0.0,double(p[HA])),cz));setQ("hatchB",glm::angleAxis(1.95*std::max(0.0,double(p[HB])),cz));setQ("hatchC",glm::angleAxis(-2*std::max(0.0,double(p[HC])),cx));setQ("doorR",glm::angleAxis(clamp(springs[8].x*.4,-.5,.9),cy));setQ("tearL",glm::angleAxis(-1.9*springs[13].x,cz));setQ("tearR",glm::angleAxis(1.9*springs[13].x,cz));
    (void)s;
}

void BossPose::Animator::chainsStep(Rig& rig,double dt,const BossPoseInput& s){
    chainAcc=std::min(chainAcc+dt,4.0/60);const double h=1.0/60;int steps=0;while(chainAcc>=h){chainAcc-=h;++steps;}
    // Match source Float32Array stores; GCC can elide a GLM float/double round trip.
    const auto storeParticle=[](glm::dvec3 value){volatile float x=static_cast<float>(value.x),y=static_cast<float>(value.y),z=static_cast<float>(value.z);return glm::dvec3(x,y,z);};
    const glm::dmat4 root=trs(rootPos,rootQ,glm::dvec3(1));
    const glm::dmat4 shellWorld=multiplyThree(root,rig.currentGlobal[rig.byName.at("shell")]);
    const glm::dmat4 toContainer=multiplyThree(multiplyThree(shellWorld,glm::translate(glm::dmat4(1),-source.containerPosition)),source.containerMatrix);
    for(std::size_t ci=0;ci<source.chains.size();++ci){
        const auto& ch=source.chains[ci];auto& c=chains[ci];const std::size_t n=ch.points.size();const auto parentId=rig.byName.at(ch.parent);
        const glm::dmat4 pm=multiplyThree(root,rig.currentGlobal[parentId]);const glm::dvec3 parentRest=ch.parentRest;
        if(!c.init){for(std::size_t i=0;i<n;++i){const glm::dvec3 world=storeParticle(point(pm,ch.points[i]-parentRest));c.p[i]=world;c.q[i]=world;}c.init=true;}
        glm::dvec3 planeN(0),planeP(0);if(ch.hasPlane){const glm::dvec3 n(ch.plane);planeN=glm::dvec3(toContainer[0][0]*n.x+toContainer[1][0]*n.y+toContainer[2][0]*n.z,toContainer[0][1]*n.x+toContainer[1][1]*n.y+toContainer[2][1]*n.z,toContainer[0][2]*n.x+toContainer[1][2]*n.y+toContainer[2][2]*n.z);planeN=normalizeThree(planeN);planeP=point(toContainer,glm::dvec3(ch.plane)*ch.plane.w);}
        const double grav=-9.8*ch.gravity*h*h,damp=ch.damp,stiff=ch.stiff;
        for(int step=0;step<steps;++step){
            const glm::dvec3 anchor=storeParticle(point(pm,ch.points[0]-parentRest));c.p[0]=anchor;
            for(std::size_t i=1;i<n;++i){const std::size_t j=i;const glm::dvec3 old(c.p[j]),prev(c.q[j]);double fx=0,fz=0;
                if(ch.flag){const double g=.5+.5*std::sin(time*1.7+i)*std::sin(time*.63);fx=(.6+g)*.00035*(.6+std::sin(time*9.1+i*1.7)*.4);fz=std::sin(time*5.3+i*2.1)*.00018;}
                glm::dvec3 v=(old-prev)*damp;const double vl=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);if(vl>.1)v*=.1/vl;glm::dvec3 next(old.x+(v.x+fx),old.y+(v.y+grav),old.z+(v.z+fz));c.p[j]=storeParticle(next);c.q[j]=storeParticle(old);
                if(stiff>0){const glm::dvec3 rest=point(pm,ch.points[i]-parentRest);glm::dvec3 x(c.p[j]);x+=(rest-x)*stiff;c.p[j]=storeParticle(x);}
            }
            for(int iteration=0;iteration<3;++iteration){
                for(std::size_t i=1;i<n;++i){const std::size_t a=i-1,b=i;const glm::dvec3 pa(c.p[a]),pb(c.p[b]);const glm::dvec3 d=pb-pa;const double dSq=d.x*d.x+d.y*d.y+d.z*d.z,dist=dSq>0?std::sqrt(dSq):1e-6;const glm::dvec3 rest=ch.points[i]-ch.points[i-1];const double restSq=rest.x*rest.x+rest.y*rest.y+rest.z*rest.z,length=static_cast<float>(std::sqrt(restSq)),k=(dist-length)/dist;
                    if(i==1)c.p[b]=storeParticle(pb-d*k);else{c.p[a]=storeParticle(pa+glm::dvec3((d.x*k)*.5,(d.y*k)*.5,(d.z*k)*.5));c.p[b]=storeParticle(pb-glm::dvec3((d.x*k)*.5,(d.y*k)*.5,(d.z*k)*.5));}}
                if(ch.hasPlane)for(std::size_t i=1;i<n;++i){glm::dvec3 pnt(c.p[i]);const glm::dvec3 delta=pnt-planeP;const double d=delta.x*planeN.x+delta.y*planeN.y+delta.z*planeN.z;if(d<0){pnt-=planeN*d;c.p[i]=storeParticle(pnt);}}
                const double floorY=rootPos.y+.03,floorStored=static_cast<float>(floorY);for(std::size_t i=1;i<n;++i)if(c.p[i].y<floorY)c.p[i].y=floorStored;
            }
        }
        glm::dquat parentQ=decomposeThree(pm);
        for(std::size_t i=0;i<ch.bones.size();++i){const glm::dvec3 pa(c.p[i]),pb(c.p[i+1]),restDir=normalizeThree(ch.points[i+1]-ch.points[i]);
            const glm::dvec3 segment=pb-pa;const double segmentLength=std::sqrt(segment.x*segment.x+segment.y*segment.y+segment.z*segment.z);const glm::dvec3 direction=segmentLength>0?segment*(1.0/segmentLength):glm::dvec3(0),worldRest=applyThreeQuat(rootQ,restDir);const glm::dquat worldQ=multiplyThreeQuat(unitQuat(worldRest,direction),rootQ);const auto bone=rig.byName.at(ch.bones[i]);rig.localRotation[bone]=multiplyThreeQuat(conjugateThree(parentQ),worldQ);parentQ=worldQ;}
    }
    (void)s;
}

void BossPose::Animator::update(BossPose& owner,Rig& rig,const BossPoseInput& s){
    const double dt=std::min(s.dt,1.0/15);
    // Public BossModel events arrive before update and act on the same spring state.
    const auto flashEvent=[&](double k,bool weak){if(k<=0)return;if(weak)weakFlash=std::max(weakFlash,k);else flash=std::max(flash,k*.6);if(modelTime-lastFlashT>.09)flinch(std::min(1.0,.3+k*.7)*(weak?1.2:1));lastFlashT=modelTime;};
    flashEvent(s.flash,s.weakFlash);flashEvent(s.weakFlashAmount,true);
    if(!s.trigger.empty()){if(s.trigger=="hurt")flinch(.8);else{oneShot=s.trigger;}}
    modelTime+=dt;time+=dt;
    rootPos=s.position;rootQ=decomposeThree(trs(rootPos,glm::angleAxis(s.yaw,glm::dvec3(0,1,0)),glm::dvec3(1)));rootQi=conjugateThree(rootQ);const double rootYaw=s.yaw;
    if(first){rootPrev=rootPos;yawPrev=rootYaw;}
    glm::dvec3 measured=(rootPos-rootPrev)/std::max(dt,1e-4);measured.y=0;if(glm::dot(measured,measured)>900)measured=glm::dvec3(0);
    vel+= (measured-vel)*dampK(14,dt);double dy=rootYaw-yawPrev;while(dy>pi)dy-=tau;while(dy<-pi)dy+=tau;
    yawRate=mix(yawRate,std::abs(dy)>1.5?0:dy/std::max(dt,1e-4),dampK(12,dt));rootPrev=rootPos;yawPrev=rootYaw;yaw=rootYaw;
    hasAim=s.hasAim&&!s.dead;
    if(hasAim){const glm::dvec3 local=rootQi*(s.aim-rootPos);aimLocal=local;const double dx=local.x-source.nozzleTip.x,dyy=local.y-source.nozzleTip.y,dz=local.z-source.nozzle.z;const double ty=std::atan2(dx,std::max(.5,dz)),tp=std::atan2(dyy,std::max(1.0,std::hypot(dx,dz)));aimYaw=mix(aimYaw,clamp(ty,-1.4,1.4),dampK(10,dt));aimPitch=mix(aimPitch,tp,dampK(10,dt));}
    if(s.hurt>0&&(s.hurt!=prevHurt||prevHurt==0)){if(!(modelTime-lastFlashT<.12))flinch(std::min(1.2,.35+s.hurt));else prevHurt=s.hurt;}prevHurt=s.hurt;
    const int nextPhase=s.phase?s.phase:1;phaseNow=nextPhase;if(prevPhase==0)prevPhase=nextPhase;if(nextPhase>prevPhase&&!s.dead)autoRoarT=0;prevPhase=nextPhase;autoRoarT+=dt;
    if(s.phase&&phase!=s.phase)phase=s.phase;
    std::string nextKey,clipName,movePhase;double clipTime=0,duration=0;bool own=false;
    if(s.dead){deadT=key=="dead"?deadT+dt:0;nextKey="dead";clipName="dead";clipTime=deadT;duration=6;}
    else if(s.move=="intro"||oneShot=="intro"){nextKey="intro";clipName="intro";duration=s.move=="intro"?(s.phaseDuration? s.phaseDuration:3.4):3.4;if(s.move=="intro")clipTime=s.moveTime;else own=true;}
    else if(s.stunned){nextKey="stun";clipName="stun";own=true;duration=s.phaseDuration?s.phaseDuration:3;}
    else if(s.move=="roar"||oneShot=="roar"||(autoRoarT<1.9&&s.move.empty())){nextKey="roar";clipName="roar";duration=s.move=="roar"?(s.phaseDuration?s.phaseDuration:1.9):1.9;if(s.move=="roar")clipTime=s.moveTime;else if(oneShot=="roar")own=true;else clipTime=autoRoarT;}
    else if(!s.move.empty()&&!s.movePhase.empty()&&(s.move=="slam"||s.move=="barrage"||s.move=="sweep"||s.move=="charge"||s.move=="crablets"||s.move=="frenzy")&&(s.movePhase=="tele"||s.movePhase=="act"||s.movePhase=="rec")){
        nextKey=s.move+":"+s.movePhase;clipName=s.move;movePhase=s.movePhase;duration=s.phaseDuration;if(!duration){static const std::map<std::string,std::array<double,3>> durations{{"slam",{1.15,1.2,1}},{"barrage",{.85,2,.8}},{"sweep",{1.15,2.1,.9}},{"charge",{1.15,1.5,.9}},{"crablets",{.9,.8,.6}},{"frenzy",{1.2,3,1.7}}};const auto i=s.movePhase=="tele"?0:s.movePhase=="act"?1:2;duration=durations.at(s.move)[i];}
        clipTime=s.moveTime;if(s.movePhase=="tele")teleDur=duration;lastMove=s.move;
    }else if(recoverT<1){nextKey="recover";clipName="recover";clipTime=recoverT;duration=1;}
    else nextKey="loco";
    if(s.stunned&&!wasStunned)stunBonk=lastMoveKey=="charge:act"&&(!s.hasParams||s.wall);
    if(!s.stunned&&wasStunned&&!s.dead)recoverT=0;wasStunned=s.stunned;recoverT+=dt;
    if(nextKey!="stun"&&nextKey!="loco")lastMoveKey=nextKey;
    if(nextKey!=key){px=p;xf=0;xfDur=nextKey=="intro"?.0001:nextKey=="dead"?.12:nextKey=="stun"?.08:(nextKey.size()>=4&&nextKey.substr(nextKey.size()-4)==":act")?.07:.2;key=nextKey;clipT=0;prevT=-1e-6;}else prevT=curT;
    if(own)clipTime=clipT;clipT+=dt;if(!oneShot.empty()&&(key!=oneShot||clipT>duration+.05)){if(key!=oneShot||clipT>duration)oneShot.clear();}curT=clipTime;
    loco(pb,dt,s);pn=pb;if(!clipName.empty())clip(pn,clipName,movePhase,clipTime,duration,s);
    xf=std::min(1.0,xf+dt/xfDur);const double w=sm(xf);
    // JavaScript promotes Float32 reads to double and rounds only when storing the blended channel.
    for(int i=0;i<NC;++i)p[i]=static_cast<float>(double(px[i])+(double(pn[i])-double(px[i]))*w);
    for(const int i:{0,1,2,5,6,7})springs[i].step(dt);
    const glm::dvec3 rootVel=rootQi*vel;const double ax=(rootVel.x-lvx)/std::max(dt,1e-3),az=(rootVel.z-lvz)/std::max(dt,1e-3);lvx=rootVel.x;lvz=rootVel.z;
    springs[3].step(dt,clamp(-az*.012,-.12,.12));springs[4].step(dt,clamp(ax*.01,-.1,.1));add(p,SP,springs[0].x*.03+springs[3].x);add(p,SR,springs[1].x*.025+springs[4].x);add(p,SY,springs[2].x*.03);add(p,BP,springs[5].x*.08);add(p,BR,springs[6].x*.05);add(p,ED,springs[7].x*.07);add(p,BY,springs[5].x*.04);add(p,HP,springs[5].x*.06);
    springs[8].step(dt);springs[8].v+=(-az*.02+ax*.03)*dt*6;springs[13].step(dt,(phase>=3||(s.dead&&deadT>1.9))?1:0);
    bodyQ=quatYXZ(p[BP],p[BYAW],-p[BR]);bodyPos=source.bodyPivot+glm::dvec3(p[BX],p[BY],p[BZ]);gait(dt,s);apply(rig,dt,s);BossPose::Evaluate(rig);chainsStep(rig,dt,s);BossPose::Evaluate(rig);first=false;
    flash=std::max(0.0,flash-dt*7);weakFlash=std::max(0.0,weakFlash-dt*4);crack+=(double(phase>=2)-crack)*std::min(1.0,dt*1.2);enrage+=((phase>=3&&!s.dead?1.0:0.0)-enrage)*std::min(1.0,dt*1.5);
    const double uCrack=crack*(s.dead?std::max(0.0,1-deadT*.3):1),uOpen=clamp(springs[13].x,0,1.2)*(s.dead?std::max(0.0,1-deadT*.25):1),uEyeMode=s.dead&&deadT>1.6?2:key=="stun"&&clipT>.3?1:0;
    if(uniforms.find("uContInv")==uniforms.end()){const auto inv=glm::inverse(source.containerMatrix);const double* v=glm::value_ptr(inv);uniforms["uContInv"]={v,v+16};}
    uniforms["uTime"]={modelTime};uniforms["uFlash"]={flash};uniforms["uWeakFlash"]={weakFlash};uniforms["uCrack"]={uCrack};uniforms["uOpen"]={uOpen};uniforms["uEnrage"]={enrage};uniforms["uGlowL"]={std::max(0.0,double(p[GLOWL]))};uniforms["uCannon"]={clamp(std::max(0.0,double(p[CANNON])),0,1.2)};uniforms["uBelly"]={clamp(p[BELLY],0,1)};uniforms["uEyeMode"]={uEyeMode};uniforms["uEyeSpin"]={uniforms["uEyeSpin"][0]+dt*(uEyeMode==1?1:0)};uniforms["uEyeGlow"]={s.dead?std::max(.35,1-deadT*.3):1+.35*enrage};uniforms["uLamp"]={s.dead?0.0:1.0};uniforms["uHeat"]={enrage};uniforms["uDrip"]={.6};uniforms["uAO"]={1};
    owner.uniforms_=uniforms;
    const bool below=p[BY]<-2.2;const glm::dmat4 hitRoot=trs(rootPos,rootQ,glm::dvec3(1));
    for(std::size_t i=0;i<owner.hitShapes_.size();++i){const auto& sock=owner.assets_[0].sockets[i];if(sock.bone>=rig.currentGlobal.size())throw std::runtime_error("Boss socket references an absent bone");const glm::dvec3 pos=point(multiplyThree(hitRoot,rig.currentGlobal[sock.bone]),glm::dvec3(sock.offset));const bool active=!s.dead&&!below&&(sock.name=="belly"?(s.stunned||phase>=3||p[BELLY]>.5):(sock.name=="crackL"||sock.name=="crackR"?springs[13].x>.5:true));owner.hitShapes_[i]={sock.name,pos,sock.radius,sock.weak,active};}
    owner.poseSockets_.clear();owner.poseSockets_.reserve(owner.assets_[0].poseSockets.size());
    for(const auto& sock:owner.assets_[0].poseSockets){if(sock.bone>=rig.currentGlobal.size())throw std::runtime_error("Boss pose socket references an absent bone");owner.poseSockets_.push_back({sock.name,point(multiplyThree(hitRoot,rig.currentGlobal[sock.bone]),glm::dvec3(sock.offset))});}
    for(auto& event:audioEvents){
        if(event.kind==BossPoseAudioEvent::Kind::Impact||event.kind==BossPoseAudioEvent::Kind::Fx){
            const auto socket=std::find_if(owner.poseSockets_.begin(),owner.poseSockets_.end(),[&](const auto& item){return item.name==event.socket;});
            event.position=socket==owner.poseSockets_.end()?rootPos+glm::dvec3(0,3,0):socket->pos;
            if(event.kind==BossPoseAudioEvent::Kind::Impact&&event.socket=="clawL")event.position.y=rootPos.y+.05;
            else if(event.kind==BossPoseAudioEvent::Kind::Impact&&event.socket=="body")event.position.y=rootPos.y+.2;
        }
        if(owner.audioEventSink_)owner.audioEventSink_(event);
    }
    audioEvents.clear();
}

void BossPose::Animator::animateCrablet(Rig& rig,const BossCrablet& crab,double dt,double t,double seed,double& ph){
    for(auto& q:rig.localRotation)q=glm::dquat(1,0,0,0);for(auto& sc:rig.localScale)sc=glm::dvec3(1);
    const auto id=[&](const char* name){return rig.byName.at(name);};const auto setQ=[&](const std::string& name,glm::dquat q){rig.localRotation[id(name.c_str())]=glm::normalize(q);};
    const double sp=std::min(1.0,crab.speed/3);ph+=dt*(1.2+crab.speed*2.4);const double angle=ph*tau,tum=std::max(0.0,1-t/.55),land=t>.5&&t<.8?std::sin(((t-.5)/.3)*pi):0;
    setQ("body",quatYXZ(tum*tum*6,0,tum*2.5+std::sin(angle*2)*.06*sp));rig.localPosition[id("body")].y=.26+std::abs(std::sin(angle))*.035*sp+std::sin(t*3+seed)*.008-land*.05+tum*.2;rig.localScale[id("body")]=glm::dvec3(1+land*.12,1-land*.15,1+land*.12);
    setQ("lid",glm::angleAxis(-.05*std::abs(std::sin(angle*2))*sp-land*.1,glm::dvec3(1,0,0)));
    for(int i=0;i<6;++i){const std::string name="leg"+std::string(i<3?"L":"R")+std::to_string(i%3);const int grp=(i%2)^((i>=3)?1:0);const double side=i<3?1:-1,a=angle+grp*pi;setQ(name,quatYXZ(0,std::sin(a)*.45*sp,side*(std::max(0.0,std::cos(a))*.5*sp+tum*.9)));}
    const double snapL=std::pow(std::max(0.0,std::sin(t*5+seed)),6),snapR=std::pow(std::max(0.0,std::sin(t*5+seed+1.7)),6);
    setQ("clawL",glm::angleAxis(-.3*snapL-.15*std::sin(angle)*sp,glm::dvec3(1,0,0)));setQ("clawR",glm::angleAxis(-.3*snapR+.15*std::sin(angle)*sp,glm::dvec3(1,0,0)));
    setQ("eyeL",glm::angleAxis(std::sin(t*7+seed)*.15,glm::dvec3(0,0,1)));setQ("eyeR",glm::angleAxis(std::sin(t*7.3+seed+1)*.15,glm::dvec3(0,0,1)));(void)dt;
}

void BossPose::AnimateCrablet(Rig& rig,const BossCrablet& crab,double dt){
    auto [it,inserted]=animator_->crabPhi.emplace(crab.id,hashN(crab.id));
    const double seed=animator_->crabSeed.emplace(crab.id,hashN(crab.id+917)*10).first->second;
    animator_->animateCrablet(rig,crab,dt,crab.age,seed,it->second);(void)inserted;
}

void BossPose::Update(const BossPoseInput& input){
    if(input.dt<0||!std::isfinite(input.dt))throw std::invalid_argument("Invalid Boss pose delta");
    AnimateHull(rigs_.at(0),input);
    hull_.root=glm::translate(glm::dmat4(1),input.position)*glm::rotate(glm::dmat4(1),input.yaw,glm::dvec3(0,1,0));hull_.skin=rigs_[0].skin;
    crablets_.clear();
}

void BossPose::Update(BossSimulation& simulation){
    const auto& state=simulation.state();BossPoseInput input;
    input.dt=hasSimulationClock_?std::max(0.0,state.clock-simulationClock_):0;simulationClock_=state.clock;hasSimulationClock_=true;
    input.position=state.pos;input.yaw=state.yaw;input.speed=glm::length(glm::dvec2(state.vel.x,state.vel.z));input.turn=state.turn;input.phase=state.phase;input.stunned=state.stunned;input.dead=state.dead;
    const auto pulse=simulation.takePoseHitPulse();input.flash=pulse.flash;input.weakFlashAmount=pulse.weakFlash;input.hurt=pulse.hurt;
    if(const auto aim=simulation.poseAim()){input.aim=*aim;input.hasAim=true;}
    if(state.anim==1||state.anim==2){input.move=state.anim==1?"intro":"roar";input.movePhase="act";input.moveTime=std::max(0.0,state.clock-state.animT0);input.phaseDuration=state.anim==1?3.4:1.9;}
    else if(simulation.move()){
        const auto& move=*simulation.move();switch(move.id){case BossMoveId::Slam:input.move="slam";break;case BossMoveId::Barrage:input.move="barrage";break;case BossMoveId::Sweep:input.move="sweep";break;case BossMoveId::Charge:input.move="charge";break;case BossMoveId::Crablets:input.move="crablets";break;case BossMoveId::Frenzy:input.move="frenzy";break;}
        const auto phase=move.phase(state.clock);double phaseStart=0;
        switch(phase){case BossMovePhase::Telegraph:input.movePhase="tele";input.phaseDuration=move.tele;break;
        case BossMovePhase::Active:input.movePhase="act";phaseStart=move.tele;input.phaseDuration=move.act;break;
        case BossMovePhase::Recovery:input.movePhase="rec";phaseStart=move.tele+move.act;input.phaseDuration=move.rec;break;
        default:input.move.clear();break;}
        if(!input.move.empty()){input.moveTime=std::max(0.0,state.clock-move.t0-phaseStart);input.hasParams=true;input.wall=move.wall;input.count=move.count;input.rings=move.rings;for(const auto& barrel:move.barrels)input.barrels.push_back({barrel.x,barrel.y,barrel.z,barrel.throwAt});}
    }
    Update(input);simulation.setHitShapes(hitShapes_);simulation.setPoseSockets(poseSockets_);
    std::vector<int> live;live.reserve(simulation.crablets().size());const double dt=input.dt;
    for(const auto& crab:simulation.crablets())if(!crab.dead){live.push_back(crab.id);AnimateCrablet(rigs_.at(1),crab,dt);BossCrabletPose pose;pose.id=crab.id;pose.root=glm::translate(glm::dmat4(1),crab.pos)*glm::rotate(glm::dmat4(1),crab.yaw,glm::dvec3(0,1,0));Evaluate(rigs_[1]);pose.skin=rigs_[1].skin;crablets_.push_back(std::move(pose));}
    for(auto it=animator_->crabPhi.begin();it!=animator_->crabPhi.end();)if(std::find(live.begin(),live.end(),it->first)==live.end()){animator_->crabSeed.erase(it->first);it=animator_->crabPhi.erase(it);}else ++it;
}
} // namespace restoration
