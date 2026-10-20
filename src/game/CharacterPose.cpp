#include "CharacterPose.hpp"
#include "CharacterData.hpp"
#include "CharacterRenderData.hpp"
#include "CharacterWeaponData.hpp"
#include "CharacterWeaponAnimation.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace restoration {
namespace {
constexpr double tau = 6.28318530717958647692;
constexpr std::array<double,8> menuTimes={0.0,1.2,1.8,3.0,3.5,5.0,5.6,8.0};
constexpr std::array<double,8> menuValues={0.0,0.0,0.5,0.5,-0.4,-0.4,0.05,0.0};

enum Channel : std::size_t {
    HipsP=0, Hips=3, Spine=6, Chest=9, Neck=12, Head=15, ClavL=18, ClavR=21,
    UArmL=24, UArmR=27, FArmL=30, FArmR=33, HandL=36, HandR=39,
    FootL=42, FootLR=45, FootR=48, FootRR=51, Anc=54, Ancr=57,
    Poler=60, Polel=63, Ltgt=66, Ltgtr=69, Kneel=72, Kneer=75,
    Ikr=78, Ikl=79, Ltw=80, Ltrot=81, Spin=82, Wpl=83, Wpr=84,
    Stab=85, Afolt=86, Afolr=87, Brow=88, BrowY=89, MCurve=90,
    MWidth=91, MOpen=92, MTilt=93, Eye=94, Wink=95, Squint=96,
    LookX=97, LookY=98, Model=99, ModelR=102, SqY=105, SqXZ=106,
    Hly=107, Hlp=108, Crouch=109, HandPL=110, HandPR=111, Ears=112,
    Anl=113, Anlr=116, Tiptoe=119, Bras=120, Pupil=121, Breath=122,
    Sneer=123, Pucker=124, Blush=125
};

constexpr std::size_t SLeanP=0, SLeanR=2, SPelY=4, SSq=6, SArmL=8, SArmR=10,
    SWpx=14, SWpy=16, SWpz=18, SWrx=20, SWry=22, SRCp=24, SRCz=26,
    SRCy=28, SRCr=30, SHitP=32, SHitR=34,
    SHitY=36, SHeadP=38, SHeadR=40, SHly=42, SHlp=44, SShift=46,
    STankX=48, STankZ=50, STankL=52, SSqY=54, SSqP=56, SSqR=58,
    SSqH=60, SClav=62, SStag=64, SLagX=66, SLagZ=68, SHemP=70,
    SHemR=72, STankY=74, STankZRot=76, STankXRot=78, SEarL=80,
    SEarR=82, SHemV=84, SRCp2=86, SRCz2=88, SRCy2=90, SGrip=96, SGripL=98,
    SAttentionY=100, SAttentionP=102;

constexpr std::size_t TShoot=0, TFlick=1, TThrow=2, TLand=3, TJump=4,
    THit=5, TLeap=6, TSlam=7, TSpawn=8, TRelease=9, TImpact=10,
    TBrake=11, TForm=12, TStagger=13, TShootL=14, TDodge=15,
    TSlosh=16;

double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
double damp(double a, double b, double rate, double dt) { return a + (b - a) * (1.0 - std::exp(-rate * dt)); }
double ease(double value) { const double t=clamp(value,0.0,1.0); return t*t*(3.0-2.0*t); }
double backOut(double value,double strength=2.2) {
    const double t=clamp(value,0.0,1.0)-1.0;
    return t*t*((strength+1.0)*t+strength)+1.0;
}
double win(double t,double a,double b,double c,double d) {
    if(t<=a||t>=d)return 0.0;
    if(t<b)return ease((t-a)/(b-a));
    if(t>c)return 1.0-ease((t-c)/(d-c));
    return 1.0;
}
void setE(std::array<double,CharacterPoseChannelCount>& pose,std::size_t index,double x,double y,double z) {
    pose[index]=x;pose[index+1]=y;pose[index+2]=z;
}
void lerpE(std::array<double,CharacterPoseChannelCount>& pose,std::size_t index,double x,double y,double z,double weight) {
    pose[index]+=(x-pose[index])*weight;pose[index+1]+=(y-pose[index+1])*weight;pose[index+2]+=(z-pose[index+2])*weight;
}
void setAnchor(std::array<double,CharacterPoseChannelCount>& pose,const CharacterHoldAnchor& anchor) {
    setE(pose,Anc,anchor.p.x,anchor.p.y,anchor.p.z);
    setE(pose,Ancr,anchor.r.x,anchor.r.y,anchor.r.z);
}
void poseLerp(std::array<double,CharacterPoseChannelCount>& out,const std::array<double,CharacterPoseChannelCount>& a,
              const std::array<double,CharacterPoseChannelCount>& b,double weight) {
    for(std::size_t i=0;i<out.size();++i)out[i]=a[i]+(b[i]-a[i])*weight;
}
double sstep(double a, double b, double x) {
    const double t = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
double easeIn(double value) { const double t=clamp(value,0.0,1.0); return t*t; }
double easeOut(double value) { const double t=clamp(value,0.0,1.0); return 1.0-(1.0-t)*(1.0-t); }
double mitPunch(double u) { return u<0.045?easeOut(u/0.045):u<0.21?1.0-ease((u-0.045)/0.165):0.0; }
double keyedCatmull(double t,const double* times,const double* values,std::size_t count) {
    if(t<=times[0]) return values[0];
    if(t>=times[count-1]) return values[count-1];
    std::size_t i=1;
    while(times[i]<t) ++i;
    const double t0=times[i-1],h=times[i]-t0,u=(t-t0)/h;
    const double m0=i>1?(values[i]-values[i-2])/(times[i]-times[i-2]):0.0;
    const double m1=i<count-1?(values[i+1]-values[i-1])/(times[i+1]-times[i-1]):0.0;
    const double u2=u*u,u3=u2*u;
    return (2.0*u3-3.0*u2+1.0)*values[i-1]+(u3-2.0*u2+u)*h*m0+
        (-2.0*u3+3.0*u2)*values[i]+(u3-u2)*h*m1;
}
double wrapAngle(double a) {
    while (a > 3.14159265358979323846) a -= tau;
    while (a < -3.14159265358979323846) a += tau;
    return a;
}
double fract(double value) { return value - std::floor(value); }
double minimumJerk(double value) {
    const double t = clamp(value, 0.0, 1.0);
    return t * t * t * (10.0 + t * (6.0 * t - 15.0));
}
std::uint32_t hashName(const std::string& text) {
    // Match the source's UTF-16 code-unit FNV-1a hash (including surrogate pairs).
    std::uint32_t hash = 2166136261u;
    auto addUnit = [&](std::uint16_t unit) { hash = (hash ^ unit) * 16777619u; };
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        std::uint32_t codepoint = lead;
        if ((lead & 0xe0u) == 0xc0u && i < text.size()) codepoint = ((lead & 0x1fu) << 6) | (static_cast<unsigned char>(text[i++]) & 0x3fu);
        else if ((lead & 0xf0u) == 0xe0u && i + 1 < text.size()) codepoint = ((lead & 0x0fu) << 12) | ((static_cast<unsigned char>(text[i++]) & 0x3fu) << 6) | (static_cast<unsigned char>(text[i++]) & 0x3fu);
        else if ((lead & 0xf8u) == 0xf0u && i + 2 < text.size()) codepoint = ((lead & 0x07u) << 18) | ((static_cast<unsigned char>(text[i++]) & 0x3fu) << 12) | ((static_cast<unsigned char>(text[i++]) & 0x3fu) << 6) | (static_cast<unsigned char>(text[i++]) & 0x3fu);
        if (codepoint <= 0xffffu) addUnit(static_cast<std::uint16_t>(codepoint));
        else {
            codepoint -= 0x10000u;
            addUnit(static_cast<std::uint16_t>(0xd800u + (codepoint >> 10)));
            addUnit(static_cast<std::uint16_t>(0xdc00u + (codepoint & 0x3ffu)));
        }
    }
    return hash;
}
double mulberry(std::uint32_t& state) {
    state += 0x6d2b79f5u;
    std::uint32_t value = (state ^ (state >> 15)) * (1u | state);
    value = (value + ((value ^ (value >> 7)) * (61u | value))) ^ value;
    return static_cast<double>(value ^ (value >> 14)) / 4294967296.0;
}
double spring(std::array<double, 104>& s, std::size_t i, double target, double hz, double zeta, double dt) {
    const double w = tau * hz, k = w * w, c = 2.0 * zeta * w;
    double x = s[i], v = s[i + 1];
    const auto n = std::max(1, static_cast<int>(std::ceil(dt * w * 1.1)));
    const double h = dt / n;
    for (int j = 0; j < n; ++j) { v += (k * (target - x) - c * v) * h; x += v * h; }
    s[i] = x; s[i + 1] = v;
    return x;
}
double springAnalytic(std::array<double, 104>& s, std::size_t i, double target, double hz, double zeta, double dt) {
    const double w=tau*hz,x0=s[i]-target,v0=s[i+1];
    double x,v;
    if(zeta<0.999) {
        const double wd=w*std::sqrt(1.0-zeta*zeta),e=std::exp(-zeta*w*dt),c=std::cos(wd*dt),sn=std::sin(wd*dt);
        const double b=(v0+zeta*w*x0)/wd;
        x=e*(x0*c+b*sn);
        v=e*((-zeta*w*x0+wd*b)*c+(-zeta*w*b-wd*x0)*sn);
    } else {
        const double e=std::exp(-w*dt),b=v0+w*x0;
        x=e*(x0+b*dt);v=e*(v0-w*b*dt);
    }
    s[i]=x+target;s[i+1]=v;
    return s[i];
}
glm::dquat euler(const glm::dvec3& e, bool yxz) {
    const auto x = glm::angleAxis(e.x, glm::dvec3(1, 0, 0));
    const auto y = glm::angleAxis(e.y, glm::dvec3(0, 1, 0));
    const auto z = glm::angleAxis(e.z, glm::dvec3(0, 0, 1));
    return yxz ? y * x * z : x * y * z;
}
glm::dquat threeDecomposeQuaternion(const glm::dmat4& matrix) {
    glm::dvec3 x{matrix[0][0],matrix[0][1],matrix[0][2]};
    glm::dvec3 y{matrix[1][0],matrix[1][1],matrix[1][2]};
    glm::dvec3 z{matrix[2][0],matrix[2][1],matrix[2][2]};
    double sx=glm::length(x), sy=glm::length(y), sz=glm::length(z);
    if(glm::determinant(glm::dmat3(matrix))<0.0) sx=-sx;
    if(std::abs(sx)<1e-12||std::abs(sy)<1e-12||std::abs(sz)<1e-12) return glm::dquat(1,0,0,0);
    x/=sx; y/=sy; z/=sz;
    const double m11=x.x,m12=y.x,m13=z.x;
    const double m21=x.y,m22=y.y,m23=z.y;
    const double m31=x.z,m32=y.z,m33=z.z;
    const double trace=m11+m22+m33;
    double qx=0.0,qy=0.0,qz=0.0,qw=1.0;
    if(trace>0.0) {
        const double inv=0.5/std::sqrt(trace+1.0); qw=0.25/inv;
        qx=(m32-m23)*inv; qy=(m13-m31)*inv; qz=(m21-m12)*inv;
    } else if(m11>m22&&m11>m33) {
        const double s=2.0*std::sqrt(1.0+m11-m22-m33); qw=(m32-m23)/s;
        qx=0.25*s; qy=(m12+m21)/s; qz=(m13+m31)/s;
    } else if(m22>m33) {
        const double s=2.0*std::sqrt(1.0+m22-m11-m33); qw=(m13-m31)/s;
        qx=(m12+m21)/s; qy=0.25*s; qz=(m23+m32)/s;
    } else {
        const double s=2.0*std::sqrt(1.0+m33-m11-m22); qw=(m21-m12)/s;
        qx=(m13+m31)/s; qy=(m23+m32)/s; qz=0.25*s;
    }
    return glm::dquat(qw,qx,qy,qz);
}
bool finite(const glm::dvec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}

glm::dmat4 CharacterTransform::matrix() const {
    if (matrixOverride) return authoredMatrix;
    return glm::translate(glm::dmat4(1.0), position) * glm::mat4_cast(rotation) * glm::scale(glm::dmat4(1.0), scale);
}

struct CharacterPose::State {
    enum FootMode { Gait = 0, Catch = 1, Settle = 2 };
    struct Foot {
        int index = 0;
        FootMode mode = Settle;
        double side = 1.0;
        glm::dvec3 plantedPosition{0.0}, normal{0,1,0}, contact{0.0}, contactNormal{0,1,0}, targetNormal{0,1,0};
        double yaw = 0.0, contactYaw = 0.0, pitch = 0.0;
        bool planted = true, inStance = true, swinging = false, displacementValid = false;
        double stanceU = 0.5, stanceTime = 1.0, swingU = 0.0, duration = 0.2;
        glm::dvec3 from{0.0}, to{0.0}, displacement{0.0};
        double fromYaw = 0.0, toYaw = 0.0, displacementYaw = 0.0;
        double lift = 0.06, toe = 0.0, land = 0.0;
    };
    struct MicroExpression {
        std::array<double,8> values{};
        double weight=0.0, time=99.0, attack=0.1, hold=0.2, decay=0.4, browAsymmetry=0.0;
        glm::dvec3 extra{0.0};
        bool active=false;
    };
    const CharacterData* data;
    const CharacterRig* rig;
    std::string name, weapon, dance, sub="bomb";
    CharacterStyle style{};
    std::string previousDance,lastDance;
    std::uint32_t seed=0;
    int danceVariant=0;
    CharacterPoseFrame frame;
    std::array<double, CharacterPoseChannelCount> p{};
    std::array<double, 104> springs{};
    std::array<double, 20> timers{};
    std::array<double, 9> expressionWeights{};
    std::uint32_t randomState = 0;
    glm::dvec3 previousPosition{0.0}, velocity{0.0}, acceleration{0.0};
    double previousYaw=0.0, yaw=0.0, yawRate=0.0, time=0.0;
    double danceTime=0.0, previousDanceTime=0.0, danceFade=1.0, danceWeight=0.0, danceOffset=0.0;
    double kvx=0.0, kvz=0.0, kax=0.0, kaz=0.0, hs=0.0, gv=0.0, gs=0.0;
    double gvx=0.0, gvz=0.0, tvx=0.0, tvz=0.0;
    bool treadmill=false, feetValid=false, replant=true;
    double plantWeight=1.0, footTwist=0.0, hipDrop=0.0, settleCooldown=0.0;
    std::array<double,6> stance{};
    std::array<Foot,2> feet{};
    std::array<glm::dvec3,2> legTargets{};
    std::array<glm::dquat,2> legTargetRotations{};
    std::array<std::array<glm::dquat,3>,2> legRotations{};
    std::array<glm::dquat,CharacterBoneCount> retainedFingerRotations{};
    std::array<CharacterTransform,CharacterBoneCount> retainedDetailLocals{};
    std::array<glm::dquat,3> armRightRotations{};
    std::array<glm::dquat,3> armLeftRotations{};
    bool armRightSolved=false, armLeftSolved=false;
    double ikError=0.0, ikErrorPre=0.0, leftReach=0.0;
    double leapEnd=-1.0;
    bool slamGround=false;
    double kgx=0.0, kgz=0.0, mdx=0.0, mdz=1.0, vy=0.0;
    double airTime=0.0, groundDistance=9.0, jumpRun=0.0;
    int jumpLead=0;
    double phase=0.0, gaitWeight=0.0, runWeight=0.0, duty=0.6, cadence=1.5, lift=0.06;
    double hipTwist=0.0, aimWeight=0.0, subWeight=0.0, twoWeight=0.0, rollWeight=0.0, lockWeight=0.0, airWeight=0.0, charge=0.0, tired=0.0, goo=0.0;
    double exert=0.0, effort=0.0, hitAmplitude=1.0, hitX=0.0, hitZ=1.0, hitAcc=0.0;
    double stepOfsX=0.0, stepOfsZ=0.0, landAmplitude=0.0, glowWeight=0.0, lowInkWeight=0.0, inkScale=1.0;
    double breathPhase=0.0, breathAmplitude=1.0, breathRate=1.0, breathNumber=0.0, sighTime=0.0, sigh=0.0;
    double idleTime=0.0, shiftTarget=1.0, shift=0.0;
    glm::dvec3 weaponSwayRawPosition{0.0}, weaponSwayEuler{0.0};
    double recoilPitch=0.0, recoilBack=0.0, recoilPitchLeft=0.0, recoilBackLeft=0.0;
    double fireHold=0.0;
    double chargeFlash=0.0;
    bool fireWant=false;
    double appliedHipDrop=0.0;
    double lastShot=99.0, lastRelease=99.0, hurt=0.0, fullTime=0.0;
    CharacterWeaponAnimation weaponAnimationRight, weaponAnimationLeft;
    double bowDraw=0.0,bowHold=0.0,bowLastRelease=99.0,bowCant=-0.85;
    double mittsChargeWeight=0.0,mittsLeapWeight=0.0,mittsClingWeight=0.0;
    double bladeTime=99.0, bladeSide=1.0, bladeRaise=0.0;
    bool bladeHeavy=false;
    bool dodgeRollActive=false;
    double dodgeRollTime=0.0, dodgeRollDuration=0.3;
    glm::dvec2 dodgeRollDirection{0.0,1.0};
    double dodgeX=0.0, dodgeZ=1.0, dodgeDuration=0.3;
    bool bombHeld=false, subPrev=false, subPropHidden=false, hairOdd=false;
    double hairAccum=0.0;
    double bombTime=0.0,bombScale=1.0;
    double tumble=0.0, tumbleX=1.0, tumbleZ=0.0, tumbleDrop=0.0;
    double swipeSide=1.0;
    double gripTimer=99.0;
    struct HairFrame {
        glm::dvec3 direction{0.0,-1.0,0.0}, intoHead{0.0},
            axis[4]{{0.0,-1.0,0.0},{0.0,-1.0,0.0},{0.0,-1.0,0.0},{0.0,-1.0,0.0}},
            bendIn[4]{{1.0,0.0,0.0},{1.0,0.0,0.0},{1.0,0.0,0.0},{1.0,0.0,0.0}},
            bendSide[4]{{0.0,0.0,1.0},{0.0,0.0,1.0},{0.0,0.0,1.0},{0.0,0.0,1.0}};
        double length=0.0, stiffness=1.0, gravity=1.0, phase=0.0;
        std::array<glm::dvec3,3> x{}, velocity{}, tipX{}, tipVelocity{};
        std::array<glm::dquat,4> rotation{};
    };
    std::vector<HairFrame> hair;
    std::array<int,CharacterBoneCount> hairStrandByBone{}, hairSegmentByBone{};
    glm::dvec3 headPrevPosition{0.0}, headPrevVelocity{0.0}, headAcceleration{0.0}, headVelocity{0.0};
    glm::dquat headPrevWorldRotation{1.0,0.0,0.0,0.0};
    double headRotationY=0.0, headRotationZ=0.0;
    std::array<MicroExpression,6> microExpressions{};
    double hemFront=0.0, hemBack=0.0, hemCenter=0.0, hemSide=0.0;
    double tankY=0.0, tankRotX=0.0, tankRotZ=0.0, tankFillRotX=0.0, tankFillRotZ=0.0, tankSlosh=0.0, tankFillLevel=0.0;
    bool headInitialized=false;
    bool squidInitialized=false, squidGhostVisible=false, squidEyesVisible=true;
    double squidYaw=0.0, squidRoll=0.0, squidHopPhase=0.0, squidEyesScaleY=1.0, squidEyesPositionY=0.0;
    double squidWigAmplitude=0.012, squidWigFrequency=9.0, squidWigMotion=0.0;
    CharacterTransform squidPivot;
    glm::dvec3 squidPivotPosition{0.0};
    glm::dquat squidPivotRotation{1.0,0.0,0.0,0.0};
    std::array<double,2> handShape{1.0,0.0};
    double lookTime=0.0, glanceYaw=0.0, glancePitch=0.0, attentionTimer=0.0, attentionGlanceYaw=0.0, attentionGlancePitch=0.0;
    int attentionKind=0;
    double earTimer=0.0, blinkTimer=-1.0, blinkClose=0.08, blinkHold=0.03,
        blinkOpen=0.16, blinkAmplitude=1.0, blinkLag=0.008, blinkNext=0.6, blinkSqueeze=0.0,
        turnBlink=0.0, blinkLeft=0.0, blinkRight=0.0;
    bool blinkDouble=false;
    double gazeX=0.0,gazeY=0.0,gazeStartX=0.0,gazeStartY=0.0,gazeTargetX=0.0,gazeTargetY=0.0,
        gazeSaccadeTime=-1.0,gazeSaccadeDuration=0.05,gazeFixX=0.0,gazeFixY=0.0,gazeMicroX=0.0,gazeMicroY=0.0,
        gazeMicroTime=0.5, attentionDistance=5.0, eyeScaleLeft=1.0, eyeScaleRight=1.0;
    glm::dvec3 attentionPoint{0.0,1.2,5.0};
    glm::dvec3 headKidPosition{0.0};
    glm::dquat headKidRotation{1.0,0.0,0.0,0.0};
    bool attentionOn=false;
    CharacterForm form=CharacterForm::Kid, previousForm=CharacterForm::Kid;
    double formTime=99.0;
    double kidScale=1.0,sqScale=0.0,kidSY=1.0,kidSXZ=1.0,sqSY=1.0,sqSXZ=1.0,kidLift=0.0;
    bool rootInitialized=false, grounded=true, moving=false, previousMoving=false;

    State(const CharacterData& sourceData, const CharacterRig& source, std::string actorName, std::string weaponName)
        : data(&sourceData), rig(&source), name(std::move(actorName)), weapon(std::move(weaponName)), seed(hashName(name)) {
        danceOffset=fract(static_cast<double>(seed)*0.61803)*2.3;
        const int left=find("footL"), right=find("footR");
        const double hipWidth=(left>=0?std::abs(source.bones[static_cast<std::size_t>(left)].rest.x):0.084)+0.008;
        const double ankleHeight=left>=0?source.bones[static_cast<std::size_t>(left)].rest.y:0.085;
        stance={
            static_cast<double>(static_cast<float>(hipWidth+0.012)),
            static_cast<double>(static_cast<float>(0.014)),
            static_cast<double>(static_cast<float>(0.19)),
            static_cast<double>(static_cast<float>(-hipWidth-0.008)),
            static_cast<double>(static_cast<float>(-0.01)),
            static_cast<double>(static_cast<float>(-0.2))};
        for(int i=0;i<2;++i) { feet[static_cast<std::size_t>(i)].index=i; feet[static_cast<std::size_t>(i)].side=i==0?1.0:-1.0; }
        retainedFingerRotations.fill(glm::dquat(1.0,0.0,0.0,0.0));
        (void)right; (void)ankleHeight;
        randomState = seed;
        hairStrandByBone.fill(-1); hairSegmentByBone.fill(-1);
        for(std::size_t i=0;i<source.bones.size();++i) {
            const auto& bone=source.bones[i];
            retainedDetailLocals[i].position=bone.parent<0?bone.rest:
                bone.rest-source.bones[static_cast<std::size_t>(bone.parent)].rest;
        }
        hair.resize(source.hair.size());
        for(auto& strand:hair) strand.rotation.fill(glm::dquat(1.0,0.0,0.0,0.0));
        const int headIndex=find("head");
        const glm::dvec3 headCenter=(headIndex>=0?source.bones[static_cast<std::size_t>(headIndex)].rest:glm::dvec3(0.0))+glm::dvec3(0.0,0.164,0.014);
        const auto floatVector=[](const glm::dvec3& value) {
            return glm::dvec3(static_cast<float>(value.x),static_cast<float>(value.y),static_cast<float>(value.z));
        };
        for(std::size_t strand=0;strand<source.hair.size();++strand) {
            auto& h=hair[strand]; const auto& meta=source.hair[strand];
            h.direction=meta.direction; h.length=meta.length; h.stiffness=meta.stiffness; h.gravity=meta.gravity;
            // character.js stores its seeded phase in a Float32Array after multiplying by TAU.
            h.phase=static_cast<float>(random()*tau);
            const int first=find("hair"+std::to_string(strand)+"_0");
            if(first>=0) h.intoHead=floatVector(glm::normalize(headCenter-source.bones[static_cast<std::size_t>(first)].rest));
            for(int segment=0;segment<4;++segment) {
                const int aIndex=segment<3?find("hair"+std::to_string(strand)+"_"+std::to_string(segment)):
                    find("hairTip"+std::to_string(strand));
                if(aIndex<0) continue;
                glm::dvec3 axis;
                if(segment<2) {
                    const int bIndex=find("hair"+std::to_string(strand)+"_"+std::to_string(segment+1));
                    axis=bIndex>=0?source.bones[static_cast<std::size_t>(bIndex)].rest-source.bones[static_cast<std::size_t>(aIndex)].rest:glm::dvec3(0,-1,0);
                } else if(segment==2) {
                    const int bIndex=find("hairTip"+std::to_string(strand));
                    axis=bIndex>=0?source.bones[static_cast<std::size_t>(bIndex)].rest-source.bones[static_cast<std::size_t>(aIndex)].rest:glm::dvec3(0,-1,0);
                } else axis=h.axis[2];
                if(glm::dot(axis,axis)<1e-10) axis={0,-1,0};
                axis=glm::normalize(axis);
                const auto out=glm::normalize(source.bones[static_cast<std::size_t>(aIndex)].rest-headCenter);
                auto bend=glm::cross(axis,out);
                if(glm::dot(bend,bend)<1e-8) bend={1,0,0};
                bend=glm::normalize(bend);
                const auto side=glm::normalize(glm::cross(axis,bend));
                h.axis[static_cast<std::size_t>(segment)]=floatVector(axis);
                h.bendIn[static_cast<std::size_t>(segment)]=floatVector(bend);
                h.bendSide[static_cast<std::size_t>(segment)]=floatVector(side);
                if(segment<3) {
                    const int bone=find("hair"+std::to_string(strand)+"_"+std::to_string(segment));
                    if(bone>=0) { hairStrandByBone[static_cast<std::size_t>(bone)]=static_cast<int>(strand); hairSegmentByBone[static_cast<std::size_t>(bone)]=segment; }
                } else {
                    const int bone=find("hairTip"+std::to_string(strand));
                    if(bone>=0) { hairStrandByBone[static_cast<std::size_t>(bone)]=static_cast<int>(strand); hairSegmentByBone[static_cast<std::size_t>(bone)]=segment; }
                }
            }
        }
        // Preserve the source constructor's RNG stream through lookT.
        const double shuffleTime=4.0+random()*6.0;
        const double shiftTime=2.0+random()*3.0;
        const double fidgetTime=3.0+random()*3.0;
        blinkNext=0.6+random()*2.5;
        attentionTimer=0.3+random()*0.8;
        const double microExpressionTime=2.0+random()*4.0;
        sighTime=4.0+random()*5.0;
        gripTimer=4.0+random()*5.0;
        lookTime=0.5+random();
        earTimer=3.0+random()*4.0;
        (void)shuffleTime; (void)shiftTime; (void)fidgetTime;
        (void)microExpressionTime;
        timers.fill(99.0);
        frame.bones.fill(CharacterTransform{});
        frame.skinMatrices.fill(glm::dmat4(1.0));
        for (std::size_t i = 0; i < source.bones.size(); ++i) {
            if (source.bones[i].parent >= static_cast<int>(i) || source.bones[i].parent < -1)
                throw std::invalid_argument("Source character bones must be in parent-first order");
            if (!finite(source.bones[i].rest)) throw std::invalid_argument("Source character rest pose is nonfinite");
        }
        frame.material=CharacterOutfit(style.outfit);
        frame.material.iris=CharacterIris[style.eyes][0]; frame.material.iris2=CharacterIris[style.eyes][1];
        frame.material.freckle=CharacterSkinLighting[style.skin].freckle;
        frame.material.hurtSeed=static_cast<double>(seed%997)*0.37;
        frame.material.team=glm::dvec3(1.0);
        frame.material.rim={0.075,0.08125,0.09375,3.4};
        frame.material.rimLight=glm::normalize(glm::dvec3(0.3,0.6,-0.75));
    }

    double random() { return mulberry(randomState); }

    void recoilImpulse(double amount,int hand=0) {
        const auto& recoil=GetCharacterWeaponHold(weapon).rc;
        const double w=tau*recoil.hz;
        if(hand==1) {
            springs[SRCp2+1]+=recoil.kick*w*1.35*amount;
            springs[SRCz2+1]+=recoil.back*w*1.35*amount;
            springs[SRCy2+1]+=(random()-0.5)*2.0*recoil.jit*w*amount;
        } else {
            springs[SRCp+1]+=recoil.kick*w*1.35*amount;
            springs[SRCz+1]+=recoil.back*w*1.35*amount;
            springs[SRCy+1]+=(random()-0.5)*2.0*recoil.jit*w*amount;
            springs[SRCr+1]+=(random()-0.5)*2.0*recoil.jit*w*amount;
        }
        springs[STankX+1]+=(random()-0.5)*0.4*amount;
        springs[STankL+1]-=0.4*amount;
    }

    void hairKick(double x,double y,double z) {
        const glm::dvec3 impulse{z*0.6+x*0.3,x*0.4,y*0.5-x*0.2};
        for(auto& strand:hair) {
            for(auto& velocity:strand.velocity) {
                velocity.x=static_cast<float>(velocity.x+impulse.x);
                velocity.y=static_cast<float>(velocity.y+impulse.y);
                velocity.z=static_cast<float>(velocity.z+impulse.z);
            }
        }
    }

    void playMicroExpression(const std::array<double,8>& values,double weight,double attack,double hold,double decay,
                             double browAsymmetry=0.0,glm::dvec3 extra=glm::dvec3(0.0)) {
        MicroExpression* best=nullptr;
        double bestWeight=std::numeric_limits<double>::infinity();
        for(auto& expression:microExpressions) {
            const double score=expression.time>expression.attack+expression.hold+expression.decay?-1.0:expression.weight*expression.weight;
            if(score<bestWeight) { bestWeight=score; best=&expression; }
        }
        if(!best) return;
        best->values=values; best->weight=weight; best->time=0.0; best->attack=attack; best->hold=hold;
        best->decay=decay; best->browAsymmetry=browAsymmetry; best->extra=extra; best->active=true;
    }

    void tickMicroExpressions(double dt) {
        static constexpr std::array<std::size_t,8> channels={Brow,BrowY,Eye,MCurve,MWidth,MOpen,MTilt,Squint};
        for(auto& expression:microExpressions) {
            if(!expression.active) continue;
            expression.time+=dt;
            const double total=expression.attack+expression.hold+expression.decay;
            if(expression.time>=total) { expression.active=false; continue; }
            const double envelope=expression.time<expression.attack
                ?sstep(0.0,1.0,expression.time/expression.attack)
                :expression.time<expression.attack+expression.hold?1.0
                :1.0-sstep(0.0,1.0,(expression.time-expression.attack-expression.hold)/expression.decay);
            const double weight=std::abs(expression.weight)*envelope;
            const double side=expression.weight<0.0?-1.0:1.0;
            for(std::size_t i=0;i<channels.size();++i)
                p[channels[i]]+=expression.values[i]*weight*(channels[i]==MTilt?side:1.0);
            p[Bras]+=expression.browAsymmetry*envelope;
            p[Sneer]+=expression.extra.x*weight;
            p[Pucker]+=expression.extra.y*weight;
            p[Blush]+=expression.extra.z*weight;
        }
    }

