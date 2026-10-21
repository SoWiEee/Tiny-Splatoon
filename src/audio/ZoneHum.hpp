#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace restoration {
// Source zone_hum's continuous oscillator/filter controls, rendered by miniaudio.
class ZoneHum {
public:
    ZoneHum();
    void Set(int mode,double tension); // 0 neutral, 1 ours, 2 theirs; called by the game thread
    void Render(float* mono,std::size_t frames); // fixed 48 kHz, called by the audio thread
private:
    struct Filter {
        double b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
        void Tune(double frequency,double q,bool bandpass);
        double Process(double sample);
    };
    std::atomic<int> mode_{0};
    std::atomic<double> tension_{0};
    std::array<double,4> phase_{};
    double pulse_=0,frequency_=196,interval_=1.5,detune_=4,hard_=0,cutoff_=1300,rate_=.7,depth_=.18,air_=.05;
    Filter low_,band_;
    std::size_t noise_=0;
    unsigned filterStep_=0;
};
}
