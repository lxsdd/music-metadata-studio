#include "djmeta/cue_manual_preview.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << "\n"; std::exit(1); }
}
void reject(const djmeta::CueMetadataInventory& cue, const std::string& text,
            const char* message) {
    bool threw = false;
    try {
        const auto candidate = djmeta::online::parse_manual_candidate(text);
        (void)djmeta::online::review_manual_cue_candidate(cue, candidate);
    } catch (const std::invalid_argument&) { threw = true; }
    check(threw, message);
}
}
int main() {
    using namespace djmeta;
    using namespace djmeta::online;

    // Deliberately repeated "TRACK 01" on two distinct FILEs.
    const std::string raw =
        "TITLE \"Album One\"\n"
        "PERFORMER \"Common Artist\"\n"
        "REM GENRE \"House\"\n"
        "FILE \"one.flac\" WAVE\n"
        "TRACK 01 AUDIO\n"
        "  TITLE \"Sunrise\"\n"
        "  PERFORMER \"First Artist\"\n"
        "FILE \"two.flac\" WAVE\n"
        "TRACK 01 AUDIO\n"
        "  TITLE \"Sunset\"\n";
    const auto before = raw;
    const auto cue = inspect_cue_metadata(raw, CueCarrierKind::ExternalText);
    check(cue.status == CueSyntaxStatus::Parsed && cue.tracks.size() == 2,
          "two qualified virtual tracks in repeated-number multi-FILE CUE");

    const auto album = parse_manual_candidate(
        "@provider=discogs\n@id=release:135\n@scope=edition\n"
        "ALBUM=Album One\nGENRE=Tech House\nLABEL=Example Label\n");
    const auto album_rows = review_manual_cue_candidate(cue, album);
    check(album_rows.size() == 3 &&
          album_rows[0].field == "Album / TITLE" &&
          album_rows[0].state == FieldReviewState::Unchanged &&
          album_rows[0].reason == "exact_noop",
          "global album TITLE exact no-op");
    check(album_rows[1].field == "Album / REM GENRE" &&
          album_rows[1].original_values == std::vector<std::string>({"House"}) &&
          album_rows[1].state == FieldReviewState::NeedsReview &&
          album_rows[1].reason.find("affected inherited tracks: 2") !=
              std::string::npos,
          "global genre difference displays affected track scope");
    check(album_rows[2].state == FieldReviewState::Blocked &&
          album_rows[2].field == "Unsupported / LABEL",
          "unrepresentable provider field never creates arbitrary REM");

    const auto second = parse_manual_candidate(
        "@provider=beatport\n@id=track:178\n@scope=recording\n"
        "@cue_track_ordinal=2\nTITLE=Sunset (Original Mix)\n"
        "ARTIST=Common Artist\nKEY=C minor\n");
    const auto track_rows = review_manual_cue_candidate(cue, second);
    check(track_rows.size() == 3 &&
          track_rows[0].field == "Track 2 / TITLE" &&
          track_rows[0].original_values == std::vector<std::string>({"Sunset"}) &&
          track_rows[0].state == FieldReviewState::NeedsReview &&
          track_rows[0].candidate.source_id == "track:178",
          "second CUE ordinal targets second FILE's track 01, never first");
    check(track_rows[1].field == "Track 2 / PERFORMER" &&
          track_rows[1].state == FieldReviewState::Unchanged &&
          track_rows[1].reason.find("inherited source") != std::string::npos,
          "same inherited performer is a no-op with provenance");
    check(track_rows[2].state == FieldReviewState::Blocked &&
          track_rows[2].candidate.provider == "beatport",
          "unsupported key remains blocked but provenance preserved");

    const auto embedded = inspect_cue_metadata(raw, CueCarrierKind::EmbeddedText);
    const auto embedded_rows = review_manual_cue_candidate(embedded, second);
    check(embedded_rows.size() == track_rows.size() &&
          embedded_rows[0].original_values == track_rows[0].original_values,
          "identical scope semantics external and embedded text CUEs");

    reject(cue,
        "@provider=discogs\n@id=track:12\n@scope=recording\nTITLE=Sunset\n",
        "no implicit arbitrary first track");
    reject(cue,
        "@provider=discogs\n@id=track:12\n@scope=recording\n"
        "@cue_track_ordinal=3\nTITLE=Sunset\n",
        "out-of-range CUE order does not select a different track");
    reject(cue,
        "@provider=discogs\n@id=release:12\n@scope=edition\n"
        "@cue_track_ordinal=2\nALBUM=New\n",
        "album changes cannot carry a confusing track target");
    reject(cue,
        "@provider=discogs\n@id=release:12\n@scope=recording\n"
        "@cue_track_ordinal=0\nTITLE=Sunset\n",
        "ordinal zero cannot alias the first track");

    const auto ambiguous = inspect_cue_metadata(
        "FILE x.mp3 MP3\nTRACK 01 AUDIO\nTITLE \"unterminated\n",
        CueCarrierKind::EmbeddedText);
    reject(ambiguous,
        "@provider=discogs\n@id=track:13\n@scope=recording\n"
        "@cue_track_ordinal=1\nTITLE=Safe\n",
        "unqualified CUE source cannot receive confident field comparison");
    check(raw == before,
          "review and parsing are pure: original CUE bytes unchanged");
    std::cout << "PASS: CUE clipboard scopes, provenance, no-op, index safety, and abstention\n";
}
