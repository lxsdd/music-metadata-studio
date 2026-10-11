#include "djmeta/online_release.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace djmeta::online;

static void check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << "\n"; std::exit(1); }
}
static bool contains(const std::vector<std::string>& values, const char* target) {
    for (const auto& value : values) if (value == target) return true;
    return false;
}
static RecordingIdentity song(const char* title, int ms = 200000) {
    return {title, "Example DJ", "", MixKind::Extended, "", ms};
}
int main() {
    const FieldEvidence year{
        "DATE_RAW", {"1998-05-06"}, "discogs", "release:100",
        EvidenceScope::Edition, DateMeaning::EditionRelease
    };
    const FieldEvidence genres{
        "GENRE", {"House", "Deep House"}, "discogs", "release:100",
        EvidenceScope::Edition, DateMeaning::NotDate
    };
    const FieldEvidence original_year{
        "DATE_RAW", {"1995"}, "musicbrainz", "recording:50",
        EvidenceScope::Recording, DateMeaning::OriginalRelease
    };
    ReleaseEdition release{"discogs", "release:100", {year, genres, original_year}, {
        {"", song("Second"), 1, 2, {}},
        {"", song("First"), 1, 1, {}},
        {"", song("Third"), 1, 3, {}}
    }};
    const auto source = release;
    const std::vector<LocalReleaseTrack> local{
        {11, song("First"), 1, 1},
        {22, song("Second"), 1, 2}
    };
    const auto aligned = align_release(local, release);
    check(aligned.matched == 2 && aligned.unmatched == 0 &&
          aligned.tracks[0].edition_track_index == 1 &&
          aligned.tracks[1].edition_track_index == 0,
          "partial album matches globally despite tracklist/input order");
    check(aligned.tracks[0].source_index == 11 &&
          aligned.tracks[1].source_index == 22,
          "stable source identities retained");
    check(release.fields == source.fields && release.tracks.size() == 3 &&
          release.fields[1].values.size() == 2 &&
          release.fields[0].date_meaning == DateMeaning::EditionRelease &&
          release.fields[2].date_meaning == DateMeaning::OriginalRelease,
          "provenance/date meaning and multivalue are immutable");

    auto double_track = release;
    double_track.tracks = {
        {"", song("First"), -1, -1, {}},
        {"", song("First"), -1, -1, {}}
    };
    const auto ambiguous = align_release({{10, song("First"), -1, -1}}, double_track);
    check(ambiguous.matched == 1 &&
          ambiguous.tracks[0].decision == MatchDecision::Review &&
          contains(ambiguous.tracks[0].conflicts, "ambiguous_release_track"),
          "same recording in two edition positions needs review");

    auto single = release;
    single.tracks = {{"", song("First"), 1, 1, {}}};
    const auto partial = align_release(local, single);
    check(partial.matched == 1 && partial.unmatched == 1 &&
          partial.tracks[1].edition_track_index == no_release_track,
          "unmatched local track must never be forced");

    const auto duplicate = align_release(
        {{10, song("First")}, {10, song("Second")}}, release);
    check(duplicate.unmatched == 2 &&
          contains(duplicate.tracks[0].conflicts, "duplicate_local_source_index"),
          "duplicate source identity blocked");

    const auto invalid = align_release(
        local, ReleaseEdition{"discogs", "", {}, release.tracks});
    check(invalid.unmatched == 2 &&
          contains(invalid.tracks[0].conflicts, "missing_edition_identity"),
          "empty edition ID blocked");
    const auto empty = align_release(
        local, ReleaseEdition{"discogs", "r1", {}, {}});
    check(empty.unmatched == 2 &&
          contains(empty.tracks[0].conflicts, "empty_release_tracklist"),
          "empty release handled");
    auto large = local;
    large.resize(129);
    const auto bounded = align_release(large, release);
    check(bounded.unmatched == 129 &&
          contains(bounded.tracks[0].conflicts, "edition_assignment_limit_exceeded"),
          "assignment limit is enforced before allocations");

    auto position_conflict = release;
    position_conflict.tracks = {{"", song("First"), 1, 2, {}}};
    const auto wrong_position = align_release(
        {{10, song("First"), 1, 1}}, position_conflict);
    check(wrong_position.matched == 1 &&
          wrong_position.tracks[0].decision == MatchDecision::Review &&
          contains(wrong_position.tracks[0].conflicts, "track_position_conflict"),
          "position conflict needs manual review");

    auto radio = release;
    radio.tracks = {{"", {"First", "Example DJ", "", MixKind::Radio, "", 200000},
                     1, 1, {}}};
    const auto wrong_mix = align_release({{10, song("First"), 1, 1}}, radio);
    check(wrong_mix.unmatched == 1,
          "different mix must never be forced despite position");

    const auto again = align_release(local, release);
    check(again.tracks[0].edition_track_index == aligned.tracks[0].edition_track_index &&
          again.tracks[1].edition_track_index == aligned.tracks[1].edition_track_index,
          "deterministic assignment");
    std::cout << "PASS: album assignment, ambiguity, provenance, zero mutation, bounds\n";
}
