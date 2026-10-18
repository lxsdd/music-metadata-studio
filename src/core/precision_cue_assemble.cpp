#include "djmeta/precision_cue_assemble.h"
#include "djmeta/precision_cue_filename.h"

#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace djmeta {
namespace {
constexpr std::int64_t kReviewDeltaMs = 1000;
constexpr std::int64_t kRejectDeltaMs = 3000;

std::uint64_t duration_difference(std::int64_t a, std::int64_t b) {
    return a >= b ? static_cast<std::uint64_t>(a - b)
                  : static_cast<std::uint64_t>(b - a);
}
} // namespace

PrecisionCuePreviewResult make_precision_cue_release_preview(
    const PrecisionCuePreviewRequest& request) {
    PrecisionCuePreviewResult result;
    const auto match = match_precision_cue_releases(request.reference, request.candidates);
    result.state = match.state;
    result.diagnostics = match.diagnostics;
    if (match.state != PrecisionCueMatchState::Selected ||
        !match.selected_candidate_index ||
        *match.selected_candidate_index >= request.candidates.size()) {
        result.diagnostics.emplace_back("RELEASE_NOT_UNAMBIGUOUSLY_MATCHED");
        return result;
    }
    const auto& selected = request.candidates[*match.selected_candidate_index];
    if (selected.tracks.empty()) {
        result.state = PrecisionCueMatchState::Rejected;
        result.diagnostics.emplace_back("EMPTY_RELEASE");
        return result;
    }
    const unsigned disc_count = selected.tracks.back().disc_number;
    if (disc_count == 0 || disc_count > 99 || request.physical_discs.size() != disc_count) {
        result.state = PrecisionCueMatchState::Rejected;
        result.diagnostics.emplace_back("PHYSICAL_DISC_COUNT_MISMATCH");
        return result;
    }

    std::vector<PrecisionCuePlan> staged;
    staged.reserve(disc_count);
    bool needs_review = false;
    for (unsigned number = 1; number <= disc_count; ++number) {
        const auto& physical = request.physical_discs[number - 1];
        if (!precision_cue_safe_mp3_basename(physical.mp3_basename)) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("UNSAFE_MP3_BASENAME_DISC_" +
                                            std::to_string(number));
            return result;
        }
        PrecisionCueDisc disc;
        disc.album_title = selected.album_title;
        disc.album_performer = selected.album_performer;
        disc.audio_file_basename = physical.mp3_basename;
        disc.genre = request.genre;
        disc.release_date = request.release_date;
        disc.disc_number = number;
        disc.total_discs = disc_count;
        for (const auto& track : selected.tracks) {
            if (track.disc_number == number)
                disc.tracks.push_back({track.title, track.performer, track.duration_ms});
        }
        if (disc.tracks.empty()) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("EMPTY_DISC_TRACKLIST");
            return result;
        }
        try {
            staged.push_back(make_precision_cue(disc));
        } catch (const std::exception&) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("CUE_STRUCTURE_INVALID");
            return result;
        }
        if (physical.measured_duration_ms < 0) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("INVALID_LOCAL_DURATION");
            return result;
        }
        if (physical.measured_duration_ms == 0) {
            needs_review = true;
            result.diagnostics.emplace_back("LOCAL_DURATION_UNKNOWN_DISC_" + std::to_string(number));
            continue;
        }
        const auto diff = duration_difference(
            physical.measured_duration_ms, staged.back().total_duration_ms);
        if (diff > static_cast<std::uint64_t>(kRejectDeltaMs)) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("LOCAL_DURATION_MISMATCH_DISC_" + std::to_string(number));
            return result;
        }
        if (diff > static_cast<std::uint64_t>(kReviewDeltaMs)) {
            needs_review = true;
            result.diagnostics.emplace_back("LOCAL_DURATION_REVIEW_DISC_" + std::to_string(number));
        }
    }
    result.discs = std::move(staged);
    result.provider = selected.provider;
    result.provider_release_id = selected.provider_release_id;
    result.state = needs_review ? PrecisionCueMatchState::Review : PrecisionCueMatchState::Selected;
    result.preview_qualified = !needs_review;
    return result;
}

} // namespace djmeta
