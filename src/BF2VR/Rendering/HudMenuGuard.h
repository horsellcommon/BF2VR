#pragma once
namespace BF2VR {
// Preserve native hit testing by leaving visible-cursor UI untransformed.
// Unknown/error states bypass processing; background rendering keeps the last
// game-owned cursor observation rather than using another app's cursor.
class HudMenuGuard {
    bool allow=false;
public:
    bool Observe(bool gameForeground,bool querySucceeded,bool cursorVisible) {
        if(gameForeground)allow=querySucceeded && !cursorVisible;
        return allow;
    }
};
}
