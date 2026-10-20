#pragma once
// Read-only, provider-independent comparison of integer-ms durations.
// Resolution metadata must be independently verified for each provider.
#include "djmeta/precision_cue_match.h"
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta {

struct PrecisionCueDurationSource {
    PrecisionCueRelease release;
    unsigned resolution_ms = 0; // 0 = unknown; never guess from JSON type
};

enum class PrecisionCueConsensusState {
    Agreed, SingleHighResolutionSource, Review, Rejected
};

struct PrecisionCueSourceDifference {
    std::string provider;
    unsigned disc_number = 0;
    unsigned track_number = 0;
    std::int64_t signed_delta_ms = 0;
    std::int64_t signed_end_frame_delta = 0;
    unsigned resolution_ms = 0;
};

struct PrecisionCueConsensusReport {
    PrecisionCueConsensusState state = PrecisionCueConsensusState::Rejected;
    std::size_t high_resolution_sources = 0;
    std::vector<PrecisionCueSourceDifference> differences;
    std::vector<std::string> diagnostics;
};

namespace precision_cue_consensus_detail {
inline std::string normalized(std::string_view input) {
    std::string result;
    bool pending_space = false;
    for (const unsigned char c : input) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pending_space = !result.empty();
        } else {
            if (pending_space) result.push_back(' ');
            pending_space = false;
            result.push_back(static_cast<char>(
                c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
        }
    }
    return result;
}

inline bool valid_release_structure(const PrecisionCueRelease& r) {
    if (r.provider.empty() || r.provider_release_id.empty() ||
        r.album_title.empty() || r.album_performer.empty() ||
        r.tracks.empty() || r.tracks.size() > 8u * 99u ||
        r.tracks.front().disc_number != 1 ||
        r.tracks.front().track_number != 1) return false;
    unsigned current_disc = 1;
    unsigned current_track = 0;
    std::int64_t cumulative_disc_ms = 0;
    constexpr std::int64_t kMaximumDiscMs = 24LL * 3600 * 1000;
    for (const auto& t : r.tracks) {
        if (t.disc_number != current_disc) {
            if (t.disc_number != current_disc + 1 ||
                t.disc_number > 8 || t.track_number != 1)
                return false;
            current_disc = t.disc_number;
            current_track = 0;
            cumulative_disc_ms = 0;
        }
        if (t.track_number != ++current_track || current_track > 99 ||
            t.title.empty() || t.performer.empty() ||
            t.duration_ms <= 0 ||
            t.duration_ms > kMaximumDiscMs - cumulative_disc_ms)
            return false;
        cumulative_disc_ms += t.duration_ms;
    }
    return true;
}

inline bool same_edition(const PrecisionCueRelease& a, const PrecisionCueRelease& b) {
    if (normalized(a.album_title) != normalized(b.album_title) ||
        normalized(a.album_performer) != normalized(b.album_performer) ||
        a.tracks.size() != b.tracks.size() ||
        // Two conflicting known release dates cannot independently confirm
        // the same edition. Unknown dates do not fabricate a mismatch.
        (!a.release_date.empty() && !b.release_date.empty() &&
         a.release_date != b.release_date)) return false;
    for (std::size_t i = 0; i < a.tracks.size(); ++i) {
        const auto& x = a.tracks[i];
        const auto& y = b.tracks[i];
        if (x.disc_number != y.disc_number || x.track_number != y.track_number ||
            normalized(x.title) != normalized(y.title) ||
            normalized(x.performer) != normalized(y.performer)) return false;
    }
    return true;
}
} // namespace precision_cue_consensus_detail

// sources[0] is the independently matched primary release. Every other
// provider is corroborating evidence, never a license to substitute editions.
// Each disc starts its own elapsed clock at zero. Per-track duration agreement
// alone does not detect accumulated transition errors.
inline PrecisionCueConsensusReport audit_precision_cue_source_consensus(
    const std::vector<PrecisionCueDurationSource>& sources) {
    using namespace precision_cue_consensus_detail;
    PrecisionCueConsensusReport report;
    if (sources.empty() || sources.size() > 8 ||
        !valid_release_structure(sources.front().release) ||
        sources.front().resolution_ms == 0 || sources.front().resolution_ms > 1000) {
        report.diagnostics.emplace_back("INVALID_PRIMARY_SOURCE");
        return report;
    }
    std::set<std::string> independent_providers;
    bool conflicting_frames = false;
    bool one_frame_ambiguity = false;
    bool coarse_reference_conflict = false;
    const auto cue_frame = [](std::int64_t ms) {
        return (ms * 75 + 500) / 1000;
    };
    for (const auto& source : sources) {
        const auto& release = source.release;
        if (!valid_release_structure(release) ||
            source.resolution_ms == 0 || source.resolution_ms > 1000 ||
            !same_edition(sources.front().release, release)) {
            report.diagnostics.emplace_back("DUPLICATE_OR_DIFFERENT_RELEASE_EDITION");
            return report;
        }
        const auto provider_key = normalized(release.provider);
        if (!independent_providers.emplace(provider_key).second) {
            report.diagnostics.emplace_back("REPEATED_PROVIDER_NOT_INDEPENDENT");
            return report;
        }
        // Deezer public album track durations are whole seconds. Caller-
        // supplied resolution metadata cannot promote them to CUE frames.
        if (provider_key == "deezer" && source.resolution_ms < 1000) {
            report.diagnostics.emplace_back("DEEZER_SECONDS_NOT_FRAME_PRECISION");
            return report;
        }
        if (source.resolution_ms <= 14) ++report.high_resolution_sources;
        if (&source == &sources.front()) continue;
        std::int64_t primary_elapsed = 0;
        std::int64_t other_elapsed = 0;
        unsigned current_disc = 0;
        std::int64_t tracks_in_disc = 0;
        for (std::size_t i = 0; i < release.tracks.size(); ++i) {
            const auto& track = release.tracks[i];
            if (track.disc_number != current_disc) {
                current_disc = track.disc_number;
                primary_elapsed = 0;
                other_elapsed = 0;
                tracks_in_disc = 0;
            }
            ++tracks_in_disc;
            const auto primary_ms = sources.front().release.tracks[i].duration_ms;
            const auto other_ms = track.duration_ms;
            primary_elapsed += primary_ms;
            other_elapsed += other_ms;
            const auto delta = cue_frame(other_elapsed) - cue_frame(primary_elapsed);
            report.differences.push_back({
                release.provider, track.disc_number, track.track_number,
                other_ms - primary_ms, delta, source.resolution_ms
            });
            if (sources.front().resolution_ms <= 14 && source.resolution_ms <= 14) {
                if (delta != 0) one_frame_ambiguity = true;
                if (delta > 1 || delta < -1) conflicting_frames = true;
            }
            // Whole-second reference durations are identity evidence only.
            // A large per-track or accumulated drift is contradictory,
            // not independent corroboration of a mixed edition.
            if (source.resolution_ms > 14) {
                const auto resolution = static_cast<std::int64_t>(
                    (std::max)(source.resolution_ms, sources.front().resolution_ms));
                const auto duration_delta = other_ms - primary_ms;
                const auto cumulative_delta = other_elapsed - primary_elapsed;
                const auto track_limit = resolution + 500;
                const auto cumulative_limit =
                    ((resolution + 1) / 2) * tracks_in_disc + 1000;
                if (duration_delta > track_limit || duration_delta < -track_limit ||
                    cumulative_delta > cumulative_limit ||
                    cumulative_delta < -cumulative_limit)
                    coarse_reference_conflict = true;
            }
        }
    }
    if (sources.front().resolution_ms > 14) {
        report.state = PrecisionCueConsensusState::Review;
        report.diagnostics.emplace_back("PRIMARY_TIMING_TOO_COARSE");
    } else if (conflicting_frames) {
        report.state = PrecisionCueConsensusState::Review;
        report.diagnostics.emplace_back("HIGH_RESOLUTION_BOUNDARIES_DISAGREE");
    } else if (coarse_reference_conflict) {
        report.state = PrecisionCueConsensusState::Review;
        report.diagnostics.emplace_back("COARSE_REFERENCE_DURATION_CONFLICT");
    } else if (one_frame_ambiguity) {
        report.state = PrecisionCueConsensusState::Review;
        report.diagnostics.emplace_back("ONE_FRAME_BOUNDARY_AMBIGUITY_REVIEW");
    } else if (report.high_resolution_sources < 2) {
        report.state = PrecisionCueConsensusState::SingleHighResolutionSource;
        report.diagnostics.emplace_back("NO_INDEPENDENT_FRAME_RESOLUTION_CORROBORATION");
    } else {
        report.state = PrecisionCueConsensusState::Agreed;
        report.diagnostics.emplace_back("FRAME_BOUNDARIES_IDENTICAL_NOT_SAMPLE_PROOF");
    }
    return report;
}

} // namespace djmeta