    void poseAir(double blend) {
        std::array<double,CharacterPoseChannelCount> target=p;
        const double up=sstep(-1.5,4.0,vy), jumpTime=timers[TJump];
        const double launch=jumpTime<0.3?1.0-sstep(0.03,0.2,jumpTime):0.0;
        const double apex=1.0-sstep(0.6,3.2,std::abs(vy));
        const double reach=(1.0-up)*sstep(0.95,0.15,groundDistance);
        const double fallLong=sstep(0.4,1.0,airTime)*(1.0-up)*(1.0-reach);
        const double leap=jumpTime<1.2?jumpRun:0.0;
        const double lead=jumpLead==1?-1.0:1.0;
        const double tuck=(1.0-launch)*std::max(apex,up*0.85)*(1.0-reach);
        const auto setE=[&](std::size_t channel,double x,double y,double z) {
            target[channel]=x; target[channel+1]=y; target[channel+2]=z;
        };
        const auto lerpE=[&](std::size_t channel,double x,double y,double z,double weight) {
            target[channel]+=(x-target[channel])*weight;
            target[channel+1]+=(y-target[channel+1])*weight;
            target[channel+2]+=(z-target[channel+2])*weight;
        };
        setE(FootL,0.1,0.16+(0.34-0.16)*tuck,0.0+0.07*tuck);
        setE(FootLR,0.15+(0.55-0.15)*tuck,0.16,0.0);
        setE(FootR,-0.1,0.16+(0.31-0.16)*tuck,-0.03+(0.04+0.03)*tuck);
        setE(FootRR,0.25+(0.6-0.25)*tuck,-0.16,0.0);
        if(leap>0.001) {
            const double pass=sstep(0.1,-3.5,vy);
            const std::size_t leadFoot=lead>0.0?FootL:FootR, leadRotation=lead>0.0?FootLR:FootRR;
            const std::size_t trailFoot=lead>0.0?FootR:FootL, trailRotation=lead>0.0?FootRR:FootLR;
            const double weight=leap*(1.0-reach*0.7);
            lerpE(leadFoot,0.09*lead,0.36+(0.16-0.36)*pass,0.2+(-0.12-0.2)*pass,weight);
            lerpE(leadRotation,0.25+(0.9-0.25)*pass,0.12*lead,0.0,weight);
            lerpE(trailFoot,-0.09*lead,0.15+(0.33-0.15)*pass,-0.27+(0.14+0.27)*pass,weight);
            lerpE(trailRotation,1.15+(0.3-1.15)*pass,-0.12*lead,0.0,weight);
            target[Hips+1]+=0.12*lead*(1.0-2.0*pass)*weight;
            target[Chest+1]-=0.1*lead*(1.0-2.0*pass)*weight;
        }
        lerpE(FootL,0.09,0.07,-0.06-0.06*leap,launch); lerpE(FootLR,1.0,0.08,0.0,launch);
        lerpE(FootR,-0.09,0.09,-0.1-0.06*leap,launch); lerpE(FootRR,1.1,-0.08,0.0,launch);
        lerpE(FootL,0.11,0.1,0.07,reach); lerpE(FootLR,-0.08,0.14,0.0,reach);
        lerpE(FootR,-0.11,0.12,-0.03,reach); lerpE(FootRR,0.05,-0.14,0.0,reach);
        target[Wpl]=0.0; target[Wpr]=0.0;
        target[Kneel]=0.12; target[Kneer]=-0.12;
        target[HipsP+1]=-0.03+0.03*up-0.02*tuck-0.03*reach;
        target[Hips]+=0.1*tuck-0.06*launch;
        target[Spine]+=0.12*tuck-0.1*launch+0.04*reach;
        target[Chest]+=0.05*tuck-0.05*launch;
        target[Hlp]+=-0.1*launch+0.12*reach+0.05*tuck;
        const double windmill=std::sin(time*10.0)*0.5*fallLong;
        target[UArmL]=(-1.0+(-0.5+1.0)*up)+(-2.3-(-1.0+(-0.5+1.0)*up))*launch+windmill;
        target[UArmL+2]=(1.25+(0.95-1.25)*up)+(0.4-(1.25+(0.95-1.25)*up))*launch+0.2*apex;
        target[FArmL]=(-0.7+(-0.45+0.7)*up)-0.4*launch;
        target[UArmR]=(-0.8+(-0.35+0.8)*up)+(-1.9-(-0.8+(-0.35+0.8)*up))*launch-windmill;
        target[UArmR+2]=-((1.15+(0.75-1.15)*up)+(0.3-(1.15+(0.75-1.15)*up))*launch);
        target[FArmR]=-0.6+(-0.4+0.6)*up;
        target[ClavL+2]+=0.1*(1.0-up)+0.06*launch; target[ClavR+2]-=0.1*(1.0-up);
        target[Anc+1]+=0.03*(1.0-aimWeight)*(apex+launch); target[Ancr]-=0.2*(1.0-aimWeight)*launch;
        target[HandPL]=1.4+0.5*(1.0-up)+0.1*fallLong; target[Ears]+=0.7*fallLong-0.4*launch+0.4*apex;
        target[SqY]*=1.0+0.09*launch+0.035*sstep(-4.0,-12.0,vy)-0.02*apex;
        target[SqXZ]*=1.0-0.045*launch;
        target[MOpen]=std::max(target[MOpen],0.25*apex+0.45*fallLong);
        target[MCurve]+=0.25*apex-0.8*fallLong;
        target[Eye]+=0.12*apex+0.15*fallLong; target[BrowY]+=0.3*apex+0.6*fallLong;
        target[LookY]-=0.15*reach;
        for(std::size_t i=0;i<p.size();++i) p[i]+=(target[i]-p[i])*blend;
    }

    double ground(double x, double z, glm::dvec3* normal, const CharacterSceneInput& scene, const glm::dvec3& rootPosition) const {
        if(scene.raycast) {
            const auto hit=scene.raycast({x,rootPosition.y+0.55,z},{0,-1,0},1.25);
            if(hit.hit&&hit.normal.y>0.55&&hit.point.y>rootPosition.y-0.55&&hit.point.y<rootPosition.y+0.52) {
                if(normal) *normal=hit.normal;
                return hit.point.y;
            }
        }
        if(normal) *normal={0,1,0};
        return rootPosition.y;
    }

    double idealFoot(Foot& foot, glm::dvec3& out, const glm::dvec3& rootPosition, double rootYaw) const {
        const std::size_t offset=foot.index==0?0:3;
        const double x=stance[offset]+stepOfsX, z=stance[offset+1]+stepOfsZ;
        const double c=std::cos(rootYaw), sn=std::sin(rootYaw);
        out.x=rootPosition.x+x*c+z*sn;
        out.z=rootPosition.z-x*sn+z*c;
        out.y=rootPosition.y;
        return rootYaw+stance[offset+2];
    }

    double gaitTarget(Foot& foot, double remaining, glm::dvec3& out, const CharacterAnimState& input) const {
        const auto& root=input.position;
        const double stepHalf=(duty/cadence)*0.5;
        const double predictedYaw=yaw+yawRate*std::min(remaining,0.25)*0.8;
        const double hipYaw=predictedYaw+hipTwist;
        const double width=std::abs(stance[foot.index==0?0:3])*(1.0-runWeight)+0.068*runWeight;
        const double c=std::cos(hipYaw), sn=std::sin(hipYaw);
        const double ta=std::min(remaining,0.15), ax=treadmill?0.0:clamp(acceleration.x,-40.0,40.0), az=treadmill?0.0:clamp(acceleration.z,-40.0,40.0);
        const double px=root.x+gvx*remaining+0.5*ax*ta*ta, pz=root.z+gvz*remaining+0.5*az*ta*ta;
        double dx=gvx*stepHalf+width*foot.side*c, dz=gvz*stepHalf-width*foot.side*sn;
        const double length=std::hypot(dx,dz);
        if(length>0.3) { dx*=0.3/length; dz*=0.3/length; }
        out.x=px+dx; out.z=pz+dz;
        if(activeScene) out.y=ground(out.x,out.z,&foot.targetNormal,*activeScene,root);
        else out.y=root.y;
        return hipYaw+foot.side*(0.1*(1.0-runWeight)+0.04*runWeight);
    }

    // The source keeps planted contacts in world space and only turns them into pose targets later.
    const CharacterSceneInput* activeScene = nullptr;
    void footPose(Foot& foot, double rootDt) {
        if(foot.planted||!foot.swinging) {
            foot.contact=foot.plantedPosition; foot.contactYaw=foot.yaw; foot.contactNormal=foot.normal;
            if(moving) {
                const double st=foot.stanceU;
                foot.pitch=-foot.land*(1.0-sstep(0.0,0.28,st))+foot.toe*sstep(0.42,1.0,st);
            } else foot.pitch=damp(foot.pitch,0.0,12.0,1.0/60.0);
            (void)rootDt;
            return;
        }
        const double u=foot.swingU, e=u+(minimumJerk(u)-u)*0.8;
        foot.contact.x=foot.from.x+(foot.to.x-foot.from.x)*e;
        foot.contact.z=foot.from.z+(foot.to.z-foot.from.z)*e;
        const double rise=std::max(0.0,foot.to.y-foot.from.y);
        const double gy=foot.from.y+(foot.to.y-foot.from.y)*sstep(0.15,0.7,u);
        const double peak=0.5+(0.4-0.5)*runWeight;
        const double liftCurve=u<peak?std::sin((u/peak)*3.14159265358979323846*0.5):std::cos(((u-peak)/(1.0-peak))*3.14159265358979323846*0.5);
        foot.contact.y=gy+(foot.lift+rise*0.6)*std::pow(std::max(0.0,liftCurve),1.15);
        foot.contactYaw=foot.fromYaw+wrapAngle(foot.toYaw-foot.fromYaw)*e;
        foot.contactNormal=glm::normalize(foot.normal+(foot.targetNormal-foot.normal)*e);
        foot.pitch=foot.toe*(1.0-sstep(0.0,0.5,u))-foot.land*sstep(0.55,0.96,u)+0.12*std::sin(3.14159265358979323846*u)*runWeight;
    }

    void liftOff(Foot& foot, FootMode mode) {
        foot.planted=false; foot.swinging=true; foot.mode=mode; foot.swingU=0.0;
        foot.from=foot.plantedPosition; foot.fromYaw=foot.yaw; foot.to=foot.plantedPosition; foot.toYaw=foot.yaw;
        const double forward=clamp(mdz*std::cos(hipTwist)+mdx*std::sin(hipTwist),-1.0,1.0);
        foot.lift=lift*(mode==Catch?0.7:1.0);
        foot.toe=(0.3*(1.0-runWeight)+0.95*runWeight)*(forward>=0?forward:forward*0.55);
        foot.land=(0.3*(1.0-runWeight)+0.1*runWeight)*(forward>=0?forward:forward*0.6);
        foot.duration=mode==Catch?0.13:(1.0-duty)/cadence;
    }

    void touchDown(Foot& foot,double loud,bool audible) {
        foot.plantedPosition=foot.to; foot.normal=foot.targetNormal; foot.yaw=foot.toYaw;
        foot.planted=true; foot.swinging=false; foot.stanceU=0.0; foot.swingU=0.0; foot.stanceTime=0.0;
        if(moving&&foot.mode==Catch) {
            phase=-foot.index*0.5; foot.inStance=true;
            auto& other=feet[static_cast<std::size_t>(1-foot.index)]; other.inStance=0.5<duty;
        }
        settleCooldown=0.045;
        if(audible&&kidScale>0.5&&activeScene&&activeScene->footstep)
            activeScene->footstep(foot.index,foot.plantedPosition,loud*std::max(gv,0.6));
    }

    double footError(Foot& foot, const glm::dvec3& rootPosition, double rootYaw) {
        glm::dvec3 ideal;
        const double idealYaw=idealFoot(foot,ideal,rootPosition,rootYaw);
        return std::hypot(foot.plantedPosition.x-ideal.x,foot.plantedPosition.z-ideal.z)+0.11*std::abs(wrapAngle(foot.yaw-idealYaw));
    }

    void startSettle(Foot& foot, double error, const glm::dvec3& rootPosition, double rootYaw, const CharacterSceneInput& scene) {
        foot.planted=false; foot.swinging=true; foot.mode=Settle; foot.swingU=0.0;
        foot.from=foot.plantedPosition; foot.fromYaw=foot.yaw;
        foot.toYaw=idealFoot(foot,foot.to,rootPosition,rootYaw);
        foot.to.y=ground(foot.to.x,foot.to.z,&foot.targetNormal,scene,rootPosition);
        foot.duration=clamp(0.15+error*0.42,0.15,0.3);
        foot.lift=clamp(0.028+error*0.22,0.03,0.085)+std::max(0.0,foot.to.y-foot.from.y);
        foot.toe=0.25; foot.land=0.12;
    }

    void updateFeet(double dt, const CharacterAnimState& input, const CharacterSceneInput& scene) {
        activeScene=&scene;
        const auto& root=input.position;
        if(treadmill) for(auto& foot:feet) {
            foot.plantedPosition.x-=tvx*dt; foot.plantedPosition.z-=tvz*dt;
            foot.from.x-=tvx*dt; foot.from.z-=tvz*dt;
            foot.displacement.x-=tvx*dt; foot.displacement.z-=tvz*dt;
        }
        const bool plantOk=input.form==CharacterForm::Kid&&input.grounded&&dance.empty()&&timers[TLeap]>1.9&&timers[TSlam]>1.4&&timers[TDodge]>dodgeDuration*0.86;
        if(!plantOk) {
            moving=false; replant=true;
            for(auto& foot:feet) foot.swinging=false;
            plantWeight=damp(plantWeight,0.0,30.0,dt);
            footPose(feet[0],dt); footPose(feet[1],dt);
            activeScene=nullptr;
            return;
        }
        plantWeight=damp(plantWeight,1.0,14.0,dt);
        if(replant||!feetValid) {
            for(auto& foot:feet) {
                double footYaw;
                if(feetValid&&foot.displacementValid) { foot.plantedPosition=foot.displacement; footYaw=foot.displacementYaw; }
                else footYaw=idealFoot(foot,foot.plantedPosition,root,yaw);
                foot.plantedPosition.y=ground(foot.plantedPosition.x,foot.plantedPosition.z,&foot.normal,scene,root);
                foot.yaw=footYaw; foot.planted=true; foot.swinging=false; foot.swingU=0.0; foot.stanceU=0.5; foot.inStance=true;
            }
            if(!feetValid) plantWeight=1.0;
            replant=false; feetValid=true; settleCooldown=0.06; moving=false;
        }
        const bool was=moving;
        moving=was?gv>0.3:gv>0.62;
        if(moving) {
            if(!was) {
                const double dx=gvx/(gv>0?gv:1.0), dz=gvz/(gv>0?gv:1.0);
                const double bL=(feet[0].plantedPosition.x-root.x)*dx+(feet[0].plantedPosition.z-root.z)*dz;
                const double bR=(feet[1].plantedPosition.x-root.x)*dx+(feet[1].plantedPosition.z-root.z)*dz;
                const int first=bL<=bR?0:1;
                phase=duty+0.001-first*0.5;
                auto& f=feet[static_cast<std::size_t>(first)];
                if(!f.swinging) { liftOff(f,Catch); f.duration=0.15; f.lift*=0.8; }
                f.inStance=false; feet[static_cast<std::size_t>(1-first)].inStance=fract(phase+(1-first)*0.5)<duty;
            }
            phase+=cadence*dt;
            for(int i=0;i<2;++i) {
                auto& f=feet[static_cast<std::size_t>(i)]; auto& other=feet[static_cast<std::size_t>(1-i)];
                const double p=fract(phase+i*0.5); const bool inStance=p<duty; const bool wasStance=f.inStance; f.inStance=inStance;
                if(f.planted) {
                    f.stanceU=inStance?p/duty:1.0;
                    const double hx=f.plantedPosition.x-root.x, hz=f.plantedPosition.z-root.z;
                    const bool far=hx*hx+hz*hz>0.37*0.37;
                    if(wasStance&&!inStance) liftOff(f,Gait);
                    else if(!inStance&&other.planted&&f.stanceTime>0.06) { liftOff(f,Catch); f.duration=clamp((1.0-p)/cadence,0.09,0.24); }
                    else if(far) liftOff(f,Catch);
                    f.stanceTime+=dt;
                }
                if(f.swinging) {
                    if(f.mode==Gait) {
                        const double u=inStance?1.0:(p-duty)/(1.0-duty);
                        f.swingU=std::max(f.swingU,std::min(1.0,u));
                        const double remain=inStance?0.0:(1.0-p)/cadence;
                        glm::dvec3 target;
                        f.toYaw=gaitTarget(f,remain,target,input);
                        const double k=f.swingU>0.82?1.0:1.0-std::exp(-dt*28.0);
                        f.to=glm::mix(f.to,target,k);
                        if(f.swingU>=1.0) touchDown(f,1.0,input.visible&&input.form==CharacterForm::Kid);
                    } else {
                        f.swingU=std::min(1.0,f.swingU+dt/f.duration);
                        f.toYaw=gaitTarget(f,(1.0-f.swingU)*f.duration,f.to,input);
                        if(f.swingU>=1.0) touchDown(f,0.8,input.visible&&input.form==CharacterForm::Kid);
                    }
                }
            }
        } else {
            int swinging=0;
            for(auto& f:feet) {
                if(!f.swinging) continue;
                if(f.mode!=Settle) {
                    const double remain=clamp((1.0-f.swingU)*(f.mode==Gait?(1.0-duty)/std::max(cadence,1.5):f.duration),0.09,0.22);
                    f.mode=Settle; f.duration=remain/std::max(0.05,1.0-f.swingU); f.lift=std::max(f.lift*0.8,0.04);
                }
                f.swingU=std::min(1.0,f.swingU+dt/f.duration);
                f.toYaw=idealFoot(f,f.to,root,yaw); f.to.y=ground(f.to.x,f.to.z,&f.targetNormal,scene,root);
                if(f.swingU>=1.0) touchDown(f,0.45,input.visible&&input.form==CharacterForm::Kid); else ++swinging;
            }
            settleCooldown-=dt;
            if(swinging==0&&settleCooldown<=0.0) {
                const double eL=footError(feet[0],root,yaw),eR=footError(feet[1],root,yaw);
                const int selected=eL>=eR?0:1; const double error=std::max(eL,eR);
                if(error>0.068) startSettle(feet[static_cast<std::size_t>(selected)],error,root,yaw,scene);
            }
        }
        footPose(feet[0],dt); footPose(feet[1],dt);
        const double twistL=wrapAngle(feet[0].contactYaw-(yaw+stance[2]));
        const double twistR=wrapAngle(feet[1].contactYaw-(yaw+stance[5]));
        footTwist=damp(footTwist,clamp((twistL+twistR)*0.5,-1.2,1.2),20.0,dt);
        activeScene=nullptr;
    }

    void updateMenuAttention(double dt,const CharacterAnimState& input,const CharacterSceneInput& scene) {
        attentionTimer-=dt;
        if(attentionTimer<=0.0) {
            const double pick=random();
            double viewer=dance=="defeat"?0.15:dance=="victory"?0.85:0.6;
            if(!scene.cameraPosition) viewer=0.0;
            const CharacterAttentionActor* neighbour=nullptr;
            double neighbourDistance=16.0;
            for(const auto& actor:scene.actors) {
                if(!actor.alive||actor.team!=scene.team) continue;
                const auto offset=actor.position-input.position;
                const double distance=glm::dot(offset,offset);
                if(distance<neighbourDistance&&distance>1e-4) { neighbour=&actor;neighbourDistance=distance; }
            }
            if(pick<viewer) {
                attentionKind=1;
                attentionTimer=2.2+random()*3.2;
            } else if(neighbour&&pick<viewer+0.24) {
                attentionKind=2;
                attentionTimer=0.9+random()*1.7;
            } else {
                attentionKind=3;
                attentionGlanceYaw=(random()-0.5)*1.1;
                attentionGlancePitch=dance=="defeat"?-0.35-random()*0.2:-0.04-random()*0.3;
                attentionTimer=0.6+random()*1.1;
            }
        }
        attentionOn=true;
        const auto inverseYaw=glm::angleAxis(-input.yaw,glm::dvec3(0,1,0));
        if(attentionKind==1&&scene.cameraPosition) {
            attentionPoint=inverseYaw*(*scene.cameraPosition-input.position);
        } else if(attentionKind==2) {
            const CharacterAttentionActor* neighbour=nullptr;
            double neighbourDistance=16.0;
            for(const auto& actor:scene.actors) {
                if(!actor.alive||actor.team!=scene.team) continue;
                const auto offset=actor.position-input.position;
                const double distance=glm::dot(offset,offset);
                if(distance<neighbourDistance&&distance>1e-4) { neighbour=&actor;neighbourDistance=distance; }
            }
            if(neighbour) attentionPoint=inverseYaw*(neighbour->position+glm::dvec3(0.0,1.15,0.0)-input.position);
            else attentionOn=false;
        } else if(attentionKind==3) {
            const double cp=std::cos(attentionGlancePitch);
            attentionPoint={std::sin(attentionGlanceYaw)*cp*4.0,
                1.15+std::sin(attentionGlancePitch)*4.0,std::cos(attentionGlanceYaw)*cp*4.0};
        } else attentionOn=false;
    }

    void poseLook(double dt, const CharacterAnimState& input, const CharacterSceneInput& scene) {
        double ty=0.0,tp=0.0;
        if(scene.inWorld) {
            lookTime-=dt;
            if(lookTime<=0.0) {
                lookTime=0.9+random()*2.2;
                if(scene.actors.empty()) {
                    glanceYaw=(random()-0.5)*(idleTime>1.0?1.1:0.4);
                    glancePitch=(random()-0.5)*0.25;
                }
            }
            const bool aiming=aimWeight>0.5;
            if(aiming) { ty=0.0; tp=input.aimPitch*0.85; }
            else {
                ty=glanceYaw*(1.0-gaitWeight*0.7);
                tp=glancePitch;
                const double travel=std::atan2(mdx,std::max(mdz,0.2))*0.5;
                ty=ty+(clamp(travel,-0.6,0.6)-ty)*(gaitWeight*0.6);
                ty+=clamp(yawRate*0.1,-0.35,0.35)*gaitWeight;
                tp=tp+(input.aimPitch*0.6-tp)*0.5;
            }
            ty=clamp(ty,-1.1,1.1); tp=clamp(tp,-0.6,0.55);
            attentionOn=true;
            const double cp=std::cos(tp);
            attentionPoint={std::sin(ty)*cp*8.0,1.2+std::sin(tp)*8.0,std::cos(ty)*cp*8.0};
        } else {
            updateMenuAttention(dt,input,scene);
        }
        const double hitLook=timers[THit]<1.1?
            win(timers[THit],0.02,0.08,0.55,1.1)*clamp(hitAmplitude,0.5,1.0):0.0;
        if(hitLook>0.01) {
            const double hitYaw=clamp(std::atan2(hitX,hitZ),-1.3,1.3);
            ty+=(hitYaw-ty)*(0.65*hitLook);
            tp+=(0.05-tp)*hitLook;
            ty=clamp(ty,-1.1,1.1); tp=clamp(tp,-0.6,0.55);
            const double cp=std::cos(tp);
            attentionPoint={std::sin(ty)*cp*8.0,1.2+std::sin(tp)*8.0,std::cos(ty)*cp*8.0};
            attentionOn=true;
        }
        const double hy=spring(springs,SHly,clamp(ty,-0.85,0.85),2.4,0.62,dt);
        const double hpp=spring(springs,SHlp,clamp(tp,-0.5,0.45),2.6,0.62,dt);
        p[Hly]+=hy; p[Hlp]+=hpp;
        p[Neck+1]+=hy*0.3; p[Neck]-=hpp*0.2;
        p[LookX]+=clamp((ty-hy)*0.9,-0.36,0.36);
        p[LookY]+=clamp((tp-hpp)*0.8+0.02,-0.3,0.3);
        const double hp=spring(springs,SHeadP,0.0,3.2,0.4,dt);
        const double hr=spring(springs,SHeadR,0.0,3.2,0.4,dt);
        p[Head]+=hp*0.5; p[Head+2]+=hr*0.4; p[Hlp]-=hp*0.4;
        p[Head+2]-=clamp(yawRate*gs*0.006,-0.12,0.12);

        // The face solver transforms this root-space point into head-space after bone evaluation,
        // then advances the same saccade and fixation-micro-saccade state as the source.
    }

    void poseWeapon(const CharacterAnimState& input,double dt) {
        const auto& hold=GetCharacterWeaponHold(weapon);
        const auto& rigData=GetCharacterWeaponRig(weapon);
        const bool dual=rigData.dual;
        const double armRight=clamp(springs[SArmR],-0.45,0.45);
        const double one=1.0-hold.twoCarry;
        const double swA=dual?0.0:armRight*one;
        const auto& carry=hold.carry;
        p[Anc]=carry.p.x;
        p[Anc+1]=carry.p.y+0.012*std::sin(tau*2.0*phase)*gaitWeight;
        p[Anc+2]=carry.p.z-swA*0.1;
        p[Ancr]=carry.r.x+swA*0.55; p[Ancr+1]=carry.r.y; p[Ancr+2]=carry.r.z;
        const double run=gaitWeight*runWeight;
        if(run>0.001) {
            if(hold.run) {
                const auto& target=*hold.run;
                p[Anc]+=(target.p.x-p[Anc])*run;
                p[Anc+1]+=(target.p.y+0.012*std::sin(tau*2.0*phase)-p[Anc+1])*run;
                p[Anc+2]+=(target.p.z-swA*0.08-p[Anc+2])*run;
                p[Ancr]+=(target.r.x+swA*0.4-p[Ancr])*run;
                p[Ancr+1]+=(target.r.y-p[Ancr+1])*run;
                p[Ancr+2]+=(target.r.z-p[Ancr+2])*run;
            } else {
                p[Anc+1]+=0.025*run; p[Anc+2]+=0.04*run; p[Ancr]-=0.18*run;
            }
        }

        const double aimPitch=clamp(input.aimPitch,-1.0,1.15);
        const double aimPose=clamp(aimPitch,-0.8,1.0);
        const double wAim=aimWeight, wRoll=rollWeight, gw=gaitWeight;
        if(wAim>0.001) {
            const auto& aim=hold.aim;
            const double lk=hold.lock?lockWeight:0.0;
            glm::dvec3 offset=aim.p;
            if(hold.lock) {
                offset=glm::mix(offset,hold.lock->p,lk);
            }
            const auto pitched=glm::angleAxis(-aimPose,glm::dvec3(1,0,0))*offset;
            const auto anchor=pitched+CharacterAimPivot;
            p[Anc]+=(anchor.x-p[Anc])*wAim;
            p[Anc+1]+=(anchor.y-p[Anc+1])*wAim;
            p[Anc+2]+=(anchor.z-p[Anc+2])*wAim;
            const glm::dvec3 anchorRotation{aim.r.x-aimPitch,
                hold.lock?aim.r.y+(hold.lock->r.y-aim.r.y)*lk:aim.r.y,
                hold.lock?aim.r.z+(hold.lock->r.z-aim.r.z)*lk:aim.r.z};
            p[Ancr]+=(anchorRotation.x-p[Ancr])*wAim;
            p[Ancr+1]+=(anchorRotation.y-p[Ancr+1])*wAim;
            p[Ancr+2]+=(anchorRotation.z-p[Ancr+2])*wAim;
            p[Poler]+=(hold.poleR.x-p[Poler])*wAim;
            p[Poler+1]+=(hold.poleR.y-p[Poler+1])*wAim;
            p[Poler+2]+=(hold.poleR.z-p[Poler+2])*wAim;
            p[Polel]+=(hold.poleL.x-p[Polel])*wAim;
            p[Polel+1]+=(hold.poleL.y-p[Polel+1])*wAim;
            p[Polel+2]+=(hold.poleL.z-p[Polel+2])*wAim;
            p[Afolr]+=(0.0-p[Afolr])*wAim;
            const double stanceWeight=wAim*(1.0-0.6*gw);
            p[Hips+1]+=hold.hip*stanceWeight;
            p[Spine+1]-=hold.hip*0.35*stanceWeight;
            p[Chest+1]+=(hold.chest-hold.hip*0.65)*stanceWeight;
            p[Spine]-=aimPose*0.1*wAim;
            p[Chest]-=aimPose*0.18*wAim;
            p[HipsP+1]-=hold.crouch*stanceWeight;
            p[Neck]-=aimPitch*0.08*wAim;
            if(hold.animKind=="charger") {
                const double ch=charge;
                const double breath=(1.0-ch)*0.012*wAim;
                p[Anc+1]+=std::sin(tau*breathPhase)*breath;
                p[Anc]+=std::sin(tau*breathPhase*0.5+1.0)*breath*0.6;
                const double tremble=ch>=0.99?0.0025*std::sin(time*71.0)+0.0018*std::sin(time*53.0+1.0):0.0;
                p[Anc+1]+=tremble; p[Ancr]+=tremble*3.0;
                p[Head+2]-=0.12*ch*wAim; p[Neck+2]-=0.05*ch*wAim; p[Hly]-=0.05*ch*wAim;
                p[HipsP+1]-=0.02*ch*wAim; p[Chest]+=0.04*ch*wAim;
            }
        }
        if(wRoll>0.001&&hold.roll) {
            const auto& rollAnchor=*hold.roll;
            p[Anc]+=(rollAnchor.p.x-p[Anc])*wRoll;
            p[Anc+1]+=(rollAnchor.p.y-p[Anc+1])*wRoll;
            p[Anc+2]+=(rollAnchor.p.z-p[Anc+2])*wRoll;
            p[Ancr]+=(rollAnchor.r.x-p[Ancr])*wRoll;
            p[Ancr+1]+=(rollAnchor.r.y-p[Ancr+1])*wRoll;
            p[Ancr+2]+=(rollAnchor.r.z-p[Ancr+2])*wRoll;
            p[Afolt]+=(0.45-p[Afolt])*wRoll; p[Afolr]+=(0.15-p[Afolr])*wRoll;
            p[Spine]+=0.17*wRoll; p[Chest]+=0.1*wRoll; p[Hips]+=0.08*wRoll;
            p[HipsP+1]-=hold.crouch*wRoll; p[HipsP+2]-=0.02*wRoll; p[Hlp]+=0.05*wRoll;
            p[Poler]+=(-0.7-p[Poler])*wRoll; p[Poler+1]+=(-0.35-p[Poler+1])*wRoll; p[Poler+2]+=(-0.7-p[Poler+2])*wRoll;
            p[Polel]+=(0.7-p[Polel])*wRoll; p[Polel+1]+=(-0.35-p[Polel+1])*wRoll; p[Polel+2]+=(-0.7-p[Polel+2])*wRoll;
            p[Hips+1]+=hold.hip*wRoll*(1.0-gw);
        }

        const double two=dual?0.0:std::max(hold.twoCarry+(hold.twoAim-hold.twoCarry)*wAim,hold.roll?1.0:0.0);
        twoWeight=two;
        p[Ikl]=dual?1.0:two;
        p[ClavL+1]-=0.28*two; p[ClavR+1]+=0.1*two; p[ClavL+2]+=0.04*two;
        const double reachError=ikErrorPre*(two>0.5?1.0:0.0);
        leftReach=clamp(leftReach+(reachError>0.004?reachError*30.0:-0.5)*dt,0.0,0.55);
        p[ClavL+1]-=leftReach; p[Chest+1]-=leftReach*0.35; p[ClavL]-=leftReach*0.3;
        if(two>0.0) p[UArmL]*=1.0-two;
        // Sustained-fire posture and release follow-through, matching Character._poseWeapon.
        const double brace=hold.rc.brace.value_or(0.0);
        const bool wantFire=brace>0.0&&input.firing&&lastShot<0.22&&hold.animKind!="roller";
        if(!wantFire&&fireWant) springs[SRCp+1]-=1.1*brace*fireHold;
        fireWant=wantFire;
        fireHold=damp(fireHold,wantFire?1.0:0.0,wantFire?9.0:4.0,dt);
        const double fireWeight=fireHold*wAim*brace;
        if(fireWeight>0.001) {
            p[Chest]+=0.055*fireWeight; p[Spine]+=0.02*fireWeight;
            p[HipsP+1]-=0.012*fireWeight; p[Neck]+=0.02*fireWeight;
            p[Ancr]+=(0.012*std::sin(time*4.1+1.0)+0.006*std::sin(time*9.7))*fireWeight;
            p[Ancr+1]+=0.01*std::sin(time*3.3)*fireWeight;
            p[Anc+2]-=0.012*fireWeight;
        }
        const double pump=weaponAnimationRight.pump;
        if(pump>0.0) {
            p[Ancr]+=0.06*pump; p[Chest+1]+=0.05*pump*wAim; p[Spine+1]+=0.02*pump*wAim; p[Anc+2]-=0.01*pump;
        }
        if(dual) {
            const double swL=clamp(springs[SArmL],-0.45,0.45);
            p[Anl]=-p[Anc]; p[Anl+1]=p[Anc+1]; p[Anl+2]=p[Anc+2];
            p[Anlr]=p[Ancr]; p[Anlr+1]=-p[Ancr+1]; p[Anlr+2]=-p[Ancr+2];
            const double carryWeight=1.0-wAim;
            p[Anc+2]-=armRight*0.1*carryWeight; p[Ancr]+=armRight*0.55*carryWeight;
            p[Anl+2]-=swL*0.1*carryWeight; p[Anlr]+=swL*0.55*carryWeight;
        }

        if(weapon=="bow") {
            const double bowCharge=clamp(input.runner?input.runner->charge:charge,0.0,1.0);
            if(lastRelease<bowLastRelease) bowHold=bowDraw;
            bowLastRelease=lastRelease;
            bowDraw=1.0-(1.0-bowCharge)*(1.0-bowCharge);
            if(lastRelease<0.5&&bowCharge<0.05)
                bowDraw=std::max(bowDraw,bowHold*(1.0-ease((lastRelease-0.1)/0.3)));
            const auto bowRotation=euler({p[Ancr],p[Ancr+1],p[Ancr+2]},true);
            const auto bowPull=bowRotation*glm::dvec3(0.0,0.0,-0.22*bowDraw);
            p[Anc]+=bowPull.x; p[Anc+1]+=bowPull.y; p[Anc+2]+=bowPull.z;
            bowCant=damp(bowCant,input.grounded?-0.85:0.0,12.0,dt);
            p[Ancr+2]+=bowCant*aimWeight;
        }

        const auto& recoil=hold.rc;
        const double rp=springAnalytic(springs,SRCp,0.0,recoil.hz,recoil.z,dt);
        const double rz=springAnalytic(springs,SRCz,0.0,recoil.hz,recoil.z,dt);
        springAnalytic(springs,SRCy,0.0,recoil.hz*1.3,0.5,dt);
        springAnalytic(springs,SRCr,0.0,recoil.hz*1.3,0.5,dt);
        p[Chest]-=rp*recoil.torso; p[Spine]-=rp*recoil.torso*0.4; p[Head]-=rp*recoil.head;
        p[HipsP+2]-=rz*0.35; p[HipsP+1]-=std::abs(rz)*recoil.crouch*8.0;
        p[ClavR+1]-=rz*1.5;
        recoilPitch=rp; recoilBack=rz;
        if(dual) {
            const double rp2=springAnalytic(springs,SRCp2,0.0,recoil.hz,recoil.z,dt);
            const double rz2=springAnalytic(springs,SRCz2,0.0,recoil.hz,recoil.z,dt);
            springAnalytic(springs,SRCy2,0.0,recoil.hz*1.3,0.5,dt);
            p[Chest]-=rp2*recoil.torso; p[Spine]-=rp2*recoil.torso*0.4; p[Head]-=rp2*recoil.head;
            p[HipsP+2]-=rz2*0.35; p[ClavL+1]+=rz2*1.5; p[Chest+1]+=(rz2-rz)*0.6;
            recoilPitchLeft=rp2; recoilBackLeft=rz2;
        }
    }

