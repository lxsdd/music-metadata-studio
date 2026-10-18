#include "djmeta/online_match.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void expect(bool condition, const char* reason) {
    if (!condition) {
        std::cerr << "FAIL: " << reason << "\n";
        std::exit(1);
    }
}
bool has(const std::vector<std::string>& xs, std::string_view value) {
    for (const auto& x : xs) if (x == value) return true;
    return false;
}
}

int main() {
    using namespace djmeta::online;
    const RecordingIdentity original{
        "  A-Track! ", "  An Artist ", "Producer X",
        MixKind::Extended, "US-ABC-26-00001", 402000
    };
    const RecordingIdentity same{
        "a track", "an artist", "producer x",
        MixKind::Extended, "USABC2600001", 403000
    };
    // ISRC punctuation/spacing is ignored without changing the raw tag.
    const Candidate strong{"beatport", "128", same};
    const auto best = compare(original, strong);
    expect(best.decision == MatchDecision::Suggested &&
           best.score == 100 && has(best.evidence, "isrc_exact_not_conclusive"),
           "exact mix and matching ISRC justify only a suggested candidate");
    const RecordingIdentity original_copy = original;
    (void)rank(original, {strong});
    expect(original.title == original_copy.title &&
           original.primary_artist == original_copy.primary_artist,
           "matching never changes source metadata");

    Candidate radio = strong;
    radio.provider_track_id = "129";
    radio.identity.mix = MixKind::Radio;
    radio.identity.duration_ms = 210000;
    const auto radio_result = compare(original, radio);
    expect(radio_result.decision == MatchDecision::Rejected &&
           has(radio_result.conflicts, "mix_type_conflict"),
           "same ISRC must not override a different radio/extended mix");

    Candidate long_mix = strong;
    long_mix.provider_track_id = "130";
    long_mix.identity.duration_ms = 500000;
    const auto long_result = compare(original, long_mix);
    expect(long_result.decision == MatchDecision::Rejected &&
           has(long_result.conflicts, "duration_far_conflict"),
           "same ISRC and title do not defeat large duration mismatch");

    Candidate uncertain = strong;
    uncertain.provider_track_id = "131";
    uncertain.identity.isrc = "GB-ZYX-26-99999";
    const auto uncertain_result = compare(original, uncertain);
    expect(uncertain_result.decision == MatchDecision::Review &&
           has(uncertain_result.conflicts, "isrc_conflict"),
           "conflicting ISRC must require review, never force a tag update");

    Candidate missing_mix_name = strong;
    missing_mix_name.provider_track_id = "134";
    missing_mix_name.identity.mix_name.clear();
    const auto missing_name_result = compare(original, missing_mix_name);
    expect(missing_name_result.decision == MatchDecision::Review &&
           has(missing_name_result.conflicts, "named_mix_missing_on_one_side"),
           "named remix on only one side requires manual review");

    const RecordingIdentity simple{"Sample", "DJ", "", MixKind::Unknown, "", 200000};
    Candidate loose{"discogs", "10", {"Sample", "DJ", "", MixKind::Unknown, "", 201000}};
    const auto loose_result = compare(simple, loose);
    expect(loose_result.decision == MatchDecision::Review,
           "artist, title and close runtime alone may not mean same recording");
    loose.identity.mix = MixKind::Extended;
    expect(compare(simple, loose).decision == MatchDecision::Review,
           "unknown-versus-explicit version must require review");

    Candidate wrong_title = strong;
    wrong_title.provider_track_id = "132";
    wrong_title.identity.title = "A Completely Different Track";
    expect(compare(original, wrong_title).decision == MatchDecision::Rejected,
           "same ISRC with different title is not an automatic match");

    Candidate wrong_artist = strong;
    wrong_artist.provider_track_id = "133";
    wrong_artist.identity.primary_artist = "Someone Else";
    expect(compare(original, wrong_artist).decision == MatchDecision::Rejected,
           "same ISRC with different primary artist is not auto-approved");

    Candidate unnamed = strong;
    unnamed.provider_track_id.clear();
    Candidate duplicate = strong;
    duplicate.identity.duration_ms = 100000;
    Candidate deezer = strong;
    deezer.provider = "deezer";
    deezer.provider_track_id = "80";
    const auto ranked = rank(original, {unnamed, radio, strong, duplicate, deezer, uncertain});
    expect(ranked.size() == 4,
           "invalid identifiers and duplicate provider records ignored");
    expect(ranked[0].provider == "deezer" &&
           ranked[0].decision == MatchDecision::Suggested,
           "independent source remains a suggestion despite colliding provider ID");
    const auto collision = std::find_if(ranked.begin(), ranked.end(),
        [](const CandidateMatch& item) {
            return item.provider == "beatport" && item.provider_track_id == "128";
        });
    expect(collision != ranked.end() &&
           collision->decision == MatchDecision::Review &&
           collision->score == 0 &&
           collision->evidence.empty() &&
           has(collision->conflicts, "provider_identity_collision"),
           "inconsistent records sharing a provider ID must never be suggested");
    const auto reverse_ranked = rank(original,
        {unnamed, radio, duplicate, strong, deezer, uncertain});
    const auto reverse_collision = std::find_if(
        reverse_ranked.begin(), reverse_ranked.end(),
        [](const CandidateMatch& item) {
            return item.provider == "beatport" && item.provider_track_id == "128";
        });
    expect(reverse_ranked.size() == ranked.size() &&
           reverse_collision != reverse_ranked.end() &&
           reverse_collision->decision == MatchDecision::Review &&
           reverse_collision->score == 0 &&
           has(reverse_collision->conflicts, "provider_identity_collision"),
           "provider identity collision must fail closed regardless of input order");
    const auto identical_duplicates = rank(original, {strong, strong});
    expect(identical_duplicates.size() == 1 &&
           identical_duplicates[0].decision == MatchDecision::Suggested,
           "byte-identical duplicate evidence may be safely deduplicated");
    Candidate nul_provider = strong;
    nul_provider.provider = std::string("beat\0port", 9);
    expect(rank(original, {nul_provider}).empty(),
           "provider IDs with embedded NUL cannot enter a result identity");

    expect(normalized_isrc("us-abc-26-00001") == "USABC2600001" &&
           normalized_isrc("usabc2600001") == "USABC2600001" &&
           normalized_isrc("US/ABC/26/00001").empty() &&
           normalized_isrc("00ABC2600001").empty() &&
           normalized_isrc("USABC2X00001").empty() &&
           normalized_isrc("USABC26000A1").empty(),
           "ISRC matching validates ISO 3901 field structure, not only length");
    expect(comparable("  A --B \t C! ") == "a b c" &&
           comparable("Björk") == "björk" &&
           comparable(std::string("A\0B", 3)).empty(),
           "search-only normalization is conservative, UTF-8 preserving, and rejects NUL");

    std::cout << "PASS: candidate evidence, version conflicts, ISRC limits, duration, identity and ordering\n";
}
