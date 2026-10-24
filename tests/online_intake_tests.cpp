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
    // A manual provider paste should not revert the Find browser to one
    // top-level row per tag. Keep one pasted-candidate group and a separate
    // full physical-source group, with every original field reachable in
    // detail and unmodified provenance on the underlying row.
    auto grouped_manual=compare;
    append_pasted_physical_comparison_rows(grouped_manual,local,candidate.provider);
    check(grouped_manual.size()==compare.size()+local.fields.size() &&
          grouped_manual.front().field=="Pasted discogs / ARTIST" &&
          grouped_manual.front().candidate.field=="ARTIST" &&
          online_evidence_origin_label(grouped_manual.front())=="Pasted: discogs" &&
          grouped_manual.front().original_values==std::vector<std::string>{"Example DJ"} &&
          grouped_manual[compare.size()].field=="Physical / ARTIST" &&
          is_local_physical_original(grouped_manual[compare.size()]) &&
          grouped_manual[compare.size()+2].original_values==
              std::vector<std::string>({"House","Deep House"}) &&
          local.fields[2].values==std::vector<std::string>({"House","Deep House"}),
          "structured pasted comparison preserves per-field evidence and complete physical inventory");
    auto forged_manual=compare;
    forged_manual.push_back(make_local_physical_original(
        local.fields.front(),"local_physical_inventory_readonly"));
    bool forged_blocked=false;
    try {append_pasted_physical_comparison_rows(
        forged_manual,local,candidate.provider);}
    catch(const std::invalid_argument&) {forged_blocked=true;}
    check(forged_blocked && forged_manual[0].field==compare[0].field,
          "pasted grouping rejects forged local provenance before mutating display rows");
    MetadataDocument many_originals;
    for(int field=0;field<1200;++field)
        many_originals.fields.push_back({"CUSTOM_"+std::to_string(field),{"unaltered"}});
    auto bounded_manual=compare;
    append_pasted_physical_comparison_rows(
        bounded_manual,many_originals,candidate.provider);
    check(bounded_manual.size()==1024 &&
          bounded_manual.back().state==FieldReviewState::Blocked &&
          bounded_manual.back().field==
              "Physical / Additional physical tags not displayed" &&
          many_originals.fields.size()==1200,
          "large physical inventories use a bounded, explicit overflow notice");

    const auto digitally_published =
        parse_manual_candidate("@provider=deezer\n@id=track:7\nDATE@digital=2003-01-02\n");
    check(digitally_published.fields[0].date_meaning == DateMeaning::DigitalPublication,
          "digital publication date remains distinguishable");
    // The full supported date vocabulary is shared by parser and comparison:
    // manually declared provider origin never upgrades a date to official API.
    const auto typed_dates=parse_manual_candidate(
        "@provider=deezer\n@id=album:7\n@scope=edition\n"
        "ORIGINALDATE@original=2001-03-05\n"
        "RELEASEDATE@edition=2003-01-02\n"
        "PUBLISHDATE@digital=2004-04-06\n"
        "PUBLISH_DATE@digital=2004-04-06\n"
        "DATE_RAW@edition=2003-01-02\n");
    const auto typed_date_review=review_online_fields(MetadataDocument{},typed_dates.fields);
    check(typed_dates.fields.size()==5 &&
          typed_dates.fields[0].date_meaning==DateMeaning::OriginalRelease &&
          typed_dates.fields[1].date_meaning==DateMeaning::EditionRelease &&
          typed_dates.fields[2].date_meaning==DateMeaning::DigitalPublication &&
          typed_dates.fields[3].date_meaning==DateMeaning::DigitalPublication &&
          typed_dates.fields[4].date_meaning==DateMeaning::EditionRelease &&
          typed_date_review.size()==5 &&
          typed_date_review[0].state==FieldReviewState::NeedsReview &&
          typed_date_review[1].state==FieldReviewState::NeedsReview &&
          typed_date_review[2].state==FieldReviewState::NeedsReview &&
          online_evidence_origin_label(typed_date_review[1])=="Pasted: deezer",
          "all supported typed dates reach read-only review without origin escalation");

    // Both still-missing live providers have a supported, explicitly
    // unverified manual lane. Merely naming a provider cannot impersonate
    // an authorized HTTPS response or authorize a physical tag write.
    const MetadataDocument spotify_local{{{"ARTIST",{"Kyau & Albert"}},
                                         {"TITLE",{"Original title"}},
                                         {"GENRE",{"Trance"}}}};
    const auto spotify_manual=parse_manual_candidate(
        "@provider=spotify\n@id=track:912345\n@scope=recording\n"
        "ARTIST=Kyau & Albert\nARTIST=Maywave\n"
        "TITLE=1000 Years (DJ Version)\nGENRE=Trance\n");
    const auto spotify_rows=review_online_fields(
        spotify_local,spotify_manual.fields);
    check(spotify_manual.provider=="spotify" &&
          spotify_manual.scope==EvidenceScope::Recording &&
          spotify_manual.fields.size()==3 &&
          spotify_manual.fields[0].values==
              std::vector<std::string>({"Kyau & Albert","Maywave"}) &&
          spotify_rows.size()==3 &&
          online_evidence_origin_label(spotify_rows[0])=="Pasted: spotify" &&
          spotify_rows[0].state==FieldReviewState::NeedsReview &&
          spotify_local.fields[0].values==
              std::vector<std::string>({"Kyau & Albert"}),
          "Spotify manual collaboration is unverified evidence, never an automatic tag edit");
    const auto deezer_manual=parse_manual_candidate(
        "@provider=deezer\n@id=track:4318295162\n@scope=recording\n"
        "ARTIST=Kenneth Thomas\nTITLE=Emily (C-Sharp) (Extended Mix)\n"
        "DATE@digital=2026-10-16\n");
    const auto deezer_rows=review_online_fields(
        spotify_local,deezer_manual.fields);
    check(deezer_manual.provider=="deezer" &&
          deezer_manual.fields.size()==3 &&
          deezer_manual.fields[2].date_meaning==
              DateMeaning::DigitalPublication &&
          online_evidence_origin_label(deezer_rows[0])=="Pasted: deezer" &&
          deezer_rows[2].candidate.date_meaning==
              DateMeaning::DigitalPublication &&
          spotify_local.fields.size()==3,
          "Deezer manual digital date retains unverified origin and never rewrites originals");
    fails("@provider=spotify\n@id=track:912345\nDATE=2026\n",
          "Spotify manual date without typed date meaning is rejected");
    for(const auto* date_field:{"ORIGINALDATE","RELEASEDATE",
                                 "PUBLISHDATE","PUBLISH_DATE","DATE_RAW"}) {
        const std::string missing="@provider=spotify\n@id=track:4\n"+
            std::string(date_field)+"=2026-01-02\n";
        fails(missing,"every date field requires an explicit temporal meaning");
    }
    fails("@provider=spotify\n@id=track:4\nRELEASEDATE@unknown=2026-01-02\n",
          "unknown release date meaning cannot be imported");
    fails("@provider=spotify\n@id=track:4\nTITLE@edition=2026-01-02\n",
          "non-date fields cannot claim an edition date");
    fails("@provider=deezer\n@id=track:4318295162\nTITLE=\n",
          "Deezer manual empty title cannot imply tag deletion");

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
