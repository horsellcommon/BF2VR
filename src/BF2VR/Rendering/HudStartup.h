#pragma once
#include <cstdint>
namespace BF2VR {
// Require a unique full-resolution sRGB alpha UI target for three complete frames.
// A missing or ambiguous candidate resets confirmation instead of guessing an index.
class HudStartupSelection {
    uintptr_t previous=0;
    unsigned confirmations=0;
public:
    static bool Matches(unsigned format,unsigned width,unsigned height,unsigned screenWidth,unsigned screenHeight) {
        return format==29 && width==screenWidth && height==screenHeight && width>=640 && height>=480;
    }
    static bool NeedsRediscovery(uint64_t frame, uint64_t producer, uint64_t binding) {
        return frame-producer>=3 || frame-binding>=3;
    }
    bool Confirm(uintptr_t uniqueCandidate) {
        if(!uniqueCandidate) { previous=0;confirmations=0;return false; }
        if(previous!=uniqueCandidate) { previous=uniqueCandidate;confirmations=0; }
        return ++confirmations>=3;
    }
};
}
