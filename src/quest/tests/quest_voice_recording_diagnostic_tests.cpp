#include "hpvr/quest_voice_recording_diagnostic.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using hpvr::quest::VoiceRecordingDiagnostic;
namespace fs = std::filesystem;
unsigned checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void Put(const fs::path& file, const std::string& content) {
    std::ofstream output(file, std::ios::binary);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    Check(static_cast<bool>(output), "test file write");
}
std::string Read(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    Check(static_cast<bool>(input), "test file read");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
fs::path Case(const fs::path& root, const char* name, const std::string& token = VoiceRecordingDiagnostic::kRequestToken) {
    auto files = root / name;
    fs::create_directories(files / "VoiceDiagnostic");
    if (!token.empty()) Put(files / "VoiceDiagnostic/request.txt", token);
    return files;
}
bool OutputExists(const fs::path& files) {
    return fs::exists(files / "VoiceDiagnostic/capture.pcm") || fs::exists(files / "VoiceDiagnostic/segments.csv");
}
void Run(const fs::path& root) {
    const std::array<std::int16_t, 4> first{1, -2, 300, -400};
    {
        VoiceRecordingDiagnostic diagnostic;
        diagnostic.Observe(1, first.data(), first.size());
        diagnostic.MarkMatch(); diagnostic.Boundary();
        Check(diagnostic.Finish(), "unconfigured finish is harmless");
        Check(diagnostic.samples() == 0, "unconfigured input ignored");
    }
    for (const auto& token : {std::string(), std::string("wrong"), std::string(VoiceRecordingDiagnostic::kRequestToken) + " ",
                             std::string(VoiceRecordingDiagnostic::kRequestToken) + "\n\n"}) {
        const auto files = Case(root, ("invalid-" + std::to_string(checks)).c_str(), token);
        VoiceRecordingDiagnostic diagnostic;
        Check(!diagnostic.Configure(files), "missing or inexact grant rejected");
        diagnostic.Observe(1, first.data(), first.size());
        Check(diagnostic.Finish(), "invalid grant writes no audio");
        Check(!OutputExists(files), "invalid grant produces no outputs");
    }
    for (const auto* ending : {"", "\n", "\r\n"}) {
        const auto files = Case(root, ("valid-" + std::to_string(checks)).c_str(), std::string(VoiceRecordingDiagnostic::kRequestToken) + ending);
        VoiceRecordingDiagnostic diagnostic;
        Check(diagnostic.Configure(files), "exact token with optional one newline accepted");
        Check(!fs::exists(files / "VoiceDiagnostic/request.txt"), "request atomically consumed");
        Check(fs::exists(files / "VoiceDiagnostic/consumed.txt"), "consent retained");
        diagnostic.Observe(3, first.data(), first.size()); diagnostic.MarkMatch();
        diagnostic.Observe(3, first.data(), 2);
        diagnostic.Boundary();
        diagnostic.Observe(3, first.data(), 1);
        diagnostic.Observe(4, first.data() + 1, 2);
        Check(!OutputExists(files), "no per-frame PCM disk writes");
        Check(diagnostic.samples() == 9 && diagnostic.Finish(), "nine exact samples finish");
        Check(!diagnostic.enabled() && diagnostic.finished(), "finish disables capture");
        Check(Read(files / "VoiceDiagnostic/segments.csv") == "generation,start_sample,end_sample,matched\n3,0,6,1\n3,6,7,0\n4,7,9,0\n", "fresh streams and successful match indexed exactly");
        const auto bytes = Read(files / "VoiceDiagnostic/capture.pcm");
        std::vector<std::int16_t> expected{1, -2, 300, -400, 1, -2, 1, -2, 300};
        Check(bytes == std::string(reinterpret_cast<const char*>(expected.data()), expected.size() * 2), "recorded bytes exactly match supplied decoder input");
        diagnostic.Observe(4, first.data(), 4);
        Check(diagnostic.samples() == 9 && diagnostic.Finish(), "post-finish input ignored and finish idempotent");
        VoiceRecordingDiagnostic restarted;
        Check(!restarted.Configure(files), "app restart cannot record again");
    }
    {
        const auto files = Case(root, "zero");
        VoiceRecordingDiagnostic diagnostic;
        Check(diagnostic.Configure(files) && diagnostic.Finish(), "zero-sample grant may finish");
        Check(!OutputExists(files), "zero samples produce no audio/metadata");
        VoiceRecordingDiagnostic restarted;
        Check(!restarted.Configure(files), "zero-sample consumed grant not reusable");
    }
    for (const auto* name : {"consumed.txt", "capture.pcm", "segments.csv"}) {
        const auto files = Case(root, (std::string("existing-") + name).c_str());
        Put(files / "VoiceDiagnostic" / name, "preserve");
        VoiceRecordingDiagnostic diagnostic;
        Check(!diagnostic.Configure(files), "existing output/consent refuses overwrite");
        Check(Read(files / "VoiceDiagnostic" / name) == "preserve", "existing bytes preserved");
        Check(fs::exists(files / "VoiceDiagnostic/request.txt"), "rejected request not consumed");
    }
    {
        const auto files = Case(root, "cap");
        VoiceRecordingDiagnostic diagnostic;
        Check(diagnostic.Configure(files), "bounded capture configured");
        std::vector<std::int16_t> audio(VoiceRecordingDiagnostic::kMaxSamples + 1000, 77);
        diagnostic.Observe(9, audio.data(), audio.size()); diagnostic.MarkMatch();
        Check(diagnostic.finished() && !diagnostic.failed() && diagnostic.samples() == 480000, "30-second cap flushes once");
        Check(fs::file_size(files / "VoiceDiagnostic/capture.pcm") == 960000, "PCM cannot exceed 30 seconds");
        Check(Read(files / "VoiceDiagnostic/segments.csv") == "generation,start_sample,end_sample,matched\n9,0,480000,0\n", "clipped final match is unannotated");
    }
    {
        const auto files = Case(root, "pause-resume-total-cap");
        VoiceRecordingDiagnostic diagnostic;
        Check(diagnostic.Configure(files), "pause-resume diagnostic configured");
        std::vector<std::int16_t> first_attempt(59202, 11), second_attempt(27842, -22);
        diagnostic.Observe(5, first_attempt.data(), first_attempt.size());
        // Preserve an earlier unsuccessful attempt, then a successful attempt.
        diagnostic.Boundary();
        diagnostic.Observe(7, second_attempt.data(), second_attempt.size()); diagnostic.MarkMatch();
        diagnostic.Boundary();
        Check(diagnostic.enabled() && !diagnostic.finished() && diagnostic.samples() == 87044,
              "5.44-second partial trace remains active across capture close");
        for (unsigned pause = 0; pause < 16; ++pause) diagnostic.Boundary();
        Check(diagnostic.samples() == 87044 && !OutputExists(files), "idle/focus gaps append no audio and do not flush");
        std::vector<std::int16_t> resumed(VoiceRecordingDiagnostic::kMaxSamples - 87044 + 320, 33);
        diagnostic.Observe(9, resumed.data(), resumed.size());
        Check(diagnostic.finished() && !diagnostic.failed() && diagnostic.samples() == 480000,
              "resumed capture shares the original 30-second total cap");
        Check(Read(files / "VoiceDiagnostic/segments.csv") ==
              "generation,start_sample,end_sample,matched\n5,0,59202,0\n7,59202,87044,1\n9,87044,480000,0\n",
              "paused generations retain unsuccessful and successful early attempts");
        std::vector<std::int16_t> expected;
        expected.insert(expected.end(), first_attempt.begin(), first_attempt.end());
        expected.insert(expected.end(), second_attempt.begin(), second_attempt.end());
        expected.resize(VoiceRecordingDiagnostic::kMaxSamples, 33);
        Check(Read(files / "VoiceDiagnostic/capture.pcm") ==
              std::string(reinterpret_cast<const char*>(expected.data()), expected.size() * 2),
              "combined recording is exact retained input without pause samples");
    }
    {
        const auto files = Case(root, "segment-cap");
        VoiceRecordingDiagnostic diagnostic;
        Check(diagnostic.Configure(files), "segment bound configured");
        for (std::size_t i = 0; i <= VoiceRecordingDiagnostic::kMaxSegments; ++i) {
            diagnostic.Observe(i, first.data(), 1); diagnostic.Boundary();
        }
        Check(diagnostic.finished() && diagnostic.samples() == VoiceRecordingDiagnostic::kMaxSegments, "metadata is bounded under hostile boundary rate");
    }
    {
        const auto files = Case(root, "changed-output");
        VoiceRecordingDiagnostic diagnostic;
        Check(diagnostic.Configure(files), "changed destination configured");
        diagnostic.Observe(1, first.data(), first.size());
        Put(files / "VoiceDiagnostic/capture.pcm", "preserve-late-file");
        Check(!diagnostic.Finish() && diagnostic.failed(), "late output collision fails closed");
        Check(Read(files / "VoiceDiagnostic/capture.pcm") == "preserve-late-file", "late output untouched");
    }
    {
        const auto files = Case(root, "hardlink");
        fs::create_hard_link(files / "VoiceDiagnostic/request.txt", files / "token-copy");
        VoiceRecordingDiagnostic diagnostic;
        Check(!diagnostic.Configure(files), "hard-linked request refused");
    }
    {
        const auto files = Case(root, "noncanonical");
        VoiceRecordingDiagnostic diagnostic;
        Check(!diagnostic.Configure(files / "."), "noncanonical root refused");
    }
    {
        const auto files = Case(root, "null-input");
        VoiceRecordingDiagnostic diagnostic;
        Check(diagnostic.Configure(files), "null input test configured");
        diagnostic.Observe(1, nullptr, 1);
        Check(!diagnostic.enabled() && diagnostic.failed(), "invalid input stops diagnostic without throwing");
        Check(!OutputExists(files), "invalid input creates no audio");
    }
    // Creating symlinks may require Windows developer/admin permission. Test them
    // where supported; a skipped capability is reported, never counted as a pass.
    {
        const auto target = Case(root, "symlink-target");
        const auto alias = root / "symlink-alias";
        std::error_code error;
        fs::create_directory_symlink(target, alias, error);
        if (!error) {
            VoiceRecordingDiagnostic diagnostic;
            Check(!diagnostic.Configure(alias), "symlinked root refused");
        } else std::cout << "SKIP symlink creation unavailable: " << error.message() << '\n';
    }
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const auto base = argc > 1 ? fs::path(argv[1]) : fs::current_path() / "local/voice-recording-test-output";
        fs::create_directories(base);
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto root = fs::canonical(base) / ("run-" + suffix);
        Check(fs::create_directory(root), "unique isolated test directory");
        Run(root);
        std::cout << "PASS " << checks << " diagnostic checks; test output: " << root << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}