    void poseBlade(double dt,const CharacterAnimState& input) {
        if(weapon!="blade") return;
        bladeTime+=dt;
        const double bt=bladeTime, sd=bladeSide, ch=clamp(input.charge,0.0,1.0);
        const bool up=ch>0.001&&input.form==CharacterForm::Kid&&dance.empty();
        bladeRaise=damp(bladeRaise,up?1.0:0.0,up?18.0:9.0,dt);
        const double rw=bladeRaise*(bladeHeavy&&bt<0.45?0.0:1.0);
        const auto ease=[](double value) { const double t=clamp(value,0.0,1.0); return t*t*(3.0-2.0*t); };
        const auto easeOut=[](double value) { const double t=clamp(value,0.0,1.0); return 1.0-(1.0-t)*(1.0-t); };
        const auto mix=[](double a,double b,double weight) { return a+(b-a)*weight; };
        const double cutW=bladeHeavy?(bt<0.6?1.0-ease((bt-0.26)/0.3):0.0)
            :(bt<0.4?ease(bt/0.03)*(1.0-ease((bt-0.17)/0.2)):0.0);
        if(rw<0.001&&cutW<0.001) return;
        auto& x=p;
        if(rw>0.001) {
            const double k=ease(ch), full=ch>=0.999?1.0:0.0;
            const double trem=full?0.004*std::sin(time*63.0)+0.003*std::sin(time*41.0+1.0):0.0;
            x[Anc]=mix(x[Anc],-0.2,rw); x[Anc+1]=mix(x[Anc+1],mix(1.16,1.3,k)+trem,rw); x[Anc+2]=mix(x[Anc+2],mix(0.05,-0.03,k),rw);
            x[Ancr]=mix(x[Ancr],mix(-1.75,-2.35,k)+trem*4.0,rw); x[Ancr+1]=mix(x[Ancr+1],mix(0.12,0.22,k),rw); x[Ancr+2]=mix(x[Ancr+2],mix(0.05,0.22,k),rw);
            x[Afolt]=mix(x[Afolt],0.55,rw); x[Afolr]=mix(x[Afolr],0.25,rw);
            x[Spine+1]-=0.16*k*rw; x[Chest+1]-=0.18*k*rw; x[Hips+1]-=0.05*rw;
            x[Spine]-=0.05*k*rw; x[Chest]-=0.05*rw; x[HipsP+1]-=0.025*k*rw;
            x[Poler]=mix(x[Poler],-0.95,rw); x[Poler+1]=mix(x[Poler+1],0.15,rw); x[Poler+2]=mix(x[Poler+2],-0.35,rw);
            effort=std::max(effort,(0.3+0.6*k)*rw);
        }
        if(cutW>0.001) {
            double ax,ay,az,rx,ry,rz;
            if(!bladeHeavy) {
                const double k=easeOut((bt-0.015)/0.095);
                const double f0=sd>0.0?-1.3:1.15, f1=sd>0.0?1.15:-1.3, f=mix(f0,f1,k);
                ax=-0.08+0.34*std::sin(f); az=0.06+0.3*std::cos(f);
                ay=sd>0.0?1.03-0.13*k:0.91+0.11*k;
                ry=f*1.08; rx=sd>0.0?mix(-0.18,0.28,k):mix(0.24,-0.16,k); rz=sd*1.57079632679489661923;
                const double u=mix(-sd,sd,k);
                x[Spine+1]+=0.26*u*cutW; x[Chest+1]+=0.24*u*cutW; x[Hips+1]+=0.1*u*cutW;
                x[Spine]+=0.08*cutW; x[HipsP+1]-=0.02*cutW;
                x[ClavR+1]+=0.12*std::max(0.0,u)*cutW;
                x[Poler]=mix(x[Poler],-0.8,cutW); x[Poler+1]=mix(x[Poler+1],-0.55,cutW); x[Poler+2]=mix(x[Poler+2],-0.1,cutW);
            } else {
                const double k1=ease(bt/0.06), k2=easeOut((bt-0.055)/0.07);
                ax=mix(mix(-0.2,-0.1,k1),-0.07,k2); ay=mix(mix(1.3,1.08,k1),0.62,k2); az=mix(mix(-0.03,0.42,k1),0.47,k2);
                rx=mix(mix(-2.35,-0.15,k1),0.95,k2); ry=mix(mix(0.22,0.08,k1),0.04,k2); rz=mix(0.22,0.0,k1);
                const double lg=ease(bt/0.08)*(1.0-ease((bt-0.3)/0.25));
                x[Spine]+=0.2*lg; x[Chest]+=0.1*lg; x[Hips]+=0.06*lg; x[Neck]-=0.1*lg;
                x[HipsP+1]-=0.045*lg; x[HipsP+2]+=0.04*lg;
                x[Spine+1]+=0.12*lg; x[Chest+1]+=0.08*lg; x[Kneel]+=0.05*lg; x[Kneer]-=0.05*lg;
                x[Poler]=mix(x[Poler],-0.85,cutW); x[Poler+1]=mix(x[Poler+1],-0.45,cutW); x[Poler+2]=mix(x[Poler+2],-0.3,cutW);
            }
            x[Anc]=mix(x[Anc],ax,cutW); x[Anc+1]=mix(x[Anc+1],ay,cutW); x[Anc+2]=mix(x[Anc+2],az,cutW);
            x[Ancr]=mix(x[Ancr],rx,cutW); x[Ancr+1]=mix(x[Ancr+1],ry,cutW); x[Ancr+2]=mix(x[Ancr+2],rz,cutW);
            x[Afolt]=mix(x[Afolt],0.35,cutW); x[Afolr]=mix(x[Afolr],0.12,cutW);
            effort=std::max(effort,bladeHeavy?0.9*cutW:0.55*cutW);
        }
    }

    void poseFlick(double t) {
        const auto ease=[](double value) { const double x=clamp(value,0.0,1.0); return x*x*(3.0-2.0*x); };
        const double kUp=ease(t/0.15), kWhip=ease((t-0.15)/0.08), kFol=easeOut((t-0.23)/0.14), kRec=ease((t-0.42)/0.26);
        double ax=-0.11, ay=0.84, az=0.18, rx=0.8, ry=0.1;
        const auto lerp=[](double a,double b,double weight) { return a+(b-a)*weight; };
        ax=lerp(ax,-0.13,kUp); ay=lerp(ay,1.22,kUp); az=lerp(az,-0.02,kUp); rx=lerp(rx,-2.3,kUp); ry=lerp(ry,0.3,kUp);
        ax=lerp(ax,-0.07,kWhip); ay=lerp(ay,0.98,kWhip); az=lerp(az,0.3,kWhip); rx=lerp(rx,-0.1,kWhip); ry=lerp(ry,0.05,kWhip);
        ax=lerp(ax,-0.06,kFol); ay=lerp(ay,0.74,kFol); az=lerp(az,0.3,kFol); rx=lerp(rx,0.75,kFol);
        const double weight=1.0-kRec;
        const auto lerpE=[&](std::size_t i,double x,double y,double z,double w) {
            p[i]+=(x-p[i])*w; p[i+1]+=(y-p[i+1])*w; p[i+2]+=(z-p[i+2])*w;
        };
        lerpE(Anc,ax,ay,az,weight); lerpE(Ancr,rx,ry,0.0,weight);
        p[Afolt]+=(0.6-p[Afolt])*weight; p[Afolr]+=(0.25-p[Afolr])*weight;
        const double coil=kUp*(1.0-kWhip), whip=kWhip*(1.0-kRec);
        p[Spine]+=-0.22*coil+0.34*whip; p[Chest]+=-0.14*coil+0.2*whip;
        p[Spine+1]+=0.2*coil-0.12*whip; p[Chest+1]+=0.18*coil-0.1*whip;
        p[HipsP+1]-=0.03*coil+0.05*whip; p[HipsP+2]+=-0.02*coil+0.03*whip;
        p[Hlp]+=0.06*coil-0.1*whip;
        lerpE(Poler,-0.8,0.1,-0.3,coil); lerpE(Polel,0.8,0.1,-0.3,coil);
        effort=std::max(effort,coil+whip*0.7);
    }

    void poseBucket(double t) {
        const auto ease=[](double value) { const double x=clamp(value,0.0,1.0); return x*x*(3.0-2.0*x); };
        const double kBack=ease(t/0.11), kFwd=ease((t-0.09)/0.1), kFol=easeOut((t-0.19)/0.12), kRec=ease((t-0.34)/0.24);
        double ax=-0.1, ay=0.7, az=0.24, rx=0.25;
        const auto lerp=[](double a,double b,double weight) { return a+(b-a)*weight; };
        ax=lerp(ax,-0.2,kBack); ay=lerp(ay,0.48,kBack); az=lerp(az,-0.1,kBack); rx=lerp(rx,0.95,kBack);
        ax=lerp(ax,-0.08,kFwd); ay=lerp(ay,0.9,kFwd); az=lerp(az,0.42,kFwd); rx=lerp(rx,-0.5,kFwd);
        ay=lerp(ay,0.98,kFol); az=lerp(az,0.46,kFol); rx=lerp(rx,-0.85,kFol);
        const double weight=1.0-kRec;
        const auto lerpE=[&](std::size_t i,double x,double y,double z,double w) {
            p[i]+=(x-p[i])*w; p[i+1]+=(y-p[i+1])*w; p[i+2]+=(z-p[i+2])*w;
        };
        lerpE(Anc,ax,ay,az,weight); lerpE(Ancr,rx,0.05,0.0,weight);
        const double back=kBack*(1.0-kFwd), heave=kFwd*(1.0-kRec);
        p[Spine]+=0.3*back-0.12*heave; p[Chest]+=0.14*back-0.1*heave;
        p[Spine+1]+=0.16*back-0.06*heave; p[Chest+1]+=0.12*back;
        p[HipsP+1]-=0.07*back-0.02*heave; p[HipsP+2]+=-0.04*back+0.05*heave;
        p[Hlp]+=0.08*back-0.06*heave;
        lerpE(Poler,-0.8,-0.7,0.1,back); lerpE(Polel,0.8,-0.7,0.1,back);
        effort=std::max(effort,back*0.6+heave);
    }

    void poseSlosh(double t,double dt) {
        static constexpr double times[]={0.0,0.13,0.25,0.4,0.62};
        static constexpr double x[]={0.0,-0.08,0.02,0.025,0.0};
        static constexpr double y[]={0.0,-0.22,0.24,0.33,0.0};
        static constexpr double z[]={0.0,-0.34,0.04,-0.04,0.0};
        static constexpr double pitch[]={0.0,-0.55,1.5,1.95,0.0};
        static constexpr double yaw[]={0.0,0.32,-0.08,-0.14,0.0};
        const auto win=[](double value,double a,double b,double c,double d) {
            return sstep(a,b,value)*(1.0-sstep(c,d,value));
        };
        const double wu=win(t,0.0,0.1,0.12,0.2), hv=win(t,0.12,0.2,0.3,0.46);
        std::array<double,CharacterPoseChannelCount> target=p;
        target[Anc]+=keyedCatmull(t,times,x,std::size(times));
        target[Anc+1]+=keyedCatmull(t,times,y,std::size(times));
        target[Anc+2]+=keyedCatmull(t,times,z,std::size(times));
        target[Ancr]+=keyedCatmull(t,times,pitch,std::size(times));
        target[Ancr+1]+=keyedCatmull(t,times,yaw,std::size(times));
        target[Afolr]=std::max(target[Afolr],0.35);
        target[HipsP+1]+=-0.045*wu+0.022*hv; target[HipsP+2]+=-0.02*wu+0.025*hv;
        target[Spine]+=0.14*wu-0.13*hv; target[Chest]+=0.06*wu-0.1*hv; target[Hips]+=0.06*wu;
        target[Spine+1]+=0.12*wu-0.1*hv; target[Chest+1]+=0.22*wu-0.18*hv;
        target[Hlp]+=-0.08*wu+0.1*hv; target[Tiptoe]=std::max(target[Tiptoe],0.85*hv);
        target[Kneel]+=0.1*wu; target[Kneer]-=0.1*wu;
        const double rel=win(t,0.06,0.13,0.36,0.56);
        target[Ikl]*=1.0-rel;
        target[UArmL]+=(0.15+0.75*hv-0.2*wu-target[UArmL])*rel;
        target[UArmL+2]+=(0.25+0.2*hv-target[UArmL+2])*rel;
        target[FArmL]+=(-0.5-0.3*hv-target[FArmL])*rel;
        target[HandPL]+=(1.6-target[HandPL])*rel;
        target[SqY]*=1.0-0.03*wu+0.045*hv; target[SqXZ]*=1.0+0.015*wu-0.02*hv;
        target[Ears]+=0.5*hv-0.3*wu;
        effort=std::max(effort,0.6*wu+hv);
        const double blend=win(t,0.0,0.015,0.5,0.66);
        for(std::size_t i=0;i<p.size();++i) p[i]+=(target[i]-p[i])*blend;
        if(t>=0.12&&t-dt<0.12) {
            springs[STankL+1]+=2.2;
            hairKick(0.0,2.4,1.6);
            springs[SPelY+1]+=0.35;
        }
    }

    void poseSwipe(double t) {
        const auto ease=[](double value) { const double x=clamp(value,0.0,1.0); return x*x*(3.0-2.0*x); };
        const double k=ease(t/0.13), kRec=ease((t-0.15)/0.2), across=-swipeSide+2.0*swipeSide*k;
        const double weight=1.0-kRec;
        const auto lerpE=[&](std::size_t i,double x,double y,double z,double w) {
            p[i]+=(x-p[i])*w; p[i+1]+=(y-p[i+1])*w; p[i+2]+=(z-p[i+2])*w;
        };
        lerpE(Anc,-0.06+0.3*across,0.78,0.28,weight);
        lerpE(Ancr,0.9,0.95*across,-0.2*across,weight);
        p[Spine+1]+=0.3*across*weight; p[Chest+1]+=0.24*across*weight; p[Hips+1]+=0.1*across*weight;
        p[Spine]+=0.14*weight; p[Chest]+=0.06*weight; p[HipsP+1]-=0.03*weight;
        lerpE(Poler,-0.7,-0.35,-0.7,weight); lerpE(Polel,0.7,-0.35,-0.7,weight);
        effort=std::max(effort,0.5*weight);
    }

    void poseSubAim(double weight) {
        std::array<double,CharacterPoseChannelCount> target=p;
        target[Ikl]=0.0; target[Ltw]=0.0;
        target[UArmL]=-2.55; target[UArmL+2]=0.48; target[FArmL]=-1.85; target[HandL]=0.2;
        target[ClavL+2]+=0.15; target[ClavL+1]-=0.08;
        target[Chest+1]+=0.34; target[Spine+1]+=0.12; target[Spine]-=0.06;
        target[Hly]-=0.25; target[HandPL]=0.0;
        for(std::size_t i=0;i<p.size();++i) p[i]+=(target[i]-p[i])*weight;
        effort=std::max(effort,0.35*weight);
    }

    void poseThrow(double t) {
        const auto ease=[](double value) { const double x=clamp(value,0.0,1.0); return x*x*(3.0-2.0*x); };
        const auto window=[&](double value,double a,double b,double c,double d) {
            if(value<=a||value>=d) return 0.0;
            if(value<b) return ease((value-a)/(b-a));
            if(value>c) return 1.0-ease((value-c)/(d-c));
            return 1.0;
        };
        const double weight=window(t,0.0,0.03,0.4,0.62);
        if(weight<=0.0) return;
        const double cock=1.0-ease(t/0.06), whip=ease(t/0.1)*(1.0-ease((t-0.1)/0.16)), follow=ease((t-0.08)/0.14);
        std::array<double,CharacterPoseChannelCount> target=p;
        target[Ikl]=0.0; target[Ltw]=0.0;
        target[UArmL]=-2.6*cock-1.35*whip-0.55*follow*(1.0-whip);
        target[UArmL+2]=0.45*cock+0.1*whip-0.1*follow;
        target[FArmL]=-1.9*cock-0.3*whip-0.4*follow; target[HandL]=0.4*whip;
        target[ClavL+2]+=0.16*cock; target[ClavL+1]+=-0.1*cock+0.12*whip;
        target[Chest+1]+=0.4*cock-0.42*whip-0.18*follow;
        target[Spine+1]+=0.16*cock-0.18*whip;
        target[Spine]+=-0.1*cock+0.22*whip+0.08*follow; target[Chest]+=0.1*whip;
        target[HipsP+2]+=0.03*whip; target[HipsP+1]-=0.025*whip;
        target[HandPL]=1.95+(1.05-1.95)*ease((t-0.12)/0.35);
        for(std::size_t i=0;i<p.size();++i) p[i]+=(target[i]-p[i])*weight;
        effort=std::max(effort,whip);
    }

    void poseLeap(double t) {
        const auto ease=[](double value) { const double x=clamp(value,0.0,1.0); return x*x*(3.0-2.0*x); };
        const auto win=[&](double value,double a,double b,double c,double d) {
            if(value<=a||value>=d) return 0.0;
            if(value<b) return ease((value-a)/(b-a));
            if(value>c) return 1.0-ease((value-c)/(d-c));
            return 1.0;
        };
        const auto lerpE=[&](std::array<double,CharacterPoseChannelCount>& values,std::size_t i,
            double x,double y,double z,double weight) {
            values[i]+=(x-values[i])*weight; values[i+1]+=(y-values[i+1])*weight; values[i+2]+=(z-values[i+2])*weight;
        };
        const double launch=win(t,0.0,0.04,0.12,0.24), tuck=win(t,0.12,0.26,0.42,0.56),
            hang=win(t,0.44,0.58,1.6,1.9);
        std::array<double,CharacterPoseChannelCount> target=p;
        target[Wpl]=0.0; target[Wpr]=0.0;
        lerpE(target,FootL,0.08,0.08,-0.05,launch); lerpE(target,FootLR,1.0,0.1,0.0,launch);
        lerpE(target,FootR,-0.08,0.1,-0.1,launch); lerpE(target,FootRR,1.1,-0.1,0.0,launch);
        lerpE(target,FootL,0.1,0.4,0.14,tuck); lerpE(target,FootLR,0.7,0.1,0.0,tuck);
        lerpE(target,FootR,-0.1,0.36,0.1,tuck); lerpE(target,FootRR,0.8,-0.1,0.0,tuck);
        lerpE(target,FootL,0.12,0.3,0.1,hang); lerpE(target,FootLR,0.4,0.15,0.0,hang);
        lerpE(target,FootR,-0.12,0.2,-0.12,hang); lerpE(target,FootRR,0.9,-0.15,0.0,hang);
        target[HipsP+1]=-0.03*tuck;
        target[Spine]+=-0.14*launch+0.5*tuck-0.22*hang; target[Chest]+=-0.08*launch+0.25*tuck-0.16*hang;
        target[Hlp]+=0.2*launch-0.1*tuck-0.35*hang; target[Neck]+=0.12*hang;
        target[SqY]*=1.0+0.12*launch; target[SqXZ]*=1.0-0.06*launch;
        const double flip=ease((t-0.14)/0.36);
        if(t>0.14) target[ModelR]+=wrapAngle(flip*tau);
        const double up=std::max(launch,hang);
        lerpE(target,Anc,-0.06,1.34,0.06,up); lerpE(target,Ancr,-1.85,0.05,0.0,up);
        lerpE(target,Anc,-0.1,0.86,0.2,tuck); lerpE(target,Ancr,-0.6,0.1,0.0,tuck);
        target[Afolt]=1.0; target[Afolr]=0.5+0.5*tuck;
        target[Ikl]=target[Ikl]+(1.0-target[Ikl])*std::max(hang,tuck); target[Ikl]+=(0.0-target[Ikl])*launch;
        target[UArmL]+=( -2.7-target[UArmL])*launch;
        target[UArmL+2]+=(0.25-target[UArmL+2])*launch;
        lerpE(target,Poler,-0.9,0.3,-0.2,up); lerpE(target,Polel,0.9,0.3,-0.2,up);
        target[Stab]=0.3;
        target[HandPL]+=(-1.0-target[HandPL])*launch; target[Ears]+=0.8*(launch+hang);
        effort=std::max(effort,launch+hang);
        double weight=win(t,0.0,0.04,1.55,1.9);
        if(leapEnd>=0.0) weight*=1.0-ease((t-leapEnd)/0.1);
        for(std::size_t i=0;i<p.size();++i) p[i]+=(target[i]-p[i])*weight;
    }

    void poseSlam(double t,bool grounded) {
        const auto ease=[](double value) { const double x=clamp(value,0.0,1.0); return x*x*(3.0-2.0*x); };
        const auto win=[&](double value,double a,double b,double c,double d) {
            if(value<=a||value>=d) return 0.0;
            if(value<b) return ease((value-a)/(b-a));
            if(value>c) return 1.0-ease((value-c)/(d-c));
            return 1.0;
        };
        const auto lerpE=[&](std::array<double,CharacterPoseChannelCount>& values,std::size_t i,
            double x,double y,double z,double weight) {
            values[i]+=(x-values[i])*weight; values[i+1]+=(y-values[i+1])*weight; values[i+2]+=(z-values[i+2])*weight;
        };
        if(!slamGround&&grounded&&t>0.02) {
            slamGround=true; timers[TImpact]=0.0; springs[SSq+1]-=3.2; springs[SPelY+1]-=0.8; hairKick(0.0,-3.0,0.0);
        }
        const double impactTime=slamGround?timers[TImpact]:-1.0;
        const double dive=slamGround?0.0:ease(t/0.07), imp=impactTime>=0.0?win(impactTime,0.0,0.02,0.35,0.8):0.0;
        std::array<double,CharacterPoseChannelCount> target=p;
        target[Wpl]=0.0; target[Wpr]=0.0;
        lerpE(target,FootL,0.1,0.1,0.06,dive); lerpE(target,FootLR,0.5,0.1,0.0,dive);
        lerpE(target,FootR,-0.1,0.12,-0.08,dive); lerpE(target,FootRR,0.7,-0.1,0.0,dive);
        lerpE(target,Anc,-0.06,1.3,0.12,dive*(1.0-ease(t/0.12))); lerpE(target,Ancr,-1.9,0.05,0.0,dive*(1.0-ease(t/0.12)));
        lerpE(target,Anc,-0.05,0.62,0.36,ease((t-0.04)/0.1)*(1.0-imp));
        lerpE(target,Ancr,0.9,0.05,0.0,ease((t-0.04)/0.1)*(1.0-imp));
        target[Spine]+=0.3*dive; target[Chest]+=0.15*dive;
        const double ankleHeight=rig->bones[static_cast<std::size_t>(std::max(0,find("footL")))].rest.y;
        lerpE(target,FootL,0.2,ankleHeight,0.06,imp); lerpE(target,FootLR,0.0,0.35,0.0,imp);
        lerpE(target,FootR,-0.2,ankleHeight,-0.1,imp); lerpE(target,FootRR,0.0,-0.5,0.0,imp);
        target[HipsP+1]-=0.25*imp; target[Spine]+=0.45*imp; target[Chest]+=0.25*imp; target[Hlp]+=0.2*imp;
        const double forward=GetCharacterWeaponHold(weapon).animKind=="roller"?0.5:0.36;
        lerpE(target,Anc,-0.05,forward,0.4,imp); lerpE(target,Ancr,forward==0.5?1.2:1.35,0.05,0.0,imp);
        target[Kneel]+=0.3*imp; target[Kneer]-=0.3*imp; target[Ikl]=1.0; target[Afolt]=1.0; target[Afolr]=0.6;
        target[Ears]-=0.6*dive; target[HandPL]=-1.0;
        lerpE(target,Poler,-0.9,-0.2,-0.3,imp); lerpE(target,Polel,0.9,-0.2,-0.3,imp);
        effort=std::max(effort,dive+imp);
        const double weight=win(t,0.0,0.03,1.0,1.4);
        for(std::size_t i=0;i<p.size();++i) p[i]+=(target[i]-p[i])*weight;
    }

    void poseForm(double dt) {
        const double t=formTime;
        const auto ease=[](double value) { const double x=clamp(value,0.0,1.0); return x*x*(3.0-2.0*x); };
        const auto window=[&](double value,double a,double b,double c,double d) {
            if(value<=a||value>=d) return 0.0;
            if(value<b) return ease((value-a)/(b-a));
            if(value>c) return 1.0-ease((value-c)/(d-c));
            return 1.0;
        };
        const auto& hold=GetCharacterWeaponHold(weapon);
        const auto& rigData=GetCharacterWeaponRig(weapon);
        const double two=twoWeight;
        if(form!=CharacterForm::Kid&&previousForm==CharacterForm::Kid) {
            if(t>0.1) return;
            const double k=ease(t/0.05), freeArm=1.0-two;
            p[UArmL]+=(-2.4-p[UArmL])*k*freeArm;
            p[UArmL+2]+=(0.35-p[UArmL+2])*k*freeArm;
            p[FArmL]+=(-0.2-p[FArmL])*k*freeArm;
            p[Spine]+=0.2*k; p[Chest]+=0.08*k; p[Hlp]+=0.22*k; p[HipsP+1]-=0.05*k; p[Ears]-=0.8*k;
            p[HandPL]+=(2.0-p[HandPL])*k; p[Eye]-=0.45*k; p[MCurve]+=0.35*k;
        } else if(form==CharacterForm::Kid&&previousForm!=CharacterForm::Kid) {
            const double fl=window(t,0.045,0.1,0.17,0.42), freeArm=1.0-two, noAim=1.0-aimWeight;
            p[UArmL]+=(-1.75-p[UArmL])*fl*freeArm; p[UArmL+2]+=(1.0-p[UArmL+2])*fl*freeArm;
            p[FArmL]+=(-0.35-p[FArmL])*fl*freeArm;
            p[HandPL]+=(2.0-p[HandPL])*fl; p[Ears]+=fl; p[Spine]-=0.08*fl; p[Chest]-=0.05*fl; p[Hlp]-=0.12*fl;
            p[Anc+1]+=0.04*fl*noAim; p[Ancr]-=0.3*fl*noAim;
            p[MCurve]+=0.5*fl; p[MOpen]=std::max(p[MOpen],0.35*fl); p[BrowY]+=0.6*fl;
            if(t>=0.045&&t-dt<0.045) {
                for(auto& strand:hair) {
                    strand.x.fill(glm::dvec3(0.0)); strand.velocity.fill(glm::dvec3(0.0));
                    strand.tipX.fill(glm::dvec3(0.0)); strand.tipVelocity.fill(glm::dvec3(0.0));
                }
                hairKick(0.0,4.2,0.6); springs[STankL+1]+=2.2;
                springs[SEarL+1]+=5.0; springs[SEarR+1]+=5.0;
            }
        }
    }

    void updateFormScales() {
        static constexpr double emST[4]={0.0,0.03,0.07,0.095}, emSY[4]={1.0,0.72,1.42,1.6}, emSX[4]={1.0,1.18,0.78,0.62};
        static constexpr double emKT[6]={0.045,0.1,0.16,0.24,0.32,0.42}, emKY[6]={1.36,1.13,0.88,1.05,0.985,1.0},
            emKX[6]={0.68,0.9,1.09,0.975,1.008,1.0};
        static constexpr double dvKT[4]={0.0,0.03,0.07,0.095}, dvKY[4]={1.0,0.82,0.34,0.16}, dvKX[4]={1.0,1.1,1.4,1.2};
        static constexpr double dvST[6]={0.045,0.08,0.13,0.2,0.28,0.38}, dvSY[6]={0.3,0.72,1.3,0.9,1.045,1.0},
            dvSX[6]={1.5,1.2,0.83,1.07,0.98,1.0};
        const bool toKid=form==CharacterForm::Kid, fromKid=previousForm==CharacterForm::Kid;
        const double t=formTime;
        double kU=toKid?1.0:0.0,kY=1.0,kXZ=1.0,sU=toKid?0.0:1.0,sY=1.0,sXZ=1.0,lift=0.0;
        if(toKid&&!fromKid&&t<0.45) {
            sU=t<0.06?1.0:1.0-easeIn((t-0.06)/0.035);
            sY=keyedCatmull(t,emST,emSY,4); sXZ=keyedCatmull(t,emST,emSX,4);
            kU=t<0.045?0.0:easeOut((t-0.045)/0.055);
            kY=keyedCatmull(t,emKT,emKY,6); kXZ=keyedCatmull(t,emKT,emKX,6);
            const double from=previousForm==CharacterForm::Swim?-0.3:previousForm==CharacterForm::Climb?-0.05:-0.12;
            lift=from*(1.0-easeOut((t-0.045)/0.1));
        } else if(!toKid&&fromKid&&t<0.45) {
            kY=keyedCatmull(t,dvKT,dvKY,4); kXZ=keyedCatmull(t,dvKT,dvKX,4);
            kU=t<0.07?1.0:1.0-easeIn((t-0.07)/0.03);
            sU=t<0.045?0.0:easeOut((t-0.045)/0.035);
            sY=keyedCatmull(t,dvST,dvSY,6); sXZ=keyedCatmull(t,dvST,dvSX,6);
        }
        kidScale=kU; sqScale=sU; kidSY=kY; kidSXZ=kXZ; sqSY=sY; sqSXZ=sXZ; kidLift=lift;
    }

