#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace hpvr::quest {

// Local, one-shot developer diagnostic. The worker must compile this only in
// explicitly opted-in diagnostic APKs and pass ONLY current armed decoder input.
// No constructor/destructor I/O, automatic microphone access, or network access.
// Configure consumes an exact request token; an ordinary install records nothing.
class VoiceRecordingDiagnostic final {
public:
    static constexpr std::size_t kMaxSamples = 16000 * 30;
    static constexpr std::size_t kMaxSegments = 1024;
    static constexpr const char* kRequestToken = "HPVR_LOCAL_VOICE_RECORDING_30S";

    VoiceRecordingDiagnostic() = default;
    VoiceRecordingDiagnostic(const VoiceRecordingDiagnostic&) = delete;
    VoiceRecordingDiagnostic& operator=(const VoiceRecordingDiagnostic&) = delete;
    ~VoiceRecordingDiagnostic() { ClearPcm(); }

    [[nodiscard]] bool Configure(const std::filesystem::path& files_root) noexcept {
        if (configured_) return enabled_;
        configured_ = true;
        try {
            if (!SafeDirectory(files_root)) return false;
            directory_ = files_root / "VoiceDiagnostic";
            if (!SafeDirectory(directory_) || !UnusedOutputs()) return false;
            const auto request = directory_ / "request.txt";
            const auto consumed = directory_ / "consumed.txt";
            if (!PlainFile(request) || std::filesystem::exists(std::filesystem::symlink_status(consumed)))
                return false;
            const std::string expected(kRequestToken);
            const auto request_size = std::filesystem::file_size(request);
            if (request_size < expected.size() || request_size > expected.size() + 2) return false;
            std::ifstream input(request, std::ios::binary);
            std::string actual(static_cast<std::size_t>(request_size), '\0');
            input.read(actual.data(), static_cast<std::streamsize>(actual.size()));
            if (!input || (actual != expected && actual != expected + "\n" && actual != expected + "\r\n")) return false;
            input.close();
            // The private request is consumed once, before any samples are kept.
            // Concurrent callers cannot both rename the same source request.
            if (!Consume(request, consumed)) return false;
            if (!PlainFile(consumed) || !UnusedOutputs()) return false;
            pcm_.reserve(kMaxSamples);
            segments_.reserve(kMaxSegments);
            enabled_ = true;
            return true;
        } catch (...) {
            failed_ = true;
            enabled_ = false;
            return false;
        }
    }

    void Observe(std::uint64_t generation, const std::int16_t* pcm, std::size_t count) noexcept {
        if (!enabled_ || count == 0) return;
        if (pcm == nullptr) { failed_ = true; enabled_ = false; ClearPcm(); return; }
        try {
            if (!active_segment_ || segments_.back().generation != generation) {
                if (segments_.size() == kMaxSegments) { (void)Finish(); return; }
                segments_.push_back({generation, sample_count_, sample_count_, 0});
                active_segment_ = true;
            }
            const auto retained = std::min(count, kMaxSamples - sample_count_);
            pcm_.insert(pcm_.end(), pcm, pcm + retained);
            sample_count_ += retained;
            segments_.back().end = sample_count_;
            if (sample_count_ == kMaxSamples) (void)Finish();
        } catch (...) {
            failed_ = true; enabled_ = false; ClearPcm();
        }
    }

    // Call on decoder reset/target loss even if the next generation is unchanged.
    // Gaps are omitted from PCM, with a new row preserving each fresh stream.
    void Boundary() noexcept { active_segment_ = false; }

    // Call immediately after a successful Process. If Observe reached the cap,
    // Finish already ran and that last clipped block's match is intentionally not
    // annotated. Audio/segment lengths remain exact and never exceed 30 seconds.
    void MarkMatch() noexcept {
        if (enabled_ && active_segment_ && !segments_.empty()) segments_.back().matched = 1;
    }

