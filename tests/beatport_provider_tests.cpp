#include "djmeta/beatport_provider.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void check(bool truth, const char* why) {
    if (!truth) {
        std::cerr << "FAIL: " << why << '\n';
        std::exit(1);
    }
}
template<typename Fn> void refuses(Fn fn, const char* why) {
    bool refused = false;
    try { fn(); } catch (const std::invalid_argument&) { refused = true; }
    check(refused, why);
}
const djmeta::online::FieldEvidence* field(
    const std::vector<djmeta::online::FieldEvidence>& evidence,
    std::string_view name) {
    for (const auto& item : evidence)
        if (item.field == name) return &item;
    return nullptr;
}
}

int main() {
    using namespace djmeta;
    using namespace djmeta::online;
    using namespace djmeta::online::beatport;

    check(search_path("Artist & Song (Extended Mix)") ==
          "/v4/catalog/search/?q=Artist%20%26%20Song%20%28Extended%20Mix%29"
          "&type=tracks&per_page=20&page=1" &&
          search_path("Song", 3).ends_with("page=3") &&
          track_path(321) == "/v4/catalog/tracks/321" &&
          release_path(987) == "/v4/catalog/releases/987" &&
          release_tracks_path(987) ==
              "/v4/catalog/releases/987/tracks?per_page=200",
          "fixed Beatport v4 endpoint paths, percent encoding and bounded pagination");
    const auto search_plan=track_search_plan(
        "Home (Extended Mix)", "DJ Example");
    check(search_plan.size()==2 &&
          search_plan[0].path==search_path("DJ Example Home") &&
          search_plan[1].path==search_path("Home") &&
          search_plan[0].reason!=search_plan[1].reason,
          "Beatport search first uses artist and base title, then broadens");
    const auto featuring_plan=track_search_plan(
        "Forever feat. Singer (Extended Mix)","Producer");
    check(featuring_plan.size()==4 &&
          featuring_plan[0].path==search_path("Producer Forever feat. Singer") &&
          featuring_plan[1].path==search_path("Forever feat. Singer") &&
          featuring_plan[2].path==search_path("Producer Forever") &&
          featuring_plan[3].path==search_path("Forever") &&
          featuring_plan[3].reason.find("identity still verified")!=std::string::npos,
          "OneTagger-inspired bounded feat fallback retains strict exact match gate");
    check(track_search_plan("Forever Ft. Singer","Producer").size()==4 &&
          track_search_plan("Forever featuring Singer","Producer").size()==4 &&
          track_search_plan("Title (Extended Mix)","Artist").size()==2 &&
          track_search_plan("feat. Singer","Producer").size()==2,
          "mixed-case features are optional query alternatives, not empty searches");
    const auto no_artist_plan=track_search_plan("Home (Radio Edit)");
    check(no_artist_plan.size()==1 &&
          no_artist_plan[0].path==search_path("Home"),
          "mixed DJ title annotation is never passed as a fragile search suffix");
    refuses([] { (void)track_search_plan("Home\nOther", "DJ"); },
            "search plans reject control-bearing local tags");
    const auto long_artist_plan=track_search_plan("Home",std::string(150,'A'));
    check(long_artist_plan.size()==1 &&
          long_artist_plan[0].path==search_path("Home"),
          "excess combined query length safely falls back to base title");

    refuses([] { (void)search_path("bad\nheader"); },
            "control bytes cannot inject request headers");
    refuses([] { (void)search_path("bad\rheader"); },
            "CR control bytes rejected");
    refuses([] { (void)search_path(std::string(151,'A')); },
            "reject oversized search");
    refuses([] { (void)search_path("Song", 4); },
            "never silently crawl unbounded pages");
    refuses([] { (void)track_path(0); }, "reject empty track identity");
    refuses([] { (void)release_path(1000000000000ULL); },
            "reject unbounded edition identity");

    const auto raw = R"JSON({
      "id":321, "name":"Home",
      "mix_name":"Extended Mix",
      "artists":[{"id":8,"name":"DJ Example"}],
      "remixers":[{"id":99,"name":"Mixer Q"}],
      "isrc":"GB-XYZ-26-01234",
      "bpm":128, "length_ms":402500, "number":2,
      "genre":{"id":20,"name":"Melodic House & Techno"},
      "sub_genre":{"id":21,"name":"Progressive House"},
      "key":{"id":4,"name":"C Minor"},
      "catalog_number":"CAT-09",
      "new_release_date":"2026-10-16",
      "publish_date":"2026-10-18",
      "release":{
        "id":987, "name":"Home EP",
        "label":{"id":7,"name":"Example Label"},
        "upc":"123456789012"
      }
    })JSON";
    const auto parsed = parse_track(raw,321);
    check(parsed.id==321 && parsed.release_id==987 &&
          parsed.title=="Home" && parsed.mix_name=="Extended Mix" &&
          parsed.artists==std::vector<std::string>{"DJ Example"} &&
          parsed.remixers==std::vector<std::string>{"Mixer Q"} &&
          parsed.duration_ms==402500 && parsed.bpm==128 &&
          parsed.genre=="Melodic House & Techno" &&
          parsed.subgenre=="Progressive House" &&
          parsed.musical_key=="C Minor" &&
          parsed.catalog_number=="CAT-09" &&
          parsed.label_name=="Example Label" &&
          parsed.release_date=="2026-10-16" &&
          parsed.publish_date=="2026-10-18",
          "Beatport recording, explicit mix, DJ features, label and two dates stay typed");
    const auto candidate=to_candidate(parsed);
    check(candidate.provider=="beatport" && candidate.provider_track_id=="321" &&
          candidate.identity.title=="Home" &&
          candidate.identity.primary_artist=="DJ Example" &&
          candidate.identity.mix==MixKind::Extended &&
          candidate.identity.mix_name=="Extended Mix" &&
          candidate.identity.duration_ms==402500,
          "Beatport version and artist enter shared matching without title flattening");
    RecordingIdentity decorated_local = candidate.identity;
    decorated_local.title = "Home (Extended Mix)";
    decorated_local.mix = MixKind::Unknown;
    decorated_local.mix_name.clear();
    check(compare(decorated_local,candidate).decision==MatchDecision::Suggested,
          "Beatport base title plus version reconciles decorated local DJ TITLE");
    decorated_local.title = "Home (Radio Edit)";
    check(compare(decorated_local,candidate).decision==MatchDecision::Rejected,
          "radio cut cannot reuse long-mix evidence despite identical ISRC");

    const auto local=candidate.identity;
    const auto verified=compare(local,candidate);
    check(verified.decision==MatchDecision::Suggested,
          "full recording identity can suggest, never directly write");
    auto wrong=candidate;
    wrong.identity.mix=MixKind::Radio;
    check(compare(local,wrong).decision==MatchDecision::Rejected,
          "radio version does not inherit extended mix identity");
    wrong=candidate;
    wrong.identity.mix_name="Producer X Remix";
    check(compare(local,wrong).decision==MatchDecision::Rejected,
          "wrong named mix is rejected even with the same ISRC");
    check(mix_kind("Original Mix")==MixKind::Original &&
          mix_kind("Club Remix")==MixKind::Remix &&
          mix_kind("Radio Edit")==MixKind::Radio &&
          mix_kind("Instrumental Mix")==MixKind::Instrumental &&
          mix_kind("Dub Version")==MixKind::Unknown,
          "explicit types classified; unrecognized versions not guessed");

    const auto evidence=fields(parsed);
    const auto* title=field(evidence,"TITLE");
    const auto* artist=field(evidence,"ARTIST");
    const auto* version=field(evidence,"VERSION");
    const auto* date=field(evidence,"DATE");
    const auto* publish=field(evidence,"PUBLISH_DATE");
    const auto* genre=field(evidence,"GENRE");
    const auto* remixer=field(evidence,"REMIXER");
    check(title && title->scope==EvidenceScope::Recording &&
          title->provider=="beatport" && title->source_id=="321" &&
          artist && artist->values.size()==1 &&
          version && version->values==std::vector<std::string>{"Extended Mix"} &&
          date && date->source_id=="987" &&
          date->scope==EvidenceScope::Edition &&
          date->date_meaning==DateMeaning::EditionRelease &&
          publish && publish->date_meaning==DateMeaning::DigitalPublication &&
          genre && remixer && remixer->values.size()==1,
          "field-level evidence retains original provider IDs, scopes and date meanings");

    const MetadataDocument original{{{"TITLE",{"Home"}},
        {"ARTIST",{"DJ Example"}}, {"BPM",{"127"}},
        {"X_MYTAG",{"mine"}}, {"CUESHEET",{"keep unchanged"}},
        {"REPLAYGAIN_TRACK_GAIN",{"-4.33 dB"}}}};
    const auto saved=original;
    const auto review=review_online_fields(original,evidence);
    check(original==saved && review.size()==evidence.size(),
          "read-only Beatport review never mutates physical or CUE tags");
    bool saw_noop=false,saw_review=false;
    for(const auto& row:review) {
        if(row.field=="TITLE") saw_noop=row.state==FieldReviewState::Unchanged;
        if(row.field=="BPM") saw_review=row.state==FieldReviewState::NeedsReview;
        check(row.state!=FieldReviewState::Blocked,
              "well-formed Beatport candidate evidence passes field validation");
    }
    check(saw_noop && saw_review && original.fields[3].values[0]=="mine",
          "unchanged tags remain no-op; BPM is REVIEW and customs preserved");

    refuses([&] { (void)parse_track(raw,322); },
            "requested track identity cannot silently switch");
    refuses([] { (void)parse_track(R"({"id":1,"name":"bad\ncontrol"})",1); },
            "reject control characters in identity");
    refuses([] { (void)parse_track(R"({"id":1,"name":""})",1); },
            "track must have valid base title");
    refuses([] { (void)parse_track(std::string(2*1024*1024+1,'x'),1); },
            "reject excessive provider JSON before allocating unbounded nodes");

    const auto multi= parse_track(R"({
      "id":999,"name":"Together","artists":[{"name":"DJ A"},{"name":"DJ B"}],
      "mix_name":"The Other Producer Remix",
      "bpm":999, "length_ms":-10,
      "new_release_date":"2025-02-29", "publish_date":"2024-02-29",
      "genre":{"name":"House"}
    })",999);
    check(multi.artists.size()==2 &&
          to_candidate(multi).identity.primary_artist.empty() &&
          to_candidate(multi).identity.mix==MixKind::Remix &&
          multi.bpm==-1 && multi.duration_ms==-1 &&
          multi.release_date.empty() && multi.publish_date=="2024-02-29",
          "collaborations preserved without guessed join; invalid numbers/dates omitted");
    const auto multi_fields=fields(multi);
    check(field(multi_fields,"ARTIST") &&
          field(multi_fields,"ARTIST")->values.size()==2 &&
          !field(multi_fields,"DATE") && !field(multi_fields,"BPM"),
          "multi-credit evidence remains multivalued; absent data cannot delete tags");
    check(calendar_date("2000-02-29") && !calendar_date("1900-02-29") &&
          !calendar_date("2026-13-01") && !calendar_date("2026-02-30"),
          "Gregorian calendar dates validated without year-only fabrication");

    const auto search=parse_search(R"({"tracks":[
      {"id":321,"name":"Home","mix_name":"Extended Mix",
       "artists":[{"name":"DJ Example"}],"length_ms":402500},
      {"id":322,"name":"Home","mix_name":"Radio Edit",
       "artists":[{"name":"DJ Example"}],"length_ms":200000},
      {"id":0,"name":"Bad id"},
      {"id":323,"name":"bad\ncontrol"}
    ]})");
    check(search.hits.size()==2 &&
          search.hits[0].mix_name=="Extended Mix" &&
          search.hits[1].mix_name=="Radio Edit",
          "search preserves distinct named versions and discards malformed hits");
    const auto pages=parse_search(R"({"count":27,"tracks":[
       {"id":1,"name":"First"}]})");
    check(pages.hits.size()==1 && pages.has_more,
          "API count reveals additional pages without automatically fetching them");
    refuses([] { (void)parse_search(R"({"tracks":{} })"); },
            "reject unexpected search result shape");
    refuses([] { (void)parse_search(R"({"tracks":[],"results":[]})"); },
            "reject ambiguous search result keys");
    refuses([] { (void)parse_search(R"({"tracks":[],"tracks":[]})"); },
            "reject duplicate JSON keys");

    const std::string release=R"({
      "id":987,"name":"Home EP","track_count":2,
      "artists":[{"name":"DJ Example"},{"name":"DJ Guest"}],
      "label":{"id":7,"name":"Example Label"},
      "upc":"123456789012","new_release_date":"2026-10-16"
    })";
    const std::string tracks=R"({"results":[
       {"id":321,"name":"Home","mix_name":"Extended Mix","number":1,
        "artists":[{"name":"DJ Example"}],
        "release":{"id":987,"name":"Home EP"}},
       {"id":322,"name":"Home","mix_name":"Radio Edit","number":2,
        "artists":[{"name":"DJ Example"}],
        "release":{"id":987,"name":"Home EP"}}
    ]})";
    const auto edition=parse_edition(release,tracks,987);
    check(edition.provider=="beatport" && edition.edition_id=="987" &&
          edition.tracks.size()==2 &&
          edition.tracks[0].source_track_id=="321" &&
          edition.tracks[1].source_track_id=="322" &&
          edition.tracks[1].track_number==2 &&
          edition.tracks[0].recording.mix==MixKind::Extended &&
          edition.tracks[1].recording.mix==MixKind::Radio &&
          edition.tracks[0].fields.size()>=2 &&
          field(edition.fields,"ALBUM ARTIST") &&
          field(edition.fields,"ALBUM ARTIST")->values.size()==2,
          "release and track editions preserve version/position and album credits");
    refuses([&] { (void)parse_edition(release,tracks,988); },
            "release ID mismatch prevents false edition alignment");
    refuses([&] { (void)parse_edition(R"({
            "id":987,"name":"Very long album","track_count":220})",
            tracks,987); },
            "release with more than a bounded track page is not accepted");
    refuses([&] { (void)parse_edition(R"({
            "id":987,"name":"Bad count","track_count":"2"})",
            tracks,987); },
            "malformed release count is not interpreted as unknown");

    refuses([&] { (void)parse_edition(release,
            R"({"results":[{"id":321,"name":"Home"}]})",987); },
            "partial track page cannot silently become complete album");
    refuses([&] { (void)parse_edition(release,
            R"({"results":[{"id":321,"name":"Home","release":{"id":999}},
                            {"id":322,"name":"Home"}]})",987); },
            "foreign release track is rejected");
    refuses([&] { (void)parse_edition(release,
            R"({"results":[{"id":321,"name":"Home"},
                            {"id":0,"name":"Invalid"}]})",987); },
            "invalid track causes fail-closed album parsing");


    // Exact-edition completeness is mandatory, even for a plausible teaser.
    refuses([&] { (void)parse_edition(
        R"({"id":987,"name":"Home EP"})",tracks,987); },
        "missing release count cannot establish complete edition");
    refuses([&] { (void)parse_edition(release,
        R"({"count":3,"results":[
          {"id":321,"name":"Home","release":{"id":987}},
          {"id":322,"name":"Home","release":{"id":987}}]})",987); },
        "pagination count must agree with release");
    refuses([&] { (void)parse_edition(release,
        R"({"next":"page-2","results":[
          {"id":321,"name":"Home","release":{"id":987}},
          {"id":322,"name":"Home","release":{"id":987}}]})",987); },
        "next-page pointer disqualifies full edition");
    refuses([&] { (void)parse_edition(release,
        R"({"page":2,"results":[
          {"id":321,"name":"Home","release":{"id":987}},
          {"id":322,"name":"Home","release":{"id":987}}]})",987); },
        "non-first page cannot be full edition");
    refuses([&] { (void)parse_edition(release,
        R"({"results":[
          {"id":321,"name":"Home","release":{"id":987}},
          {"id":321,"name":"Home Again","release":{"id":987}}]})",987); },
        "duplicate recording IDs are not two tracks");
    refuses([&] { (void)parse_edition(release,
        R"({"results":[
          {"id":321,"name":"Home","number":1,"release":{"id":987}},
          {"id":322,"name":"Home","number":1,"release":{"id":987}}]})",987); },
        "duplicate edition positions are ambiguous");
    refuses([&] { (void)parse_edition(release,
        R"({"results":[
          {"id":321,"name":"Home"},
          {"id":322,"name":"Home","release":{"id":987}}]})",987); },
        "missing per-track release membership cannot be assumed");
    std::cout << "PASS: Beatport bounded v4 catalog parser, editions, versions, provenance, no writes and safety\n";
}
