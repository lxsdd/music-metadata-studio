#pragma once

#include "djmeta/online_match.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace djmeta::online {

// Provider-independent *read-only* evidence. Recording, release and edition
// identities must not be collapsed into a single "song" object.
enum class EvidenceScope { Recording, Release, Edition };
enum class DateMeaning { NotDate, OriginalRelease, EditionRelease, DigitalPublication };

struct FieldEvidence {
    std::string field;
    std::vector<std::string> values;
    std::string provider;
    std::string source_id;
    EvidenceScope scope = EvidenceScope::Recording;
    DateMeaning date_meaning = DateMeaning::NotDate;

    bool operator==(const FieldEvidence&) const = default;
};

struct LocalReleaseTrack {
    std::size_t source_index = 0; // Stable selected-track identity, NOT physical write target.
    RecordingIdentity recording;
    int disc_number = -1;
    int track_number = -1;
};

struct EditionTrack {
    std::string source_track_id; // Empty for providers without track-level IDs.
    RecordingIdentity recording;
    int disc_number = -1;
    int track_number = -1;
    std::vector<FieldEvidence> fields;
};

struct ReleaseEdition {
    std::string provider;
    std::string edition_id; // Exact release edition ID, not release-group/master ID.
    std::vector<FieldEvidence> fields;
    std::vector<EditionTrack> tracks;
};

inline constexpr std::size_t no_release_track = std::numeric_limits<std::size_t>::max();

struct TrackAssociation {
    std::size_t source_index = 0;
    std::size_t edition_track_index = no_release_track;
    MatchDecision decision = MatchDecision::Review;
    int score = 0; // Uncalibrated evidence only. Never a write authorization.
    std::vector<std::string> evidence;
    std::vector<std::string> conflicts;
};

struct ReleaseAlignment {
    std::string provider;
    std::string edition_id;
    std::vector<TrackAssociation> tracks; // Same order and identities as input.
    std::size_t matched = 0;
    std::size_t unmatched = 0;
};

struct PositionEvidence {
    int bonus = 0;
    bool conflict = false;
    const char* reason = "";
};

inline PositionEvidence compare_position(const LocalReleaseTrack& local,
                                         const EditionTrack& remote) {
    const bool has_disc = local.disc_number > 0 && remote.disc_number > 0;
    const bool has_track = local.track_number > 0 && remote.track_number > 0;
    if (has_disc && has_track && local.disc_number == remote.disc_number &&
        local.track_number == remote.track_number)
        return {12, false, "disc_track_position_exact"};
    if (has_track && local.track_number == remote.track_number) {
        if (has_disc && local.disc_number != remote.disc_number)
            return {0, true, "disc_position_conflict"};
        return {6, false, "track_position_exact"};
    }
    if (has_disc && local.disc_number == remote.disc_number && !has_track)
        return {2, false, "disc_position_exact"};
    if ((has_track && local.track_number != remote.track_number) ||
        (has_disc && local.disc_number != remote.disc_number))
        return {0, true, "track_position_conflict"};
    return {};
}