    [[nodiscard]] bool Finish() noexcept {
        if (finished_) return !failed_;
        finished_ = true;
        const bool had_grant = enabled_;
        enabled_ = false;
        Boundary();
        if (!had_grant || sample_count_ == 0) { ClearPcm(); return !failed_; }
        try {
            if (!SafeDirectory(directory_) || !PlainFile(directory_ / "consumed.txt") || !UnusedOutputs())
                throw std::runtime_error("diagnostic destination changed");
            std::ostringstream csv;
            csv << "generation,start_sample,end_sample,matched\n";
            for (const auto& segment : segments_)
                csv << segment.generation << ',' << segment.start << ',' << segment.end << ',' << segment.matched << '\n';
            const auto metadata = csv.str();
            ExclusiveFile audio(directory_, "capture.pcm");
            ExclusiveFile index(directory_, "segments.csv");
            if (!audio.valid() || !index.valid() ||
                !audio.Write(pcm_.data(), pcm_.size() * sizeof(std::int16_t)) ||
                !index.Write(metadata.data(), metadata.size()))
                throw std::runtime_error("diagnostic write failed");
        } catch (...) { failed_ = true; }
        ClearPcm();
        return !failed_;
    }

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::size_t samples() const noexcept { return sample_count_; }

private:
    struct Segment { std::uint64_t generation; std::size_t start, end; unsigned matched; };
    static bool Consume(const std::filesystem::path& request, const std::filesystem::path& consumed) noexcept {
#if defined(_WIN32)
        return MoveFileW(request.c_str(), consumed.c_str()) != 0;
#elif defined(SYS_renameat2)
        // RENAME_NOREPLACE: retain the old consent record even under a race.
        return syscall(SYS_renameat2, AT_FDCWD, request.c_str(), AT_FDCWD, consumed.c_str(), 1U) == 0;
#else
        return false;  // No portable atomic no-replace rename: fail closed.
#endif
    }
    static bool NoLink(const std::filesystem::path& path) {
        const auto status = std::filesystem::symlink_status(path);
        if (!std::filesystem::exists(status) || std::filesystem::is_symlink(status)) return false;
#if defined(_WIN32)
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;
#endif
        return true;
    }
    static bool SafeDirectory(const std::filesystem::path& path) {
        if (!path.is_absolute() || path != path.lexically_normal()) return false;
        auto part = path.root_path();
        if (!NoLink(part)) return false;
        for (const auto& component : path.relative_path()) {
            part /= component;
            if (!NoLink(part)) return false;
        }
        return std::filesystem::is_directory(path) && std::filesystem::canonical(path) == path;
    }
    static bool PlainFile(const std::filesystem::path& path) {
        return NoLink(path) && std::filesystem::is_regular_file(path) && std::filesystem::hard_link_count(path) == 1;
    }
    bool UnusedOutputs() const {
        for (const auto* name : {"capture.pcm", "segments.csv"})
            if (std::filesystem::exists(std::filesystem::symlink_status(directory_ / name))) return false;
        return true;
    }
    void ClearPcm() noexcept {
        auto* bytes = reinterpret_cast<volatile unsigned char*>(pcm_.data());
        for (std::size_t i = 0; i < pcm_.size() * sizeof(std::int16_t); ++i) bytes[i] = 0;
        pcm_.clear();
    }

    // Exclusive creation: never truncate a file, never follow an output symlink.
    class ExclusiveFile final {
    public:
        ExclusiveFile(const std::filesystem::path& directory, const char* name) {
#if defined(_WIN32)
            handle_ = CreateFileW((directory / name).c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
#else
            const int parent = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (parent >= 0) {
                descriptor_ = openat(parent, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
                close(parent);
            }
#endif
        }
        ~ExclusiveFile() {
#if defined(_WIN32)
            if (valid()) CloseHandle(handle_);
#else
            if (valid()) close(descriptor_);
#endif
        }
        [[nodiscard]] bool valid() const noexcept {
#if defined(_WIN32)
            return handle_ != INVALID_HANDLE_VALUE;
#else
            return descriptor_ >= 0;
#endif
        }
        bool Write(const void* data, std::size_t count) noexcept {
            const auto* bytes = static_cast<const unsigned char*>(data);
            while (count != 0) {
#if defined(_WIN32)
                DWORD done = 0;
                if (!WriteFile(handle_, bytes, static_cast<DWORD>(std::min<std::size_t>(count, 1U << 20)), &done, nullptr) || done == 0)
                    return false;
#else
                const auto done = write(descriptor_, bytes, count);
                if (done < 0 && errno == EINTR) continue;
                if (done <= 0) return false;
#endif
                bytes += done; count -= done;
            }
            return true;
        }
    private:
#if defined(_WIN32)
        HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
        int descriptor_ = -1;
#endif
    };

    std::filesystem::path directory_;
    std::vector<std::int16_t> pcm_;
    std::vector<Segment> segments_;
    std::size_t sample_count_ = 0;
    bool configured_ = false, enabled_ = false, finished_ = false, failed_ = false, active_segment_ = false;
};

}  // namespace hpvr::quest