    void enterForm(CharacterForm next) {
        const double previousTime=formTime;
        const CharacterForm previous=form;
        previousForm=previous; form=next; formTime=0.0;
        if(previousTime<0.1) {
            if(next==CharacterForm::Kid&&previous!=CharacterForm::Kid)
                formTime=clamp(0.06+0.035*(1.0-sqScale),0.0,0.095)*(sqScale<0.999?1.0:0.0);
            else if(next!=CharacterForm::Kid&&previous==CharacterForm::Kid)
                formTime=kidScale<0.999?clamp(0.07+0.03*(1.0-kidScale),0.0,0.1):0.0;
        }
    }

    void poseFace(double dt,const CharacterAnimState& input) {
        const auto pulse=[](double value,double attack,double decay) {
            return value<0.0?0.0:value<attack?value/attack:std::exp(-(value-attack)*decay);
        };
        const auto win=[](double value,double a,double b,double c,double d) {
            return sstep(a,b,value)*(1.0-sstep(c,d,value));
        };
        const auto& hold=GetCharacterWeaponHold(weapon);
        const double hit=timers[THit]<0.7?pulse(timers[THit],0.03,4.5)*clamp(hitAmplitude,0.5,1.2):0.0;
        const double firing=lastShot<0.25?1.0:0.0;
        const std::array<double,9> target={
            aimWeight*(1.0-firing*0.5)*(hold.animKind=="charger"?1.0:0.8),
            firing*(hold.animKind=="shooter"?1.0:0.4),
            std::max(effort,hold.animKind=="charger"?charge*aimWeight:0.0),
            hit,
            lowInkWeight*(1.0-hit),
            glowWeight*(1.0-lowInkWeight)*0.9,
            timers[TSpawn]<1.3?win(timers[TSpawn],0.0,0.05,0.5,0.9):0.0,
            std::max(tired,goo*0.7),
            timers[TSpawn]<1.6?win(timers[TSpawn],0.75,0.9,1.2,1.6):0.0
        };
        const std::array<double,9> rates={12.0,12.0,18.0,10.0,6.0,5.0,20.0,4.0,8.0};
        for(std::size_t i=0;i<expressionWeights.size();++i) {
            const double next=damp(expressionWeights[i],target[i],rates[i],dt);
            expressionWeights[i]=i==3?std::max(target[i],next):next;
        }
        static constexpr std::array<std::array<double,8>,9> expressions={{
            {{-0.38,-0.25,-0.08,-0.5,-0.18,0.0,0.0,0.25}},
            {{-0.2,0.0,-0.05,0.3,0.12,0.22,0.0,0.2}},
            {{-0.62,-0.35,-0.22,-0.35,0.22,0.14,0.0,0.45}},
            {{0.85,0.1,-0.62,-1.9,-0.1,0.42,0.18,0.7}},
            {{0.62,0.45,0.08,-1.1,-0.28,0.1,0.0,0.0}},
            {{-0.45,-0.1,-0.06,0.2,0.05,0.05,0.16,0.2}},
            {{0.25,0.9,0.18,-0.75,-0.42,0.55,0.0,0.0}},
            {{0.5,-0.1,-0.2,-0.9,-0.2,0.22,0.0,0.1}},
            {{0.1,0.7,-0.12,0.25,0.12,0.72,0.0,0.35}}
        }};
        static constexpr std::array<std::size_t,8> expressionChannels={Brow,BrowY,Eye,MCurve,MWidth,MOpen,MTilt,Squint};
        for(std::size_t expression=0;expression<expressionWeights.size();++expression) {
            double weight=expressionWeights[expression];
            if(expression==0||expression==1||expression==2||expression==7) weight*=1.0-expressionWeights[3];
            if(expression==5) weight*=1.0-expressionWeights[0]*0.5;
            if(expression==8) weight*=0.6;
            for(std::size_t channel=0;channel<8;++channel) p[expressionChannels[channel]]+=expressions[expression][channel]*weight;
        }
        p[Ears]+=0.25*expressionWeights[0]+0.1*expressionWeights[1]+0.15*expressionWeights[2]
            -0.6*expressionWeights[3]-0.9*expressionWeights[4]+0.5*expressionWeights[5]
            +0.8*expressionWeights[6]-0.8*expressionWeights[7]+0.8*expressionWeights[8]
            -0.3*runWeight*gaitWeight;
        p[HandPL]=std::min(2.0,p[HandPL]+0.9*hit);
        const double breathWeight=std::max(exert,tired);
        p[MOpen]=std::max(p[MOpen],(0.08+0.14*breathWeight)*(0.5+0.5*std::sin(tau*breathPhase))*breathWeight);
        if(runWeight*gaitWeight>0.5) p[MOpen]=std::max(p[MOpen],0.12*runWeight);
        effort=0.0;
    }

    void updateWeaponSway(double dt) {
        const double sx=spring(springs,SWpx,clamp(-kax*0.0011,-0.035,0.035),3.0,0.34,dt);
        const double sy=spring(springs,SWpy,clamp(-vy*0.002,-0.03,0.03),3.4,0.34,dt);
        const double sz=spring(springs,SWpz,clamp(-kaz*0.0011,-0.035,0.035),3.0,0.34,dt);
        const double rx=spring(springs,SWrx,clamp(vy*0.01,-0.12,0.12),2.6,0.32,dt);
        const double ry=spring(springs,SWry,clamp(-yawRate*0.035,-0.22,0.22),2.6,0.38,dt);
        weaponSwayRawPosition={sx,sy,sz};
        weaponSwayEuler={rx,ry,0.0};
    }

    static double handKey(double h,double fist,double grip,double relaxed,double open) {
        if(h<0.0) return grip+(fist-grip)*std::min(1.0,-h);
        if(h<1.0) return grip+(relaxed-grip)*h;
        return relaxed+(open-relaxed)*std::min(1.0,h-1.0);
    }

    void updateFingers(double dt) {
        const bool bombOn=bombHeld&&!subPropHidden&&form==CharacterForm::Kid&&subWeight>0.2;
        const double gripLeft=std::max(clamp(p[Ikl],0.0,1.0)*(1.0-clamp(p[Ltw],0.0,1.0)),bombOn?1.0:0.0);
        gripTimer-=dt;
        if(gripTimer<=0.0) {
            gripTimer=4.5+random()*6.0;
            if(dance.empty()&&aimWeight<0.2&&gaitWeight<0.3) {
                springs[SGrip+1]-=3.2;
                if(gripLeft>0.5&&random()<0.5) springs[SGripL+1]-=2.6;
            }
        }
        spring(springs,SGrip,0.0,5.2,0.42,dt);
        spring(springs,SGripL,0.0,5.2,0.42,dt);
        handShape[0]=damp(handShape[0],clamp(p[HandPL]*(1.0-gripLeft),-1.0,2.0),20.0,dt);
        handShape[1]=damp(handShape[1],clamp(p[HandPR],-1.0,2.0),20.0,dt);
    }

    void poseLand(double timeValue) {
        const double a=landAmplitude;
        auto x=p;
        const double k=win(timeValue,0.0,0.035,0.07+0.13*a,0.3+0.38*a)*sstep(0.3,0.75,a);
        if(k<=0.001) return;
        const double big=sstep(0.7,0.95,a)*(1.0-aimWeight)*(1.0-twoWeight);
        x[HipsP+1]-=0.11*a; x[HipsP+2]-=0.02*a; x[Hips]+=0.14*a; x[Spine]+=0.26*a; x[Chest]+=0.1*a;
        x[Hlp]+=0.12*a; x[Neck]+=0.08*a; x[Kneel]+=0.16*a; x[Kneer]-=0.16*a;
        x[UArmL]+=( -0.35-x[UArmL])*0.8; x[UArmL+2]+=(0.95-x[UArmL+2])*0.8; x[FArmL]+=(-0.25-x[FArmL])*0.8;
        x[HandPL]+=(2.0-x[HandPL])*0.8;
        x[Ltw]=std::max(x[Ltw],big); setE(x,Ltgt,0.24,0.1,0.2); x[Ikl]*=1.0-big;
        lerpE(x,Polel,1.0,0.2,0.0,big); x[HipsP+1]-=0.05*big; x[Spine]+=0.12*big;
        x[Squint]+=0.5*a; x[MCurve]-=0.3*a; x[Ears]-=0.7*a;
        effort=std::max(effort,0.5*a*k);
        poseLerp(p,p,x,k);
    }

    void poseDodge(double timeValue) {
        auto x=p;
        const double u=clamp(timeValue/dodgeDuration,0.0,1.0);
        const double tuck=win(u,0.0,0.1,0.62,0.96), out=sstep(0.6,1.0,u);
        x[Wpl]+=(0.0-x[Wpl])*tuck; x[Wpr]+=(0.0-x[Wpr])*tuck;
        lerpE(x,FootL,0.1,0.38,0.12,tuck); lerpE(x,FootLR,1.15,0.25,0.0,tuck);
        lerpE(x,FootR,-0.1,0.35,0.08,tuck); lerpE(x,FootRR,1.25,-0.25,0.0,tuck);
        const int foot=find("footL");
        const double ankle=foot>=0?rig->bones[static_cast<std::size_t>(foot)].rest.y:0.085;
        constexpr std::array<double,6> locked={0.165,0.035,0.42,-0.165,-0.035,-0.42};
        lerpE(x,FootL,locked[0],ankle,locked[1],out*(1.0-tuck));
        lerpE(x,FootR,locked[3],ankle,locked[4],out*(1.0-tuck));
        x[FootLR+1]+=(locked[2]-x[FootLR+1])*out; x[FootRR+1]+=(locked[5]-x[FootRR+1])*out;
        x[Kneel]+=0.25*tuck; x[Kneer]-=0.25*tuck;
        x[HipsP+1]-=0.05*tuck+0.05*out*(1.0-tuck);
        x[Spine]+=0.6*tuck; x[Chest]+=0.35*tuck; x[Neck]+=0.25*tuck; x[Hlp]+=0.4*tuck; x[Stab]*=1.0-0.85*tuck;
        lerpE(x,Anc,-0.09,0.93,0.17,tuck); lerpE(x,Ancr,-0.5,0.35,0.4,tuck);
        lerpE(x,Anl,0.09,0.93,0.17,tuck); lerpE(x,Anlr,-0.5,-0.35,-0.4,tuck);
        x[Afolt]+=(1.0-x[Afolt])*tuck; x[Afolr]+=(1.0-x[Afolr])*tuck;
        lerpE(x,Poler,-0.9,-0.2,-0.3,tuck); lerpE(x,Polel,0.9,-0.2,-0.3,tuck);
        x[SqY]*=1.0-0.07*tuck+0.05*out*(1.0-out); x[SqXZ]*=1.0+0.035*tuck;
        x[Ears]-=0.7*tuck; x[Squint]+=0.7*tuck; x[MCurve]-=0.5*tuck; x[Brow]-=0.3*tuck;
        effort=std::max(effort,tuck);
        tumble=u<1.0?tau*(1.0-std::pow(1.0-u,2.4)):0.0;
        tumbleX=dodgeZ; tumbleZ=-dodgeX; tumbleDrop=0.26*tuck;
        poseLerp(p,p,x,win(timeValue,0.0,0.015,dodgeDuration,dodgeDuration+0.28));
    }

    void poseMitts(const CharacterAnimState& input,double dt) {
        if(weapon!="mitts") return;
        const bool kid=form==CharacterForm::Kid&&dance.empty();
        const double rightPunch=kid?mitPunch(timers[TShoot]):0.0;
        const double leftPunch=kid?mitPunch(timers[TShootL]):0.0;
        if(rightPunch+leftPunch>0.001) {
            const double aim=clamp(input.aimPitch,-0.8,1.0);
            const double forwardY=std::sin(aim)*0.24,forwardZ=std::cos(aim)*0.24;
            p[Anc]+=0.07*rightPunch; p[Anc+1]+=forwardY*rightPunch; p[Anc+2]+=forwardZ*rightPunch;
            p[Anl]-=0.07*leftPunch; p[Anl+1]+=forwardY*leftPunch; p[Anl+2]+=forwardZ*leftPunch;
            p[Ancr+1]+=0.22*rightPunch; p[Ancr+2]-=0.45*rightPunch;
            p[Anlr+1]-=0.22*leftPunch; p[Anlr+2]+=0.45*leftPunch;
            const double twist=rightPunch-leftPunch;
            p[Chest+1]+=0.3*twist; p[Spine+1]+=0.12*twist; p[Hips+1]+=0.05*twist;
            p[ClavR+1]+=0.22*rightPunch; p[ClavL+1]-=0.22*leftPunch;
            p[Spine]+=0.035*(rightPunch+leftPunch);
            effort=std::max(effort,0.45*std::max(rightPunch,leftPunch));
        }
        const auto* runner=input.runner?&*input.runner:nullptr;
        const bool charging=kid&&runner&&runner->charging;
        mittsChargeWeight=damp(mittsChargeWeight,charging?1.0:0.0,charging?14.0:9.0,dt);
        if(mittsChargeWeight>0.001) {
            const double c=charging?clamp(runner->charge,0.0,1.0):0.0,w=mittsChargeWeight;
            const double trem=c>=0.999?0.004*std::sin(time*71.0)+0.003*std::sin(time*53.0+1.0):0.0015*std::sin(time*40.0)*c;
            p[HipsP+1]-=(0.13+0.07*c)*w; p[HipsP+2]-=0.04*w;
            p[Hips]+=0.3*w; p[Spine]+=0.28*w; p[Chest]+=0.1*w; p[Neck]-=0.2*w; p[Hlp]-=0.1*w;
            p[Kneel]+=0.24*w; p[Kneer]-=0.24*w;
            lerpE(p,Anc,-0.2,0.56+trem,-0.08,w); lerpE(p,Ancr,1.5,0.35,0.3,w);
            lerpE(p,Anl,0.2,0.56-trem,-0.08,w); lerpE(p,Anlr,1.5,-0.35,-0.3,w);
            lerpE(p,Poler,-0.9,0.2,-0.7,w); lerpE(p,Polel,0.9,0.2,-0.7,w);
            p[SqY]*=1.0-(0.03+0.05*c)*w; p[SqXZ]*=1.0+0.03*w; p[Ears]-=0.6*w;
            effort=std::max(effort,(0.4+0.5*c)*w);
        }
        const bool leaping=kid&&runner&&runner->leaping;
        mittsLeapWeight=damp(mittsLeapWeight,leaping?1.0:0.0,leaping?18.0:8.0,dt);
        if(mittsLeapWeight>0.001) {
            const double w=mittsLeapWeight,rise=sstep(-3.0,4.0,vy),wr=rise*w,wf=(1.0-rise)*w;
            p[ModelR]+=0.5*wr+0.12*wf; p[SqY]*=1.0+0.07*wr; p[SqXZ]*=1.0-0.035*wr;
            lerpE(p,Anc,-0.09,1.26,0.32,wr); lerpE(p,Ancr,-0.95,0.12,-0.15,wr);
            lerpE(p,Anl,0.09,1.26,0.32,wr); lerpE(p,Anlr,-0.95,-0.12,0.15,wr);
            lerpE(p,Anc,-0.11,1.36,0.1,wf); lerpE(p,Ancr,-1.7,0.2,0.0,wf);
            lerpE(p,Anl,0.11,1.36,0.1,wf); lerpE(p,Anlr,-1.7,-0.2,0.0,wf);
            p[Afolt]+=(1.0-p[Afolt])*w; p[Afolr]+=(0.7-p[Afolr])*w;
            lerpE(p,Poler,-0.8,0.3,-0.3,w); lerpE(p,Polel,0.8,0.3,-0.3,w);
            lerpE(p,FootL,0.09,0.2,-0.26,wr); lerpE(p,FootLR,1.2,0.1,0.0,wr);
            lerpE(p,FootR,-0.09,0.26,-0.2,wr); lerpE(p,FootRR,1.0,-0.1,0.0,wr);
            lerpE(p,FootL,0.11,0.34,0.1,wf); lerpE(p,FootLR,0.5,0.12,0.0,wf);
            lerpE(p,FootR,-0.11,0.3,0.04,wf); lerpE(p,FootRR,0.6,-0.12,0.0,wf);
            p[Wpl]*=1.0-w; p[Wpr]*=1.0-w; p[Ears]+=0.9*w; p[MOpen]=std::max(p[MOpen],0.3*wf);
            effort=std::max(effort,0.6*w);
        }
        const double landing=kid&&runner?win(runner->mittsLandTime,0.0,0.03,0.16,0.5):0.0;
        if(landing>0.001) {
            const double w=landing;
            p[HipsP+1]-=0.2*w; p[HipsP+2]-=0.02*w; p[Hips]+=0.2*w; p[Spine]+=0.4*w; p[Chest]+=0.2*w; p[Hlp]+=0.2*w;
            p[Kneel]+=0.3*w; p[Kneer]-=0.3*w;
            lerpE(p,Anc,-0.13,0.3,0.34,w); lerpE(p,Ancr,1.35,0.2,0.0,w);
            lerpE(p,Anl,0.13,0.3,0.34,w); lerpE(p,Anlr,1.35,-0.2,0.0,w);
            p[Afolt]+=(1.0-p[Afolt])*w; p[Afolr]+=(0.6-p[Afolr])*w;
            lerpE(p,Poler,-0.9,-0.2,-0.3,w); lerpE(p,Polel,0.9,-0.2,-0.3,w); p[Ears]-=0.6*w;
            effort=std::max(effort,w);
        }
        mittsClingWeight=damp(mittsClingWeight,0.0,9.0,dt);
    }

    void updateSquid(double dt,const CharacterAnimState& input,const CharacterSceneInput& scene) {
        if(sqScale<=0.001) { squidInitialized=false; frame.squid.visible=false; return; }
        CharacterForm squidForm=form;
        if(squidForm==CharacterForm::Kid) squidForm=previousForm==CharacterForm::Kid?CharacterForm::Squid:previousForm;
        const double v=hs,t=time;
        glm::dvec3 position{0.0};
        glm::dquat rotation{1.0,0.0,0.0,0.0};
        double sy=1.0,sxz=1.0;
        bool local=false;
        const bool airborne=!grounded;
        const auto worldRotation=frame.root.rotation*frame.model.rotation;
        const auto modelWorld=frame.root.matrix()*frame.model.matrix();
        const auto worldToModel=glm::inverse(modelWorld);
        if(squidForm==CharacterForm::Climb) {
            auto normal=input.wallNormal;
            if(glm::dot(normal,normal)<1e-12) normal={0.0,0.0,1.0}; else normal=glm::normalize(normal);
            auto by=glm::dvec3(0.0,1.0,0.0)-normal*normal.y;
            if(glm::dot(by,by)<1e-4) by={0.0,0.0,1.0};
            by=glm::normalize(by);
            const auto bz=-normal;
            const auto bx=glm::normalize(glm::cross(by,bz));
            glm::dmat3 basis(1.0); basis[0]=bx; basis[1]=by; basis[2]=bz;
            auto climbRotation=glm::quat_cast(basis);
            const double climbV=std::min(1.0,std::abs(vy)/5.0+v/6.0);
            const auto twist=glm::angleAxis(std::sin(t*11.0)*0.16*climbV,bz);
            climbRotation=twist*climbRotation;
            rotation=glm::inverse(worldRotation)*climbRotation;
            const auto worldPosition=input.position+glm::dvec3(0.0,0.26+0.025*std::sin(t*14.0)*climbV,0.0)+
                normal*(-0.38-0.07)+bx*(0.022*std::sin(t*5.5)*climbV);
            position=glm::dvec3(worldToModel*glm::dvec4(worldPosition,1.0));
            sy=1.0+0.1*climbV+0.05*std::sin(t*14.0)*climbV; sxz=1.0/std::sqrt(sy);
            squidWigAmplitude=0.014+0.022*climbV; squidWigFrequency=10.0+8.0*climbV;
            local=true;
        } else if(squidForm==CharacterForm::Swim&&!airborne) {
            if(v>0.3) squidYaw=wrapAngle(squidYaw+wrapAngle(std::atan2(mdx,mdz)-squidYaw)*(1.0-std::exp(-10.0*dt)));
            const double speed=std::min(1.0,v/11.0);
            const double und=std::sin(t*(5.0+(14.0-5.0)*speed));
            squidRoll=damp(squidRoll,clamp(-yawRate*0.08,-0.5,0.5),6.0,dt);
            rotation=euler({1.5707963267948966+0.06*und*speed,
                squidYaw+0.1*std::sin(t*(4.0+(11.0-4.0)*speed)+1.0)*speed,
                squidRoll+und*0.12*std::min(1.0,v/4.0)},true);
            position={0.0,-0.085+0.012*std::sin(t*5.0),0.0};
            position+=rotation*glm::dvec3(0.0,-0.12,0.0);
            sy=1.0+0.2*speed+0.03*und*speed; sxz=1.0/std::sqrt(sy);
            squidWigAmplitude=0.018+0.022*speed; squidWigFrequency=12.0+8.0*speed;
            local=true;
        } else if(airborne&&(hs>3.0||squidForm==CharacterForm::Swim||previousForm==CharacterForm::Climb)) {
            const double pitch=std::atan2(vy,std::max(hs,0.5));
            if(v>0.3) squidYaw=wrapAngle(squidYaw+wrapAngle(std::atan2(mdx,mdz)-squidYaw)*(1.0-std::exp(-8.0*dt)));
            rotation=euler({1.5707963267948966-pitch,squidYaw,0.0},true);
            position={0.0,0.22,0.0};
            const double speed=std::min(1.0,std::hypot(hs,vy)/10.0);
            sy=1.0+0.22*speed; sxz=1.0/std::sqrt(sy);
            squidWigAmplitude=0.03; squidWigFrequency=16.0;
        } else {
            double hop=0.0,tilt=0.0;
            if(!airborne&&v>0.25) {
                squidHopPhase+=dt*(v/0.72);
                const double hp=fract(squidHopPhase),k=std::min(1.0,v/1.5),airU=clamp((hp-0.14)/0.72,0.0,1.0);
                hop=0.14*std::sin(3.14159265358979323846*airU)*k;
                tilt=(0.3*std::cos(3.14159265358979323846*airU)-0.1)*k;
                const double anti=hp<0.14?std::sin(3.14159265358979323846*hp/0.14):0.0;
                const double land=hp>0.86?std::sin(3.14159265358979323846*(hp-0.86)/0.14):0.0;
                sy=1.0-0.2*anti*k+0.16*std::sin(3.14159265358979323846*airU)*k-0.16*land*k;
                if(v>0.3) squidYaw=wrapAngle(squidYaw+wrapAngle(std::atan2(mdx,mdz)-squidYaw)*(1.0-std::exp(-10.0*dt)));
            } else if(airborne) {
                squidHopPhase=0.0;
                sy=1.0+clamp(std::abs(vy)*0.022,0.0,0.24);
                tilt=clamp(-vy*0.03,-0.3,0.3);
            } else {
                squidHopPhase=0.0;
                sy=1.0+0.035*std::sin(t*3.1)+0.012*std::sin(t*7.3);
                squidYaw=wrapAngle(squidYaw+wrapAngle(-squidYaw)*(1.0-std::exp(-3.0*dt)));
            }
            const double sqY=spring(springs,SSqY,0.0,5.0,0.3,dt),sqP=spring(springs,SSqP,0.0,4.0,0.35,dt);
            sy*=1.0+clamp(sqY,-0.3,0.3); tilt+=sqP*0.3;
            rotation=euler({tilt,squidYaw,0.05*std::sin(t*2.3)},true);
            position={0.0,0.165+hop,0.0}; sxz=1.0/std::sqrt(sy);
            const double speed=std::min(1.0,v);
            squidWigAmplitude=0.014+0.014*speed; squidWigFrequency=7.0+6.0*speed;
        }
        sy*=sqSY; sxz*=sqSXZ;
        if(!squidInitialized) { squidPivotPosition=position; squidPivotRotation=rotation; squidInitialized=true; }
        const double blend=1.0-std::exp(-dt*26.0);
        squidPivotPosition+=(position-squidPivotPosition)*blend;
        squidPivotRotation=glm::slerp(squidPivotRotation,rotation,blend);
        squidPivot.position=squidPivotPosition; squidPivot.rotation=squidPivotRotation; squidPivot.scale={sxz,sy,sxz};
        squidPivot.visible=true;
        frame.squid.position=glm::dvec3(0.0); frame.squid.rotation=glm::dquat(1.0,0.0,0.0,0.0);
        const double pop=std::max(0.001,sqScale); frame.squid.scale=glm::dvec3(pop); frame.squid.visible=sqScale>0.001;
        squidWigMotion=clamp(v/11.0,0.0,1.0);
        frame.material.wig={squidWigAmplitude,squidWigFrequency,squidWigMotion};
        frame.material.time=time;
        const double blink=std::max(blinkLeft,blinkRight);
        squidEyesScaleY=std::max(0.08,1.0-0.92*blink); squidEyesPositionY=0.09*(1.0-squidEyesScaleY);
        squidGhostVisible=local&&scene.isLocal;
        squidEyesVisible=!(local&&!scene.isLocal);
        if(form!=CharacterForm::Kid) blinkTick(dt,1);
    }

    bool fingerRotation(const std::string& name,glm::dquat& rotation) const {
        const bool left=name.rfind("handL_",0)==0, right=name.rfind("handR_",0)==0;
        if(!left&&!right) return false;
        const auto suffix=name.substr(6);
        const std::size_t hand=left?0u:1u;
        const double side=left?1.0:-1.0,h=handShape[hand];
        const double relax=clamp(h,0.0,1.0)*clamp(2.0-h,0.0,1.0),open=clamp(h-1.0,0.0,1.0);
        const double life=0.025*std::sin(time*0.9+static_cast<double>(hand)*2.1)*relax;
        const double tweak=clamp(springs[(left?SGripL:SGrip)]*0.05,-0.2,0.14)*clamp(1.0-std::abs(h),0.0,1.0);
        const double f1=handKey(h,-0.35,0.0,0.45,0.9)-tweak;
        const double f2=handKey(h,-0.5,0.0,0.35,0.75)-tweak*1.3;
        if(suffix=="thumb1") {
            rotation=euler({handKey(h,0,0,0.35,0.55),side*handKey(h,-0.1,0,0,0),side*handKey(h,0.12,0,0,0)},false);
            return true;
        }
        if(suffix=="thumb2") { rotation=euler({0,0,side*handKey(h,0.3,0,-0.25,-0.5)},false); return true; }
        static constexpr std::array<double,4> spread={-0.1,-0.03,0.04,0.11};
        static constexpr std::array<const char*,4> names={"index","middle","ring","pinky"};
        for(std::size_t i=0;i<names.size();++i) {
            const std::string first=std::string(names[i])+"1",second=std::string(names[i])+"2";
            if(suffix==first||suffix==second) {
                const double cascade=(static_cast<double>(i)-1.5)*0.07*relax;
                const double trigger=i==0&&!left?weaponAnimationRight.trig*clamp(1.0-std::abs(h),0.0,1.0):0.0;
                rotation=suffix==first
                    ?euler({spread[i]*(open+0.35*relax),0,side*(f1-cascade+life-0.3*trigger)},false)
                    :euler({0,0,side*(f2-cascade*0.8+life*0.5-0.35*trigger)},false);
                return true;
            }
        }
        return false;
    }

    void blinkStart(double amplitude, bool hard=false) {
        if(blinkTimer>=0.0&&blinkTimer<blinkClose) return;
        blinkTimer=0.0; blinkAmplitude=amplitude;
        blinkClose=0.06+random()*0.025;
        blinkHold=hard?0.08+random()*0.06:0.008+random()*0.03;
        blinkOpen=0.14+random()*0.06+(hard?0.05:0.0);
        blinkLag=(random()-0.5)*0.024; blinkSqueeze=hard?1.0:0.0;
        blinkNext=99.0;
        blinkDouble=!hard&&amplitude>0.9&&random()<0.14;
    }

    double blinkCurve(double t) const {
        if(t<=0.0) return 0.0;
        if(t<blinkClose) { const double u=t/blinkClose; return u*u; }
        t-=blinkClose;
        if(t<blinkHold) return 1.0;
        t-=blinkHold;
        return t<blinkOpen?std::pow(1.0-t/blinkOpen,2.2):0.0;
    }

    void blinkTick(double dt, int lifeLevel) {
        if(lifeLevel==0) { blinkLeft=blinkRight=0.0; return; }
        blinkNext-=dt; turnBlink-=dt;
        if(std::abs(yawRate)>5.5&&turnBlink<=0.0&&blinkTimer<0.0) {
            turnBlink=1.1;
            if(random()<0.7) blinkStart(0.9+0.1*random());
        }
        if(blinkTimer<0.0&&blinkNext<=0.0) blinkStart(random()<0.18?0.55+random()*0.3:1.0);
        blinkLeft=blinkRight=0.0;
        if(blinkTimer>=0.0) {
            blinkTimer+=dt;
            blinkLeft=blinkCurve(blinkTimer-(blinkLag<0.0?-blinkLag:0.0))*blinkAmplitude;
            blinkRight=blinkCurve(blinkTimer-(blinkLag>0.0?blinkLag:0.0))*blinkAmplitude;
            if(blinkTimer>blinkClose+blinkHold+blinkOpen+std::abs(blinkLag)) {
                blinkTimer=-1.0; blinkSqueeze=0.0;
                if(blinkDouble) { blinkDouble=false; blinkNext=0.05+random()*0.05; }
                else {
                    double mean=6.4;
                    mean=mean+(2.8-mean)*std::max(exert,tired);
                    const double r=random(); blinkNext=mean*(0.3+1.35*std::pow(r,1.7));
                }
            }
        }
    }

    void gazeTick(double dt, double dx, double dy) {
        const double targetX=clamp(dx,-0.62,0.62),targetY=clamp(dy,-0.42,0.38);
        if(gazeSaccadeTime>=0.0) {
            gazeSaccadeTime+=dt;
            const double u=std::min(1.0,gazeSaccadeTime/gazeSaccadeDuration),k=u*u*u*(10.0+u*(6.0*u-15.0));
            gazeX=gazeStartX+(gazeTargetX-gazeStartX)*k;
            gazeY=gazeStartY+(gazeTargetY-gazeStartY)*k;
            if(u>=1.0) { gazeSaccadeTime=-1.0; gazeFixX=gazeTargetX; gazeFixY=gazeTargetY; }
            return;
        }
        const double ex=targetX-gazeFixX,ey=targetY-gazeFixY,amplitude=std::hypot(ex,ey);
        if(amplitude>0.075&&dt>0.0) {
            gazeStartX=gazeX; gazeStartY=gazeY;
            const double undershoot=amplitude>0.25?0.9+0.05*random():1.0;
            gazeTargetX=gazeFixX+ex*undershoot; gazeTargetY=gazeFixY+ey*undershoot;
            gazeSaccadeDuration=0.022+0.1*amplitude; gazeSaccadeTime=0.0;
            if(amplitude>0.3&&blinkTimer<0.0&&blinkNext>0.8&&random()<amplitude*0.3)
                blinkStart(0.85+0.15*random());
            if(amplitude>0.4&&random()<0.3) {
                static constexpr std::array<double,8> browFlash={-0.1,0.85,0.1,0.25,0.05,0.04,0.0,0.0};
                playMicroExpression(browFlash,0.3,0.06,0.1,0.3);
            }
            return;
        }
        gazeFixX=targetX; gazeFixY=targetY;
        gazeMicroTime-=dt;
        if(gazeMicroTime<=0.0) {
            gazeMicroTime=0.28+random()*1.1;
            gazeMicroX=(random()-0.5)*0.07; gazeMicroY=(random()-0.5)*0.045;
        }
        gazeX=damp(gazeX,gazeFixX+gazeMicroX,45.0,dt);
        gazeY=damp(gazeY,gazeFixY+gazeMicroY,45.0,dt);
    }