// One-to-one maximum-weight assignment using bounded rectangular Hungarian
// matching with dummy columns for unmatched local tracks. No network, SDK,
// filesystem I/O, tag proposals or automatic approval are performed.
inline ReleaseAlignment align_release(const std::vector<LocalReleaseTrack>& local,
                                      const ReleaseEdition& edition) {
    ReleaseAlignment output;
    output.provider = edition.provider;
    output.edition_id = edition.edition_id;
    const std::size_t n = local.size();
    const std::size_t k = edition.tracks.size();
    for (const auto& track : local) {
        TrackAssociation row;
        row.source_index = track.source_index;
        output.tracks.push_back(row);
    }
    if (n == 0) return output;

    const auto fail = [&](const char* reason) {
        for (auto& track : output.tracks) track.conflicts.emplace_back(reason);
        output.unmatched = n;
        return output;
    };
    if (edition.provider.empty() || edition.edition_id.empty())
        return fail("missing_edition_identity");
    if (n > 128 || k > 256) return fail("edition_assignment_limit_exceeded");
    std::set<std::size_t> identities;
    for (const auto& track : local)
        if (!identities.insert(track.source_index).second)
            return fail("duplicate_local_source_index");
    if (k == 0) return fail("empty_release_tracklist");

    const std::size_t m = k + n;
    constexpr std::int64_t inf = 1'000'000'000LL;
    std::vector<std::vector<std::int64_t>> cost(
        n + 1, std::vector<std::int64_t>(m + 1));
    std::vector<std::vector<CandidateMatch>> candidate_matches(
        n, std::vector<CandidateMatch>(k));
    std::vector<std::vector<PositionEvidence>> position_matches(
        n, std::vector<PositionEvidence>(k));

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < k; ++j) {
            const auto& track = edition.tracks[j];
            candidate_matches[i][j] = compare(
                local[i].recording,
                Candidate{edition.provider, track.source_track_id, track.recording});
            position_matches[i][j] = compare_position(local[i], track);
            cost[i + 1][j + 1] =
                candidate_matches[i][j].decision == MatchDecision::Rejected
                    ? inf
                    : -static_cast<std::int64_t>(
                          candidate_matches[i][j].score + position_matches[i][j].bonus);
        }
    }

    std::vector<std::int64_t> u(n + 1), v(m + 1);
    std::vector<std::size_t> p(m + 1), way(m + 1);
    for (std::size_t i = 1; i <= n; ++i) {
        p[0] = i;
        std::size_t j0 = 0;
        std::vector<std::int64_t> minv(m + 1, inf);
        std::vector<bool> used(m + 1);
        do {
            used[j0] = true;
            const std::size_t i0 = p[j0];
            std::int64_t delta = inf;
            std::size_t j1 = 0;
            for (std::size_t j = 1; j <= m; ++j) {
                if (used[j]) continue;
                const auto current = cost[i0][j] - u[i0] - v[j];
                if (current < minv[j]) {
                    minv[j] = current;
                    way[j] = j0;
                }
                if (minv[j] < delta) {
                    delta = minv[j];
                    j1 = j;
                }
            }
            for (std::size_t j = 0; j <= m; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            const std::size_t j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    std::vector<std::size_t> assignment(n, no_release_track);
    for (std::size_t j = 1; j <= k; ++j)
        if (p[j] && cost[p[j]][j] < 0)
            assignment[p[j] - 1] = j - 1;

    for (std::size_t i = 0; i < n; ++i) {
        auto& result = output.tracks[i];
        const auto chosen = assignment[i];
        if (chosen == no_release_track) {
            ++output.unmatched;
            result.conflicts.emplace_back("no_compatible_release_track");
            continue;
        }
        ++output.matched;
        result.edition_track_index = chosen;
        const auto& candidate = candidate_matches[i][chosen];
        const auto& position = position_matches[i][chosen];
        result.score = candidate.score + position.bonus;
        result.decision = candidate.decision;
        result.evidence = candidate.evidence;
        result.conflicts = candidate.conflicts;
        if (*position.reason) {
            if (position.conflict) {
                result.conflicts.emplace_back(position.reason);
                result.decision = MatchDecision::Review;
            } else {
                result.evidence.emplace_back(position.reason);
            }
        }
        const auto chosen_cost = cost[i + 1][chosen + 1];
        int ties = 0;
        bool displaced = false;
        for (std::size_t j = 0; j < k; ++j) {
            if (cost[i + 1][j + 1] == chosen_cost) ++ties;
            if (cost[i + 1][j + 1] < chosen_cost) displaced = true;
        }
        if (ties > 1) {
            result.conflicts.emplace_back("ambiguous_release_track");
            result.decision = MatchDecision::Review;
        }
        if (displaced) {
            result.conflicts.emplace_back("global_assignment_displaced");
            result.decision = MatchDecision::Review;
        }
    }
    return output;
}

} // namespace djmeta::online
