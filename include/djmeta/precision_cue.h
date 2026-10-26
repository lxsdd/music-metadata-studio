#pragma once

// Precision CUE planning only. Pure, deterministic and completely write-free.
// Source album matching and safe host authorization belong to separate layers.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

struct PrecisionCueTrack {
    std::string title;
    std::string performer;
    std::int64_t duration_ms = 0;
};

struct PrecisionCueDisc {
    std::string album_title;
    std::string album_performer;
    std::string audio_file_basename;
    std::string genre;
    std::string release_date;
    unsigned disc_number = 1;
    unsigned total_discs = 1;
    std::vector<PrecisionCueTrack> tracks;
};

struct PrecisionCueBoundary {
    unsigned track_number = 0;
    std::int64_t elapsed_ms = 0;
    std::int64_t cue_frame = 0; // 75 frames/second; not a PCM sample offset.
};

struct PrecisionCuePlan {
    std::string external_cue_utf8;
    std::vector<PrecisionCueBoundary> boundaries;
    std::int64_t total_duration_ms = 0;
};

// Rounds the cumulative source millisecond clock ONCE at each INDEX 01.
// Never round individual track durations before summation.
// Throws std::invalid_argument or std::overflow_error on invalid input.
std::int64_t milliseconds_to_cue_frame(std::int64_t ms);
std::string cue_frame_to_index(std::int64_t frame);
PrecisionCuePlan make_precision_cue(const PrecisionCueDisc& disc);

enum class PrecisionCueComparison {
    ExactFrame,
    WithinOneFrame,
    Review,
};

// Compare exact CUE frame positions, not approximated seconds.
// A frame match is not proof of sample-perfect source identity.
PrecisionCueComparison classify_cue_delta(
    std::int64_t expected_frame, std::int64_t reference_frame) noexcept;

} // namespace djmeta
