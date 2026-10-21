#pragma once
#include <array>
#include <cmath>
#include <charconv>
#include <string_view>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>

namespace restoration {
inline const std::array<glm::dvec3,2> SourceInkColors{{{1,.2541520943200296,.0069954101845983935},{.028426039499072558,.10461648408208657,1}}};
inline std::array<glm::dvec3,2> InkColors=SourceInkColors;
inline std::array<glm::vec3,2> SrgbInkColors{{{1,.5412f,.0784f},{.1843f,.3569f,1}}};
inline void SetInkPalette(const std::array<std::string_view,2>& colors){
    for(int i=0;i<2;++i){
        unsigned rgb=0;const auto text=colors[i];
        if(text.size()!=7||text.front()!='#')continue;
        const auto parsed=std::from_chars(text.data()+1,text.data()+7,rgb,16);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+7)continue;
        for(int c=0;c<3;++c){
            const double s=double((rgb>>(16-8*c))&255)/255;
            SrgbInkColors[i][c]=float(s);InkColors[i][c]=s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);
        }
    }
}
} // namespace restoration
