#include "djmeta/precision_cue_match.h"
#include "djmeta/precision_cue_cumulative_evidence.h"
#include "djmeta/precision_cue_unicode_text.h"
#include "djmeta/precision_cue_calendar_date.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace djmeta {
namespace {

constexpr std::int64_t kStrongDurationDeltaMs = 1000;
constexpr std::int64_t kRejectDurationDeltaMs = 3000;
constexpr std::int64_t kMaximumTrackMs = 24LL * 60 * 60 * 1000;

bool ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string canonical(std::string_view s) {
    std::string out;
    bool pending_space = false;
    for (unsigned char c : s) {
        if (ascii_space(c)) {
            pending_space = !out.empty();
        } else {
            if (pending_space) out.push_back(' ');
            pending_space = false;
            out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A'))
                                               : static_cast<char>(c));
        }
    }
    return out;
}

bool valid_field(const std::string& s, bool required = true) {
    return (!required || !canonical(s).empty()) && precision_cue_safe_unicode_text(s) && s.size() <= 4096;
}

bool valid_release(const PrecisionCueRelease& release, bool reference) {
    if (!valid_field(release.album_title) ||
        !valid_field(release.album_performer, false) ||
        !valid_field(release.provider, false) ||
        !valid_field(release.provider_release_id, false) ||
        !valid_field(release.genre, false) ||
        !valid_field(release.release_date, false) ||
        !precision_cue_valid_calendar_date(release.release_date) ||
        release.tracks.empty() || release.tracks.size() > 8u * 99u) return false;
    unsigned previous_disc = 0;
    unsigned previous_track = 0;
    std::int64_t cumulative_disc_ms = 0;
    for (const auto& track : release.tracks) {
        if (!valid_field(track.title) || !valid_field(track.performer, false) ||
            track.disc_number == 0 || track.disc_number > 8 ||
            track.track_number == 0 || track.track_number > 99 ||
            track.duration_ms < 0 || track.duration_ms > kMaximumTrackMs ||
            (!reference && track.duration_ms == 0)) return false;
        if (track.disc_number == previous_disc) {
            if (track.track_number != previous_track + 1) return false;
        } else if (track.disc_number != previous_disc + 1 || track.track_number != 1) {
            return false;
        } else {
            cumulative_disc_ms = 0;
        }
        if (track.duration_ms > kMaximumTrackMs - cumulative_disc_ms) return false;
        cumulative_disc_ms += track.duration_ms;
        previous_disc = track.disc_number;
        previous_track = track.track_number;
    }
    return true;
}

bool same_payload(const PrecisionCueRelease& a, const PrecisionCueRelease& b) {
    if (a.provider != b.provider || a.provider_release_id != b.provider_release_id ||
        a.album_title != b.album_title || a.album_performer != b.album_performer ||
        a.genre != b.genre || a.release_date != b.release_date ||
        a.tracks.size() != b.tracks.size()) return false;
    for (std::size_t i = 0; i < a.tracks.size(); ++i) {
        const auto& x = a.tracks[i];
        const auto& y = b.tracks[i];
        if (x.disc_number != y.disc_number || x.track_number != y.track_number ||
            x.title != y.title || x.performer != y.performer ||
            x.duration_ms != y.duration_ms) return false;
    }
    return true;
}

std::uint64_t duration_delta(std::int64_t a, std::int64_t b) {
    return a > b ? static_cast<std::uint64_t>(a - b)
                 : static_cast<std::uint64_t>(b - a);
}

