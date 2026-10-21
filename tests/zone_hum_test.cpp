#include "ZoneHum.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
void check(bool x,const char* message){if(!x)throw std::runtime_error(message);}
int main(){try{using namespace restoration;
    std::vector<float> a(96000),b(a.size()),c(a.size()),high(a.size()),split(a.size());
    ZoneHum neutral,ours,theirs,tense,chunked;ours.Set(1,0);theirs.Set(2,0);tense.Set(0,1);
    neutral.Render(a.data(),a.size());ours.Render(b.data(),b.size());theirs.Render(c.data(),c.size());tense.Render(high.data(),high.size());
    for(std::size_t i=0;i<split.size();i+=128)chunked.Render(split.data()+i,std::min<std::size_t>(128,split.size()-i));
    double energy=0,ownDifference=0,theirDifference=0,tensionDifference=0;
    for(std::size_t i=0;i<a.size();++i){check(std::isfinite(a[i])&&std::isfinite(b[i])&&std::isfinite(c[i])&&std::isfinite(high[i]),"Zone hum produced non-finite PCM");check(std::abs(a[i])<1&&std::abs(b[i])<1&&std::abs(c[i])<1,"Zone hum clipped before mixing");check(a[i]==split[i],"Audio callback block size changed oscillator continuity");energy+=a[i]*a[i];ownDifference+=std::abs(a[i]-b[i]);theirDifference+=std::abs(a[i]-c[i]);tensionDifference+=std::abs(a[i]-high[i]);}
    check(energy>1&&ownDifference>100&&theirDifference>100&&tensionDifference>100,"Ownership and tension must audibly change the hum, rather than reuse a fixed loop");
    theirs.Set(1,.9);theirs.Render(a.data(),a.size());for(const auto sample:a)check(std::isfinite(sample)&&std::abs(sample)<1,"Live mode transition became unstable");
    theirs.Set(100,std::numeric_limits<double>::quiet_NaN());theirs.Render(a.data(),128);for(int i=0;i<128;++i)check(std::isfinite(a[i]),"Invalid external parameters must remain finite");
    std::cout<<"Zone hum modes, tension, continuous callbacks and parameter bounds passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