    void updateFace(const CharacterSceneInput& scene, double dt) {
        const auto headIndex=find("head");
        if(headIndex>=0&&attentionOn) {
            const glm::dquat headRotation=glm::normalize(headKidRotation);
            const auto eyePosition=headKidPosition+headRotation*glm::dvec3(0.0,0.188,0.15);
            const auto kidPoint=glm::inverse(frame.kid.matrix())*glm::dvec4(attentionPoint,1.0);
            const auto local=glm::inverse(headRotation)*(glm::dvec3(kidPoint)-eyePosition);
            const double distance=glm::length(local);
            if(distance>0.05) {
                attentionDistance=distance;
                gazeTick(dt,std::atan2(local.x,local.z),std::atan2(local.y,std::hypot(local.x,local.z)));
            }
        } else gazeTick(dt,p[LookX]/0.9,(p[LookY]-0.02)/0.8);

        const double sqz=clamp(p[Squint]+0.35*blinkSqueeze*std::max(blinkLeft,blinkRight),0.0,1.0);
        const double smile=clamp((p[MCurve]-0.3)/0.6,0.0,1.0);
        const double follow=clamp(-gazeY*0.75,0.0,0.3)-clamp(gazeY*0.3,0.0,0.1);
        const double base=clamp(p[Eye]-0.22*sqz-follow+0.04*(p[Pupil]-0.5),0.07,1.25);
        eyeScaleLeft=std::max(0.07,base*(1.0-0.94*blinkLeft));
        eyeScaleRight=std::max(0.07,base*(1.0-0.94*blinkRight)*(1.0-0.93*clamp(p[Wink],0.0,1.0)));
        frame.material.look={clamp(gazeX*0.9,-0.4,0.4)*0.55,clamp(gazeY*0.8+0.02,-0.4,0.4)*0.5};
        const double vergence=attentionOn?std::atan2(0.062,std::max(0.3,attentionDistance)):0.0;
        const double ex2=0.9*gazeX-frame.material.look.x*1.25,ey2=0.9*gazeY-frame.material.look.y*1.25;
        frame.material.gaze={ex2-vergence,ey2,ex2+vergence,ey2};
        const double lid=clamp(0.4*sqz+0.22*smile,0.0,0.6);
        frame.material.lid={0.0,0.0,lid,lid};
        frame.material.pupil=p[Pupil];
        frame.material.mouth2={smile,clamp(p[Sneer],0.0,1.0),clamp(p[Pucker],0.0,1.0),clamp(p[Blush],0.0,1.0)};
    }

    void neutralPose(std::array<double,CharacterPoseChannelCount>& pose) const {
        pose.fill(0.0);
        const auto left = find("footL"), right = find("footR");
        const double ankleH = left >= 0 ? rig->bones[static_cast<std::size_t>(left)].rest.y : 0.085;
        const double hipW = (left >= 0 ? std::abs(rig->bones[static_cast<std::size_t>(left)].rest.x) : 0.084) + 0.008;
        pose[FootL]=hipW; pose[FootL+1]=ankleH; pose[FootL+2]=-0.004; pose[FootLR+1]=0.1;
        pose[FootR]=-hipW; pose[FootR+1]=ankleH; pose[FootR+2]=-0.004; pose[FootRR+1]=-0.1;
        pose[Poler]=-0.7; pose[Poler+1]=-0.55; pose[Poler+2]=-0.45;
        pose[Polel]=0.7; pose[Polel+1]=-0.55; pose[Polel+2]=-0.45;
        pose[Ikr]=1; pose[Wpl]=1; pose[Wpr]=1; pose[Stab]=0.82; pose[Afolt]=1; pose[Afolr]=1;
        pose[MCurve]=0.75; pose[MWidth]=1; pose[Eye]=1; pose[SqY]=1; pose[SqXZ]=1; pose[HandPL]=1; pose[Pupil]=0.5;
        pose[UArmL+2]=0.1; pose[UArmR+2]=-0.1; pose[FArmL]=-0.3; pose[FArmR]=-0.3;
        pose[HandL+2]=-0.1; pose[HandR+2]=0.1;
        (void)right;
    }

    void buildNeutral() { neutralPose(p); }

    bool noDanceTwirl() const {
        const auto kind=GetCharacterWeaponHold(weapon).animKind;
        return kind=="roller"||kind=="charger"||kind=="spinner"||kind=="bucket"||kind=="mitts";
    }

    void poseDance(std::array<double,CharacterPoseChannelCount>& pose,const std::string& danceName,double t,double dt) const {
        (void)dt;
        pose[Wpl]=0.0; pose[Wpr]=0.0; pose[Stab]=0.0; pose[Afolt]=0.0; pose[Afolr]=0.0;
        if(danceName=="victory") {
            if(danceVariant==0) danceVictoryPump(pose,t);
            else if(danceVariant==1) danceVictoryFlourish(pose,t);
            else danceVictoryHops(pose,t);
        } else if(danceName=="defeat") {
            if(danceVariant==0) danceDefeatSlump(pose,t);
            else if(danceVariant==1) danceDefeatSulk(pose,t);
            else danceDefeatKick(pose,t);
        } else if(danceName=="menu_idle") danceMenuIdle(pose,t);
        else if(danceName=="lobby_pose") danceLobby(pose,t);
        else if(danceName=="locker_idle") danceLocker(pose,t);
    }

    void danceVictoryPump(std::array<double,CharacterPoseChannelCount>& d,double t) const {
        const auto& h=GetCharacterWeaponHold(weapon);
        const double b=std::fmod(t*2.1,8.0),bf=fract(b),pump=std::pow(std::abs(std::sin(3.14159265358979323846*b)),0.6),hit=std::exp(-bf*7.0);
        const int left=find("footL"); const double ankleH=left>=0?rig->bones[static_cast<std::size_t>(left)].rest.y:0.085;
        d[HipsP+1]=-0.055+0.045*pump; d[Spine]=0.05-0.05*pump; d[Chest]=-0.08*pump;
        d[Head]=0.12*hit-0.1; d[Head+2]=0.1*std::sin(3.14159265358979323846*b*0.5);
        d[SqY]=1.0-0.05*hit; d[SqXZ]=1.0+0.03*hit; setAnchor(d,h.raise);
        d[Ikl]=0; d[Poler]=-0.8; d[Poler+1]=0.1; d[Poler+2]=-0.5;
        setE(d,FootL,0.11,ankleH,0.01); setE(d,FootR,-0.11,ankleH,-0.01); d[FootLR+1]=0.2; d[FootRR+1]=-0.2;
        d[FootLR]=0.25*pump*(b<4.0?1.0:0.0);
        if(b<4.0) {
            d[UArmL]=-2.6-0.35*pump; d[UArmL+2]=0.35; d[FArmL]=-0.2-1.3*(1.0-pump); d[HandL]=0; d[HandPL]=-1;
            d[Anc+1]+=0.05*pump; d[Chest+1]=0.12*std::sin(3.14159265358979323846*b);
            const double pre=win(b,3.4,3.8,3.9,4.0);
            d[HipsP+1]-=0.07*pre; d[Spine]+=0.15*pre; d[UArmL]=d[UArmL]+(-0.6-d[UArmL])*pre;
        } else if(b<6.0) {
            const double k=(b-4.0)/2.0,air=std::sin(3.14159265358979323846*clamp(k*1.15,0.0,1.0));
            d[ModelR+1]=wrapAngle(tau*ease(k)); d[Model+1]=0.2*air;
            d[FootL+1]+=0.14*air; d[FootR+1]+=0.14*air; d[FootLR]=0.45*air; d[FootRR]=0.45*air;
            d[UArmL]=-1.4; d[UArmL+2]=1.1; d[FArmL]=-0.6; d[HandPL]=1.8;
            d[Head+1]=-0.3*std::sin(tau*k);
            const double land=k>0.87?std::exp(-(k-0.87)*18.0):0.0;
            d[SqY]*=1.0-0.1*land; d[HipsP+1]-=0.06*land;
        } else {
            const double k=backOut((b-6.0)/0.4,2.0);
            d[FootL]=0.11+(0.16-0.11)*k; d[FootR]=-0.11+(-0.16+0.11)*k; d[FootLR+1]=0.3; d[FootRR+1]=-0.3;
            d[HipsP+1]=-0.07; d[Hips+2]=0.06*k; d[Chest]=-0.12;
            d[UArmL]=-2.0; d[UArmL+2]=k; d[FArmL]=-0.9; d[HandL+2]=0.3; d[HandPL]=1.9;
            d[Head+2]=-0.18*k; d[Head]=-0.1;
            d[Wink]=ease((b-6.3)/0.12)*(1.0-ease((b-7.6)/0.15));
            setAnchor(d,CharacterHoldAnchor{{-0.14,1.05,0.25},{-0.35,0.35,-0.2}});
        }
        d[MOpen]=0.75; d[MCurve]=1; d[Brow]=-0.1; d[BrowY]=0.8; d[Eye]=0.92; d[LookY]=0.1; d[Ears]=0.9+0.1*hit;
    }

    void danceVictoryFlourish(std::array<double,CharacterPoseChannelCount>& d,double t) const {
        const auto& h=GetCharacterWeaponHold(weapon); const double c=std::fmod(t,4.6);
        const double dip=win(c,0,0.3,0.42,0.62),rise=win(c,0.45,0.7,1.4,1.7),strike=win(c,1.45,1.62,3.9,4.45);
        const double bounce=strike*std::max(0.0,std::sin((c-1.7)*tau*1.05));
        const int left=find("footL"); const double ankleH=left>=0?rig->bones[static_cast<std::size_t>(left)].rest.y:0.085;
        setE(d,FootL,0.1,ankleH,0.03); setE(d,FootR,-0.12,ankleH,-0.04); d[FootLR+1]=0.25; d[FootRR+1]=-0.35;
        d[HipsP+1]=-0.03-0.08*dip+0.03*rise-0.04*strike+0.015*bounce;
        d[Spine]=0.2*dip-0.12*rise-0.02*strike; d[Chest]=0.1*dip-0.1*rise;
        d[FootLR]=0.45*rise; d[FootRR]=0.45*rise; d[FootL+1]+=0.03*rise; d[FootR+1]+=0.03*rise;
        setAnchor(d,h.carry); lerpE(d,Anc,-0.16,0.8,-0.02,dip); lerpE(d,Ancr,0.9,0.3,0.2,dip);
        lerpE(d,Anc,-0.15,1.36,0.06,rise); lerpE(d,Ancr,-1.6,0.2,0.0,rise);
        d[Spin]=wrapAngle(tau*2.0*ease((c-0.55)/0.85))*(noDanceTwirl()?0.0:1.0);
        lerpE(d,Anc,-0.1,1.02,0.32,strike); lerpE(d,Ancr,-0.12,-0.05,0.25,strike);
        d[Ikl]=0; d[Poler]=-0.9; d[Poler+1]=0.05; d[Poler+2]=-0.3;
        d[UArmL]=-2.4*rise-0.4*dip; d[UArmL+2]=0.3+0.2*dip; d[FArmL]=-0.3-0.8*dip;
        d[Ltw]=strike; setE(d,Ltgt,0.19,0.735,-0.01); lerpE(d,Polel,1,0.1,-0.35,strike);
        d[HandPL]=(1.0+(1.9-1.0)*rise)+((1.5-(1.0+(1.9-1.0)*rise))*strike); d[Ears]=0.4+0.5*rise+0.4*strike;
        d[Hips+2]=0.08*strike; d[HipsP]=0.02*strike; d[Hips+1]=0.12*strike; d[Chest+1]=-0.15*strike;
        d[Head+2]=-0.16*strike+0.04*bounce; d[Head]=-0.06*strike-0.1*rise; d[Head+1]=0.08*strike;
        d[Wink]=win(c,1.75,1.85,2.4,2.55); d[MCurve]=1; d[MOpen]=0.3+0.45*rise+0.2*bounce;
        d[MTilt]=0.2*strike; d[Brow]=-0.2*strike; d[BrowY]=0.6*rise; d[LookX]=-0.05; d[LookY]=0.05+0.2*rise; d[Eye]=1.0-0.1*strike;
    }

    void danceVictoryHops(std::array<double,CharacterPoseChannelCount>& d,double t) const {
        const auto& h=GetCharacterWeaponHold(weapon); const double b=std::fmod(t*2.5,8.0),bf=fract(b);
        const int bi=static_cast<int>(std::floor(b)),side=bi%2?-1:1; const double sd=static_cast<double>(side);
        const int left=find("footL"); const double ankleH=left>=0?rig->bones[static_cast<std::size_t>(left)].rest.y:0.085;
        setAnchor(d,h.raise); d[Ikl]=0;
        if(b<6.0) {
            const double hop=std::sin(3.14159265358979323846*clamp(bf/0.62,0.0,1.0));
            const double squash=bf>0.62?std::exp(-(bf-0.62)*14.0):0.0;
            d[Model]=(-0.07*sd)+(0.07*sd-(-0.07*sd))*ease(bf/0.62); d[Model+1]=0.09*hop;
            d[Hips+2]=-0.1*sd*hop; d[Chest+2]=0.08*sd*hop; d[Head+2]=0.12*sd*hop;
            setE(d,FootL,0.11,ankleH+0.06*hop*(sd>0?1.0:0.4),0); setE(d,FootR,-0.11,ankleH+0.06*hop*(sd<0?1.0:0.4),0);
            d[FootLR]=0.4*hop; d[FootRR]=0.4*hop; d[FootLR+1]=0.15; d[FootRR+1]=-0.15;
            d[HipsP+1]=-0.04-0.06*squash+0.01*hop; d[SqY]=1.0-0.06*squash+0.03*hop;
            d[UArmL]=-2.5; d[UArmL+2]=0.3+0.45*std::sin(3.14159265358979323846*b);
            d[FArmL]=-0.3-0.3*std::sin(3.14159265358979323846*b*2.0);
            d[Anc+1]+=0.06*hop; d[Anc]+=0.03*sd; d[HandPL]=2; d[Ears]=0.7+0.3*hop;
        } else {
            const double k=(b-6.0)/2.0;
            const double crouch=win(k,0,0.12,0.18,0.26),air=std::sin(3.14159265358979323846*clamp((k-0.2)/0.62,0.0,1.0));
            const double land=k>0.82?std::exp(-(k-0.82)*16.0):0.0;
            d[Model+1]=0.28*air; d[HipsP+1]=-0.03-0.1*crouch-0.07*land;
            d[Spine]=0.2*crouch-0.1*air; d[SqY]=1.0-0.08*crouch+0.08*air-0.08*land;
            setE(d,FootL,0.12,ankleH+0.18*air,0.02*air); setE(d,FootR,-0.12,ankleH+0.18*air,0.02*air);
            d[FootLR]=0.6*air; d[FootRR]=0.6*air; d[UArmL]=-0.5+(-2.7+0.5)*air;
            d[UArmL+2]=0.2+(0.75-0.2)*air; d[FArmL]=-0.2;
            lerpE(d,Anc,-0.18,1.4,0.05,air); lerpE(d,Ancr,-1.9,0.3,-0.5,air);
            d[Head]=-0.2*air; d[MOpen]=0.9*air; d[HandPL]=2; d[Ears]=1;
        }
        d[MOpen]=std::max(d[MOpen],0.65); d[MCurve]=1; d[BrowY]=0.9; d[Eye]=0.88; d[LookY]=0.12;
    }

    void danceDefeatSlump(std::array<double,CharacterPoseChannelCount>& d,double t) const {
        const double cyc=std::fmod(t,6.0),sw=std::sin(t*tau*0.28),sighK=win(cyc,3.0,3.7,3.9,4.8),drop=win(cyc,4.3,4.6,4.7,5.6);
        d[HipsP+1]=-0.045-0.02*drop; d[HipsP]=0.01*sw; d[Hips+2]=-0.03*sw;
        d[Spine]=0.2-0.1*sighK+0.05*drop; d[Chest]=0.16-0.12*sighK+0.05*drop; d[Neck]=0.1;
        d[Head]=0.2-0.16*sighK+0.1*drop+0.03*sw; d[Head+2]=0.06*sw; d[Head+1]=0.05*std::sin(t*0.7);
        d[ClavL+2]=-0.12+0.2*sighK; d[ClavR+2]=0.12-0.2*sighK; d[ClavL+1]=-0.1; d[ClavR+1]=0.1;
        d[UArmL]=0.1+0.03*sw; d[UArmL+2]=0.05; d[FArmL]=-0.12; d[UArmR]=0.1-0.03*sw; d[UArmR+2]=-0.05; d[FArmR]=-0.12;
        d[Ikr]=0; d[Ikl]=0; d[FootL]=0.075; d[FootR]=-0.075; d[FootLR+1]=-0.18; d[FootRR+1]=0.18;
        d[FootLR]=0.12; d[FootL+2]=0.02; d[Kneel]=-0.2; d[Kneer]=0.2;
        d[MCurve]=-0.9; d[MWidth]=0.8; d[Brow]=0.75; d[BrowY]=-0.3; d[Eye]=0.45+0.25*sighK;
        d[LookY]=-0.4; d[LookX]=0.1*sw; d[MOpen]=0.3*sighK; d[HandPL]=1.15; d[Ears]=-1.0+0.2*sighK;
    }

    void danceDefeatSulk(std::array<double,CharacterPoseChannelCount>& d,double t) const {
        const auto& h=GetCharacterWeaponHold(weapon); const double c=std::fmod(t,6.5);
        const double st1=win(c,0.1,0.25,0.3,0.42),st2=win(c,0.55,0.7,0.75,0.87),stomp=std::max(st1,st2);
        const double impulse=(c>0.42&&c<0.6?std::exp(-(c-0.42)*16.0):0.0)+(c>0.87&&c<1.05?std::exp(-(c-0.87)*16.0):0.0);
        const double turn=win(c,1.0,1.5,5.7,6.4),glance=win(c,3.2,3.45,3.9,4.15);
        const int left=find("footL"); const double ankleH=left>=0?rig->bones[static_cast<std::size_t>(left)].rest.y:0.085;
        setE(d,FootL,0.095,ankleH,0.01); setE(d,FootR,-0.1,ankleH+0.14*stomp,0.03*stomp);
        d[FootLR+1]=0.12; d[FootRR+1]=-0.2; d[FootRR]=-0.2*stomp;
        d[HipsP+1]=-0.035-0.05*impulse-0.02*stomp; d[Spine]=0.1+0.15*impulse; d[Chest]=0.05; d[Hips+2]=-0.06*stomp;
        d[UArmL]=0.15; d[UArmL+2]=0.2; d[FArmL]=-0.5; d[ClavL+2]=0.12*stomp; d[ClavR+2]=-0.12*stomp;
        setAnchor(d,h.carry); d[Anc+1]-=0.05*stomp; d[Ancr]+=0.4;
        d[ModelR+1]=-0.9*ease(turn); d[Ltw]=turn; setE(d,Ltgt,-0.06,0.9,0.13); lerpE(d,Polel,1,-0.3,0.2,turn); d[Ikl]=0;
        lerpE(d,Anc,-0.16,0.86,0.1,turn); lerpE(d,Ancr,1.1,0.8,0.4,turn);
        d[Chest]-=0.08*turn; d[Head]=-0.12*turn+0.2*impulse; d[Head+2]=0.1*turn;
        d[Head+1]=0.8*glance*turn-0.15*turn; d[Neck+1]=0.3*glance*turn;
        d[LookX]=0.35*glance-0.2*turn*(1.0-glance); d[MCurve]=-1.4; d[MWidth]=0.55; d[MTilt]=0.15;
        d[Brow]=-0.5+1.1*turn*(1.0-glance); d[BrowY]=-0.2; d[Eye]=0.75-0.3*impulse; d[Squint]=0.6*stomp; d[MOpen]=0.25*stomp;
        d[HandPL]=-1.0+(0.6+1.0)*ease(turn); d[Ears]=-0.6-0.4*turn+0.5*glance;
    }

    void danceDefeatKick(std::array<double,CharacterPoseChannelCount>& d,double t) const {
        const auto& h=GetCharacterWeaponHold(weapon); const double c=std::fmod(t,5.2);
        const double k1=win(c,0.6,0.85,0.95,1.2),k2=win(c,1.4,1.65,1.75,2.05),scuff=std::max(k1,k2);
        const double sighK=win(c,2.6,3.2,3.4,4.2),sniff=win(c,4.4,4.5,4.55,4.75);
        const int left=find("footL"); const double ankleH=left>=0?rig->bones[static_cast<std::size_t>(left)].rest.y:0.085;
        setE(d,FootL,0.085,ankleH,0.0); d[FootLR+1]=-0.1; d[FootRR+1]=-0.25;
        setE(d,FootR,-0.08,ankleH+0.03*scuff,-0.02+0.12*std::sin(3.14159265358979323846*clamp((c-(k2>k1?1.4:0.6))/0.65,0.0,1.0))*scuff);
        d[FootRR]=-0.3*scuff; d[HipsP+1]=-0.05; d[HipsP]=0.02; d[Hips+2]=0.05;
        d[Spine]=0.18-0.08*sighK; d[Chest]=0.13-0.1*sighK; d[Neck]=0.1; d[Head]=0.2-0.16*sighK-0.2*sniff;
        d[ClavL+2]=-0.1+0.18*sighK; d[ClavR+2]=0.1-0.18*sighK;
        d[UArmL]=0.05; d[UArmL+2]=0.04; d[FArmL]=-0.15; d[Ikl]=0;
        setAnchor(d,h.carry); d[Anc+1]-=0.12; d[Ancr]+=0.55; d[Anc+2]-=0.04;
        d[MCurve]=-1.0; d[MWidth]=0.7; d[Brow]=0.85; d[BrowY]=-0.2; d[Eye]=0.5+0.3*sniff;
        d[LookY]=-0.35; d[LookX]=-0.1*scuff; d[MOpen]=0.35*sighK; d[HandPL]=1.1; d[Ears]=-0.9+0.3*sniff;
    }

    void danceMenuIdle(std::array<double,CharacterPoseChannelCount>& d,double t) const {
        const auto& h=GetCharacterWeaponHold(weapon); const double cyc=std::fmod(t,8.0),w=std::sin(t*tau/8.0);
        const int left=find("footL"); const double ankleH=left>=0?rig->bones[static_cast<std::size_t>(left)].rest.y:0.085;
        const double hipW=(left>=0?std::abs(rig->bones[static_cast<std::size_t>(left)].rest.x):0.084)+0.008;
        d[HipsP]=0.028*w; d[Hips+2]=-0.05*w; d[Spine+2]=0.03*w; d[Chest+2]=0.015*w; d[HipsP+1]=-0.024-0.01*std::abs(w);
        setE(d,FootL,hipW+0.005,ankleH,-0.004); setE(d,FootR,-hipW-0.005,ankleH,-0.004); d[FootLR+1]=0.14; d[FootRR+1]=-0.14;
        if(w>0.0) { d[FootRR]=0.25*w; d[FootR+2]+=0.03*w; d[FootR]-=0.01*w; }
        else { d[FootLR]=-0.25*w; d[FootL+2]-=0.03*w; d[FootL]+=0.01*w; }
        const double yaw=keyedCatmull(cyc,menuTimes.data(),menuValues.data(),menuTimes.size()),pitch=-0.08*std::sin(cyc*0.9);
        d[Head+1]=yaw*0.7; d[Neck+1]=yaw*0.3; d[Head]=pitch; d[Head+2]=-0.05*w;
        const double yawL=keyedCatmull(cyc+0.25,menuTimes.data(),menuValues.data(),menuTimes.size());
        d[LookX]=clamp(yawL*0.9,-0.35,0.35); d[LookY]=-pitch*0.5; setAnchor(d,h.carry); d[Ikl]=h.twoCarry?1.0:0.0;
        const double tw=cyc>=6.1&&cyc<7.1?(cyc-6.1): -1.0;
        if(tw>=0.0&&!noDanceTwirl()) {
            const double k=ease(clamp(tw/0.8,0.0,1.0)),lift=std::sin(3.14159265358979323846*clamp(tw,0.0,1.0));
            d[Spin]=wrapAngle(tau*2.0*k); d[Ikl]=0; d[Anc+1]+=0.1*lift; d[Anc+2]+=0.08*lift; d[Anc]-=0.03*lift; d[Ancr]-=0.5*lift;
            d[MCurve]=1; d[MOpen]=0.25*lift;
        } else if(tw>=0.0) {
            const double lift=std::sin(3.14159265358979323846*clamp(tw,0.0,1.0)); d[Anc+1]+=0.06*lift; d[Ancr]-=0.25*lift;
        }
        d[UArmL]=-0.05; d[UArmL+2]=0.12; d[FArmL]=-0.35; d[MCurve]=std::max(d[MCurve],0.8); d[Ears]=0.15+0.2*std::abs(yaw);
    }

    void danceLocker(std::array<double,CharacterPoseChannelCount>& d,double t) const {
        const auto& h=GetCharacterWeaponHold(weapon); const double w=std::sin(t*tau/6.5),cyc=std::fmod(t,9.0);
        const int left=find("footL"); const double ankleH=left>=0?rig->bones[static_cast<std::size_t>(left)].rest.y:0.085;
        setE(d,FootL,0.12,ankleH,0.02); setE(d,FootR,-0.115,ankleH,-0.02); d[FootLR+1]=0.26; d[FootRR+1]=-0.24;
        if(w>0.0) { d[FootRR]=0.22*w; d[FootR+2]+=0.02*w; } else { d[FootLR]=-0.22*w; d[FootL+2]-=0.02*w; }
        d[HipsP]=0.03*w; d[HipsP+1]=-0.035-0.012*std::abs(w); d[Hips+2]=0.075*w; d[Hips+1]=0.05*w;
        d[Spine+2]=-0.045*w; d[Chest+2]=-0.03*w; d[Chest]=-0.03; d[ClavL+2]=0; d[ClavR+2]=0;
        const double tilt=win(cyc,3.2,3.6,4.6,5.1),look=win(cyc,6.2,6.6,7.2,7.7);
        d[Head+2]=-0.04*w-0.14*tilt; d[Head]=-0.04; d[Head+1]=0.25*look; d[LookX]=0.28*look; d[LookY]=0.05;
        d[Ltw]=1; setE(d,Ltgt,0.17,0.745,-0.005); d[Ikl]=0;
        d[UArmL+2]=0.9; d[FArmL]=-1.6; d[HandL]=0.3; d[HandL+2]=0.6; d[Polel]=1; d[Polel+1]=0.1; d[Polel+2]=-0.35;
        setAnchor(d,h.carry); if(h.twoCarry) { d[Ikl]=0; lerpE(d,Anc,-0.19,0.74,0.12,1.0); }
        d[MCurve]=0.95; d[MTilt]=0.12*tilt; d[BrowY]=0.2*tilt; d[Eye]=0.95; d[HandPL]=1.4; d[Ears]=0.3+0.3*tilt;
    }

    void danceLobby(std::array<double,CharacterPoseChannelCount>& d,double t) const {
        const auto& h=GetCharacterWeaponHold(weapon); const double cyc=std::fmod(t,7.0),fl=win(cyc,4.6,4.9,5.6,6.1);
        const int left=find("footL"); const double ankleH=left>=0?rig->bones[static_cast<std::size_t>(left)].rest.y:0.085;
        setE(d,FootL,0.15,ankleH,0.02); setE(d,FootR,-0.15,ankleH,-0.02); d[FootLR+1]=0.3; d[FootRR+1]=-0.28;
        d[HipsP+1]=-0.05; d[HipsP]=0.012; d[Hips+2]=-0.05; d[Hips+1]=0.08;
        d[Spine+1]=-0.05; d[Spine+2]=0.03; d[Chest]=-0.1; d[Chest+1]=-0.08;
        d[Head]=-0.1; d[Head+2]=-0.1; d[Head+1]=0.12; d[Neck+1]=0.05; setAnchor(d,h.lobby);
        d[Poler]=-0.9; d[Poler+1]=-0.4; d[Poler+2]=-0.1;
        if(h.lobbyTwo) { d[Ikl]=1; d[Ltw]=0; d[Chest+1]=0.05; }
        else {
            d[Ltw]=1; setE(d,Ltgt,0.17,0.745,-0.005); d[Ikl]=0;
            d[UArmL+2]=0.9; d[FArmL]=-1.6; d[HandL]=0.3; d[HandL+2]=0.6; d[Polel]=1; d[Polel+1]=0.1; d[Polel+2]=-0.35;
        }
        const auto kind=GetCharacterWeaponHold(weapon).animKind;
        if(kind=="shooter"||kind=="blaster") { d[Spin]=wrapAngle(tau*ease((cyc-4.8)/0.55))*((cyc>4.8&&cyc<5.5)?1.0:0.0); d[Anc+1]+=0.05*fl; }
        else d[Ancr]-=0.2*fl;
        d[Head]-=0.08*fl; d[HipsP+1]-=0.015*fl; d[MCurve]=1; d[MTilt]=0.22; d[Brow]=-0.25; d[Eye]=0.92;
        d[LookX]=-0.12; d[LookY]=0.04; d[MOpen]=0.2*fl; d[HandPL]=h.lobbyTwo?0.0:1.5; d[Ears]=0.45+0.4*fl;
    }

    int find(const std::string& name) const {
        for (std::size_t i = 0; i < rig->bones.size(); ++i) if (rig->bones[i].name == name) return static_cast<int>(i);
        return -1;
    }

    static glm::dquat alignVector(const glm::dvec3& from, const glm::dvec3& to) {
        const glm::dvec3 a=glm::normalize(from), b=glm::normalize(to);
        const double cosine=glm::dot(a,b);
        if(cosine < -0.999999) {
            glm::dvec3 axis=glm::cross(a,glm::dvec3(1,0,0));
            if(glm::dot(axis,axis)<1e-8) axis=glm::cross(a,glm::dvec3(0,0,1));
            return glm::angleAxis(3.14159265358979323846,glm::normalize(axis));
        }
        const glm::dvec3 axis=glm::cross(a,b);
        return glm::normalize(glm::dquat(1.0+cosine,axis.x,axis.y,axis.z));
    }

    static glm::dmat3 limbRestBasis(const glm::dvec3& direction, const glm::dvec3& guide) {
        const auto r=glm::normalize(direction);
        auto h=guide-r*glm::dot(guide,r);
        if(glm::dot(h,h)<1e-12) h=glm::dvec3(0,0,1)-r*r.z;
        h=glm::normalize(h);
        const auto c=glm::cross(r,h);
        glm::dmat3 basis(1.0); basis[0]=r; basis[1]=h; basis[2]=c;
        return glm::transpose(basis);
    }

    static std::array<glm::dquat,3> solveLimbRotations(
        const CharacterBone& upper, const CharacterBone& lower, const CharacterBone& end,
        const glm::dvec3& upperOffset, const glm::dvec3& parentPosition, const glm::dquat& parentRotation,
        const glm::dvec3& target, const glm::dvec3& pole, const glm::dquat& endRotation,
        const glm::dvec3& guide, const glm::dquat& baseUpper, const glm::dquat& baseLower,
        const glm::dquat& baseEnd, double weight=1.0, bool solveEndOrientation=true) {
        const glm::dvec3 origin=parentPosition+parentRotation*upperOffset;
        const double a=glm::length(lower.rest-upper.rest), b=glm::length(end.rest-lower.rest);
        glm::dvec3 delta=target-origin;
        double distance=glm::length(delta);
        if(a<1e-8||b<1e-8||distance<1e-8) return {baseUpper,baseLower,baseEnd};
        const double minDistance=std::abs(a-b)+1e-3, maxDistance=(a+b)*0.9995;
        distance=clamp(distance,minDistance,maxDistance);
        const auto direction=glm::normalize(delta);
        const double cosA=clamp((a*a+distance*distance-b*b)/(2.0*a*distance),-1.0,1.0), sinA=std::sqrt(1.0-cosA*cosA);
        glm::dvec3 bend=pole-direction*glm::dot(pole,direction);
        if(glm::dot(bend,bend)<1e-8) bend=glm::dvec3(0,0,1)-direction*direction.z;
        bend=glm::normalize(bend);
        const glm::dvec3 knee=origin+direction*(a*cosA)+bend*(a*sinA), distal=origin+direction*distance;
        const glm::dvec3 plane=glm::normalize(glm::cross(bend,direction));
        const auto makeWorld=[&](const glm::dvec3& axis,const glm::dvec3& restDirection) {
            const auto by=glm::normalize(axis), bz=glm::cross(by,plane);
            glm::dmat3 basis(1.0); basis[0]=by; basis[1]=plane; basis[2]=bz;
            return glm::normalize(glm::quat_cast(basis*limbRestBasis(restDirection,guide)));
        };
        const auto wantedUpper=makeWorld(knee-origin,lower.rest-upper.rest);
        const auto wantedLower=makeWorld(distal-knee,end.rest-lower.rest);
        const auto upperLocal=glm::normalize(glm::slerp(baseUpper,glm::inverse(parentRotation)*wantedUpper,weight));
        const auto lowerParent=glm::normalize(parentRotation*upperLocal);
        const auto lowerLocal=glm::normalize(glm::slerp(baseLower,glm::inverse(lowerParent)*wantedLower,weight));
        const auto endParent=glm::normalize(lowerParent*lowerLocal);
        const auto endLocal=solveEndOrientation
            ?glm::normalize(glm::slerp(baseEnd,glm::inverse(endParent)*endRotation,weight)):baseEnd;
        return {upperLocal,lowerLocal,endLocal};
    }

