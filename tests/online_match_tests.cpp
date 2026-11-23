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

    // Beatport stores base TITLE and mix_name separately, whereas local DJ
    // TITLE commonly contains a terminal '(Extended Mix)' annotation.
    RecordingIdentity dj_title_local = original;
    dj_title_local.title = "A-Track (Extended Mix)";
    dj_title_local.mix_name.clear();
    dj_title_local.mix = MixKind::Unknown;
    Candidate beatport_separate = strong;
    beatport_separate.identity.title = "A-Track";
    beatport_separate.identity.mix_name = "Extended Mix";
    beatport_separate.identity.mix = MixKind::Extended;
    const auto separated_match = compare(dj_title_local, beatport_separate);
    expect(separated_match.decision == MatchDecision::Suggested &&
           has(separated_match.evidence, "mix_type_exact") &&
           has(separated_match.evidence, "title_exact"),
           "qualified suffix from local DJ title matches separate Beatport mix");
    dj_title_local.title = "A-Track (Radio Edit)";
    const auto wrong_suffix = compare(dj_title_local, beatport_separate);
    expect(wrong_suffix.decision == MatchDecision::Rejected &&
           has(wrong_suffix.conflicts, "mix_type_conflict"),
           "same ISRC must not turn Radio Edit into Extended Mix");
    dj_title_local.title = "A-Track (Other Producer Remix)";
    const auto wrong_remixer = compare(dj_title_local, beatport_separate);
    expect(wrong_remixer.decision == MatchDecision::Rejected,
           "named remixer in TITLE cannot become an Extended Mix");
    dj_title_local.title = "A-Track (Part Two)";
    expect(compare(dj_title_local, beatport_separate).decision ==
           MatchDecision::Rejected,
           "unrelated parenthetical song subtitle must not be stripped");
    dj_title_local.title = "A-Track (Extended Mix)";
    dj_title_local.mix = MixKind::Radio;
    const auto contradictory_local = compare(dj_title_local, beatport_separate);
    expect(contradictory_local.decision != MatchDecision::Suggested &&
           has(contradictory_local.conflicts,
               "title_version_annotation_conflict"),
           "conflicting local title and version metadata prevent an auto-suggestion");
    const RecordingIdentity club_title{
        "A-Track (Club Mix)", "An Artist", "", MixKind::Unknown,
        "US-ABC-26-00001", 402000};
    Candidate club = strong;
    club.identity.title = "A-Track";
    club.identity.mix_name = "Club Mix";
    club.identity.mix = MixKind::Unknown;
    expect(compare(club_title, club).decision == MatchDecision::Suggested,
           "identical explicit custom mix labels may match with corroborating evidence");
    club.identity.mix_name = "Dub Mix";
    expect(compare(club_title, club).decision == MatchDecision::Review,
           "different custom DJ mix names cannot auto-suggest even with same ISRC");
    
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
           ranked[0].decision == MatchDecision::Suggested &&
           ranked[1].decision == MatchDecision::Review &&
           ranked[2].decision == MatchDecision::Rejected &&
           ranked[3].decision == MatchDecision::Rejected,
           "conflicting duplicate provider ID cannot outrank independent evidence");
    const auto find_match = [](const std::vector<CandidateMatch>& matches,
                               std::string_view provider,
                               std::string_view id) -> const CandidateMatch* {
        for (const auto& match : matches)
            if (match.provider == provider && match.provider_track_id == id)
                return &match;
        return nullptr;
    };
    const auto* disputed = find_match(ranked, "beatport", "128");
    expect(disputed && disputed->decision == MatchDecision::Rejected &&
           disputed->score == 0 && disputed->evidence.empty() &&
           has(disputed->conflicts, "provider_identity_conflict"),
           "conflicting same-provider track identity is fail-closed");
    const auto reversed = rank(original, {duplicate, strong, deezer});
    const auto* reversed_disputed = find_match(reversed, "beatport", "128");
    expect(reversed_disputed &&
           reversed_disputed->decision == MatchDecision::Rejected &&
           has(reversed_disputed->conflicts, "provider_identity_conflict"),
           "duplicate conflict cannot depend on provider response ordering");
    const auto exact_repeat = rank(original, {strong, strong, deezer});
    const auto* repeated = find_match(exact_repeat, "beatport", "128");
    expect(exact_repeat.size() == 2 && repeated &&
           repeated->decision == MatchDecision::Suggested &&
           !has(repeated->conflicts, "provider_identity_conflict"),
           "byte-identical provider repeats deduplicate without false conflict");
    Candidate altered_artist = strong;
    altered_artist.identity.primary_artist = "Different artist";
    const auto inconsistent = rank(original, {strong, altered_artist});
    expect(inconsistent.size() == 1 &&
           inconsistent[0].decision == MatchDecision::Rejected &&
           has(inconsistent[0].conflicts, "provider_identity_conflict"),
           "conflicting artist under same provider ID cannot be suggested");
    Candidate same_id_other_provider = strong;
    same_id_other_provider.provider = "musicbrainz";
    const auto independent = rank(original, {strong, same_id_other_provider});
    expect(independent.size() == 2 &&
           independent[0].decision == MatchDecision::Suggested &&
           independent[1].decision == MatchDecision::Suggested,
           "same opaque ID in separate provider namespaces remains independent");

    // Display priority reflects the owner's DJ sources. It is solely a
    // stable tie-break after independently verified recording/mix evidence.
    const Candidate beatport_equal{"beatport", "900", same};
    const Candidate crates_equal{"crates", "903", same};
    const Candidate discogs_equal{"discogs", "901", same};
    const Candidate deezer_equal{"deezer", "904", same};
    const Candidate musicbrainz_equal{"musicbrainz", "902", same};
    const auto catalog_priority = rank(original,
        {musicbrainz_equal, deezer_equal, discogs_equal,
         crates_equal, beatport_equal});
    expect(catalog_priority.size() == 5 &&
           catalog_priority[0].provider == "beatport" &&
           catalog_priority[1].provider == "crates" &&
           catalog_priority[2].provider == "discogs" &&
           catalog_priority[3].provider == "deezer" &&
           catalog_priority[4].provider == "musicbrainz" &&
           catalog_priority[0].score == catalog_priority[4].score &&
           catalog_priority[0].decision == catalog_priority[4].decision,
           "DJ source preference breaks equal evidence only");
    const auto reordered = rank(original,
        {beatport_equal, musicbrainz_equal, crates_equal,
         deezer_equal, discogs_equal});
    expect(reordered.size() == 5 &&
           reordered[0].provider == "beatport" &&
           reordered[1].provider == "crates" &&
           reordered[2].provider == "discogs" &&
           reordered[3].provider == "deezer" &&
           reordered[4].provider == "musicbrainz",
           "preferred source ordering is independent of retrieval order");
    const ProviderPreferences discogs_first{{"discogs", "beatport", "crates",
                                             "deezer", "musicbrainz"}};
    const auto customized = rank(original,
        {beatport_equal, discogs_equal, musicbrainz_equal}, discogs_first);
    expect(customized[0].provider == "discogs" &&
           customized[1].provider == "beatport",
           "source preference can be explicitly customized");
    const auto saved_priority = serialize_provider_preferences(discogs_first);
    expect(saved_priority ==
               "v1|discogs,beatport,crates,deezer,musicbrainz" &&
           parse_provider_preferences(saved_priority) == discogs_first &&
           parse_provider_preferences(serialize_provider_preferences({})) ==
               ProviderPreferences{},
           "full five-provider priority round-trips without losing Crates");
    expect(first_supported_search_mode({}) == 1 &&
           first_supported_search_mode(discogs_first) == 1 &&
           first_supported_search_mode(
               ProviderPreferences{{"deezer", "musicbrainz", "discogs",
                                    "crates", "beatport"}}) == 0 &&
           first_supported_search_mode(
               ProviderPreferences{{"deezer", "crates", "discogs",
                                    "musicbrainz", "beatport"}}) == 1 &&
           first_supported_search_mode(
               ProviderPreferences{{"beatport", "crates", "musicbrainz",
                                    "deezer", "discogs"}}) == 0 &&
           first_supported_search_mode(
               ProviderPreferences{{"beatport", "beatport", "discogs",
                                    "deezer", "musicbrainz"}}) == 0,
           "auto Find provider skips URL-only Beatport/Crates and unavailable Deezer");
    const auto restored = rank(original,
        {beatport_equal, discogs_equal, musicbrainz_equal},
        parse_provider_preferences(saved_priority));
    expect(restored.size() == 3 && restored.front().provider == "discogs" &&
           restored.front().score == restored[1].score,
           "restored preference governs ranking ties, not evidence score");
    for (std::string invalid : {
            "", "v2|beatport,crates,discogs,deezer,musicbrainz",
            "v1|beatport,crates,discogs,deezer",
            "v1|beatport,beatport,discogs,deezer,musicbrainz",
            "v1|beatport,crates,discogs,deezer,other",
            "v1|beatport,crates,discogs,deezer,musicbrainz,",
            "v1|beatport,crates,discogs,deezer,musicbrainz,other"}) {
        expect(parse_provider_preferences(invalid) == ProviderPreferences{},
               "corrupt source priority safely falls back to the full DJ order");
    }
    expect(serialize_provider_preferences(
               ProviderPreferences{{"discogs", "beatport"}}).empty() &&
           serialize_provider_preferences(
               ProviderPreferences{{"discogs", "discogs", "beatport",
                                    "deezer", "musicbrainz"}}).empty(),
           "partial or repeated identities cannot be persisted");
    Candidate wrong_mix_beatport = beatport_equal;
    wrong_mix_beatport.identity.mix = MixKind::Radio;
    const auto safety_wins = rank(original,
        {wrong_mix_beatport, musicbrainz_equal});
    expect(safety_wins[0].provider == "musicbrainz" &&
           safety_wins[0].decision == MatchDecision::Suggested &&
           safety_wins[1].provider == "beatport" &&
           safety_wins[1].decision == MatchDecision::Rejected,
           "primary DJ source cannot outrank a confirmed mix conflict");
    Candidate weaker_beatport = beatport_equal;
    weaker_beatport.identity.isrc.clear();
    const auto evidence_wins = rank(original,
        {weaker_beatport, musicbrainz_equal});
    expect(evidence_wins[0].provider == "musicbrainz" &&
           evidence_wins[0].score > evidence_wins[1].score,
           "higher verified evidence outranks preferred provider");
    
    expect(normalized_isrc("us-abc-26-00001") == "USABC2600001" &&
           normalized_isrc("usabc2600001") == "USABC2600001" &&
           normalized_isrc("US/ABC/26/00001").empty(),
           "ISRC token matching accepts optional separators but rejects bad codes");
    expect(comparable("  A --B \t C! ") == "a b c" &&
           comparable("Björk") == "björk" &&
           comparable(std::string("A\0B", 3)).empty(),
           "search-only normalization is conservative, UTF-8 preserving, and rejects NUL");

    expect(normalized_isrc("123456789012").empty() &&
           normalized_isrc("12ABC2600001").empty() &&
           normalized_isrc("USABC2X00001").empty() &&
           normalized_isrc("USABC26000A1").empty() &&
           normalized_isrc("USABC260001").empty() &&
           normalized_isrc("USABC2600001\n").empty(),
           "ISRC must satisfy country/registrant/year/designation grammar");

    Candidate invalid_isrc = strong;
    invalid_isrc.identity.isrc = "12ABC2600001";
    const auto invalid_code = compare(original, invalid_isrc);
    expect(invalid_code.decision == MatchDecision::Review &&
           has(invalid_code.conflicts, "invalid_isrc_unverified") &&
           !has(invalid_code.evidence, "isrc_exact_not_conclusive"),
           "invalid provider ISRC never contributes matching evidence");
    Candidate missing_isrc = strong;
    missing_isrc.identity.isrc.clear();
    const auto missing_code = compare(original, missing_isrc);
    expect(missing_code.decision == MatchDecision::Review &&
           has(missing_code.conflicts, "isrc_missing_on_one_side"),
           "one-sided ISRC remains an explicit review uncertainty");

    const RecordingIdentity unknown_version{
        "Sample", "DJ", "", MixKind::Unknown, "USABC2600001", 200000};
    const Candidate unknown_remote{
        "discogs", "11",
        {"Sample", "DJ", "", MixKind::Unknown, "US-ABC-26-00001", 200000}};
    const auto unknown_result = compare(unknown_version, unknown_remote);
    expect(unknown_result.decision == MatchDecision::Review &&
           has(unknown_result.conflicts, "mix_identity_unverified") &&
           has(unknown_result.evidence, "isrc_exact_not_conclusive"),
           "same ISRC cannot auto-suggest unidentified recording version");

    auto remix_local = original;
    remix_local.mix = MixKind::Remix;
    remix_local.mix_name.clear();
    Candidate unnamed_remix = strong;
    unnamed_remix.identity.mix = MixKind::Remix;
    unnamed_remix.identity.mix_name.clear();
    const auto remix_result = compare(remix_local, unnamed_remix);
    expect(remix_result.decision == MatchDecision::Review &&
           has(remix_result.conflicts, "remix_identity_unverified"),
           "unnamed remixes require review despite matching ISRC and runtime");

    std::cout << "PASS: candidate evidence, version conflicts, ISRC limits, duration, identity and ordering\n";
}