PrecisionCueCandidateMatch assess(
    const PrecisionCueRelease& reference, const PrecisionCueRelease& candidate,
    std::size_t index) {
    PrecisionCueCandidateMatch result;
    result.candidate_index = index;
    result.state = PrecisionCueMatchState::Selected;
    if (!valid_release(candidate, false) ||
        !valid_field(candidate.provider) ||
        !valid_field(candidate.provider_release_id)) {
        result.state = PrecisionCueMatchState::Rejected;
        result.diagnostics.emplace_back("CANDIDATE_INVALID_OR_MISSING_PROVIDER_ID");
        return result;
    }
    if (reference.tracks.size() != candidate.tracks.size()) {
        result.state = PrecisionCueMatchState::Rejected;
        result.diagnostics.emplace_back("TRACK_COUNT_MISMATCH");
        return result;
    }
    for (std::size_t i = 0; i < reference.tracks.size(); ++i) {
        const auto& x = reference.tracks[i];
        const auto& y = candidate.tracks[i];
        if (x.disc_number != y.disc_number || x.track_number != y.track_number) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("DISC_OR_TRACK_POSITION_MISMATCH");
            return result;
        }
        if (canonical(x.title) != canonical(y.title)) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("TRACK_TITLE_OR_MIX_VERSION_MISMATCH");
            return result;
        }
        if (x.duration_ms > 0 &&
            duration_delta(x.duration_ms, y.duration_ms) >
                static_cast<std::uint64_t>(kRejectDurationDeltaMs)) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("TRACK_DURATION_INCOMPATIBLE");
            return result;
        }
        if (x.duration_ms == 0) {
            result.state = PrecisionCueMatchState::Review;
            result.diagnostics.emplace_back("REFERENCE_DURATION_UNKNOWN");
        } else if (duration_delta(x.duration_ms, y.duration_ms) >
                   static_cast<std::uint64_t>(kStrongDurationDeltaMs)) {
            result.state = PrecisionCueMatchState::Review;
            result.diagnostics.emplace_back("TRACK_DURATION_REVIEW");
        }
        if (canonical(x.performer).empty() || canonical(y.performer).empty() ||
            canonical(x.performer) != canonical(y.performer)) {
            result.state = PrecisionCueMatchState::Review;
            result.diagnostics.emplace_back("TRACK_PERFORMER_REVIEW");
        }
    }
    // Per-track +/-1s agreement can conceal a many-second offset by the
    // end of a continuous mix. Withhold automatic selection at the first
    // excessive cumulative boundary; the user sees its exact disc/track.
    if (first_precision_cue_cumulative_evidence_conflict(reference, candidate)) {
        result.state = PrecisionCueMatchState::Review;
        result.diagnostics.emplace_back("CUMULATIVE_DISC_DURATION_REVIEW");
    }
    if (canonical(reference.album_title) != canonical(candidate.album_title)) {
        result.state = PrecisionCueMatchState::Review;
        result.diagnostics.emplace_back("ALBUM_EDITION_TITLE_REVIEW");
    }
    if (canonical(reference.album_performer).empty() ||
        canonical(candidate.album_performer).empty() ||
        canonical(reference.album_performer) != canonical(candidate.album_performer)) {
        result.state = PrecisionCueMatchState::Review;
        result.diagnostics.emplace_back("ALBUM_PERFORMER_REVIEW");
    }
    // Conflicting known dates are edition evidence requiring human review.
    if (!reference.release_date.empty() && !candidate.release_date.empty() &&
        reference.release_date != candidate.release_date) {
        result.state = PrecisionCueMatchState::Review;
        result.diagnostics.emplace_back("RELEASE_DATE_REVIEW");
    }
    // A genre is catalog classification, not mastering proof. However, two
    // independently known and contradictory genres must not silently become
    // a strong edition match merely because all CUE frames coincide.
    if (!reference.genre.empty() && !candidate.genre.empty() &&
        canonical(reference.genre) != canonical(candidate.genre)) {
        result.state = PrecisionCueMatchState::Review;
        result.diagnostics.emplace_back("RELEASE_GENRE_REVIEW");
    }
    if (reference.provider.empty() || reference.provider_release_id.empty()) {
        result.state = PrecisionCueMatchState::Review;
        result.diagnostics.emplace_back("REFERENCE_PROVIDER_ID_UNVERIFIED");
    }
    return result;
}

} // namespace

PrecisionCueMatchReport match_precision_cue_releases(
    const PrecisionCueRelease& reference,
    const std::vector<PrecisionCueRelease>& candidates) {
    PrecisionCueMatchReport report;
    if (!valid_release(reference, true)) {
        report.diagnostics.emplace_back("REFERENCE_RELEASE_INVALID");
        return report;
    }
    if (candidates.size() > 200) {
        report.diagnostics.emplace_back("CANDIDATE_LIMIT_EXCEEDED");
        return report;
    }
    std::map<std::pair<std::string, std::string>, std::size_t> seen;
    std::vector<std::size_t> plausible;
    bool conflicting_ids = false;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& candidate = candidates[i];
        auto match = assess(reference, candidate, i);
        // Detect inconsistent responses for the SAME official provider ID,
        // including when one of the two payloads would otherwise be rejected.
        // Never silently select a conflicting release as if evidence agreed.
        if (valid_field(candidate.provider) &&
            valid_field(candidate.provider_release_id)) {
            const auto key = std::make_pair(candidate.provider, candidate.provider_release_id);
            const auto [it, inserted] = seen.emplace(key, i);
            if (!inserted) {
                if (same_payload(candidate, candidates[it->second])) {
                    match.state = PrecisionCueMatchState::Duplicate;
                    match.diagnostics.emplace_back("DUPLICATE_EXACT_PROVIDER_PAYLOAD");
                } else {
                    conflicting_ids = true;
                    match.state = PrecisionCueMatchState::Ambiguous;
                    match.diagnostics.emplace_back("CONFLICTING_PAYLOAD_FOR_PROVIDER_ID");
                }
            } else if (match.state != PrecisionCueMatchState::Rejected) {
                plausible.push_back(i);
            }
        }
        report.candidates.push_back(std::move(match));
    }
    if (conflicting_ids || plausible.size() > 1) {
        report.state = PrecisionCueMatchState::Ambiguous;
        report.diagnostics.emplace_back(conflicting_ids
            ? "CONFLICTING_PROVIDER_RELEASE_PAYLOADS" : "MULTIPLE_PLAUSIBLE_RELEASE_EDITIONS");
    } else if (plausible.size() == 1) {
        const auto index = plausible.front();
        report.state = report.candidates[index].state;
        if (report.state == PrecisionCueMatchState::Selected)
            report.selected_candidate_index = index;
    } else {
        report.diagnostics.emplace_back("NO_COMPATIBLE_RELEASE");
    }
    return report;
}

} // namespace djmeta