    void prepareLegTargets(double dt, const CharacterAnimState& input) {
        const auto leftFoot=find("footL"), rightFoot=find("footR"), hipsIndex=find("hips");
        const double ankleHeight=leftFoot>=0?rig->bones[static_cast<std::size_t>(leftFoot)].rest.y:0.085;
        const auto kidRotation=glm::normalize(frame.kid.rotation), inverseKid=glm::inverse(kidRotation);
        const double sx=std::max(1e-3,frame.kid.scale.x), sy=std::max(1e-3,frame.kid.scale.y);
        const double c=std::cos(yaw), sn=std::sin(yaw);
        const auto hipsRotation=euler({p[Hips],p[Hips+1],p[Hips+2]},true);
        glm::dvec3 hipsPosition=hipsIndex>=0?rig->bones[static_cast<std::size_t>(hipsIndex)].rest:glm::dvec3(0.0);
        hipsPosition+=glm::dvec3(p[HipsP],p[HipsP+1],p[HipsP+2]);

        for(int i=0;i<2;++i) {
            const auto& f=feet[static_cast<std::size_t>(i)];
            const std::size_t footChannel=i==0?FootL:FootR, rotationChannel=i==0?FootLR:FootRR, plantChannel=i==0?Wpl:Wpr;
            const double weight=p[plantChannel]*plantWeight*(feetValid?1.0:0.0);
            glm::dvec3 target{p[footChannel],p[footChannel+1],p[footChannel+2]};
            glm::dquat targetRotation=euler({p[rotationChannel],p[rotationChannel+1],p[rotationChannel+2]},true);
            if(weight>0.001) {
                const glm::dvec3 delta=f.contact-input.position;
                const glm::dvec3 rootPoint{delta.x*c-delta.z*sn,delta.y,delta.x*sn+delta.z*c};
                const double relativeYaw=f.contactYaw-yaw;
                double pitch=f.pitch;
                if(!moving&&i==(shiftTarget>0.0?1:0)) pitch+=0.1*std::abs(shift)*(1.0-gaitWeight);
                pitch+=0.55*std::max(p[Tiptoe],0.0);
                const glm::dvec3 normalRoot{f.contactNormal.x*c-f.contactNormal.z*sn,f.contactNormal.y,f.contactNormal.x*sn+f.contactNormal.z*c};
                glm::dquat groundRotation=alignVector({0,1,0},normalRoot)*glm::angleAxis(relativeYaw,glm::dvec3(0,1,0));
                double ay, az;
                if(pitch>=0.0) { ay=ankleHeight*std::cos(pitch)+0.11*std::sin(pitch); az=0.11+ankleHeight*std::sin(pitch)-0.11*std::cos(pitch); }
                else { ay=ankleHeight*std::cos(pitch)-0.065*std::sin(pitch); az=-0.065+ankleHeight*std::sin(pitch)+0.065*std::cos(pitch); }
                const glm::dvec3 ankleRoot=rootPoint+groundRotation*glm::dvec3(0,ay,az);
                target=inverseKid*(ankleRoot-frame.kid.position);
                target.x/=sx; target.y/=sy; target.z/=sx;
                targetRotation=glm::normalize(inverseKid*groundRotation*glm::angleAxis(pitch,glm::dvec3(1,0,0)));
                legTargets[static_cast<std::size_t>(i)]=glm::mix(glm::dvec3(p[footChannel],p[footChannel+1],p[footChannel+2]),target,weight);
                legTargetRotations[static_cast<std::size_t>(i)]=glm::normalize(glm::slerp(euler({p[rotationChannel],p[rotationChannel+1],p[rotationChannel+2]},true),targetRotation,weight));
            } else {
                legTargets[static_cast<std::size_t>(i)]=target;
                legTargetRotations[static_cast<std::size_t>(i)]=targetRotation;
            }
        }

        double drop=0.0;
        for(int i=0;i<2;++i) {
            const int upper=find(i==0?"thighL":"thighR");
            if(upper<0) continue;
            const auto& foot=feet[static_cast<std::size_t>(i)];
            if(plantWeight>0.5&&p[i==0?Wpl:Wpr]>0.5&&foot.swinging&&foot.swingU>0.02&&foot.swingU<0.9) continue;
            const auto upperLocal=rig->bones[static_cast<std::size_t>(upper)].rest-rig->bones[static_cast<std::size_t>(hipsIndex)].rest;
            const glm::dvec3 origin=hipsPosition+hipsRotation*upperLocal;
            const double hx=legTargets[static_cast<std::size_t>(i)].x-origin.x, hz=legTargets[static_cast<std::size_t>(i)].z-origin.z;
            const int lower=find(i==0?"shinL":"shinR"), end=find(i==0?"footL":"footR");
            if(lower<0||end<0) continue;
            const double a=glm::length(rig->bones[static_cast<std::size_t>(lower)].rest-rig->bones[static_cast<std::size_t>(upper)].rest);
            const double b=glm::length(rig->bones[static_cast<std::size_t>(end)].rest-rig->bones[static_cast<std::size_t>(lower)].rest);
            const double reach=(a+b)*0.985, vertical=std::sqrt(std::max(0.0,reach*reach-hx*hx-hz*hz));
            drop=std::max(drop,origin.y-(legTargets[static_cast<std::size_t>(i)].y+vertical));
        }
        drop=std::min(drop,0.26);
        hipDrop=drop>hipDrop?damp(hipDrop,drop,40.0,dt):damp(hipDrop,drop,16.0,dt);
        appliedHipDrop=std::max(drop*0.85,hipDrop);
        hipsPosition.y-=appliedHipDrop;
        for(int i=0;i<2;++i) {
            const int upper=find(i==0?"thighL":"thighR"), lower=find(i==0?"shinL":"shinR"), end=find(i==0?"footL":"footR");
            if(upper<0||lower<0||end<0||hipsIndex<0) continue;
            const auto& upperRest=rig->bones[static_cast<std::size_t>(upper)];
            const auto& lowerRest=rig->bones[static_cast<std::size_t>(lower)];
            const auto& endRest=rig->bones[static_cast<std::size_t>(end)];
            const auto upperLocal=upperRest.rest-rig->bones[static_cast<std::size_t>(hipsIndex)].rest;
            const auto origin=hipsPosition+hipsRotation*upperLocal;
            const double maxReach=(glm::length(lowerRest.rest-upperRest.rest)+glm::length(endRest.rest-lowerRest.rest))*0.985*0.97;
            auto reachDelta=legTargets[static_cast<std::size_t>(i)]-origin;
            const double reachDistance=glm::length(reachDelta);
            if(reachDistance>maxReach) legTargets[static_cast<std::size_t>(i)]=origin+reachDelta*(maxReach/reachDistance);
        }
        for(int i=0;i<2;++i) {
            const int upper=find(i==0?"thighL":"thighR"), lower=find(i==0?"shinL":"shinR"), end=find(i==0?"footL":"footR");
            if(upper<0||lower<0||end<0||hipsIndex<0) continue;
            const auto& upperRest=rig->bones[static_cast<std::size_t>(upper)];
            const auto& lowerRest=rig->bones[static_cast<std::size_t>(lower)];
            const auto& endRest=rig->bones[static_cast<std::size_t>(end)];
            const auto upperLocal=upperRest.rest-rig->bones[static_cast<std::size_t>(hipsIndex)].rest;
            const auto origin=hipsPosition+hipsRotation*upperLocal;
            glm::dvec3 delta=legTargets[static_cast<std::size_t>(i)]-origin;
            double distance=glm::length(delta);
            const double a=glm::length(lowerRest.rest-upperRest.rest), b=glm::length(endRest.rest-lowerRest.rest);
            if(a<1e-8||b<1e-8||distance<1e-8) continue;
            const double minDistance=std::abs(a-b)+1e-3, maxDistance=(a+b)*0.9995;
            distance=clamp(distance,minDistance,maxDistance);
            const auto direction=glm::normalize(delta);
            const double cosA=clamp((a*a+distance*distance-b*b)/(2.0*a*distance),-1.0,1.0), sinA=std::sqrt(1.0-cosA*cosA);
            const glm::dvec3 forward=legTargetRotations[static_cast<std::size_t>(i)]*glm::dvec3(0,0,1);
            const double side=i==0?1.0:-1.0, hipYaw=p[Hips+1];
            const std::size_t knee=i==0?Kneel:Kneer;
            glm::dvec3 pole{forward.x*0.7+std::sin(hipYaw)*0.3+0.1*side+p[knee],0.05+p[knee+1],forward.z*0.7+std::cos(hipYaw)*0.3+p[knee+2]};
            glm::dvec3 bend=pole-direction*glm::dot(pole,direction);
            if(glm::dot(bend,bend)<1e-8) bend=glm::dvec3(0,0,1)-direction*direction.z;
            bend=glm::normalize(bend);
            const glm::dvec3 kneePoint=origin+direction*(a*cosA)+bend*(a*sinA);
            const glm::dvec3 target=origin+direction*distance;
            const glm::dvec3 planeNormal=glm::normalize(glm::cross(bend,direction));
            const auto makeWorldRotation=[&](const glm::dvec3& axis, const glm::dvec3& restDirection) {
                const auto y=glm::normalize(axis);
                const auto z=glm::cross(y,planeNormal);
                glm::dmat3 basis(1.0); basis[0]=y; basis[1]=planeNormal; basis[2]=z;
                return glm::normalize(glm::quat_cast(basis*limbRestBasis(restDirection,{1,0,0})));
            };
            const auto upperWorld=makeWorldRotation(kneePoint-origin,lowerRest.rest-upperRest.rest);
            const auto lowerWorld=makeWorldRotation(target-kneePoint,endRest.rest-lowerRest.rest);
            const auto upperLocalQ=glm::normalize(glm::inverse(hipsRotation)*upperWorld);
            const auto lowerLocalQ=glm::normalize(glm::inverse(hipsRotation*upperLocalQ)*lowerWorld);
            const auto footLocalQ=glm::normalize(glm::inverse(hipsRotation*upperLocalQ*lowerLocalQ)*legTargetRotations[static_cast<std::size_t>(i)]);
            legRotations[static_cast<std::size_t>(i)]={upperLocalQ,lowerLocalQ,footLocalQ};
        }
    }

    void updateHair(double dt, bool kidForm) {
        if(hair.empty()) return;
        constexpr glm::dvec3 headCenterOffset{0.0,0.164,0.014};
        const glm::dmat4 kidWorld=frame.root.matrix()*frame.model.matrix()*frame.kid.matrix();
        const glm::dvec3 headCenterKid=headKidPosition+headKidRotation*headCenterOffset;
        const glm::dvec3 headCenterWorld=glm::dvec3(kidWorld*glm::dvec4(headCenterKid,1.0));
        const glm::dquat kidWorldRotation=threeDecomposeQuaternion(kidWorld);
        const glm::dquat headWorldRotation=kidWorldRotation*headKidRotation;
        if(!headInitialized||dt<=0.0) {
            headPrevPosition=headCenterWorld; headPrevVelocity=glm::dvec3(0.0); headVelocity=glm::dvec3(0.0);
            headPrevWorldRotation=headWorldRotation; headInitialized=true;
            if(dt<=0.0) return;
        }
        glm::dvec3 measuredVelocity=(headCenterWorld-headPrevPosition)/dt;
        const double speedSquared=glm::dot(measuredVelocity,measuredVelocity);
        if(speedSquared>900.0) measuredVelocity*=30.0/std::sqrt(speedSquared);
        glm::dvec3 measuredAcceleration=(measuredVelocity-headPrevVelocity)/dt;
        const double accelerationLength=glm::length(measuredAcceleration);
        if(accelerationLength>34.0) measuredAcceleration*=34.0/accelerationLength;
        headAcceleration=glm::mix(headAcceleration,measuredAcceleration,1.0-std::exp(-dt*24.0));
        headVelocity=glm::mix(headVelocity,measuredVelocity,1.0-std::exp(-dt*12.0));
        headPrevPosition=headCenterWorld; headPrevVelocity=measuredVelocity;

        glm::dvec3 apparentGravity{0.0,-9.8,0.0};
        apparentGravity-=headAcceleration;
        apparentGravity+=headVelocity*(-0.55);
        apparentGravity=glm::conjugate(headWorldRotation)*apparentGravity;
        apparentGravity.y+=9.8;

        glm::dquat delta=glm::conjugate(headPrevWorldRotation)*headWorldRotation;
        if(delta.w<0.0) delta=-delta;
        const double angle=2.0*std::acos(clamp(delta.w,-1.0,1.0));
        const double sinHalf=std::sqrt(std::max(1e-12,1.0-delta.w*delta.w));
        const double rx=(delta.x/sinHalf)*angle, ry=(delta.y/sinHalf)*angle, rz=(delta.z/sinHalf)*angle;
        headRotationY=clamp(ry,-0.2,0.2); headRotationZ=clamp(rz,-0.2,0.2);
        headPrevWorldRotation=headWorldRotation;

        const int steps=std::min(4,std::max(1,static_cast<int>(std::ceil(dt*60.0-0.25))));
        const double h=dt/static_cast<double>(steps);
        constexpr double gain0=0.5, gain1=0.3, gain2=0.2;
        constexpr double HK=0.62, HZ=0.3, HINE=1.15, HGK=0.85, HCL=0.68, HBR=0.6;
        const double idleL=(1.0-gaitWeight)*(1.0-airWeight)*(kidForm?1.0:0.0);
        const double wave= tau*0.42*time;
        const auto f32=[](double value) { return static_cast<double>(static_cast<float>(value)); };
        const auto f32v=[&](const glm::dvec3& value) { return glm::dvec3(f32(value.x),f32(value.y),f32(value.z)); };
        for(std::size_t si=0;si<hair.size();++si) {
            auto& strand=hair[si];
            const auto& u=strand.direction;
            glm::dvec3 torque=glm::cross(u,apparentGravity);
            const double gk=(strand.gravity*0.075*HGK)*clamp(strand.length/0.22,0.4,1.6);
            torque.x*=gk; torque.y*=gk*0.3; torque.z*=gk;
            const double torqueLength=glm::length(torque);
            if(torqueLength>0.62) torque*=0.62/torqueLength;
            const auto into=strand.intoHead;
            const auto w=glm::cross(u,into); const double ww=glm::dot(w,w);
            const double breeze=HBR*(0.03*std::sin(time*1.7+static_cast<double>(si)*1.3)+0.012*std::sin(time*4.3+static_cast<double>(si)*2.1));
            for(int k=0;k<3;++k) {
                const std::size_t segment=static_cast<std::size_t>(k);
                auto& x=strand.x[segment]; auto& velocity=strand.velocity[segment];
                const double gain=k==0?gain0:k==1?gain1:gain2;
                const double inertia=(k==0?0.55:k==1?0.3:0.15)*clamp(1.2-strand.stiffness*0.3,0.3,1.0)*HINE;
                x.x=f32(x.x-rx*inertia); x.y=f32(x.y-ry*inertia*0.5); x.z=f32(x.z-rz*inertia);
                const double springK=170.0*HK*strand.stiffness*(k==0?1.0:k==1?0.75:0.55)/clamp(strand.length/0.22,0.6,1.6);
                const double damping=2.0*HZ*std::sqrt(springK);
                glm::dvec3 target=torque*gain+glm::dvec3(breeze*0.3,0.0,breeze*0.5);
                if(idleL>0.01) {
                    const double waveAmount=idleL*0.03*std::sin(wave-0.6*k+strand.phase)*clamp(1.6-strand.stiffness*0.4,0.3,1.0);
                    target+=(strand.bendSide[segment]*0.8+strand.bendIn[segment]*0.35)*waveAmount;
                }
                for(int n=0;n<steps;++n) {
                    velocity.x=f32(velocity.x+(springK*(target.x-x.x)-damping*velocity.x)*h);
                    velocity.y=f32(velocity.y+(springK*(target.y-x.y)-damping*velocity.y)*h);
                    velocity.z=f32(velocity.z+(springK*(target.z-x.z)-damping*velocity.z)*h);
                    x.x=f32(x.x+velocity.x*h); x.y=f32(x.y+velocity.y*h); x.z=f32(x.z+velocity.z*h);
                }
                if(ww>1e-6) {
                    const double intoAmount=glm::dot(x,w)/ww;
                    if(intoAmount>0.05) {
                        const double excess=intoAmount-0.05; x=f32v(x-w*excess);
                        const double inwardVelocity=glm::dot(velocity,w)/ww;
                        if(inwardVelocity>0.0) velocity=f32v(velocity-w*inwardVelocity);
                    }
                }
                x.x=f32(clamp(x.x,-HCL,HCL)); x.y=f32(clamp(x.y,-HCL,HCL)); x.z=f32(clamp(x.z,-HCL,HCL));
                const auto axis=strand.axis[segment];
                const double twist=glm::dot(x,axis), excess=twist-clamp(twist,-0.12,0.12);
                x=f32v(x-axis*excess);
                const double twistVelocity=glm::dot(velocity,axis);
                velocity=f32v(velocity-axis*twistVelocity);
                const double bend=glm::length(x);
                strand.rotation[segment]=bend>1e-6?glm::angleAxis(bend,x/bend):glm::dquat(1.0,0.0,0.0,0.0);
            }
            auto& tipX=strand.tipX[0]; auto& tipVelocity=strand.tipVelocity[0];
            tipX.x=f32(tipX.x-rx*0.12); tipX.y=f32(tipX.y-ry*0.06); tipX.z=f32(tipX.z-rz*0.12);
            const double tipK=95.0*0.7*strand.stiffness/clamp(strand.length/0.22,0.6,1.6);
            const double tipD=2.0*0.24*std::sqrt(tipK);
            const double coil=idleL*0.08*std::sin(wave*0.95+strand.phase*1.7-1.8);
            const auto tipAxis=strand.axis[3], tipBend=strand.bendIn[3];
            const auto last=strand.x[2];
            const glm::dvec3 target{last.x*0.45+torque.x*0.12+breeze*0.25+tipBend.x*coil,
                last.y*0.3+torque.y*0.1+tipBend.y*coil,
                last.z*0.45+torque.z*0.12+breeze*0.35+tipBend.z*coil};
            for(int n=0;n<steps;++n) {
                tipVelocity.x=f32(tipVelocity.x+(tipK*(target.x-tipX.x)-tipD*tipVelocity.x)*h);
                tipVelocity.y=f32(tipVelocity.y+(tipK*(target.y-tipX.y)-tipD*tipVelocity.y)*h);
                tipVelocity.z=f32(tipVelocity.z+(tipK*(target.z-tipX.z)-tipD*tipVelocity.z)*h);
                tipX.x=f32(tipX.x+tipVelocity.x*h); tipX.y=f32(tipX.y+tipVelocity.y*h); tipX.z=f32(tipX.z+tipVelocity.z*h);
            }
            tipX.x=f32(clamp(tipX.x,-0.4,0.4)); tipX.y=f32(clamp(tipX.y,-0.4,0.4)); tipX.z=f32(clamp(tipX.z,-0.4,0.4));
            const double tipTwist=glm::dot(tipX,tipAxis), tipExcess=tipTwist-clamp(tipTwist,-0.12,0.12);
            tipX=f32v(tipX-tipAxis*tipExcess);
            const double tipBendAmount=glm::length(tipX);
            strand.rotation[3]=tipBendAmount>1e-6?glm::angleAxis(tipBendAmount,tipX/tipBendAmount):glm::dquat(1.0,0.0,0.0,0.0);
        }
    }

    void updateBones(int lifeLevel) {
        std::array<glm::dmat4, CharacterBoneCount> world;
        std::array<glm::dquat, CharacterBoneCount> worldRotation;
        armRightSolved=false;
        armLeftSolved=false;
        armRightRotations={euler({p[UArmR],p[UArmR+1],p[UArmR+2]},false),
            euler({p[FArmR],p[FArmR+1],p[FArmR+2]},false),euler({p[HandR],p[HandR+1],p[HandR+2]},false)};
        armLeftRotations={euler({p[UArmL],p[UArmL+1],p[UArmL+2]},false),
            euler({p[FArmL],p[FArmL+1],p[FArmL+2]},false),euler({p[HandL],p[HandL+1],p[HandL+2]},false)};
        glm::dvec3 leftClavEuler{p[ClavL],p[ClavL+1],p[ClavL+2]};
        for (std::size_t i = 0; i < rig->bones.size(); ++i) {
            const auto& b = rig->bones[i];
            CharacterTransform local;
            local.position = b.parent < 0 ? b.rest : b.rest - rig->bones[static_cast<std::size_t>(b.parent)].rest;
            const auto setRotation = [&](std::size_t channel, bool yxz) { local.rotation = euler({p[channel], p[channel+1], p[channel+2]}, yxz); };
            if (b.name == "hips") { local.position += glm::dvec3(p[HipsP], p[HipsP+1], p[HipsP+2]); local.position.y -= appliedHipDrop; setRotation(Hips, true); }
            else if (b.name == "spine") setRotation(Spine, true);
            else if (b.name == "chest") setRotation(Chest, true);
            else if (b.name == "neck") setRotation(Neck, true);
            else if (b.name == "head") setRotation(Head, true);
            else if (b.name == "earL") {
                const double hd=style.hat==3?-0.35:style.hat==1?-0.3:0.0;
                local.rotation=glm::angleAxis(clamp(springs[SEarL],hd!=0.0?hd-0.3:-0.45,hd!=0.0?hd+0.08:0.35),glm::dvec3(0,0,1));
            }
            else if (b.name == "earR") {
                const double hd=style.hat==3?-0.35:style.hat==1?-0.3:0.0;
                local.rotation=glm::angleAxis(-clamp(springs[SEarR],hd!=0.0?hd-0.3:-0.45,hd!=0.0?hd+0.08:0.35),glm::dvec3(0,0,1));
            }
            else if (b.name == "hemF") local.rotation=euler({hemFront,0.0,hemSide},false);
            else if (b.name == "hemB") local.rotation=euler({hemBack,0.0,hemSide},false);
            else if (b.name == "hem") local.rotation=euler({hemCenter,0.0,hemSide},false);
            else if (b.name == "tank") { local.position.y+=tankY; local.rotation=euler({tankRotX,0.0,tankRotZ},false); }
            else if (b.name == "clavL") setRotation(ClavL, true);
            else if (b.name == "clavR") setRotation(ClavR, true);
            else if (b.name == "uArmL") { if(armLeftSolved) local.rotation=armLeftRotations[0]; else setRotation(UArmL, false); }
            else if (b.name == "uArmR") { if(armRightSolved) local.rotation=armRightRotations[0]; else setRotation(UArmR, false); }
            else if (b.name == "fArmL") { if(armLeftSolved) local.rotation=armLeftRotations[1]; else setRotation(FArmL, false); }
            else if (b.name == "fArmR") { if(armRightSolved) local.rotation=armRightRotations[1]; else setRotation(FArmR, false); }
            else if (b.name == "handL") { if(armLeftSolved) local.rotation=armLeftRotations[2]; else setRotation(HandL, false); }
            else if (b.name == "handR") { if(armRightSolved) local.rotation=armRightRotations[2]; else setRotation(HandR, false); }
            else if (b.name == "footL") local.rotation=legRotations[0][2];
            else if (b.name == "footR") local.rotation=legRotations[1][2];
            else if (b.name == "toeL"||b.name=="toeR") {
                const int side=b.name=="toeL"?0:1;
                const std::size_t foot=side==0?FootL:FootR, footRotation=side==0?FootLR:FootRR, plant=side==0?Wpl:Wpr;
                const auto& state=feet[static_cast<std::size_t>(side)];
                const int ankle=find(side==0?"footL":"footR");
                const double ankleHeight=ankle>=0?rig->bones[static_cast<std::size_t>(ankle)].rest.y:0.085;
                const double onGround=state.planted||!state.swinging?1.0:std::max(0.0,1.0-state.swingU*5.0);
                const double posed=p[footRotation]*sstep(0.05,0.005,p[foot+1]-ankleHeight);
                const double weight=p[plant]*plantWeight*(feetValid?1.0:0.0);
                local.rotation=glm::angleAxis(-clamp(posed+(state.pitch*onGround-posed)*weight,0.0,0.5),glm::dvec3(1,0,0));
            }
            else if (b.name == "thighL") local.rotation=legRotations[0][0];
            else if (b.name == "thighR") local.rotation=legRotations[1][0];
            else if (b.name == "shinL") local.rotation=legRotations[0][1];
            else if (b.name == "shinR") local.rotation=legRotations[1][1];
            if(hairStrandByBone[i]>=0) {
                const auto strand=static_cast<std::size_t>(hairStrandByBone[i]);
                const auto segment=static_cast<std::size_t>(hairSegmentByBone[i]);
                local.rotation=hair[strand].rotation[segment];
            }
            if (b.name == "head" && b.parent >= 0 && p[Stab] > 0.001) {
                const double torsoPitch=p[Hips]+p[Spine]+p[Chest]+p[Neck];
                const double torsoRoll=p[Hips+2]+p[Spine+2]+p[Chest+2];
                const auto target=euler({-p[Hlp]+p[Head]*0.5+torsoPitch*0.4,p[Hly],p[Head+2]+torsoRoll*0.35},true);
                const auto parentRotation=worldRotation[static_cast<std::size_t>(b.parent)];
                local.rotation=glm::slerp(local.rotation,glm::inverse(parentRotation)*target,clamp(p[Stab],0.0,1.0));
            }
            const double sqz=clamp(p[Squint]+0.35*blinkSqueeze*std::max(blinkLeft,blinkRight),0.0,1.0);
            const double smile=clamp((p[MCurve]-0.3)/0.6,0.0,1.0);
            const double openL=eyeScaleLeft, openR=eyeScaleRight;
            const double bra=clamp(p[Bras],-1.0,1.0);
            if(b.name=="browL") { local.rotation=glm::angleAxis(-p[Brow]*0.42-0.12*bra,glm::dvec3(0,0,1)); local.position.y+=(p[BrowY]+0.6*bra)*0.008-0.004*sqz+(1.0-openL)*-0.004; }
            else if(b.name=="browR") { local.rotation=glm::angleAxis(p[Brow]*0.42-0.12*bra,glm::dvec3(0,0,1)); local.position.y+=(p[BrowY]-0.6*bra)*0.008-0.004*sqz+(1.0-openR)*-0.004; }
            else if(b.name=="jaw") local.rotation=glm::angleAxis(clamp(p[MOpen],0.0,1.0)*0.35,glm::dvec3(1,0,0));
            else if(b.name=="cheekL"||b.name=="cheekR") {
                const double mo=clamp(p[MOpen],0.0,1.0);
                const double cheekSmile=clamp(smile*(1.0-0.4*mo)+(p[MCurve]>0.3?clamp(mo-0.35,0.0,1.0)*0.9:0.0),0.0,1.0);
                const double pout=clamp((-p[MCurve]-1.2)/0.4,0.0,1.0);
                const double puff=clamp(std::max(cheekSmile,pout*0.85)+0.3*sqz,0.0,1.0);
                local.position.y+=0.0035*cheekSmile+0.001*pout; local.scale=glm::dvec3(1.0+0.06*puff);
            }
            else if(b.name=="lidL") local.rotation=glm::angleAxis((1.0-openL)*1.2,glm::dvec3(1,0,0));
            else if(b.name=="lidR") local.rotation=glm::angleAxis((1.0-openR)*1.2,glm::dvec3(1,0,0));
            if(b.name.rfind("handL_",0)==0||b.name.rfind("handR_",0)==0) {
                if(lifeLevel>0) {
                    glm::dquat fingerQ;
                    if(fingerRotation(b.name,fingerQ)) retainedFingerRotations[i]=fingerQ;
                }
                local.rotation=retainedFingerRotations[i];
            }
            const bool faceBone=b.name=="eyeL"||b.name=="eyeR"||b.name=="browL"||b.name=="browR"||
                b.name=="mouth"||b.name=="mouthO"||b.name=="jaw"||b.name=="cheekL"||b.name=="cheekR"||
                b.name=="lidL"||b.name=="lidR";
            const bool detailBone=faceBone||b.name=="earL"||b.name=="earR"||b.name=="hemF"||b.name=="hemB"||
                b.name=="hem"||b.name=="tank"||b.name=="toeL"||b.name=="toeR";
            if(detailBone) {
                if(b.name=="eyeL") local.scale=glm::dvec3(1.0+0.06*(1.0-eyeScaleLeft),eyeScaleLeft,1.0);
                else if(b.name=="eyeR") local.scale=glm::dvec3(1.0+0.06*(1.0-eyeScaleRight),eyeScaleRight,1.0);
                if(lifeLevel>0) retainedDetailLocals[i]=local;
                else local=retainedDetailLocals[i];
            }
            worldRotation[i]=b.parent<0?local.rotation:worldRotation[static_cast<std::size_t>(b.parent)]*local.rotation;
            if (b.parent < 0) world[i] = local.matrix();
            else world[i] = world[static_cast<std::size_t>(b.parent)] * local.matrix();
            if(b.name=="clavR"&&p[Ikr]>0.001) {
                const int chestIndex=find("chest"), upper=find("uArmR"), lower=find("fArmR"), end=find("handR");
                if(chestIndex>=0&&upper>=0&&lower>=0&&end>=0) {
                    glm::dvec3 anchor{p[Anc],p[Anc+1],p[Anc+2]};
                    glm::dquat anchorRotation=euler({p[Ancr],p[Ancr+1],p[Ancr+2]},true);
                    const auto chestQ=worldRotation[static_cast<std::size_t>(chestIndex)];
                    const auto followQ=glm::slerp(glm::dquat(1,0,0,0),chestQ,clamp(p[Afolr],0.0,1.0));
                    const glm::dvec3 chestP=glm::dvec3(world[static_cast<std::size_t>(chestIndex)][3]);
                    const auto followed=chestP+followQ*(anchor-rig->bones[static_cast<std::size_t>(chestIndex)].rest);
                    anchor=glm::mix(anchor,followed,clamp(p[Afolt],0.0,1.0));
                    anchorRotation=glm::normalize(followQ*anchorRotation);
                    const double swayWeight=(1.0-0.7*aimWeight)*(1.0-lockWeight);
                    anchor+=weaponSwayRawPosition*swayWeight;
                    anchorRotation=glm::normalize(euler(weaponSwayEuler*swayWeight,true)*anchorRotation);
                    anchor+=anchorRotation*glm::dvec3(0.0,0.0,-recoilBack);
                    anchorRotation=glm::normalize(anchorRotation*euler({-recoilPitch,springs[SRCy],springs[SRCr]},true));

                    const auto& weaponRig=GetCharacterWeaponRig(weapon);
                    const auto target=anchor+anchorRotation*weaponRig.handR.position;
                    const auto targetRotation=glm::normalize(anchorRotation*weaponRig.handR.rotation);
                    const auto parentPosition=glm::dvec3(world[i][3]);
                    armRightRotations=solveLimbRotations(rig->bones[static_cast<std::size_t>(upper)],
                        rig->bones[static_cast<std::size_t>(lower)],rig->bones[static_cast<std::size_t>(end)],
                        rig->bones[static_cast<std::size_t>(upper)].rest-b.rest,parentPosition,worldRotation[i],target,
                        {p[Poler],p[Poler+1],p[Poler+2]},targetRotation,{-1,0,0},armRightRotations[0],
                        armRightRotations[1],armRightRotations[2],clamp(p[Ikr],0.0,1.0));
                    armRightSolved=true;
                }
            }
            if(b.name=="head") { headKidPosition=glm::dvec3(world[i][3]); headKidRotation=worldRotation[i]; }
            frame.bones[i] = local;
            frame.skinMatrices[i] = world[i] * b.inverseBind;
        }

        // The support hand is attached to the solved right fist through the weapon's actual grip pivot and in-hand
        // transform. This mirrors _kidXform(handR) → pivot → off → handL in character.js; the hold anchor alone is
        // insufficient because the weapon is a child of the solved right hand.
        const auto& weaponRig=GetCharacterWeaponRig(weapon);
        const int clav=find("clavL"), upper=find("uArmL"), lower=find("fArmL"), end=find("handL"), rightHand=find("handR");
        if(clav>=0&&upper>=0&&lower>=0&&end>=0&&rightHand>=0&&
            (p[Ikl]>0.001||p[Ltw]>0.001)) {
            glm::dvec3 target;
            glm::dquat targetRotation;
            if(weaponRig.dual) {
                glm::dvec3 anchor{p[Anl],p[Anl+1],p[Anl+2]};
                auto anchorRotation=euler({p[Anlr],p[Anlr+1],p[Anlr+2]},true);
                if(p[Afolt]>0.001||p[Afolr]>0.001) {
                    const int chest=find("chest");
                    const auto chestQ=worldRotation[static_cast<std::size_t>(chest)];
                    const auto followQ=glm::slerp(glm::dquat(1,0,0,0),chestQ,clamp(p[Afolr],0.0,1.0));
                    const auto chestP=glm::dvec3(world[static_cast<std::size_t>(chest)][3]);
                    const auto followed=chestP+followQ*(anchor-rig->bones[static_cast<std::size_t>(chest)].rest);
                    anchor=glm::mix(anchor,followed,clamp(p[Afolt],0.0,1.0));
                    anchorRotation=glm::normalize(followQ*anchorRotation);
                }
                const double swayWeight=(1.0-0.7*aimWeight)*(1.0-lockWeight);
                anchor+=weaponSwayRawPosition*swayWeight;
                anchorRotation=glm::normalize(euler(weaponSwayEuler*swayWeight,true)*anchorRotation);
                anchor+=anchorRotation*glm::dvec3(0.0,0.0,-recoilBackLeft);
                anchorRotation=glm::normalize(anchorRotation*euler({-recoilPitchLeft,-springs[SRCy2],0.0},true));
                const auto handQuat=glm::normalize(anchorRotation*weaponRig.handL.rotation);
                target=anchor+anchorRotation*weaponRig.handL.position;
                targetRotation=handQuat;
            } else {
                const auto rightPosition=glm::dvec3(world[static_cast<std::size_t>(rightHand)][3]);
                const auto rightRotation=worldRotation[static_cast<std::size_t>(rightHand)];
                const auto pivotRotation=glm::angleAxis(static_cast<double>(p[Spin]),glm::dvec3(1.0,0.0,0.0));
                const glm::dvec3 pivotPosition=CharacterFistOffset;
                const auto pivotWorldRotation=glm::normalize(rightRotation*pivotRotation);
                const auto offPosition=weaponRig.inHand.position-CharacterFistOffset;
                const auto offWorldPosition=rightPosition+rightRotation*pivotPosition+pivotWorldRotation*offPosition;
                const auto offWorldRotation=glm::normalize(pivotWorldRotation*weaponRig.inHand.rotation);
                auto supportPosition=weaponRig.handL.position;
                supportPosition+=weaponAnimationRight.offPositionOffset;
                supportPosition.z-=0.036*weaponAnimationRight.pump;
                target=offWorldPosition+offWorldRotation*supportPosition;
                targetRotation=glm::normalize(offWorldRotation*weaponRig.handL.rotation);
            }
            if(!weaponRig.dual&&p[Ltw]>0.001)
                target=glm::mix(target,glm::dvec3(p[Ltgt],p[Ltgt+1],p[Ltgt+2]),clamp(p[Ltw],0.0,1.0));
            const double targetWeight=clamp(std::max(p[Ikl],p[Ltw]),0.0,1.0);
            const auto solveLeft=[&]() {
                const auto parentPosition=glm::dvec3(world[static_cast<std::size_t>(clav)][3]);
                armLeftRotations=solveLimbRotations(rig->bones[static_cast<std::size_t>(upper)],
                    rig->bones[static_cast<std::size_t>(lower)],rig->bones[static_cast<std::size_t>(end)],
                    rig->bones[static_cast<std::size_t>(upper)].rest-rig->bones[static_cast<std::size_t>(clav)].rest,
                    parentPosition,worldRotation[static_cast<std::size_t>(clav)],target,
                    {p[Polel],p[Polel+1],p[Polel+2]},targetRotation,{-1,0,0},armLeftRotations[0],
                    armLeftRotations[1],armLeftRotations[2],targetWeight,p[Ltw]<=p[Ikl]);
                armLeftSolved=true;
            };
            const auto reachError=[&]() {
                const auto origin=glm::dvec3(world[static_cast<std::size_t>(clav)][3])+worldRotation[static_cast<std::size_t>(clav)]*
                    (rig->bones[static_cast<std::size_t>(upper)].rest-rig->bones[static_cast<std::size_t>(clav)].rest);
                const double a=glm::length(rig->bones[static_cast<std::size_t>(lower)].rest-rig->bones[static_cast<std::size_t>(upper)].rest);
                const double b=glm::length(rig->bones[static_cast<std::size_t>(end)].rest-rig->bones[static_cast<std::size_t>(lower)].rest);
                return std::max(0.0,glm::length(target-origin)-(a+b)*0.9995);
            };
            solveLeft();
            ikError=reachError();
            if(!weaponRig.dual&&p[Ltw]<=0.001) ikErrorPre=ikError;
            if(!weaponRig.dual&&p[Ltw]<=0.001&&p[Ikl]>0.5&&ikError>0.0005) {
                const double initialY=leftClavEuler.y;
                for(int iteration=0;iteration<3&&ikError>0.0005&&leftClavEuler.y>initialY-0.55;++iteration) {
                    const double extra=clamp(ikError*12.0,0.0,0.3);
                    leftClavEuler.y-=extra; leftClavEuler.x-=extra*0.35;
                    // Rebuild the left clavicle's world transform after its source-authored protraction.
                    const auto& clavBone=rig->bones[static_cast<std::size_t>(clav)];
                    const auto clavLocalPosition=clavBone.rest-rig->bones[static_cast<std::size_t>(clavBone.parent)].rest;
                    const auto clavLocalRotation=euler(leftClavEuler,true);
                    const auto parentIndex=static_cast<std::size_t>(clavBone.parent);
                    worldRotation[static_cast<std::size_t>(clav)]=glm::normalize(worldRotation[parentIndex]*clavLocalRotation);
                    const glm::dmat4 clavLocal=glm::translate(glm::dmat4(1.0),clavLocalPosition)*glm::mat4_cast(clavLocalRotation);
                    world[static_cast<std::size_t>(clav)]=world[parentIndex]*clavLocal;
                    solveLeft();
                    ikError=reachError();
                }
            }

            // Update the local arm pose and recalculate descendant transforms so returned bones and skin matrices
            // include the final analytic IK result.
            frame.bones[static_cast<std::size_t>(upper)].rotation=armLeftRotations[0];
            frame.bones[static_cast<std::size_t>(lower)].rotation=armLeftRotations[1];
            frame.bones[static_cast<std::size_t>(end)].rotation=armLeftRotations[2];
            for(std::size_t i=0;i<rig->bones.size();++i) {
                const auto& b=rig->bones[i];
                if(i==static_cast<std::size_t>(clav)) {
                    frame.bones[i].rotation=euler(leftClavEuler,true);
                }
                const auto& local=frame.bones[i];
                worldRotation[i]=b.parent<0?local.rotation:worldRotation[static_cast<std::size_t>(b.parent)]*local.rotation;
                world[i]=b.parent<0?local.matrix():world[static_cast<std::size_t>(b.parent)]*local.matrix();
                frame.skinMatrices[i]=world[i]*b.inverseBind;
            }
        }

        const int leftFoot=find("footL"),rightFoot=find("footR");
        const auto cacheFoot=[&](int index,std::size_t side) {
            if(index<0) return;
            const auto localPosition=glm::dvec3(world[static_cast<std::size_t>(index)][3]);
            const auto kidPosition=glm::dvec3(frame.kid.matrix()*glm::dvec4(localPosition,1.0));
            const double c=std::cos(yaw),sn=std::sin(yaw);
            auto& foot=feet[side];
            foot.displacement={previousPosition.x+kidPosition.x*c+kidPosition.z*sn,
                previousPosition.y+std::max(0.0,kidPosition.y-rig->bones[static_cast<std::size_t>(index)].rest.y),
                previousPosition.z-kidPosition.x*sn+kidPosition.z*c};
            auto forward=frame.kid.rotation*worldRotation[static_cast<std::size_t>(index)]*glm::dvec3(0.0,0.0,1.0);
            const double fx=forward.x*c+forward.z*sn,fz=-forward.x*sn+forward.z*c;
            foot.displacementYaw=yaw+std::atan2(fx,fz);
            foot.displacementValid=true;
        };
        cacheFoot(leftFoot,0);
        cacheFoot(rightFoot,1);
    }

