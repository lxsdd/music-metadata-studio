#include "djmeta/online_intake.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void check(bool truth, const char* message) {
    if (!truth) { std::cerr << "FAIL: " << message << "\n"; std::exit(1); }
}
void fails(std::string_view candidate, const char* message) {
    bool rejected = false;
    try { (void)djmeta::online::parse_manual_candidate(candidate); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, message);
}
}

int main() {
    using namespace djmeta;
    using namespace djmeta::online;
    const std::string text =
        "@provider=discogs\r\n"
        "@id=release:100\r\n"
        "@scope=edition\r\n"
        "ARTIST=Example DJ\r\n"
        "TITLE=First Track (Extended Mix)\r\n"
        "GENRE=House\r\n"
        "Genre=Deep House\r\n"
        "DATE_RAW@edition=1998-05-06\r\n"
        "COMMENT=one; two\r\n"
        "X_CUSTOM=Björk - 東京\r\n";
    const auto snapshot = text;
    const auto candidate = parse_manual_candidate(text);
    check(text == snapshot && candidate.provider == "discogs" &&
          candidate.source_id == "release:100" &&
          candidate.scope == EvidenceScope::Edition,
          "input immutability and edition source");
    check(candidate.fields.size() == 6 &&
          candidate.fields[2].values == std::vector<std::string>({"House", "Deep House"}) &&
          candidate.fields[2].field == "GENRE" &&
          candidate.fields[2].scope == EvidenceScope::Edition,
          "repeated fields become exact ordered vectors, first spelling retained");
    check(candidate.fields[3].date_meaning == DateMeaning::EditionRelease &&
          candidate.fields[4].values == std::vector<std::string>({"one; two"}) &&
          candidate.fields[5].values.front() == "Björk - 東京",
          "date meaning, literal semicolon and UTF-8 preserved");
    const MetadataDocument local{{{"ARTIST", {"Example DJ"}},
                                  {"TITLE", {"Old track"}},
                                  {"GENRE", {"House", "Deep House"}},
                                  {"DATE_RAW", {"1998"}}}};
    const auto compare = review_online_fields(local, candidate.fields);
    check(compare.size() == 6 &&
          compare[0].state == FieldReviewState::Unchanged &&
          compare[1].state == FieldReviewState::NeedsReview &&
          compare[2].state == FieldReviewState::Unchanged &&
          compare[3].candidate.date_meaning == DateMeaning::EditionRelease &&
          compare[3].state == FieldReviewState::NeedsReview &&
          compare[4].state == FieldReviewState::NeedsReview,
          "parsed candidate flows unchanged into existing read-only provenance/no-op review");
    const auto digitally_published =
        parse_manual_candidate("@provider=deezer\n@id=track:7\nDATE@digital=2003-01-02\n");
    check(digitally_published.fields[0].date_meaning == DateMeaning::DigitalPublication,
          "digital publication date remains distinguishable");

    const auto cue_track = parse_manual_candidate(
        "@provider=discogs\n@id=track:12\n@scope=recording\n"
        "@cue_track_ordinal=2\nTITLE=Sunset\n");
    check(cue_track.cue_track_ordinal &&
          *cue_track.cue_track_ordinal == 1 &&
          cue_track.scope == EvidenceScope::Recording,
          "explicit CUE order is 1-based and converted once to index 1");
    const auto album_scope = parse_manual_candidate(
        "@provider=discogs\n@id=release:123\n@scope=edition\nALBUM=Example\n");
    check(!album_scope.cue_track_ordinal, "legacy album evidence is unchanged");
    fails("@provider=discogs\n@id=track:12\n@cue_track_ordinal=0\nTITLE=A\n",
          "track order zero rejected");
    fails("@provider=discogs\n@id=track:12\n@cue_track_ordinal=02\nTITLE=A\n",
          "noncanonical leading zero order rejected");
    fails("@provider=discogs\n@id=track:12\n@cue_track_ordinal=1025\nTITLE=A\n",
          "CUE position capped at 1024");
    fails("@provider=discogs\n@id=track:12\n@cue_track_ordinal=1\n"
          "@cue_track_ordinal=2\nTITLE=A\n", "duplicate CUE order rejected");
    fails("@provider=discogs\n@id=track:12\n@cue_track_ordinal=12x\nTITLE=A\n",
          "untrusted CUE order rejects trailing text");
    fails("@provider=discogs\n@id=track:12\nTITLE=A\n@cue_track_ordinal=1\n",
          "CUE order must be before fields");

    fails("", "empty");
    fails("@provider=discogs\n@id=release:1\n", "missing tag");
    fails("@provider=discogs\nTITLE=Unknown\n", "no identity");
    fails("@provider=discogs\n@id=release:1\n@provider=spotify\nTITLE=A\n", "duplicate provider");
    fails("@provider=discogs\n@id=release:1\n@scope=edition\n@scope=release\nTITLE=A\n", "duplicate scope");
    fails("@provider=discogs\n@id=release:1\n@unsupported=X\nTITLE=A\n", "unknown header");
    fails("@provider=discogs\n@id=release:1\nTITLE=A\n@scope=edition\n", "late directive");
    fails("@provider=discogs\n@id=release:1\nDATE=2000\n", "date role required");
    fails("@provider=discogs\n@id=release:1\nGENRE@original=House\n", "date role on non-date");
    fails("@provider=discogs\n@id=release:1\nDATE@original=2000\nDATE@edition=2010\n", "date role conflict");
    fails("@provider=discogs\n@id=release:1\nTITLE=\n", "implicit deletion");
    fails("@provider=discogs\n@id=release:1\nTITLE=A\nBADVALUE\n", "no equals");
    fails("@provider=discogs\n@id=release:1\nTITLE=A\rB\n", "embedded CR");
    fails(std::string("@provider=discogs\n@id=release:1\nTITLE=A") +
          std::string(1, '\0') + "B\n", "embedded NUL");
    fails("@provider=discogs\n@id=release:1\nTITLE=\xC0\xAF\n", "invalid UTF8");
    fails("@provider=discogs\n@id=release:1\nTITLE@original@edition=A\n", "multiple role suffixes");
    fails("@provider=discogs\n@id=release:1\nTITLE=A\n" + std::string(65536, 'X'),
          "oversized source text");
    std::string too_many =
        "@provider=discogs\n@id=release:1\n";
    for (int n = 0; n < 97; ++n)
        too_many += "FIELD" + std::to_string(n) + "=value\n";
    fails(too_many, "too many fields");
    std::string too_many_values =
        "@provider=discogs\n@id=release:1\n";
    for (int n = 0; n < 33; ++n) too_many_values += "GENRE=value\n";
    fails(too_many_values, "too many values");

    std::cout << "PASS: manual candidate text intake, immutable vectors, provenance, errors and bounds\n";
}
