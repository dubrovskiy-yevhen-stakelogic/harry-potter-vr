#pragma once

namespace hpvr::quest {

// Both stick clicks recenter once. Startup, tracking loss and focus loss each
// require a fresh neutral sample before accepting a chord.
struct RecenterChord {
    bool consumed = false;

    void Reset() { armed_ = false; consumed = false; }

    [[nodiscard]] bool Update(bool focused, bool tracked, bool left_click, bool right_click) {
        if (!focused || !tracked) {
            armed_ = false;
            consumed = left_click || right_click;
            return false;
        }
        if (!left_click && !right_click) {
            armed_ = true;
            consumed = false;
            return false;
        }
        if (!armed_) {
            consumed = true;
            return false;
        }
        if (left_click && right_click) {
            armed_ = false;
            consumed = true;
            return true;
        }
        return false;
    }

private:
    bool armed_ = false;
};

} // namespace hpvr::quest
