#pragma once
#include <cmath>
#include <optional>
namespace BF2VR {
struct MenuPoint { double x,y; };
inline std::optional<MenuPoint> MenuPointToNative(MenuPoint point,double width,double height,double scale) {
    if(!std::isfinite(scale)||scale<.25||scale>1 || width<=0 || height<=0)return std::nullopt;
    const double x=(point.x-width*.5)/scale+width*.5,y=(point.y-height*.5)/scale+height*.5;
    if(x<0||y<0||x>=width||y>=height)return std::nullopt;
    return MenuPoint{x,y};
}
}