    void updateParts() {
        frame.parts.clear();
        const auto append = [&](const CharacterModel& model, bool body) {
            for(const auto& node:model.nodes) {
                const auto& binding=node.binding;
                if(body ? !(binding.rfind("tank/",0)==0||binding=="squid"||binding.rfind("squid/",0)==0)
                        : binding.rfind("weapon/",0)!=0) continue;
                if(body ? binding.rfind("tank/group/",0)==0
                        : binding.find("/drum/")!=std::string::npos||binding.find("/spin/")!=std::string::npos) continue;
                CharacterPartPose part;
                part.name=binding; part.parentBone=node.bone; part.local=node.local;
                if(binding=="tank/fill") {
                    part.local.rotation=euler({tankFillRotX,0.0,tankFillRotZ},false);
                    part.local.scale={1.0-tankSlosh*0.03,std::max(0.004,inkScale*(1.0+clamp(tankFillLevel*0.06,-0.08,0.08)))*0.18,1.0-tankSlosh*0.03};
                    part.local.visible=inkScale>0.005;
                }
                if(binding=="weapon/right/pivot")
                    part.local.rotation=glm::angleAxis(static_cast<double>(p[Spin]),glm::dvec3(1.0,0.0,0.0));
                if(binding=="squid") part.local=frame.squid;
                else if(binding=="squid/0") part.local=squidPivot;
                else if(binding=="squid/0/1") part.local.visible=squidGhostVisible;
                else if(binding=="squid/0/2") part.local.visible=squidEyesVisible;
                else if(binding=="squid/0/3") {
                    part.local.visible=squidEyesVisible;
                    part.local.scale.y=squidEyesScaleY; part.local.position.y=squidEyesPositionY;
                }
                if(binding=="squid/0/0") part.transparent=false;
                frame.parts.push_back(std::move(part));
            }
        };
        append(data->model("main/"+weapon),false);
        append(data->model("body/0/0/0/game"),true);
        const auto& subModel=data->model("sub/"+sub);
        const bool bombOn=bombHeld&&!subPropHidden&&form==CharacterForm::Kid&&subWeight>0.2;
        if(bombOn) bombScale=std::max(0.05,backOut(clamp(bombTime/0.14,0.0,1.0),2.6));
        for(const auto& node:subModel.nodes) {
            if(node.binding.rfind("sub/",0)!=0) continue;
            CharacterPartPose part;
            part.name=node.binding; part.parentBone=node.bone; part.local=node.local;
            if(node.binding=="sub/group") {
                part.local.visible=bombOn&&!subPropHidden;
                part.local.scale=glm::dvec3(bombScale);
            }
            frame.parts.push_back(std::move(part));
        }
        const auto& weaponModel=data->model("main/"+weapon);
        weaponAnimationRight.apply(frame.parts,"right");
        weaponAnimationLeft.apply(frame.parts,"left");
        for(auto& part:frame.parts) {
            const auto node=std::find_if(weaponModel.nodes.begin(),weaponModel.nodes.end(),[&](const auto& candidate){return candidate.binding==part.name;});
            if(node==weaponModel.nodes.end()||node->name.rfind("__alias__:",0)!=0) continue;
            const std::string target=node->name.substr(10);
            const auto source=std::find_if(frame.parts.begin(),frame.parts.end(),[&](const auto& candidate){return candidate.name==target;});
            if(source!=frame.parts.end()) part.local=source->local;
        }

        const auto partTransform=[&](const std::string& binding) -> const CharacterTransform* {
            const auto found=std::find_if(frame.parts.begin(),frame.parts.end(),[&](const auto& part){return part.name==binding;});
            return found==frame.parts.end()?nullptr:&found->local;
        };
        const auto boneWorld=[&](std::size_t index) { return frame.skinMatrices[index]*glm::inverse(rig->bones[index].inverseBind); };
        const auto modelWorld=[&](const CharacterModel& model,const std::string& wanted) -> std::optional<glm::dmat4> {
            std::vector<glm::dmat4> world; world.reserve(model.nodes.size());
            std::optional<glm::dmat4> result;
            for(const auto& node:model.nodes) {
                CharacterTransform local=node.local;
                if(const auto* part=partTransform(node.binding)) local=*part;
                if(node.binding=="root") local=frame.root;
                else if(node.binding=="model") local=frame.model;
                else if(node.binding=="kid") local=frame.kid;
                else if(node.binding=="squid") local=frame.squid;
                const auto parent=node.bone>=0?boneWorld(static_cast<std::size_t>(node.bone)):
                    node.parent>=0?world[static_cast<std::size_t>(node.parent)]:glm::dmat4(1.0);
                world.push_back(parent*local.matrix());
                if(node.binding==wanted) result=world.back();
            }
            return result;
        };
        const auto kidWorld=frame.root.matrix()*frame.model.matrix()*frame.kid.matrix();
        const auto muzzle=modelWorld(weaponModel,"weapon/right/muzzle");
        const auto leftMuzzle=modelWorld(weaponModel,"weapon/left/muzzle");
        const int headIndex=find("head");
        if(form==CharacterForm::Kid&&headIndex>=0) {
            const auto headKid=boneWorld(static_cast<std::size_t>(headIndex))*glm::dvec4(0.0,0.164,0.014,1.0);
            frame.head=glm::dvec3(kidWorld*headKid);
        } else if(const auto pivot=modelWorld(data->model("body/0/0/0/game"),"squid/0")) {
            frame.head=glm::dvec3(*pivot*glm::dvec4(0.0,0.0,0.0,1.0));
        } else frame.head=glm::dvec3(kidWorld[3]);
        if(form==CharacterForm::Kid&&muzzle) frame.muzzle=glm::dvec3(kidWorld*(*muzzle)[3]);
        else frame.muzzle=frame.head;
        if(form==CharacterForm::Kid&&leftMuzzle) frame.muzzleLeft=glm::dvec3(kidWorld*(*leftMuzzle)[3]);
        else frame.muzzleLeft=frame.muzzle;
    }

    void updateMaterials(const CharacterAnimState& input) {
        auto& material=frame.material;
        const double pulse=0.5+0.5*std::sin(time*tau*1.6);
        const double blink=lowInkWeight*(0.5+0.5*std::sin(time*tau*3.2));
        const double glow=glowWeight*(0.35+0.45*pulse);
        material.flash=glm::dvec3(input.invuln?0.22*(0.5+0.5*std::sin(time*tau*6.0)):0.0);
        material.glow=material.team*glow;
        material.tankEmissive=material.team*(0.12+0.9*blink+0.3*glowWeight*pulse);
        const double weaponCharge=GetCharacterWeaponHold(weapon).animKind=="charger"?charge:0.0;
        const double full=weaponCharge>=0.995?0.5+0.5*std::sin(time*tau*8.0):0.0;
        material.coilEmissiveIntensity=0.15+2.6*weaponCharge*weaponCharge+1.5*full+3.0*chargeFlash;
    }

