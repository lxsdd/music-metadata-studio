#include "djmeta/online_release.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace djmeta::online;

static int brute_force(const std::vector<LocalReleaseTrack>& local,
                       const ReleaseEdition& edition, std::size_t i,
                       std::vector<bool>& assigned) {
    if (i == local.size()) return 0;
    int best = brute_force(local, edition, i + 1, assigned);
    for (std::size_t j = 0; j < edition.tracks.size(); ++j) {
        if (assigned[j]) continue;
        const auto candidate = compare(
            local[i].recording,
            Candidate{edition.provider, edition.tracks[j].source_track_id,
                      edition.tracks[j].recording});
        if (candidate.decision == MatchDecision::Rejected) continue;
        assigned[j] = true;
        best = std::max(best,
            candidate.score + compare_position(local[i], edition.tracks[j]).bonus +
            brute_force(local, edition, i + 1, assigned));
        assigned[j] = false;
    }
    return best;
}

int main() {
    std::mt19937 rng(11392);
    for (int test = 0; test < 3000; ++test) {
        const int n = 1 + static_cast<int>(rng() % 5);
        const int k = 1 + static_cast<int>(rng() % 5);
        std::vector<LocalReleaseTrack> local;
        ReleaseEdition edition{"discogs", "r1", {}, {}};
        for (int i = 0; i < n; ++i) {
            const int title = static_cast<int>(rng() % 4);
            RecordingIdentity recording{
                std::to_string(title), "Artist", "",
                rng() % 2 ? MixKind::Extended : MixKind::Radio, "", 200000
            };
            local.push_back({
                static_cast<std::size_t>(i), recording, 1,
                1 + static_cast<int>(rng() % 4)
            });
        }
        for (int j = 0; j < k; ++j) {
            const int title = static_cast<int>(rng() % 4);
            RecordingIdentity recording{
                std::to_string(title), "Artist", "",
                rng() % 2 ? MixKind::Extended : MixKind::Radio, "", 200000
            };
            edition.tracks.push_back({
                "", recording, 1, 1 + static_cast<int>(rng() % 4), {}
            });
        }
        const auto actual = align_release(local, edition);
        int total = 0;
        for (const auto& row : actual.tracks) total += row.score;
        std::vector<bool> assigned(edition.tracks.size());
        const int optimum = brute_force(local, edition, 0, assigned);
        if (total != optimum) {
            std::cerr << "FAIL: score " << total << " != " << optimum
                      << " on deterministic case " << test << "\n";
            return 1;
        }
    }
    std::cout << "PASS: 3000 deterministic maximum-assignment comparisons\n";
}
