#include "ZoneHum.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace restoration {
namespace {
constexpr double Pi=3.14159265358979323846,SampleRate=48000;
const std::vector<float>& PinkNoise(){
    static const auto noise=[] {
        std::vector<float> white(96000),out(96000),filtered(96000);std::uint32_t seed=23;
        for(auto& x:white){seed+=0x6d2b79f5u;auto t=seed;t=(t^(t>>15))*(t|1u);t^=t+((t^(t>>7))*(t|61u));x=float(double(t^(t>>14))/4294967296.0*2-1);}
        double b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0;
        for(int pass=0;pass<2;++pass)for(std::size_t i=0;i<out.size();++i){const double x=white[i];
            b0=.99886*b0+x*.0555179;b1=.99332*b1+x*.0750759;b2=.969*b2+x*.153852;b3=.8665*b3+x*.3104856;b4=.55*b4+x*.5329522;b5=-.7616*b5-x*.016898;
            const double y=b0+b1+b2+b3+b4+b5+b6+x*.5362;b6=x*.115926;if(pass)out[i]=float(y);
        }
        const double a=std::exp(-2*Pi*25/SampleRate);double y=0,previous=out.back();
        for(int pass=0;pass<2;++pass)for(std::size_t i=0;i<out.size();++i){const double x=out[i];y=a*(y+x-previous);previous=x;if(pass)filtered[i]=float(y);}
        double mean=0,ss=0;for(const auto x:filtered)mean+=x;mean/=filtered.size();
        for(auto& x:filtered){x=float(x-mean);ss+=x*x;}const double gain=.3/std::sqrt(ss/filtered.size());for(auto& x:filtered)x=float(x*gain);
        return filtered;
    }();return noise;
}
double Saw(double phase,double step){
    // PolyBLEP removes the discontinuity aliasing while controls move continuously.
    double edge=0;if(phase<step){const double x=phase/step;edge=x+x-x*x-1;}
    else if(phase>1-step){const double x=(phase-1)/step;edge=x*x+x+x+1;}
    return 2*phase-1-edge;
}
}
void ZoneHum::Filter::Tune(double frequency,double q,bool bandpass){
    const double w=2*Pi*std::clamp(frequency,10.,22000.)/SampleRate,c=std::cos(w),alpha=std::sin(w)/(2*q),a0=1+alpha;
    b0=(bandpass?alpha:(1-c)/2)/a0;b1=(bandpass?0:1-c)/a0;b2=(bandpass?-alpha:(1-c)/2)/a0;a1=-2*c/a0;a2=(1-alpha)/a0;
}
double ZoneHum::Filter::Process(double x){const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;x2=x1;x1=x;y2=y1;y1=y;return y;}
ZoneHum::ZoneHum(){(void)PinkNoise();low_.Tune(1300,.9,false);band_.Tune(2600,1.1,true);}
void ZoneHum::Set(int mode,double tension){mode_.store(std::clamp(mode,0,2),std::memory_order_relaxed);tension_.store(std::isfinite(tension)?std::clamp(tension,0.,1.):0.,std::memory_order_relaxed);}
void ZoneHum::Render(float* mono,std::size_t frames){
    static constexpr double modes[3][8]={{196,1.5,0,4,1300,.7,.18,.05},{220,1.26,.1,5,2300,1.1,.2,.02},{185,1.414,.26,24,950,1.1,.28,.02}};
    const auto& h=modes[mode_.load(std::memory_order_relaxed)];const double tension=tension_.load(std::memory_order_relaxed),k=1-std::exp(-1/(.22*SampleRate));
    const double f=h[0]*std::pow(2,2.5*tension/12),hard=h[2]*(1+.6*tension),cut=std::min(9000.,h[4]*(1+1.3*tension)),rate=h[5]*(1+4.5*tension),depth=std::min(.55,h[6]+.3*tension),air=h[7]+.06*tension;
    const auto& noise=PinkNoise();
    for(std::size_t i=0;i<frames;++i){
        frequency_+=(f-frequency_)*k;interval_+=(h[1]-interval_)*k;hard_+=(hard-hard_)*k;detune_+=(h[3]-detune_)*k;
        cutoff_+=(cut-cutoff_)*k;rate_+=(rate-rate_)*k;depth_+=(depth-depth_)*k;air_+=(air-air_)*k;
        if((filterStep_++&31)==0)low_.Tune(cutoff_,.9,false);
        const double s0=frequency_/SampleRate,s1=frequency_*interval_/SampleRate,s2=frequency_*.5/SampleRate,s3=s2*std::pow(2,detune_/1200);
        const double triangle0=2/Pi*std::asin(std::sin(2*Pi*phase_[0])),triangle1=2/Pi*std::asin(std::sin(2*Pi*phase_[1]));
        const double harmonic=.62*(triangle0+.42*triangle1)+hard_*(Saw(phase_[2],s2)+Saw(phase_[3],s3));
        const double amp=.72+depth_*std::sin(2*Pi*pulse_);
        const double sample=low_.Process(harmonic*amp)+band_.Process(noise[noise_])*air_;
        if(mono)mono[i]=float(sample*.14);
        const double steps[]={s0,s1,s2,s3};for(int c=0;c<4;++c){phase_[c]+=steps[c];phase_[c]-=std::floor(phase_[c]);}
        pulse_+=rate_/SampleRate;pulse_-=std::floor(pulse_);noise_=(noise_+1)%noise.size();
    }
}
}