    void tickWeaponAnimations(double dt,const CharacterAnimState& input,const CharacterSceneInput& scene) {
        CharacterWeaponKitState kit;
        if(input.runner) {
            const auto& runner=*input.runner;
            kit.charging=runner.charging; kit.charge=runner.charge; kit.leaping=runner.leaping;
            kit.landT=runner.mittsLandTime; kit.flashT=runner.bladeFlashTime;
            kit.open=runner.canopyOpen; kit.grow=runner.canopyGrow; kit.hp=runner.canopyHp; kit.hpMax=runner.canopyHpMax;
            kit.shake=runner.canopyShake; kit.flash=runner.canopyFlash; kit.regrowK=runner.canopyRegrowK;
            kit.opening=runner.canopyOpening; kit.state=runner.canopyState;
            kit.hasSinceHand=runner.hasSinceHand; kit.sinceHand=runner.sinceHand;
        }
        CharacterWeaponAnimationInput animation;
        animation.weapon=weapon; animation.t=time; animation.dt=dt;
        animation.sinceRelease=lastRelease; animation.charge=charge;
        animation.chargeFlash=chargeFlash; animation.rolling=rollWeight; animation.groundSpeed=gv;
        animation.full=fullTime>0.0; animation.firing=input.firing; animation.grounded=grounded;
        animation.color=frame.material.team;
        kit.charge=input.runner?input.runner->charge:charge;
        if(weapon=="bow") kit.bowDraw=bowDraw;
        const auto cameraDelta=scene.cameraPosition?*scene.cameraPosition-input.position:glm::dvec3(0.0);
        animation.near=scene.lifeLevel>0&&(!scene.inWorld||scene.isLocal||!scene.cameraPosition||glm::dot(cameraDelta,cameraDelta)<225.0);
        animation.runner=input.runner;
        if(input.runner||weapon=="bow") animation.kit=kit;
        animation.hand=0; animation.sinceShoot=lastShot; animation.sinceFlick=timers[TFlick];
        weaponAnimationRight.tick(animation);
        animation.hand=1; animation.sinceShoot=timers[TShootL];
        weaponAnimationLeft.tick(animation);
    }
};

CharacterPose::CharacterPose(const CharacterData& data, const CharacterRig& rig, std::string name, std::string weapon)
    : state_(std::make_unique<State>(data, rig, std::move(name), std::move(weapon))) {}
CharacterPose::~CharacterPose() = default;
CharacterPose::CharacterPose(CharacterPose&&) noexcept = default;
CharacterPose& CharacterPose::operator=(CharacterPose&&) noexcept = default;

void CharacterPose::setWeapon(const std::string& weapon) {
    if(state_->weapon==weapon) return;
    state_->weapon=weapon;
    state_->weaponAnimationRight=CharacterWeaponAnimation{};
    state_->weaponAnimationLeft=CharacterWeaponAnimation{};
}
void CharacterPose::setSub(const std::string& sub) {
    if(sub.empty()) throw std::invalid_argument("Source character sub id must not be empty");
    (void)state_->data->model("sub/"+sub);
    state_->sub=sub;
}
void CharacterPose::setStyle(const CharacterStyle& style) {
    if(style.hair<0||style.hair>=8||style.skin<0||style.skin>=9||style.outfit<0||style.outfit>=10||
       style.eyes<0||style.eyes>=8||style.hat<0||style.hat>=4||style.brows<0||style.brows>=4)
        throw std::invalid_argument("Unknown source character style");
    state_->style=style;
    const auto outfit=CharacterOutfit(style.outfit);
    auto& material=state_->frame.material;
    material.shirt=outfit.shirt; material.shorts=outfit.shorts; material.shoe=outfit.shoe;
    material.sole=outfit.sole; material.sock=outfit.sock; material.strap=outfit.strap; material.pattern=outfit.pattern;
    material.iris=CharacterIris[style.eyes][0]; material.iris2=CharacterIris[style.eyes][1];
    material.freckle=CharacterSkinLighting[style.skin].freckle;
}
void CharacterPose::setDance(const std::string& requested) {
    auto& s=*state_;
    const std::string dance=requested.empty()?std::string{}:requested;
    if(dance==s.dance) return;
    if(!s.dance.empty()&&!dance.empty()) {
        s.previousDance=s.dance; s.previousDanceTime=s.danceTime; s.danceFade=0.0;
    }
    s.dance=dance; s.danceTime=0.0;
    s.danceVariant=(dance=="victory"||dance=="defeat")?static_cast<int>((s.seed>>5)%3u):0;
    static constexpr std::array<double,8> joy={0.1,0.7,-0.12,0.25,0.12,0.72,0.0,0.35};
    static constexpr std::array<double,8> sigh={0.3,0.1,-0.35,-0.2,-0.1,0.22,0.0,0.0};
    if(dance=="victory") {
        s.playMicroExpression(joy,0.7,0.08,0.5,0.8,0.0,{0.0,0.0,0.45});
        s.blinkStart(1.0,false);
    } else if(dance=="defeat") {
        s.playMicroExpression(sigh,0.9,0.3,0.8,1.2);
        s.sigh=1.0;
    }
}
void CharacterPose::setHurt(double amount, glm::dvec3 enemyLinearColor) {
    state_->hurt = clamp(amount, 0.0, 1.0);
    state_->frame.material.hurt = glm::dvec4(enemyLinearColor, state_->hurt);
}
void CharacterPose::setColor(glm::dvec3 linearColor) {
    if (!finite(linearColor)) throw std::invalid_argument("Character team color must be finite");
    state_->frame.material.team = linearColor;
    state_->frame.material.glow = linearColor;
}
void CharacterPose::trigger(const CharacterTrigger& event) {
    if(event.name=="stream") { state_->lastShot=0.0; return; }
    if(event.name=="shoot"||event.name=="shoot_left") {
        const auto& hold=GetCharacterWeaponHold(state_->weapon);
        if(event.name=="shoot"&&hold.animKind=="roller") {
            state_->timers[TFlick]=0.0;
            return;
        }
        if(event.name=="shoot"&&hold.fire=="slosh") {
            if(state_->timers[TSlosh]>0.3) state_->timers[TSlosh]=0.0;
            state_->lastShot=0.0;
            return;
        }
        const auto& rig=GetCharacterWeaponRig(state_->weapon);
        const int hand=rig.dual&&(event.name=="shoot_left"||event.hand==1)?1:0;
        state_->timers[hand==1?TShootL:TShoot]=0.0;
        state_->lastShot=0.0;
        state_->recoilImpulse(event.amount.value_or(1.0),hand);
        if(hand==1) state_->springs[SGripL+1]+=2.2;
        else {
            state_->springs[SGrip+1]+=2.2;
            if(!rig.dual) state_->springs[SGripL+1]+=1.2;
        }
        return;
    }
    if(event.name=="blade_slash") {
        state_->bladeTime=0.0; state_->bladeSide=event.amount.value_or(1.0); state_->bladeHeavy=false;
        state_->lastShot=0.0; state_->springs[SGrip+1]+=2.4; return;
    }
    if(event.name=="blade_heavy") {
        state_->bladeTime=0.0; state_->bladeHeavy=true;
        state_->lastShot=0.0; state_->springs[SSq+1]-=1.2; state_->springs[SGrip+1]+=3.2; return;
    }
    if(event.name=="roll") {
        auto direction=glm::dvec2(event.direction.x,event.direction.z);
        if(glm::dot(direction,direction)>1e-8) direction=glm::normalize(direction);
        else direction={0.0,1.0};
        state_->dodgeRollActive=true; state_->dodgeRollTime=0.0;
        state_->dodgeRollDuration=event.duration>0.0?event.duration:0.3;
        state_->dodgeRollDirection=direction; return;
    }
    static const std::array<std::string, 17> names={"shoot","flick","throw","land","jump","hit","special_leap","special_slam","spawn","charge_release","impact","brake","form","stagger","shoot_left","dodge","slosh"};
    const auto it=std::find(names.begin(),names.end(),event.name);
    if(it==names.end()) throw std::invalid_argument("Unknown source character trigger: " + event.name);
    const auto index=static_cast<std::size_t>(it-names.begin());
    const std::array<std::size_t,17> channels={TShoot,TFlick,TThrow,TLand,TJump,THit,TLeap,TSlam,TSpawn,TRelease,TImpact,TBrake,TForm,TStagger,TShootL,TDodge,TSlosh};
    const double oldLandTime=state_->timers[TLand];
    state_->timers[channels[index]]=0.0;
    if(event.name=="jump") {
        state_->springs[SSq+1]+=2.6;
        state_->springs[STankL+1]+=2.0;
        state_->hairKick(0.0,2.4,0.0);
        if(state_->form==CharacterForm::Kid&&state_->dance.empty()&&state_->random()<0.6) {
            static constexpr std::array<double,8> jumpExpression={-0.3,0.3,0.06,0.1,0.06,0.3,0.0,0.1};
            state_->playMicroExpression(jumpExpression,0.6,0.05,0.12,0.3);
        }
        state_->jumpRun=state_->form==CharacterForm::Kid?sstep(1.5,4.5,state_->gs):0.0;
        state_->jumpLead=state_->feet[0].swinging&&!state_->feet[1].swinging?0
            :state_->feet[1].swinging&&!state_->feet[0].swinging?1
            :(state_->feet[0].swingU>state_->feet[1].swingU?0:1);
    }
    if(event.name=="flick") {
        state_->lastShot=0.0;
        state_->springs[SGrip+1]+=3.0; state_->springs[SGripL+1]+=3.0;
        if(event.amount&&*event.amount!=0.0) state_->swipeSide=*event.amount;
    }
    if(event.name=="throw") state_->bombHeld=false;
    if(event.name=="land") {
        const double amount=event.amount.value_or(8.0);
        const double a=clamp((amount-2.5)/13.0,0.12,1.0);
        state_->landAmplitude=oldLandTime<0.25?std::max(state_->landAmplitude,a):a;
        auto& sp=state_->springs;
        sp[SPelY+1]-=3.0*a; sp[SSq+1]-=3.6*a; sp[SLeanP+1]+=2.2*a; sp[SHeadP+1]+=3.5*a;
        sp[SArmL+1]-=4.0*a; sp[SArmR+1]-=4.0*a; sp[SWpy+1]-=0.9*a; sp[SWrx+1]+=4.0*a; sp[STankL+1]-=3.0*a;
        state_->hairKick(0.0,-3.2*a,0.0); sp[SEarL+1]-=5.0*a; sp[SEarR+1]-=5.0*a;
        if(a>0.55) state_->blinkStart(1.0,false);
    }
    if(event.name=="hit") {
        const bool withDirection=event.hasDirection;
        double hx=0.0,hz=1.0,amp=1.0;
        if(withDirection) {
            hx=event.direction.x; hz=event.direction.z;
            const double length=std::hypot(hx,hz);
            if(length>1e-4) { hx/=length; hz/=length; } else { hx=0.0; hz=1.0; }
            amp=clamp(event.amount.value_or(1.0),0.3,1.6);
        } else {
            hx=(state_->random()-0.5)*1.2;
            hz=1.0;
            if(event.amount) amp=clamp(*event.amount,0.3,1.4);
        }
        state_->hitX=hx; state_->hitZ=hz; state_->hitAmplitude=amp; state_->hitAcc+=amp;
        auto& sp=state_->springs;
        sp[SHitP+1]-=hz*6.5*amp; sp[SHitR+1]+=hx*6.5*amp; sp[SHitY+1]+=hx*5.0*amp; sp[SPelY+1]-=0.5*amp;
        sp[SHeadP+1]-=hz*7.5*amp; sp[SHeadR+1]+=hx*7.0*amp; sp[SClav+1]+=3.5*amp; sp[SWrx+1]-=2.0*amp;
        state_->hairKick(hx*1.5,1.2,-hz*1.8); sp[SEarL+1]+=(4.0+3.0*hx)*amp; sp[SEarR+1]+=(4.0-3.0*hx)*amp;
        state_->blinkStart(1.0,amp>0.7);
        if(amp>0.8&&state_->form==CharacterForm::Kid) {
            static constexpr std::array<double,8> oof={0.7,0.35,0.1,-1.3,-0.3,0.55,-0.1,0.35};
            state_->playMicroExpression(oof,0.35+0.3*amp,0.04,0.1,0.45);
        }
        sp[SGrip+1]+=3.0*amp;
        if(state_->hitAcc>2.6&&state_->timers[TStagger]>0.9) {
            state_->timers[TStagger]=0.0; state_->hitAcc=0.0;
            state_->stepOfsZ=-0.16*hz; state_->stepOfsX=-0.1*hx; sp[SStag+1]-=2.5;
        }
        if(state_->form!=CharacterForm::Kid) { sp[SSqP+1]+=5.0*amp; sp[SSqY+1]-=3.0*amp; }
    }
    if(event.name=="spawn") {
        state_->previousForm=CharacterForm::Kid; state_->form=CharacterForm::Kid; state_->formTime=99.0;
        state_->kidScale=1.0; state_->sqScale=0.0; state_->feetValid=false; state_->replant=true;
        state_->headInitialized=false; state_->rootInitialized=false;
    }
    if(event.name=="dodge") {
        double x=event.hasDirection?event.direction.x:0.0;
        double z=event.hasDirection?event.direction.z:1.0;
        const double length=std::hypot(x,z);
        if(length>1e-4) { x/=length; z/=length; } else { x=0.0; z=1.0; }
        state_->dodgeX=x; state_->dodgeZ=z;
        state_->dodgeDuration=clamp(event.duration>0.0?event.duration:0.3,0.15,0.8);
        state_->lockWeight=std::max(state_->lockWeight,0.001);
        auto& sp=state_->springs;
        sp[SSq+1]-=1.6; sp[STankL+1]+=1.5; state_->hairKick(-x*2.0,1.2,-z*2.0);
    }
    if(event.name=="charge_release") {
        state_->lastRelease=0.0;
        state_->lastShot=0.0;
        state_->chargeFlash=1.0;
        state_->recoilImpulse(0.5+0.7*state_->charge);
        state_->springs[SGrip+1]+=3.0;
        state_->springs[SGripL+1]+=2.0;
        if(state_->charge>0.8) {
            static constexpr std::array<double,8> effort={-0.5,-0.2,-0.12,-0.3,0.2,0.2,0.0,0.4};
            state_->playMicroExpression(effort,0.5,0.03,0.08,0.3);
        }
    }
    if(event.name=="slosh") state_->lastShot=0.0;
    if(event.name=="special_leap") {
        state_->timers[TSlam]=99.0; state_->timers[TImpact]=99.0; state_->slamGround=false; state_->leapEnd=-1.0;
        state_->hairKick(0.0,-2.0,0.0);
    }
    if(event.name=="special_slam") {
        state_->leapEnd=state_->timers[TLeap]<1.9?state_->timers[TLeap]:-1.0;
        state_->timers[TImpact]=99.0; state_->slamGround=false;
    }
}

const CharacterPoseFrame& CharacterPose::update(double dt, const CharacterAnimState& input, const CharacterSceneInput& scene) {
    auto& s=*state_;
    if(!std::isfinite(dt)) dt=0.0;
    dt=clamp(dt,0.0,0.1); s.time+=dt;
    if(!s.dance.empty()) s.danceTime+=dt;
    s.previousDanceTime+=dt; s.danceFade=std::min(1.0,s.danceFade+dt/0.45);
    s.danceWeight=damp(s.danceWeight,s.dance.empty()?0.0:1.0,5.0,dt);
    for(auto& t:s.timers) t=static_cast<float>(t+dt);
    s.lastShot+=dt; s.lastRelease+=dt;
    s.grounded=input.grounded;
    s.yaw=input.yaw;
    if(input.form!=s.form) {
        if((input.form==CharacterForm::Kid)!=(s.form==CharacterForm::Kid)) s.enterForm(input.form);
        else s.form=input.form;
    }
    s.formTime+=dt;
    s.updateFormScales();

    if(!finite(input.position)||!std::isfinite(input.yaw)) throw std::invalid_argument("Character animation transform must be finite");
    if(!s.rootInitialized||glm::dot(input.position-s.previousPosition,input.position-s.previousPosition)>9.0) {
        s.previousPosition=input.position; s.velocity=glm::dvec3(0); s.acceleration=glm::dvec3(0);
        s.previousYaw=input.yaw; s.rootInitialized=true; s.yawRate=0.0; s.moving=false;
    }
    if(dt>0.0) {
        const auto oldVelocity=s.velocity;
        const auto measured=(input.position-s.previousPosition)/dt;
        s.velocity += (measured-s.velocity)*(1.0-std::exp(-32.0*dt));
        const auto measuredAcceleration=(s.velocity-oldVelocity)/dt;
        s.acceleration += (measuredAcceleration-s.acceleration)*(1.0-std::exp(-16.0*dt));
        const double dy=wrapAngle(input.yaw-s.previousYaw);
        const double targetYawRate=input.turnRate.value_or(dy/dt);
        s.yawRate=damp(s.yawRate,clamp(targetYawRate,-14.0,14.0),input.turnRate?30.0:14.0,dt);
    }
    s.previousPosition=input.position; s.previousYaw=input.yaw;
    const double c=std::cos(input.yaw), sn=std::sin(input.yaw);
    s.kvx=s.velocity.x*c-s.velocity.z*sn; s.kvz=s.velocity.x*sn+s.velocity.z*c;
    s.kax=s.acceleration.x*c-s.acceleration.z*sn; s.kaz=s.acceleration.x*sn+s.acceleration.z*c;
    s.hs=std::hypot(s.kvx,s.kvz);
    const double moveLength=glm::length(input.localMove);
    const bool treadmill=s.hs<0.12&&input.speed>0.4&&moveLength>0.05&&input.grounded;
    s.treadmill=treadmill;
    if(treadmill) {
        s.kgx=-input.localMove.x/moveLength*input.speed; s.kgz=input.localMove.y/moveLength*input.speed;
        const double worldX=s.kgx*c+s.kgz*sn, worldZ=-s.kgx*sn+s.kgz*c;
        s.tvx=worldX; s.tvz=worldZ; s.gvx=worldX; s.gvz=worldZ;
    } else { s.tvx=0.0; s.tvz=0.0; s.kgx=s.kvx; s.kgz=s.kvz; s.gvx=s.velocity.x; s.gvz=s.velocity.z; }
    s.gv=std::hypot(s.gvx,s.gvz);
    s.gs=damp(s.gs,input.grounded?s.gv:s.gs,s.gv>s.gs?16.0:7.0,dt);
    if(s.gv>0.35) {
        s.mdx=damp(s.mdx,s.kgx/s.gv,10.0,dt); s.mdz=damp(s.mdz,s.kgz/s.gv,10.0,dt);
        const double ml=std::hypot(s.mdx,s.mdz); if(ml>0.0) { s.mdx/=ml; s.mdz/=ml; }
    }
    s.vy=damp(s.vy,input.vy.value_or(s.velocity.y),14.0,dt);
    s.airTime=input.grounded?0.0:s.airTime+dt;
    if(!input.grounded&&scene.raycast) {
        const auto hit=scene.raycast({input.position.x,input.position.y+0.3,input.position.z},{0,-1,0},4.0);
        s.groundDistance=hit.hit?std::max(0.0,hit.distance-0.3):9.0;
    } else s.groundDistance=input.grounded?0.0:9.0;

    s.previousMoving=s.moving;
    const double v=s.moving?std::max(s.gv,0.6):s.gs;
    s.runWeight=sstep(1.7,4.3,v);
    s.duty=0.62+(0.3-0.62)*s.runWeight+0.06*s.goo;
    const double half=(0.19+(0.265-0.19)*sstep(0.4,5.0,v))*(1.0-0.18*s.goo);
    s.cadence=clamp(std::max(v,0.6)*s.duty/(2.0*half),1.1,4.4)*(1.0-0.1*s.goo);
    s.lift=(0.05+(0.2-0.05)*sstep(1.2,5.5,v))*(1.0+0.9*s.goo);
    s.gaitWeight=damp(s.gaitWeight,s.moving?1.0:0.0,s.moving?7.0:4.5,dt);
    double twist=0.0;
    if(s.moving) {
        twist=std::atan2(s.mdx,std::abs(s.mdz)+0.3)*0.78;
        if(s.mdz< -0.25) twist=-twist*0.8;
        twist=clamp(twist,-0.8,0.8)*sstep(0.5,2.2,v);
    }
    s.hipTwist=damp(s.hipTwist,twist,7.0,dt);
    const double ch=clamp(input.charge,0.0,1.0);
    const auto& weaponHold=GetCharacterWeaponHold(s.weapon);
    const auto& weaponRig=GetCharacterWeaponRig(s.weapon);
    const bool dual=weaponRig.dual;
    const double dodgeK=s.timers[TDodge]/s.dodgeDuration;
    const bool locking=input.form==CharacterForm::Kid&&s.dance.empty()&&dual&&
        ((input.runner?(input.runner->lockT>0.0||(input.runner->dodge&&dodgeK>0.55)):s.timers[TDodge]<s.dodgeDuration+0.5)||
            (dodgeK>0.55&&dodgeK<1.0));
    s.lockWeight=damp(s.lockWeight,locking?1.0:0.0,locking?18.0:7.0,dt);
    const bool aiming=input.form==CharacterForm::Kid&&s.dance.empty()&&weaponHold.animKind!="roller"&&
        (input.firing||ch>0.01||s.lastShot<0.5||s.lastRelease<0.35||s.lockWeight>0.5);
    s.aimWeight=damp(s.aimWeight,aiming?1.0:0.0,aiming?22.0:4.5,dt);
    const bool rolling=input.form==CharacterForm::Kid&&s.dance.empty()&&input.rolling&&
        weaponHold.animKind=="roller"&&s.timers[TFlick]>0.6;
    s.rollWeight=damp(s.rollWeight,rolling?1.0:0.0,rolling?11.0:6.0,dt);
    const int stanceFoot=s.find("footL");
    const double stanceHipWidth=(stanceFoot>=0?
        std::abs(s.rig->bones[static_cast<std::size_t>(stanceFoot)].rest.x):0.084)+0.008;
    const std::array<double,6> idleStance={
        stanceHipWidth+0.012,0.014,0.19,-stanceHipWidth-0.008,-0.01,-0.2};
    constexpr std::array<double,6> lockStance={0.165,0.035,0.42,-0.165,-0.035,-0.42};
    const double aimSt=std::max(s.aimWeight,s.rollWeight)*(1.0-s.gaitWeight);
    const double stanceWeight=std::max(aimSt,s.lockWeight);
    const double stanceRate=s.lockWeight>0.5?16.0:9.0;
    for(std::size_t i=0;i<s.stance.size();++i) {
        const double weaponStance=weaponHold.stance[i]+
            (lockStance[i]-weaponHold.stance[i])*s.lockWeight;
        const double target=idleStance[i]+(weaponStance-idleStance[i])*stanceWeight;
        s.stance[i]=static_cast<double>(static_cast<float>(damp(s.stance[i],target,stanceRate,dt)));
    }
    s.airWeight=damp(s.airWeight,input.grounded?0.0:1.0,input.grounded?24.0:12.0,dt);
    s.charge=damp(s.charge,ch,25.0,dt);
    s.fullTime=ch>=0.995?s.fullTime+dt:0.0;
    s.subWeight=damp(s.subWeight,input.subAim&&input.form==CharacterForm::Kid&&s.dance.empty()?1.0:0.0,
        input.subAim?14.0:9.0,dt);
    if(input.subAim&&!s.subPrev&&input.form==CharacterForm::Kid) { s.bombHeld=true; s.bombTime=0.0; }
    if(!input.subAim&&s.subWeight<0.3) s.bombHeld=false;
    s.subPrev=input.subAim; s.bombTime+=dt; s.subPropHidden=input.subPropHidden;
    s.hitAcc=std::max(0.0,s.hitAcc-dt*1.4);
    s.stepOfsX=damp(s.stepOfsX,0.0,2.2,dt); s.stepOfsZ=damp(s.stepOfsZ,0.0,2.2,dt);
    s.tired=damp(s.tired,input.hp?sstep(0.45,0.12,*input.hp>1.001?*input.hp/100.0:*input.hp):sstep(0.55,0.9,s.hurt)*0.6,3.0,dt);
    s.goo=damp(s.goo,input.inEnemyInk&&input.grounded&&input.form==CharacterForm::Kid?1.0:0.0,6.0,dt);
    s.glowWeight=damp(s.glowWeight,input.special>=0.999?1.0:0.0,6.0,dt);
    s.lowInkWeight=damp(s.lowInkWeight,input.lowInk?1.0:0.0,8.0,dt);
    s.inkScale=damp(s.inkScale,clamp(input.ink,0.0,1.0),8.0,dt);
    s.exert=clamp(s.exert+(s.gs>3.5?dt*0.12:-dt*0.06)+(s.timers[TLand]<dt*1.5?0.05*s.landAmplitude:0.0),0.0,1.0);
    s.chargeFlash=std::max(0.0,s.chargeFlash-dt*3.5);
    s.breathPhase+=dt*(0.26+(0.72-0.26)*std::max(s.exert,s.tired*0.8))*s.breathRate*(s.sigh>0.0?0.62:1.0);
    const double tw=2.0*3.14159265358979323846;
    const bool idleNow=input.form==CharacterForm::Kid&&s.dance.empty()&&!s.previousMoving&&input.grounded&&
        s.aimWeight<0.05&&s.rollWeight<0.05&&s.timers[TLand]>0.5&&s.timers[TSpawn]>1.2;
    s.idleTime=idleNow?s.idleTime+dt:0.0;
    s.updateFeet(dt,input,scene);
    const double idleWeight=(1.0-s.gaitWeight)*(1.0-s.airWeight);

    // Character.update intentionally skips the body pose once the kid has popped below this threshold.
    // The source leaves its last applied kid/bone/face transforms in place while the squid root continues
    // its independent pop. Keep the native frame at that same last applied pose during the gap.
    if(s.kidScale<=0.001||!input.visible) {
        s.feetValid=false; s.headInitialized=false;
        auto& f=s.frame;
        f.root.position=input.position; f.root.rotation=glm::angleAxis(input.yaw,glm::dvec3(0,1,0)); f.root.visible=input.visible;
        f.kid.visible=false;
        if(input.visible) {
            s.updateSquid(dt,input,scene);
        }
        else { s.squidInitialized=false; f.squid.visible=false; }
        s.updateParts(); s.updateMaterials(input);
        return f;
    }

    s.buildNeutral();
    const double ready=idleWeight*(1.0-0.35*s.tired);
    s.p[HipsP+1]-=0.036*idleWeight;
    s.p[Hips]+=0.07*ready; s.p[Spine]-=0.03*ready; s.p[Chest]-=0.012*ready; s.p[Neck]-=0.02*ready;
    s.p[UArmL]-=0.1*ready; s.p[UArmL+2]+=0.07*ready; s.p[FArmL]-=0.32*ready; s.p[HandL+2]-=0.12*ready;
    s.p[ClavL+2]-=0.02*ready; s.p[ClavR+2]+=0.02*ready;
    s.shift=spring(s.springs,SShift,s.shiftTarget*idleWeight*(1.0-s.aimWeight*0.8)*(1.0-s.tired*0.3),0.7,0.85,dt);
    s.p[HipsP]+=0.03*s.shift; s.p[Hips+2]+=0.08*s.shift; s.p[Spine+2]-=0.05*s.shift; s.p[Chest+2]-=0.035*s.shift;
    s.p[Hips+1]+=0.04*s.shift; s.p[Chest+1]-=0.025*s.shift; s.p[Head+2]+=0.035*s.shift; s.p[HipsP+1]-=0.008*std::abs(s.shift);
    const double sway=idleWeight*(1.0-s.aimWeight);
    s.p[Hips+1]+=0.018*std::sin(s.time*0.41+1.3)*sway;
    s.p[Spine+2]+=0.012*std::sin(s.time*0.53)*sway;
    s.p[Chest+1]+=0.02*std::sin(s.time*0.29+2.0)*sway;
    const double footTurn=s.footTwist*(1.0-s.gaitWeight);
    s.p[Hips+1]+=footTurn*0.55; s.p[Spine+1]-=footTurn*0.3; s.p[Chest+1]-=footTurn*0.25;

    if(s.gaitWeight>0.001) {
        const double phase=std::fmod(s.phase,1.0), back=sstep(0.1,-0.7,s.mdz), rn=s.runWeight;
        const double yawOsc=-(0.11+0.09*rn)*std::cos(tw*phase)*s.gaitWeight*(1.0-back*0.5);
        const double rollOsc=(0.06+0.015*rn)*std::cos(tw*(phase-s.duty*0.5))*s.gaitWeight;
        const double swayX=(0.022-0.009*rn)*std::cos(tw*(phase-s.duty*0.5-0.06))*s.gaitWeight;
        const double c2=std::cos(tw*2.0*(s.phase-s.duty*0.45));
        const double bob=(0.014-0.05*rn)*c2*s.gaitWeight*sstep(0.3,2.0,s.gs);
        s.p[HipsP+1]+=s.gaitWeight*(-0.024+( -0.064+0.024)*rn)*(1.0+0.5*s.goo)+bob;
        s.p[HipsP]+=swayX; s.p[Hips+1]+=s.hipTwist+yawOsc; s.p[Hips+2]+=rollOsc;
        s.p[Spine+1]-=s.hipTwist*0.45+yawOsc*0.65; s.p[Chest+1]-=s.hipTwist*0.55+yawOsc*0.8;
        s.p[Spine+2]-=rollOsc*0.6; s.p[Chest+2]-=rollOsc*0.35;
        const double lean=s.gaitWeight*((0.05+0.25*rn)*(1.0-back*1.3)*(1.0-0.55*s.aimWeight)+0.14*s.goo);
        s.p[Hips]+=lean*0.3; s.p[Spine]+=lean*0.45; s.p[Chest]+=lean*0.25;
        const double lat=clamp(s.kgx/6.0,-1.0,1.0)*s.gaitWeight*(1.0-0.3*s.goo);
        s.p[Spine+2]-=0.07*lat; s.p[Chest+2]-=0.04*lat; s.p[Hips+2]-=0.03*lat;
        const double squash=0.024*c2*rn*s.gaitWeight;
        s.p[SqY]*=1.0-squash; s.p[SqXZ]*=1.0+squash*0.45;
        s.p[Head]-=0.03*c2*rn*s.gaitWeight; s.p[Neck]+=0.02*c2*rn*s.gaitWeight;
    }

    // The source always evaluates the arm spring layer; at idle the spring target is zero,
    // but its relaxed elbow/shoulder offsets still shape the ready pose.
    const double rn=s.runWeight, rw=rn*s.gaitWeight, armA=s.gaitWeight*(0.3+0.65*rn)*(1.0-0.35*s.goo);
    const double target=armA*std::cos(tw*(std::fmod(s.phase,1.0)-0.03));
    const double aL=spring(s.springs,SArmL,target,3.2+1.6*rn,0.5,dt);
    const double aR=spring(s.springs,SArmR,-target,3.2+1.6*rn,0.5,dt);
    // Source _poseWeapon computes wTwo after the arm spring layer, so use last frame's weight here.
    const double freeArm=1.0-s.twoWeight;
    s.p[UArmL]+=aL*freeArm; s.p[UArmR]+=aR;
    s.p[FArmL]-=(s.gaitWeight*(0.3+1.15*rn)+0.4*std::max(0.0,-aL)*rn)*freeArm;
    s.p[UArmL+2]+=(0.05+0.13*rw+0.08*std::max(0.0,aL)*rn+0.35*s.goo*s.gaitWeight)*freeArm;
    s.p[UArmL+1]+=0.18*std::max(0.0,-aL)*rn*freeArm;
    s.p[ClavL+1]+=0.09*aL*freeArm; s.p[ClavR+1]-=0.07*aR;
    s.p[ClavL]-=0.05*std::max(0.0,-aL)*rn*freeArm; s.p[HandL]-=0.15*rw*freeArm;
    s.p[HandPL]=1.0-0.45*rw;
    if(s.tired>0.01) {
        s.p[Spine]+=0.1*s.tired; s.p[Chest]+=0.08*s.tired; s.p[Neck]+=0.05*s.tired;
        s.p[HipsP+1]-=0.02*s.tired; s.p[ClavL+2]-=0.05*s.tired; s.p[ClavR+2]+=0.05*s.tired;
    }
    const double leanGate=1.0-s.airWeight*0.75;
    const double leanP=spring(s.springs,SLeanP,clamp(s.kaz*0.0075,-0.36,0.3)*leanGate,2.2,0.4,dt);
    const double leanR=spring(s.springs,SLeanR,clamp(-s.kax*0.0068,-0.34,0.34)*leanGate,2.0,0.48,dt);
    s.p[Hips]+=leanP*0.3; s.p[Spine]+=leanP*0.42; s.p[Chest]+=leanP*0.28;
    s.p[Hips+2]+=leanR*0.4; s.p[Spine+2]+=leanR*0.35; s.p[Chest+2]+=leanR*0.25;
    s.p[HipsP+1]-=std::abs(leanP)*0.06+std::abs(leanR)*0.03;
    s.p[HipsP+2]+=spring(s.springs,SLagZ,clamp(-s.kaz*0.0012,-0.055,0.07)*leanGate,2.3,0.6,dt);
    s.p[HipsP]+=spring(s.springs,SLagX,clamp(-s.kax*0.0012,-0.05,0.05)*leanGate,2.3,0.6,dt);
    s.p[HipsP+1]+=clamp(spring(s.springs,SPelY,0.0,3.4,0.42,dt),-0.2,0.08);
    const double squash=spring(s.springs,SSq,0.0,4.8,0.34,dt);
    s.p[SqY]*=1.0+clamp(squash,-0.22,0.16); s.p[SqXZ]*=1.0-clamp(squash,-0.22,0.16)*0.5;
    const double hitPitch=spring(s.springs,SHitP,0.0,3.4,0.38,dt);
    const double hitRoll=spring(s.springs,SHitR,0.0,3.4,0.38,dt);
    const double hitYaw=spring(s.springs,SHitY,0.0,3.4,0.42,dt);
    s.p[Spine]+=hitPitch*0.5; s.p[Chest]+=hitPitch*0.6; s.p[Hips]+=hitPitch*0.2;
    s.p[Spine+2]+=hitRoll*0.5; s.p[Chest+2]+=hitRoll*0.5;
    s.p[Spine+1]+=hitYaw*0.5; s.p[Chest+1]+=hitYaw*0.6;
    s.p[HipsP+2]+=hitPitch*0.04+spring(s.springs,SStag,0.0,2.2,0.5,dt)*0.05;
    const double clavSpring=spring(s.springs,SClav,0.0,3.5,0.45,dt);
    s.p[ClavL+2]+=clavSpring*0.12; s.p[ClavR+2]-=clavSpring*0.12;
    s.p[UArmL+2]-=clavSpring*0.08; s.p[UArmR+2]+=clavSpring*0.08;
    s.p[HipsP+1]-=std::abs(hitPitch)*0.08;

    if(s.airWeight>0.001) s.poseAir(s.airWeight);
    s.poseWeapon(input,dt);
    if(s.timers[TFlick]<0.7) {
        if(s.weapon=="brush") s.poseSwipe(s.timers[TFlick]);
        else if(s.weapon=="bucket") s.poseBucket(s.timers[TFlick]);
        else if(GetCharacterWeaponHold(s.weapon).animKind=="roller") s.poseFlick(s.timers[TFlick]);
    }
    if(s.timers[TSlosh]<0.66&&GetCharacterWeaponHold(s.weapon).fire=="slosh") s.poseSlosh(s.timers[TSlosh],dt);
    s.poseBlade(dt,input);
    if(s.subWeight>0.001&&s.timers[TThrow]>0.05) s.poseSubAim(s.subWeight);
    if(s.timers[TThrow]<0.62) s.poseThrow(s.timers[TThrow]);
    if(s.timers[TLeap]<1.9) s.poseLeap(s.timers[TLeap]);
    if(s.timers[TSlam]<1.4) s.poseSlam(s.timers[TSlam],input.grounded);
    if(s.timers[TLand]<0.8&&s.landAmplitude>0.3&&s.form==CharacterForm::Kid&&input.grounded&&s.timers[TSpawn]>1.4) s.poseLand(s.timers[TLand]);
    if(s.timers[TDodge]<s.dodgeDuration+0.3) s.poseDodge(s.timers[TDodge]);
    else { s.tumble=0.0; s.tumbleDrop=0.0; }
    s.poseForm(dt);
    s.poseMitts(input,dt);
    s.poseLook(dt,input,scene);
    s.poseFace(dt,input);
    s.blinkTick(dt,scene.lifeLevel);
    if(s.danceWeight>0.001||!s.dance.empty()) {
        std::array<double,CharacterPoseChannelCount> dancePose;
        s.neutralPose(dancePose);
        if(!s.dance.empty()) {
            s.poseDance(dancePose,s.dance,s.danceTime+s.danceOffset,dt);
            if(s.danceFade<1.0&&!s.previousDance.empty()) {
                std::array<double,CharacterPoseChannelCount> previousPose;
                s.neutralPose(previousPose);
                s.poseDance(previousPose,s.previousDance,s.previousDanceTime+s.danceOffset,0.0);
                poseLerp(dancePose,previousPose,dancePose,ease(s.danceFade));
            }
            s.lastDance=s.dance;
        } else if(!s.lastDance.empty()) s.poseDance(dancePose,s.lastDance,s.danceTime+s.danceOffset,dt);
        poseLerp(s.p,s.p,dancePose,ease(s.danceWeight));
    }
    if(scene.lifeLevel>0) {
        const double bu=fract(s.breathPhase), bn=std::floor(s.breathPhase);
        if(bn!=s.breathNumber) {
            s.breathNumber=bn; s.breathAmplitude=0.8+0.4*s.random(); s.breathRate=0.88+0.24*s.random();
            if(s.sigh>0.0) s.sigh=0.0;
            else if(s.idleTime>3.0||(!scene.inWorld&&s.danceWeight>0.5&&s.dance!="victory")) {
                s.sighTime-=1.0;
                if(s.sighTime<=0.0) { s.sighTime=5.0+s.random()*6.0; s.sigh=1.0; }
            }
        }
        const double breath=bu<0.4?ease(bu/0.4):bu<0.85?1.0-ease((bu-0.4)/0.45):0.0;
        const double brA=(1.0+1.3*std::max(s.exert,s.tired))*s.breathAmplitude*(s.sigh>0.0?2.3:1.0)*(s.form==CharacterForm::Kid?1.0:0.0);
        const double br=(breath*2.0-1.0)*brA;
        s.p[Chest]-=0.034*br; s.p[Spine]-=0.012*br; s.p[ClavL+2]+=0.032*br; s.p[ClavR+2]-=0.032*br;
        s.p[ClavL]-=0.012*br; s.p[ClavR]-=0.012*br; s.p[UArmL+2]+=0.008*br; s.p[UArmR+2]-=0.008*br;
        s.p[HipsP+1]-=0.003*br*idleWeight; s.p[Head]+=0.016*br; s.p[Neck]+=0.004*br;
        s.p[Anc+1]+=0.003*br*s.danceWeight; s.p[Breath]=breath;
        if(s.sigh>0.0&&bu>0.4) {
            const double k=win(bu,0.4,0.5,0.75,0.95);
            s.p[MOpen]=std::max(s.p[MOpen],0.2*k); s.p[Eye]-=0.25*k; s.p[Brow]+=0.2*k; s.p[MCurve]-=0.2*k;
            s.p[ClavL+2]-=0.03*k; s.p[ClavR+2]+=0.03*k;
        }
        if(!scene.inWorld&&s.attentionOn&&s.form==CharacterForm::Kid&&s.timers[TSpawn]>1.4) {
            const double headWeight=(s.dance=="victory"?0.3:s.dance=="defeat"?0.25:!s.dance.empty()?0.55:0.75)*(1.0-s.airWeight);
            const double currentYaw=s.p[ModelR+1]+s.p[Hips+1]+s.p[Spine+1]+s.p[Chest+1]+s.p[Neck+1]+s.p[Head+1]+s.p[Hly];
            const double currentPitch=s.p[Hlp]-(s.p[Neck]+s.p[Head]+0.5*(s.p[Chest]+s.p[Spine]));
            const double targetYaw=std::atan2(s.attentionPoint.x,s.attentionPoint.z);
            const double targetPitch=std::atan2(s.attentionPoint.y-1.2,std::max(0.3,std::hypot(s.attentionPoint.x,s.attentionPoint.z)));
            const double yaw=clamp(wrapAngle(targetYaw-currentYaw),-0.85,0.85)*headWeight;
            const double pitch=clamp(targetPitch-currentPitch,-0.35,0.35)*headWeight*0.6;
            const double attentionYaw=spring(s.springs,SAttentionY,yaw,1.5,0.78,dt);
            const double attentionPitch=spring(s.springs,SAttentionP,pitch,1.6,0.8,dt);
            s.p[Hly]+=attentionYaw; s.p[Hlp]+=attentionPitch; s.p[Neck+1]+=attentionYaw*0.25;
        } else {
            spring(s.springs,SAttentionY,0.0,1.5,0.78,dt);
            spring(s.springs,SAttentionP,0.0,1.6,0.8,dt);
        }
        s.tickMicroExpressions(dt);
        s.p[Pupil]=clamp(s.p[Pupil]+0.22*s.expressionWeights[8]+0.2*s.glowWeight-0.16*s.aimWeight
            +(s.dance=="victory"?0.2:0.0)+(!scene.inWorld&&s.attentionKind==1?0.12:0.0),0.0,1.0);
    }
    s.p[Blush]+=0.4*s.exert+0.2*s.tired+(s.dance=="victory"?0.5:0.0);

    auto& f=s.frame;
    s.updateWeaponSway(dt);
    f.root.position=input.position; f.root.rotation=glm::angleAxis(input.yaw,glm::dvec3(0,1,0)); f.root.visible=input.visible;
    f.model=CharacterTransform{};
    if(s.dodgeRollActive) {
        s.dodgeRollTime+=dt;
        const double u=s.dodgeRollTime/s.dodgeRollDuration;
        if(u>=1.0) {
            s.dodgeRollActive=false; f.model=CharacterTransform{};
        } else {
            const double c=std::cos(-input.yaw),sn=std::sin(-input.yaw);
            const double lx=s.dodgeRollDirection.x*c+s.dodgeRollDirection.y*sn;
            const double lz=-s.dodgeRollDirection.x*sn+s.dodgeRollDirection.y*c;
            auto axis=glm::dvec3(lz,0.0,-lx);
            if(glm::dot(axis,axis)<1e-8) axis={1.0,0.0,0.0};
            else axis=glm::normalize(axis);
            const double eased=u<0.5?2.0*u*u:1.0-2.0*(1.0-u)*(1.0-u);
            f.model.rotation=glm::angleAxis(tau*eased,axis);
            const glm::dvec3 center{0.0,0.55,0.0};
            f.model.position=center-f.model.rotation*center;
            f.model.position.y-=std::sin(3.14159265358979323846*u)*0.28;
        }
    }
    if(s.kidScale>0.001) {
        const double sx=std::max(1e-3,s.p[SqXZ]*s.kidSXZ*s.kidScale);
        const double sy=std::max(1e-3,s.p[SqY]*s.kidSY*s.kidScale);
        f.kid.scale=glm::dvec3(sx,sy,sx);
        f.kid.rotation=euler({s.p[ModelR],s.p[ModelR+1],s.p[ModelR+2]},true);
        const glm::dvec3 center{0.0,s.tumble!=0.0?0.56:0.62,0.0};
        if(s.tumble!=0.0) f.kid.rotation=glm::angleAxis(s.tumble,glm::normalize(glm::dvec3(s.tumbleX,0.0,s.tumbleZ)))*f.kid.rotation;
        const auto pivot=f.kid.rotation*center;
        f.kid.position=glm::dvec3(s.p[Model],s.p[Model+1]-s.tumbleDrop,s.p[Model+2])+center-pivot;
        f.kid.position.y+=s.kidLift;
        f.kid.visible=true;
    } else f.kid.visible=false;
    f.material.mouth=glm::dvec4(clamp(s.p[MCurve],-1.2,1.2),clamp(s.p[MWidth],0.2,1.4),clamp(s.p[MOpen],0.0,1.0),s.p[MTilt]);
    if(s.sqScale>0.001) f.material.time=s.time;
    s.updateSquid(dt,input,scene);
    s.tickWeaponAnimations(dt,input,scene);
    s.prepareLegTargets(dt,input);
    if(scene.lifeLevel>0) s.updateFingers(dt);
    s.updateBones(scene.lifeLevel);
    if(scene.lifeLevel>0) s.updateFace(scene,dt);
    if(scene.lifeLevel>0||!(s.hairOdd=!s.hairOdd)) { s.updateHair(dt+s.hairAccum,input.form==CharacterForm::Kid); s.hairAccum=0.0; }
    else s.hairAccum+=dt;
    if(scene.lifeLevel>0) {
        const double hemFl=spring(s.springs,SHemP,clamp(s.kaz*0.005+s.gs*0.022*s.gaitWeight,-0.3,0.3),3.0,0.2,dt);
        const double hemLift=spring(s.springs,SHemV,clamp(-s.vy*0.018,-0.1,0.25),3.4,0.25,dt);
        s.hemSide=spring(s.springs,SHemR,clamp(-s.kax*0.005,-0.15,0.15),3.0,0.25,dt);
        const double hemFlutter=0.05*std::sin(tau*2.0*s.phase+0.7)*s.gaitWeight*s.runWeight;
        s.hemFront=clamp(std::min(0.0,hemFl)-std::max(0.0,hemLift)-std::max(0.0,hemFlutter),-0.25,0.0);
        s.hemBack=clamp(std::max(0.0,hemFl)+std::max(0.0,hemLift)+std::max(0.0,-hemFlutter),0.0,0.25);
        s.hemCenter=clamp(hemFl,-0.25,0.25);
    }
    const double tankBob=s.gaitWeight*s.runWeight*std::cos(tau*2.0*(s.phase-s.duty*0.5));
    s.tankY=spring(s.springs,STankY,clamp(-s.headAcceleration.y*0.001,-0.012,0.012),4.5,0.22,dt);
    s.tankRotX=spring(s.springs,STankXRot,clamp(s.kaz*0.0022+0.035*tankBob-s.vy*0.004,-0.12,0.12),3.0,0.26,dt);
    s.tankRotZ=spring(s.springs,STankZRot,clamp(-s.kax*0.002+0.02*std::sin(tau*s.phase)*s.gaitWeight,-0.1,0.1),3.0,0.26,dt);
    const double tankFillTx=spring(s.springs,STankX,clamp(s.kax*0.004,-0.1,0.1),1.6,0.14,dt);
    const double tankFillTz=spring(s.springs,STankZ,clamp(-s.kaz*0.004,-0.1,0.1),1.6,0.14,dt);
    s.tankFillRotX=clamp(tankFillTz,-0.06,0.06);
    s.tankFillRotZ=clamp(tankFillTx,-0.06,0.06);
    s.tankFillLevel=spring(s.springs,STankL,0.0,2.4,0.18,dt);
    s.tankSlosh=std::hypot(tankFillTx,tankFillTz);
    if(scene.lifeLevel>0) {
        s.earTimer-=dt;
        if(s.earTimer<=0.0) {
            s.earTimer=3.0+s.random()*6.0;
            const std::size_t ear=s.random()<0.5?SEarL:SEarR;
            s.springs[ear+1]+=s.random()<0.7?5.0:-4.0;
        }
        s.springs[SEarL+1]+=(s.headRotationY*1.2-s.headRotationZ*1.6)*60.0*dt;
        s.springs[SEarR+1]+=(-s.headRotationY*1.2-s.headRotationZ*1.6)*60.0*dt;
        const double earSignal=clamp(s.p[Ears],-1.0,1.0);
        const double hatDrop=s.style.hat==3?-0.35:s.style.hat==1?-0.3:0.0;
        const double earBase=hatDrop+(earSignal>=0.0?(hatDrop!=0.0?0.04:0.2)*earSignal:0.3*earSignal);
        spring(s.springs,SEarL,earBase,3.6,0.2,dt);
        spring(s.springs,SEarR,earBase,3.6,0.2,dt);
    }
    s.updateBones(scene.lifeLevel);
    for(std::size_t i=0;i<f.channels.size();++i) f.channels[i]=static_cast<float>(s.p[i]);
    s.updateParts();
    s.updateMaterials(input);
    return f;
}

const CharacterPoseFrame& CharacterPose::frame() const { return state_->frame; }

} // namespace restoration
