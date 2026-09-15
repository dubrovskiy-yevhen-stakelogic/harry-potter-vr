#include "hpvr/quest_voice_cast.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
using namespace hpvr::quest;
VoiceCastArm Armed(std::uint64_t generation = 1) {
    return {generation, true, true, true, true, true};
}
void TestTransportAndPermissions() {
    for (unsigned flags = 0; flags < 32; ++flags) {
        for (auto generation : {0ULL, 1ULL, 255ULL, 123456789ULL,
                                static_cast<unsigned long long>(kVoiceMaxGeneration)}) {
            VoiceCastArm arm{generation, (flags & 1) != 0, (flags & 2) != 0,
                (flags & 4) != 0, (flags & 8) != 0, (flags & 16) != 0};
            auto decoded = UnpackVoiceArm(PackVoiceArm(arm));
            Check(decoded.generation == generation, "generation survives atomic command transport");
            Check(decoded.enabled == arm.enabled && decoded.permission_granted == arm.permission_granted &&
                  decoded.focused == arm.focused && decoded.gameplay_allowed == arm.gameplay_allowed &&
                  decoded.target_locked == arm.target_locked, "all permission and lifecycle gates roundtrip");
            Check(VoiceMayListen(decoded) == (flags == 31 && generation != 0),
                  "record only with every explicit permission and an actual target lock");
        }
    }
    Check(PackVoiceArm(Armed(kVoiceMaxGeneration + 1)) == 0, "overflow generation fails closed");
    Check(!VoiceMayListen(Armed(std::numeric_limits<std::uint64_t>::max())), "sentinel is not a capture command");
    Check((PackVoiceArm(Armed(kVoiceMaxGeneration)) >> 63) == 0, "capture can never encode shutdown");
}
void TestExactWordAndDuration() {
    VoiceCastGate gate;
    VoiceCastEvent event;
    const auto arm = Armed();
    for (auto word : {"", "noise", "lumos", "flip", "endo", "flippendo", "flipendo?",
                      "say flipendo", "not flipendo", "FLIPENDO", "f l i p e n d o"}) {
        Check(!gate.Accept(arm, 1, word, 0.8F, &event), "non-keyword text never becomes a cast");
    }
    for (float seconds : {-1.0F, 0.0F, 0.01F, 0.249F, 1.801F, 100.0F,
                           std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::infinity()}) {
        Check(!gate.Accept(arm, 1, "flipendo", seconds, &event), "invalid or implausible keyword duration rejected");
    }
    Check(!gate.Accept(arm, 1, "flipendo", 0.8F, nullptr), "null result cannot consume an attempt");
    Check(gate.Accept(arm, 1, "flipendo", 0.8F, &event), "matching acoustic keyword accepted");
    Check(event.generation == 1 && event.utterance_seconds == 0.8F, "result retains captured target generation");
    Check(!gate.CanListen(arm), "accepted attempt closes microphone");
    Check(!gate.Accept(arm, 1, "flipendo", 0.8F, &event), "repeated detector result cannot cast twice");
}
void TestStaleResults() {
    VoiceCastGate gate;
    VoiceCastEvent event;
    auto arm = Armed(10);
    Check(!gate.Accept(arm, 9, "flipendo", 0.8F, &event), "late audio from previous lock cannot hit a different target");
    Check(!gate.Accept(arm, 11, "flipendo", 0.8F, &event), "future generation cannot select the current target");
    for (unsigned flag = 0; flag < 5; ++flag) {
        auto blocked = arm;
        if (flag == 0) blocked.enabled = false;
        if (flag == 1) blocked.permission_granted = false;
        if (flag == 2) blocked.focused = false;
        if (flag == 3) blocked.gameplay_allowed = false;
        if (flag == 4) blocked.target_locked = false;
        Check(!gate.Accept(blocked, 10, "flipendo", 0.8F, &event), "pending event rejected on disable, permission loss, focus, cutscene or target loss");
    }
    Check(gate.Accept(arm, 10, "flipendo", kVoiceMinWordSeconds, &event), "lower timing boundary accepted");
    Check(!gate.Accept(Armed(9), 9, "flipendo", 0.8F, &event), "generation replay cannot resurrect an earlier attempt");
    Check(gate.Accept(Armed(11), 11, "flipendo", kVoiceMaxWordSeconds, &event), "fresh lock allows the next spell");
    Check(!VoiceEventIsCurrent(arm, event), "consumer independently checks the captured generation");
}
void TestNumericInputMeter() {
    VoiceInputMeter meter;
    Check(meter.Rms() == 0 && meter.samples == 0, "empty input has no fabricated level");
    const std::array<std::int16_t, 4> silence{};
    meter.Add(silence.data(), silence.size());
    Check(meter.samples == 4 && meter.nonzero == 0 && meter.peak == 0 && meter.Rms() == 0,
          "zero PCM is distinguishable from a microphone returning no frames");
    meter.Reset();
    const std::array<std::int16_t, 4> loud{32767, -32768, 32767, -32768};
    meter.Add(loud.data(), loud.size());
    Check(meter.samples == 4 && meter.nonzero == 4 && meter.clipped == 4 && meter.peak == 32768,
          "both signed clipping endpoints are counted without integer overflow");
    Check(std::isfinite(meter.Rms()) && meter.Rms() > 32767 && meter.Rms() <= 32768,
          "loud RMS uses a wide sum of squares");
    meter.Reset();
    Check(meter.samples == 0 && meter.nonzero == 0 && meter.clipped == 0 && meter.peak == 0 && meter.Rms() == 0,
          "capture reset retains no previous numeric level");
    const std::array<std::int16_t, 4> quiet{1, -1, 1, -1};
    meter.Add(quiet.data(), quiet.size());
    Check(meter.nonzero == 4 && meter.clipped == 0 && meter.peak == 1 && meter.Rms() == 1,
          "quiet nonzero input is not mislabeled as missing PCM");
}
void TestSpellIdentity() {
    auto arm = Armed(20);
    arm.spell = VoiceSpell::Alohomora;
    const auto unpacked = UnpackVoiceArm(PackVoiceArm(arm));
    Check(unpacked.spell == arm.spell && unpacked.generation == arm.generation,
          "atomic command preserves spell identity without changing generation");
    VoiceCastGate gate;
    VoiceCastEvent event;
    Check(!gate.Accept(arm,20,"flipendo",.8F,&event), "Flipendo cannot open an Alohomora target");
    Check(gate.Accept(arm,20,"alohomora",.8F,&event), "Alohomora accepts its own acoustic identity");
    arm.generation=21;arm.spell=VoiceSpell::Flipendo;
    Check(!gate.Accept(arm,21,"alohomora",.8F,&event), "Alohomora cannot cast Flipendo");
    Check(!VoiceEventIsCurrent(arm,event), "changing spell generation invalidates pending audio");
    arm.generation=22;arm.spell=VoiceSpell::Wingardium;
    Check(UnpackVoiceArm(PackVoiceArm(arm)).spell==VoiceSpell::Wingardium,"Wingardium survives atomic transport");
    Check(!gate.Accept(arm,22,"flipendo",.8F,&event),"Flipendo cannot levitate a block");
    Check(gate.Accept(arm,22,"wingardium",2.5F,&event)&&VoiceEventIsCurrent(arm,event),
          "complete two-word Wingardium phrase accepts longer duration");
    arm.spell=static_cast<VoiceSpell>(3);
    Check(!VoiceMayListen(arm)&&PackVoiceArm(arm)==0, "unsupported spells fail closed");
}
}  // namespace

int main() {
    try {
        TestTransportAndPermissions(); TestExactWordAndDuration(); TestStaleResults(); TestNumericInputMeter(); TestSpellIdentity();
        std::cout << "voice casting policy: PASS (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "voice casting policy: FAIL: " << error.what() << '\n'; return 1;
    }
}
