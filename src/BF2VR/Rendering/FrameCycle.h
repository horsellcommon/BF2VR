// Copyright Ethan Porcaro

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

#pragma once
namespace BF2VR {
// The acquired eye never changes until its image has been released.
struct FrameCycle {
    int eye = 0;
    bool begun = false;
    bool acquired = false;
    void Reset() { eye = 0; begun = false; acquired = false; }
    bool ReleaseEye() {
        acquired = false;
        if (eye == 0) { eye = 1; return false; }
        Reset();
        return true;
    }
};
}
