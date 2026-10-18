#include "djmeta/precision_cue.h"
#include "djmeta/precision_cue_filename.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace djmeta {
namespace {

constexpr std::int64_t kFramesPerSecond = 75;
constexpr std::int64_t kMillisecondsPerSecond = 1000;
constexpr std::int64_t kMaximumDiscMs = 24LL * 60 * 60 * 1000;

void validate_cue_text(std::string_view value, std::string_view field, bool required = true) {
    if (required && value.empty()) {
        throw std::invalid_argument(std::string(field) + " is empty");
    }
    for (const unsigned char c : value) {
        if (c < 0x20 || c == 0x7F || c == '"') {
            // The external CUE grammar does not portably represent a literal
            // double quote inside a quoted field; never silently alter tags.
            throw std::invalid_argument(std::string(field) + " contains an unsafe CUE character");
        }
    }
}

std::string quoted(std::string_view input, std::string_view field, bool required = true) {
    validate_cue_text(input, field, required);
    return "\"" + std::string(input) + "\"";
}


} // namespace

std::int64_t milliseconds_to_cue_frame(std::int64_t ms) {
    if (ms < 0 || ms > kMaximumDiscMs) {
        throw std::invalid_argument("CUE time must be within the supported 24-hour disc window");
    }
    // Rounded once from the absolute cumulative clock; exact integer arithmetic.
    return (ms * kFramesPerSecond + kMillisecondsPerSecond / 2) /
           kMillisecondsPerSecond;
}

std::string cue_frame_to_index(std::int64_t frame) {
    if (frame < 0 || frame > kMaximumDiscMs * kFramesPerSecond / 1000) {
        throw std::invalid_argument("CUE frame out of supported range");
    }
    const auto minutes = frame / (60 * kFramesPerSecond);
    const auto seconds = (frame / kFramesPerSecond) % 60;
    const auto frames = frame % kFramesPerSecond;
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << minutes << ':'
        << std::setw(2) << seconds << ':' << std::setw(2) << frames;
    return out.str();
}

PrecisionCuePlan make_precision_cue(const PrecisionCueDisc& disc) {
    validate_cue_text(disc.album_title, "album title");
    validate_cue_text(disc.album_performer, "album performer");
    validate_cue_text(disc.genre, "genre", false);
    validate_cue_text(disc.release_date, "release date", false);
    validate_cue_text(disc.audio_file_basename, "audio filename");
    if (!precision_cue_safe_mp3_basename(disc.audio_file_basename)) {
        throw std::invalid_argument("audio file must be an MP3 basename, not a path");
    }
    if (disc.disc_number == 0 || disc.total_discs == 0 ||
        disc.disc_number > disc.total_discs || disc.total_discs > 99) {
        throw std::invalid_argument("disc numbering is invalid");
    }
    if (disc.tracks.empty() || disc.tracks.size() > 99) {
        throw std::invalid_argument("CUE requires 1-99 physical tracks per file");
    }

    PrecisionCuePlan plan;
    std::ostringstream out;
    if (!disc.genre.empty()) out << "REM GENRE " << quoted(disc.genre, "genre") << '\n';
    if (!disc.release_date.empty()) out << "REM DATE " << quoted(disc.release_date, "release date") << '\n';
    out << "REM DISCNUMBER " << disc.disc_number << '\n'
        << "REM TOTALDISCS " << disc.total_discs << '\n'
        << "PERFORMER " << quoted(disc.album_performer, "album performer") << '\n'
        << "TITLE " << quoted(disc.album_title, "album title") << '\n'
        // foobar accepts WAVE declaration for a continuous MP3, matching
        // its embedded/external CUE conventions. No audio is decoded or written.
        << "FILE " << quoted(disc.audio_file_basename, "audio filename") << " WAVE\n";

    std::int64_t cumulative_ms = 0;
    for (std::size_t i = 0; i < disc.tracks.size(); ++i) {
        const auto& track = disc.tracks[i];
        validate_cue_text(track.title, "track title");
        validate_cue_text(track.performer, "track performer");
        if (track.duration_ms <= 0 || track.duration_ms > kMaximumDiscMs - cumulative_ms) {
            throw std::invalid_argument("invalid track duration or disc longer than 24 hours");
        }
        const auto frame = milliseconds_to_cue_frame(cumulative_ms);
        if (!plan.boundaries.empty() && frame <= plan.boundaries.back().cue_frame) {
            throw std::invalid_argument("two source tracks collapse to the same CUE INDEX frame");
        }
        plan.boundaries.push_back({static_cast<unsigned>(i + 1), cumulative_ms, frame});
        out << "  TRACK " << std::setfill('0') << std::setw(2) << (i + 1) << " AUDIO\n"
            << "    TITLE " << quoted(track.title, "track title") << '\n'
            << "    PERFORMER " << quoted(track.performer, "track performer") << '\n'
            << "    INDEX 01 " << cue_frame_to_index(frame) << '\n';
        cumulative_ms += track.duration_ms;
    }
    plan.total_duration_ms = cumulative_ms;
    plan.external_cue_utf8 = out.str();
    return plan;
}

PrecisionCueComparison classify_cue_delta(
    std::int64_t expected_frame, std::int64_t reference_frame) noexcept {
    if (expected_frame < 0 || reference_frame < 0) return PrecisionCueComparison::Review;
    if (expected_frame == reference_frame) return PrecisionCueComparison::ExactFrame;
    const auto larger = static_cast<std::uint64_t>(std::max(expected_frame, reference_frame));
    const auto smaller = static_cast<std::uint64_t>(std::min(expected_frame, reference_frame));
    return larger - smaller == 1 ? PrecisionCueComparison::WithinOneFrame
                                 : PrecisionCueComparison::Review;
}

} // namespace djmeta
