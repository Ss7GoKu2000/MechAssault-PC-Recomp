#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <filesystem>

// diagnostics switched on by environment variables, read once
namespace diag {

// XBE_PERF set: perf log lines once a second and the read-backs named
inline bool Perf() {
    static const bool on = std::getenv("XBE_PERF") != nullptr;
    return on;
}

// XBE_CAPTURE_DIR: where F3 saves a frame, empty: F3 does not capture
inline const std::filesystem::path& CaptureDir() {
    static const std::filesystem::path dir = [] {
        const wchar_t* v = _wgetenv(L"XBE_CAPTURE_DIR");
        return v ? std::filesystem::path(v) : std::filesystem::path();
    }();
    return dir;
}

inline std::atomic<bool> overlay_open{false};
inline std::atomic<bool> ultrawide_projection{true};
inline std::atomic<bool> overlay_capture{false};
enum class ProjectedDarkeningTest { Off, SkipDraws, BiasDepth };
inline std::atomic<ProjectedDarkeningTest> projected_darkening_test{ProjectedDarkeningTest::BiasDepth};

struct AudioEvents {
    std::array<std::array<char, 160>, 32> lines{};
    size_t next = 0;
    std::mutex lock;
};

inline AudioEvents audio_events;
inline const auto audio_epoch = std::chrono::steady_clock::now();
inline FILE* audio_trace = nullptr;  // guarded by audio_events.lock

inline double AudioSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - audio_epoch).count();
}

inline void SetAudioTrace(bool enabled, const std::filesystem::path& directory) {
    std::lock_guard guard(audio_events.lock);
    if (audio_trace) {
        std::fprintf(audio_trace, "%.3f end trace\n", AudioSeconds());
        std::fclose(audio_trace);
        audio_trace = nullptr;
    }
    if (!enabled) return;
    audio_trace = _wfopen((directory / "audio-trace.log").c_str(), L"a");
    if (audio_trace) {
        std::fprintf(audio_trace, "\n%.3f begin trace (time in seconds since game launch)\n", AudioSeconds());
        std::fflush(audio_trace);
    }
}

inline void AudioMixTrace(uint32_t voice, uint32_t data, double sample, size_t queued, uint32_t mixed,
                          bool paused, bool sync, float decoded, float contribution, float output) {
    std::lock_guard guard(audio_events.lock);
    if (!audio_trace) return;
    std::fprintf(audio_trace, "%.3f mix voice=%08X data=%08X sample=%.0f queue=%zu mixed=%u "
                 "paused=%u sync=%u decoded=%.6f contribution=%.6f output=%.6f\n",
                 AudioSeconds(), voice, data, sample, queued, mixed, unsigned(paused), unsigned(sync),
                 decoded, contribution, output);
    std::fflush(audio_trace);
}

inline void AudioEvent(const char* event, uint32_t voice, uint32_t data, double position, size_t queued) {
    if (!overlay_open.load(std::memory_order_relaxed)) return;
    const double seconds = AudioSeconds();
    std::lock_guard guard(audio_events.lock);
    auto& line = audio_events.lines[audio_events.next++ % audio_events.lines.size()];
    std::snprintf(line.data(), line.size(), "%8.3fs  %-20s %08X  %08X  %8.0f  %zu",
                  seconds, event, voice, data, position, queued);
    if (audio_trace) std::fprintf(audio_trace, "%s\n", line.data());
}

template<typename Display>
void ReadAudioEvents(Display display) {
    std::lock_guard guard(audio_events.lock);
    const size_t capacity = audio_events.lines.size();
    const size_t count = audio_events.next < capacity ? audio_events.next : capacity;
    for (size_t i = audio_events.next - count; i < audio_events.next; ++i)
        display(audio_events.lines[i % capacity].data());
}

}  // namespace diag
