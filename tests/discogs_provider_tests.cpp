#include "djmeta/discogs_provider.h"
#include "djmeta/discogs_search_hints.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void check(bool yes, const char* why) {
    if(!yes) { std::cerr<<"FAIL: "<<why<<"\n"; std::exit(1); }
}
template<class F> void rejects(F fn, const char* why) {
    bool refused=false;
    try {fn();}catch(const std::invalid_argument&){refused=true;}
    check(refused,why);
}
} // namespace

int main() {
    using namespace djmeta::online;
    using namespace djmeta::online::discogs;
    check(request_spacing_wait_ms(0,100)==0 &&
          request_spacing_wait_ms(100,100)==1600 &&
          request_spacing_wait_ms(100,101)==1599 &&
          request_spacing_wait_ms(100,1699)==1 &&
          request_spacing_wait_ms(100,1700)==0 &&
          request_spacing_wait_ms(100,1900)==0 &&
          request_spacing_wait_ms(500,200)==1600,
          "staged Discogs requests wait in their worker instead of failing");
    const auto request=search_path("What Time Is Love (Original Version)","The KLF");
    check(request.starts_with("/database/search?q=") &&
          request.find("type=release")!=std::string::npos &&
          request.find("per_page=20")!=std::string::npos &&
          request.find("artist=The%20KLF")!=std::string::npos &&
          request.find("Authorization")==std::string::npos &&
          request.find("token")==std::string::npos,
          "Discogs search fixed endpoint, bounded page, URL-encoded and no secret");
    // Legal artist-only release discovery; broad hits stay unverified.
    const auto by_artist=artist_search_path("Kyau & Albert x Maywave");
    check(by_artist==
          "/database/search?artist=Kyau%20%26%20Albert%20x%20Maywave&type=release&page=1&per_page=20" &&
          allowed_readonly_path(by_artist) &&
          !allowed_readonly_path("/database/search?artist=Artist&type=master&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?artist=Artist&type=release&artist=Other&page=1&per_page=20") &&
          allowed_readonly_path("/database/search?artist=Artist&type=release&page=2&per_page=20") &&
          !allowed_readonly_path("/database/search?artist=Artist&type=release&token=secret&page=1&per_page=20"),
          "artist-only request canonical, bounded and token-free");
    rejects([]{(void)artist_search_path("");},"empty artist rejected");
    rejects([]{(void)artist_search_path("bad\nHeader: stolen");},
            "CRLF artist rejected");
    rejects([]{(void)artist_search_path(std::string(151,'x'));},
            "oversized artist rejected");
    // Vertical paged candidate discovery: a user-initiated next page
    // cannot change filters or silently turn a search hit into an edition.
    const auto first=track_search_path("Via Balearia","Estiva");
    const auto second=search_result_page_path(first,2);
    check(second=="/database/search?track=Via%20Balearia&type=release&artist=Estiva&page=2&per_page=20" &&
          allowed_readonly_path(second) &&
          search_result_page_path(first,1)==first &&
          allowed_readonly_path(search_result_page_path(
              artist_search_path("Estiva"),3)),
          "next page preserves exact canonical filters and artist fallback");
    rejects([]{(void)search_result_page_path(
                "/database/search?track=X&type=release&token=x&page=1&per_page=20",2);},
            "pagination cannot launder unauthorized query keys");
    rejects([]{(void)search_result_page_path(
                "/database/search?q=X&type=master&page=2&per_page=20",3);},
            "pagination input must be the canonical first page");
    rejects([&]{(void)search_result_page_path(first,0);},
            "zero page is not a request");
    rejects([&]{(void)search_result_page_path(first,100001);},
            "unbounded bulk pagination is forbidden");
    check(!allowed_readonly_path(
              "/database/search?track=X&type=release&page=02&per_page=20") &&
          !allowed_readonly_path(
              "/database/search?track=X&type=release&page=100001&per_page=20") &&
          !allowed_readonly_path(
              "/database/search?track=X&type=release&page=2&per_page=100") &&
          !allowed_readonly_path(
              "/database/search?track=X&type=release&token=t&page=2&per_page=20"),
          "only a bounded canonical integer page may cross native WinHTTP");
    const auto response=std::string(R"DJ({
        "pagination":{"page":2,"per_page":20,"pages":4},
        "results":[{"type":"release","id":38012673,
                    "title":"Estiva - Via Balearia"}]
    })DJ");
    const auto search_page2=parse_search_result_page(response,2);
    check(search_page2.page==2 && search_page2.pages==4 &&
          search_page2.results.hits.size()==1 &&
          search_page2.results.hits[0].id==38012673,
          "exact release identities on page 2 retain the official API source");
    rejects([&]{(void)parse_search_result_page(response,3);},
            "stale or foreign page number never publishes results");
    rejects([]{(void)parse_search_result_page(
                R"({"results":[{"id":1,"title":"X","type":"release"}]})",1);},
            "missing official pagination cannot fabricate next-page navigation");
    rejects([]{(void)parse_search_result_page(
                R"({"pagination":{"page":1,"per_page":100,"pages":3},
                    "results":[]})",1);},
            "unexpected page size fails closed");
    check(parse_search_result_page(
              R"({"pagination":{"page":1,"per_page":20,"pages":0},"results":[]})",1)
                  .pages==1,
          "genuinely empty official search may have zero reported pages");
    check(master_detail_path(4343)=="/masters/4343" &&
          allowed_readonly_path(master_detail_path(4343)) &&
          !allowed_readonly_path("/masters/04343") &&
          !allowed_readonly_path("/masters/4343/") &&
          !allowed_readonly_path("/masters/4343?token=secret"),
          "only canonical official Discogs master detail is permitted");
    rejects([]{(void)master_detail_path(0);},
            "master detail path has a qualified positive identity");
    const auto year_master=parse_master_year(
        R"({"id":4343,"year":1998,"main_release":321})",4343);
    check(year_master && year_master->field=="DATE" &&
          year_master->values==std::vector<std::string>{"1998"} &&
          year_master->source_id=="master:4343" &&
          year_master->scope==EvidenceScope::Release &&
          year_master->date_meaning==DateMeaning::OriginalRelease,
          "Master year is source-qualified original-release evidence, not edition date");
    check(!parse_master_year(R"({"id":4343,"year":0})",4343) &&
          !parse_master_year(R"({"id":4343})",4343) &&
          !parse_master_year(R"({"id":4343,"year":20265})",4343),
          "unknown or malformed master year cannot fabricate a tag");
    rejects([]{(void)parse_master_year(R"({"id":4344,"year":1998})",4343);},
            "master response with mismatched ID fails closed");
    const auto linked_master=parse_release(
        R"({"id":321,"master_id":4343,"title":"Verified edition",
            "year":2024,"tracklist":[{"title":"Original Mix","duration":"4:00"}]})",
        321);
    check(linked_master.master_id=="4343" &&
          linked_master.fields[1].values==std::vector<std::string>{"2024"} &&
          linked_master.fields[1].date_meaning==DateMeaning::EditionRelease,
          "exact release links the separate master identity but retains its own year");
    const auto master_request=master_search_path("1000 Years");
    check(master_request==
              "/database/search?q=1000%20Years&type=master&page=1&per_page=20" &&
          allowed_readonly_path(master_request) &&
          !allowed_readonly_path(
              "/database/search?track=1000%20Years&type=master&page=1&per_page=20") &&
          !allowed_readonly_path(
              "/database/search?q=1000%20Years&type=master&artist=X&page=1&per_page=20"),
          "master discovery is canonical, read-only and navigation-only");
    rejects([]{(void)master_search_path("bad\nheader");},
            "master search rejects header injection");
    rejects([]{(void)search_path("song\r\nAuthorization: stolen");},
            "reject CRLF/header injection");
    rejects([]{(void)search_path("");},"reject empty title");
    rejects([]{(void)search_path(std::string(151,'x'));},
            "reject unbounded query");
    rejects([]{(void)release_path(0);},"no zero release ID");
    check(release_path(2442)=="/releases/2442", "release lookup is canonical");
    check(selected_exact_release_id("release:38012673",38012673) &&
          selected_exact_release_id("release:37332711",37332711) &&
          !selected_exact_release_id("release:38012673",37332711) &&
          !selected_exact_release_id("release:0",38012673) &&
          !selected_exact_release_id("release:038012673",38012673) &&
          !selected_exact_release_id("release:38012673/track/1",38012673) &&
          !selected_exact_release_id("master:38012673",38012673) &&
          !selected_exact_release_id("release:38012673\n",38012673) &&
          !selected_exact_release_id("release:9999999999999",38012673),
          "async Discogs release detail requires unchanged exact selected release");

    const auto edition_first=track_search_plan(
        "Home (Extended Mix)","DJ Example","004299999998","CAT 007/A");
    check(edition_first.size()==8 &&
          edition_first[0].path==
            edition_identifier_search_path("barcode","004299999998") &&
          edition_first[1].path==
            edition_identifier_search_path("catno","CAT 007/A") &&
          edition_first[2].path==search_path("Home (Extended Mix)","DJ Example") &&
          edition_first[0].label=="Exact edition barcode" &&
          edition_first[1].label=="Exact edition catalog number" &&
          edition_first[0].path.find("004299999998")!=std::string::npos &&
          edition_first[1].path.find("CAT%20007%2FA")!=std::string::npos,
          "official barcode and catalog filters precede bounded title fallback");
    check(track_search_plan("Home","DJ Example",{},"CAT 007/A").size()==6,
          "a single catalog number participates without any barcode guess");
    rejects([]{(void)edition_identifier_search_path("ISRC","USABC2601234");},
            "Discogs API does not support a fabricated ISRC search filter");
    rejects([]{(void)edition_identifier_search_path("barcode","x\r\nAuthorization: bad");},
            "barcode search forbids header injection");
    rejects([]{(void)edition_identifier_search_path("catno",std::string(151,'x'));},
            "catalog identifiers are bounded");

    const auto tagged_album=track_search_plan(
        "Only You (Extended Mix)","Matt Fax",{},{},"Contrast EP");
    check(tagged_album.size()==7 &&
          tagged_album[0].label=="Tagged album release title" &&
          tagged_album[0].path==release_title_search_path("Contrast EP","Matt Fax") &&
          tagged_album[1].path==search_path("Only You (Extended Mix)","Matt Fax") &&
          tagged_album[2].path==track_search_path("Only You (Extended Mix)","Matt Fax"),
          "independent physical ALBUM search precedes recording-title fallback");
    const auto cue_album=track_search_plan(
        "A Continuous Mix","DJ Example",{},{},"A Continuous Mix");
    check(cue_album[0].path==
              release_title_search_path("A Continuous Mix","DJ Example") &&
          cue_album[1].path==search_path("A Continuous Mix","DJ Example"),
          "CUE-global album uses official release_title filter even if title matches");
    const auto album_no_artist=track_search_plan(
        "Side A",{}, {}, {}, "Various Artists Collection");
    check(album_no_artist[0].path==
              release_title_search_path("Various Artists Collection") &&
          album_no_artist[1].path==search_path("Side A"),
          "artist-free album search remains distinct from track title");
    // Full native chain: an ALBUM-only physical source or an identifier-only
    // file can reach a real release search without a fabricated TITLE.
    const auto album_only=track_search_plan("", "Matt Fax", {}, {}, "Contrast EP");
    check(album_only.size()==2 &&
          album_only[0].path==release_title_search_path("Contrast EP","Matt Fax") &&
          album_only[1].path==artist_search_path("Matt Fax") &&
          allowed_readonly_path(album_only[0].path),
          "album-only discovery has separate broad artist fallback");
    const auto ids_only=track_search_plan("", {}, "004299999998", "CAT 007/A");
    check(ids_only.size()==2 &&
          ids_only[0].path==edition_identifier_search_path("barcode","004299999998") &&
          ids_only[1].path==edition_identifier_search_path("catno","CAT 007/A") &&
          allowed_readonly_path(ids_only[0].path) &&
          allowed_readonly_path(ids_only[1].path),
          "identifier-only source searches editions without empty track queries");
    const auto artist_only=track_search_plan("", "Estiva");
    check(artist_only.size()==1 &&
          artist_only[0].path==artist_search_path("Estiva") &&
          allowed_readonly_path(artist_only[0].path) &&
          artist_only[0].label.find("broad")!=std::string::npos,
          "artist-only discovery is explicitly unverified");
    rejects([]{(void)track_search_plan("", {}, {}, {}, "");},
            "no recording, album or identifier never produces a request");
    rejects([]{(void)track_search_plan("Valid",{}, {}, {}, "bad\nheader");},
            "malformed album hint cannot cross the native provider boundary");
    rejects([]{(void)release_title_search_path(std::string(151,'x'));},
            "album release title search is bounded");

    const djmeta::MetadataDocument credited_collaboration{{
        {"TITLE",{"1000 Years (DJ Version)"}},
        {"ARTIST",{"Kyau & Albert","Maywave"}},
        {"GENRE",{"Trance"}}
    }};
    const auto credited_hints=physical_search_hints(credited_collaboration);
    check(credited_hints.title=="1000 Years (DJ Version)" &&
          credited_hints.artist=="Kyau & Albert Maywave",
          "Discogs physical hint preserves all explicit multivalue collaborators");
    const auto credited_plan=track_search_plan(
        credited_hints.title,credited_hints.artist);
    check(credited_plan.front().path==
              search_path("1000 Years (DJ Version)","Kyau & Albert Maywave") &&
          std::any_of(credited_plan.begin(),credited_plan.end(),
              [](const auto& step) {
                  return step.path==track_search_path("1000 Years (DJ Version)");
              }),
          "Discogs collaborator query always retains a title-only fallback");
    const djmeta::MetadataDocument duplicate_artist{{
        {"TITLE",{"1000 Years"}},{"ARTIST",{"First"}},{"artist",{"Second"}}
    }};
    check(physical_search_hints(duplicate_artist).artist.empty() &&
          physical_search_hints(duplicate_artist).title=="1000 Years",
          "duplicate source ARTIST names remain ambiguous, not concatenated");
    const auto unchanged_collaboration=credited_collaboration;
    check(physical_search_hints(credited_collaboration).artist==
              credited_hints.artist &&
          credited_collaboration==unchanged_collaboration,
          "Discogs artist discovery cannot mutate original metadata");
    // End-to-end discovery for a common physical layout: TITLE holds the
    // base name, VERSION holds the mix. Search both exact mix and fallback
    // without ever treating a search hit as a verified recording/edition.
    check(collaboration_tail_search_artist("Kyau & Albert X Maywave")=="Maywave" &&
          collaboration_tail_search_artist("Kyau & Albert x Maywave")=="Maywave" &&
          collaboration_tail_search_artist("Artist feat. Guest")=="Guest" &&
          collaboration_tail_search_artist("Kyau & Albert").empty() &&
          collaboration_tail_search_artist("A x B vs C").empty() &&
          collaboration_tail_search_artist("A x ").empty(),
          "explicit collaborator-only search hint preserves raw joined artist identity");
    const auto versioned=track_search_plan(
        "1000 Years","Kyau & Albert x Maywave",{}, {}, {}, true,"DJ Version");
    check(versioned.size()==10 &&
          versioned[0].path==track_search_path(
              "1000 Years (DJ Version)","Kyau & Albert x Maywave") &&
          versioned[1].path==search_path(
              "1000 Years (DJ Version)","Kyau & Albert x Maywave") &&
          versioned[2].path==search_path(
              "1000 Years","Kyau & Albert x Maywave") &&
          versioned.back().path==master_search_path("1000 Years") &&
          std::all_of(versioned.begin(),versioned.end(),
              [](const auto& stage){return allowed_readonly_path(stage.path);}),
          "separate physical VERSION reaches authorized mix-aware track/release stages");
    const auto already_annotated=track_search_plan(
        "1000 Years (DJ Version)","Kyau & Albert x Maywave",
        {}, {}, {}, true,"DJ Version");
    check(already_annotated.size()==9 &&
          already_annotated[0].path==search_path(
              "1000 Years (DJ Version)","Kyau & Albert x Maywave"),
          "an already annotated title must not duplicate its mix suffix");
    rejects([]{(void)track_search_plan(
        "Home","Artist",{}, {}, {}, false,std::string("Bad")+char(10)+"Authorization");},
        "unsafe VERSION is rejected before any provider request");
    const auto oversized_version=track_search_plan(
        std::string(145,'x'),"Artist",{}, {}, {}, false,"Extended Mix");
    check(oversized_version[0].path==search_path(std::string(145,'x'),"Artist"),
          "overlong combined mix title skips only the optional mix stage");

    const auto master_staged=track_search_plan(
        "1000 Years (DJ Version)", "Kyau & Albert x Maywave",
        {}, {}, {}, true);
    check(master_staged.size()==9 &&
          master_staged.back().path==master_search_path("1000 Years") &&
          master_staged.back().label.find("navigation only")!=std::string::npos,
          "native opt-in search adds one bounded master navigation stage");
    const auto staged=track_search_plan(
        "1000 Years (DJ Version)", "Kyau & Albert x Maywave");
    check(staged.size()==8 &&
          staged[0].path==search_path("1000 Years (DJ Version)",
                                     "Kyau & Albert x Maywave") &&
          staged[1].path.find("track=1000%20Years%20%28DJ%20Version%29")!=
              std::string::npos &&
          staged[2].path==track_search_path("1000 Years","Maywave") &&
          staged[3].path==search_path("1000 Years","Maywave") &&
          staged[4].path==track_search_path("1000 Years (DJ Version)") &&
          staged[5].path==track_search_path("1000 Years") &&
          staged[6].path==search_path("1000 Years") &&
          staged[7].path==artist_search_path("Kyau & Albert x Maywave") &&
          staged[0].label!=staged[2].label &&
          std::all_of(staged.begin(),staged.end(),
              [](const auto& stage){return allowed_readonly_path(stage.path);}),
          "tiered Discogs queries broaden artist, track and version constraints");
    check(next_search_attempt("old","new",3,5)==0 &&
          next_search_attempt("same","same",0,5)==1 &&
          next_search_attempt("same","same",3,5)==4 &&
          next_search_attempt("same","same",4,5)==0,
          "completed Discogs searches advance even with hits and wrap safely");
    rejects([]{(void)next_search_attempt("x","x",0,0);},
            "empty Discogs stage plan is not executable");
    rejects([]{(void)next_search_attempt("x","x",7,5);},
            "stale stage index is never silently reused");
    // Same safe discovery-only suffix interpretation across native
    // Discogs and MusicBrainz. Full title must remain in exact attempts.
    for(const std::string& full : {
            std::string("Via Balearia - Extended Mix"),
            std::string("Via Balearia \xE2\x80\x93 Radio Edit"),
            std::string("Via Balearia \xE2\x80\x94 Original Version")}) {
        const auto plan=track_search_plan(full,"Estiva");
        check(plan.size()==7 &&
              plan[0].path==search_path(full,"Estiva") &&
              plan[1].path==track_search_path(full,"Estiva") &&
              plan[2].path==track_search_path("Via Balearia","Estiva") &&
              plan[3].path==track_search_path(full) &&
              plan[4].path==track_search_path("Via Balearia") &&
              plan[5].path==search_path("Via Balearia") &&
              plan[6].path==artist_search_path("Estiva") &&
              std::all_of(plan.begin(),plan.end(),
                  [](const auto& step){return allowed_readonly_path(step.path);}),
              "Discogs keeps exact original title and searches qualified DJ base");
    }
    const auto unknown_dash=track_search_plan(
        "Only You - Live at the Arena","Matt Fax");
    check(unknown_dash.size()==5 &&
          unknown_dash[0].path==search_path(
              "Only You - Live at the Arena","Matt Fax") &&
          std::none_of(unknown_dash.begin(),unknown_dash.end(),
              [](const auto& stage){
                  return stage.path==track_search_path("Only You","Matt Fax");
              }),
          "unknown dash segments are not invented as mix qualifiers");

    const auto simple_plan=track_search_plan("Via Balearia","Estiva");
    check(simple_plan.size()==5 &&
          simple_plan[2].path==track_search_path("Via Balearia") &&
          simple_plan[3].path==search_path("Via Balearia") &&
          simple_plan[4].path==artist_search_path("Estiva"),
          "fallback supports a track whose release has a different title");
    const auto no_artist_plan=track_search_plan("Only You");
    check(no_artist_plan.size()==2 &&
          no_artist_plan[0].path==search_path("Only You") &&
          no_artist_plan[1].path==track_search_path("Only You"),
          "artist-free lookup stays bounded and does not repeat same attempt");
    check(release_id_from_url(
              "https://www.discogs.com/release/38012673-Estiva-Via-Balearia")
              ==38012673 &&
          release_id_from_url(
              "https://discogs.com/release/37332711?utm_source=example")
              ==37332711 &&
          release_id_from_url(
              "https://www.discogs.com/releases/36397609") ==36397609 &&
          release_path(release_id_from_url(
              "https://www.discogs.com/release/36819730-Matt-Fax")) ==
              "/releases/36819730",
          "known missing Discogger examples resolve to direct fixed release paths");
    check(release_id_from_url("http://www.discogs.com/release/38012673")==0 &&
          release_id_from_url(
              "https://www.discogs.com.evil.test/release/38012673")==0 &&
          release_id_from_url(
              "https://evil.test/www.discogs.com/release/38012673")==0 &&
          release_id_from_url("https://www.discogs.com/release/0")==0 &&
          release_id_from_url(
              "https://www.discogs.com/release/99999999999999")==0 &&
          release_id_from_url(
              "https://www.discogs.com/release/38012673\r\nbad")==0,
          "direct release URL parser rejects spoofing, insecure origins and invalid IDs");
    rejects([]{(void)track_search_path("track\nbad");},
            "track filter rejects control/header injection");
    check(track_search_plan("", "Artist").size()==1,
          "artist-only source can discover release candidates without track title");

    check(allowed_readonly_path(search_path("Via Balearia","Estiva")) &&
          allowed_readonly_path(track_search_path("1000 Years")) &&
          allowed_readonly_path(track_search_path("Only You","Matt Fax")) &&
          allowed_readonly_path(release_title_search_path("Contrast EP","Matt Fax")) &&
          allowed_readonly_path(release_title_search_path("A Continuous Mix")) &&
          allowed_readonly_path(release_path(38012673)) &&
          allowed_readonly_path(edition_identifier_search_path(
              "barcode","004299999998")) &&
          allowed_readonly_path(edition_identifier_search_path(
              "catno","CAT 007/A")),
          "only canonical authenticated Discogs API paths cross native HTTP");
    check(!allowed_readonly_path("https://api.discogs.com/releases/123") &&
          !allowed_readonly_path("/users/example/collection") &&
          allowed_readonly_path("/database/search?q=X&type=master&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?q=Hi%0D%0ABad&type=release&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?q=Hi&type=release&token=secret&page=1&per_page=20") &&
          allowed_readonly_path("/database/search?q=Hi&type=release&page=2&per_page=20") &&
          !allowed_readonly_path("/database/search?track=Hi&type=release&page=1&per_page=200") &&
          !allowed_readonly_path("/database/search?barcode=123&type=release&artist=Any&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?catno=XYZ&type=release&token=secret&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?isrc=USABC2601234&type=release&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?release_title=EP&type=release&token=secret&page=1&per_page=20") &&
          allowed_readonly_path("/database/search?release_title=EP&type=release&artist=Any&page=2&per_page=20") &&
          !allowed_readonly_path("/database/search?release_title=EP%0D&type=release&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?barcode=%30%30&type=release&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?barcode=0%0A&type=release&page=1&per_page=20") &&
          !allowed_readonly_path("/releases/0") &&
          !allowed_readonly_path("/releases/42/notes") &&
          !allowed_readonly_path("/releases/042"),
          "native Discogs host permits canonical bounded pages but rejects foreign endpoints, tokens and malformed requests");
    // One native search action can turn a canonical master URL into concrete
    // release candidates, but never turn its master ID into a tag identity.
    check(master_id_from_url(
              "https://www.discogs.com/master/782-The-KLF")==782 &&
          master_id_from_url("https://discogs.com/masters/782")==782 &&
          master_id_from_url("https://www.discogs.com/release/782")==0 &&
          master_id_from_url("https://www.discogs.com.evil/master/782")==0 &&
          master_id_from_url("http://www.discogs.com/master/782")==0 &&
          master_versions_path(782)=="/masters/782/versions?page=1&per_page=20" &&
          allowed_readonly_path(master_versions_path(782)) &&
          allowed_readonly_path("/masters/782/versions?page=2&per_page=20") &&
          allowed_readonly_path(master_versions_path(782,2)) &&
          !allowed_readonly_path("/masters/782/versions?page=0&per_page=20") &&
          !allowed_readonly_path("/masters/782/versions?page=0002&per_page=20") &&
          !allowed_readonly_path("/masters/782/versions?page=100001&per_page=20") &&
          !allowed_readonly_path("/masters/782/versions?page=1&per_page=200") &&
          !allowed_readonly_path("/masters/0782/versions?page=1&per_page=20") &&
          !allowed_readonly_path("/masters/782/versions?token=abc&page=1&per_page=20"),
          "canonical master URL only permits official, bounded edition discovery");
    const auto versions=parse_master_versions(R"DJ({
       "pagination":{"page":1,"per_page":20,"pages":3,"items":48},
       "versions":[
         {"id":38012673,"title":"Via Balearia","country":"Germany",
          "released":"2026-10-02","label":"Label A","catno":"00-123",
          "format":"File, MP3"},
         {"id":37332711,"title":"1000 Years","released":"2026"},
         {"id":38012673,"title":"duplicate"},
         {"id":0,"title":"invalid"}]
    })DJ",782);
    check(versions.hits.size()==2 &&
          versions.hits[0].kind==EntityKind::Release &&
          versions.hits[0].id==38012673 &&
          versions.hits[0].year=="2026" &&
          versions.hits[0].label=="Label A" &&
          versions.hits[0].catalog_number=="00-123" &&
          versions.hits[0].format=="File, MP3" &&
          versions.hits[0].canonical_url==
              "https://www.discogs.com/release/38012673" &&
          selected_exact_release_id("release:38012673",versions.hits[0].id) &&
          !selected_exact_release_id("master:782",versions.hits[0].id),
          "master versions yield distinct concrete releases and edition provenance");
    const auto page2=parse_master_versions_page(R"({
        "pagination":{"page":2,"per_page":20,"pages":3},
        "versions":[{"id":36397609,"title":"Only You","released":"2026"}]
    })",782,2);
    check(page2.page==2 && page2.pages==3 &&
          page2.results.hits.size()==1 &&
          page2.results.hits[0].id==36397609,
          "second page can show concrete editions absent on page one");
    rejects([]{(void)parse_master_versions_page(
        R"({"pagination":{"page":1,"per_page":20,"pages":3},"versions":[]})",782,2);},
        "unexpected/stale reply page fails closed");
    rejects([]{(void)parse_master_versions_page(
        R"({"pagination":{"page":4,"per_page":20,"pages":3},"versions":[]})",782,4);},
        "page beyond provider total fails closed");
    rejects([]{(void)parse_master_versions(
        R"({"pagination":{"page":2,"per_page":20,"pages":3},"versions":[]})",782);},
        "first page adapter still refuses second page");
    rejects([]{(void)master_versions_path(782,100001);},
        "page number above safety cap fails closed");
    rejects([]{(void)parse_master_versions(
        R"({"pagination":{"page":1,"per_page":200,"pages":3},"versions":[]})",782);},
        "unbounded master page refused");
    rejects([]{(void)parse_master_versions(R"({"versions":[]})",782);},
        "missing pagination refused");
    const auto hits=parse_search(R"DJ({
        "pagination":{"page":1,"pages":200,"items":99999},
        "results":[
            {"type":"release","id":124,"title":"The KLF - What Time Is Love?",
             "year":1990,"country":"UK","genre":["Electronic","Electronic"],
             "style":["House","Ambient"],"uri":"https://evil.example/bad"},
            {"type":"master","id":125,"title":"The KLF - What Time Is Love"},
            {"type":"artist","id":126,"title":"Artist is not a release"},
            {"type":"release","id":-1,"title":"Bad id"},
            {"type":"release","id":127,"title":""}
        ]
    })DJ");
    check(hits.hits.size()==2 &&
          hits.hits[0].kind==EntityKind::Release &&
          hits.hits[0].id==124 &&
          hits.hits[0].year=="1990" &&
          hits.hits[0].genres.size()==1 &&
          hits.hits[0].styles.size()==2 &&
          hits.hits[0].canonical_url=="https://www.discogs.com/release/124" &&
          hits.hits[1].kind==EntityKind::Master &&
          hits.hits[1].canonical_url=="https://www.discogs.com/master/125",
          "separate master/release IDs, dedupe tag vectors, never trust remote URL");
    const auto duplicate_hits=parse_search(R"DJ({"results":[
        {"type":"release","id":44,"title":"First edition"},
        {"type":"release","id":44,"title":"Duplicate edition"},
        {"type":"master","id":44,"title":"Same number, different type"},
        {"type":"release","id":45,"title":"Second edition"},
        {"type":"master","id":44,"title":"Repeated master"}]})DJ");
    check(duplicate_hits.hits.size()==3 &&
          duplicate_hits.hits[0].title=="First edition" &&
          duplicate_hits.hits[1].kind==EntityKind::Master &&
          duplicate_hits.hits[1].id==44 &&
          duplicate_hits.hits[2].id==45,
          "dedupe exact Discogs identities without merging master and release");
    rejects([]{(void)parse_search(R"({"results":{}})");},
            "fail closed on malformed result type");
    rejects([]{(void)parse_search(std::string(2*1024*1024+1,'x'));},
            "never parse unbounded remote payload");

    const std::string sample=R"DJ({
        "id":321, "title":"Sample EP",
        "year":2003,
        "artists":[{"name":"Primary Artist"}],
        "genres":["Electronic","Electronic","Dance"],
        "styles":["Breakbeat","House"],
        "labels":[{"name":"Example Label","catno":"ABC 001"},
                  {"name":"Example Label","catno":"ABC 002"}],
        "tracklist":[
          {"position":"A1","title":"Main Track (Club Mix)","duration":"4:02"},
          {"position":"2","title":"Other Track","artists":[{"name":"Guest"}]},
          {"type_":"heading","title":"Bonus"},
          {"type_":"index","position":"B","title":"Medley",
           "sub_tracks":[{"position":"B1","title":"Part 1"}]},
          {"type_":"video","title":"Not an audio track"},
          {"position":"1-03","title":"Complex position"}
        ]
    })DJ";
    const auto edition=parse_release(sample,321);
    check(edition.provider=="discogs" && edition.edition_id=="321" &&
          edition.tracks.size()==4 &&
          edition.tracks[0].source_track_id=="A1" &&
          edition.tracks[0].track_number==-1 &&
          edition.tracks[0].recording.duration_ms==242000 &&
          edition.tracks[0].recording.primary_artist=="Primary Artist" &&
          edition.tracks[1].track_number==2 &&
          edition.tracks[1].recording.primary_artist=="Guest" &&
          edition.tracks[2].source_track_id=="B1" &&
          edition.tracks[2].recording.title=="Part 1" &&
          edition.tracks[2].track_number==-1 &&
          edition.tracks[3].source_track_id=="1-03" &&
          edition.tracks[3].track_number==-1,
          "preserve side/position and direct artist provenance without inventing track numbers");
    const auto track_field=[](const EditionTrack& track,
                              std::string_view name)->const FieldEvidence* {
        for(const auto& field:track.fields)
            if(field.field==name) return &field;
        return nullptr;
    };
    const auto* title0=track_field(edition.tracks[0],"TITLE");
    const auto* title1=track_field(edition.tracks[1],"TITLE");
    const auto* artist1=track_field(edition.tracks[1],"ARTIST");
    const auto* number1=track_field(edition.tracks[1],"TRACKNUMBER");
    check(title0 && title0->values==std::vector<std::string>{"Main Track (Club Mix)"} &&
          title0->provider=="discogs" &&
          title0->source_id=="321/track/1" &&
          title0->scope==EvidenceScope::Edition &&
          title0->date_meaning==DateMeaning::NotDate &&
          !track_field(edition.tracks[0],"ARTIST") &&
          !track_field(edition.tracks[0],"TRACKNUMBER") &&
          title1 && title1->source_id=="321/track/2" &&
          artist1 && artist1->values==std::vector<std::string>{"Guest"} &&
          artist1->source_id==title1->source_id &&
          number1 && number1->values==std::vector<std::string>{"2"} &&
          !track_field(edition.tracks[2],"TRACKNUMBER"),
          "edition row provenance covers explicit title/artist/numeric position only");
    const auto credited=parse_release(R"DJ({
        "id":881,"title":"Credits EP",
        "extraartists":[{"name":"Release Only","role":"Remix"}],
        "tracklist":[
          {"position":"1","title":"Night (Club Remix)",
           "extraartists":[
             {"name":"DJ One","role":"Remix"},
             {"name":"DJ Two","role":"Remix By"},
             {"name":"DJ One","role":"Remix"},
             {"name":"Writer A","role":"Written-By"},
             {"name":"Writer B","role":"Composed By"},
             {"name":"Producer","role":"Producer"},
             {"name":"Vague","role":"Remix, Producer"},
             {"name":"Bad\nCredit","role":"Remix"}]},
          {"position":"2","title":"Day (Original Mix)"}
        ]
    })DJ",881);
    const auto* remixers=track_field(credited.tracks[0],"REMIXER");
    const auto* writers=track_field(credited.tracks[0],"SONGWRITER");
    check(remixers && remixers->values==
              std::vector<std::string>({"DJ One","DJ Two"}) &&
          writers && writers->values==
              std::vector<std::string>({"Writer A","Writer B"}) &&
          remixers->provider=="discogs" &&
          remixers->scope==EvidenceScope::Edition &&
          remixers->source_id=="881/track/1" &&
          writers->source_id==remixers->source_id,
          "exact track credit roles retain independent multivalues and edition-row provenance");
    check(!track_field(credited.tracks[1],"REMIXER") &&
          !track_field(credited.tracks[1],"SONGWRITER") &&
          credited.fields.size()==1,
          "do not infer remix credits from titles or leak release-wide credits to tracks");
    const auto malformed_credits=parse_release(R"DJ({
        "id":882,"title":"Untrusted credits",
        "tracklist":[{"title":"Track",
          "extraartists":[{"name":"Bad\nName","role":"Remix"},
                          {"name":"Unknown","role":"Mastering"},
                          {"name":"Good","role":"Remix"}]}]
    })DJ",882);
    const auto* qualified=track_field(malformed_credits.tracks[0],"REMIXER");
    check(qualified && qualified->values==std::vector<std::string>{"Good"},
          "invalid or unrelated credits never become remixer evidence");
    auto by_name=[&](std::string_view field) -> const FieldEvidence* {
        for(const auto& value:edition.fields)
            if(value.field==field) return &value;
        return nullptr;
    };
    const auto* genres=by_name("GENRE");
    const auto* styles=by_name("STYLE");
    const auto* labels=by_name("LABEL");
    const auto* catalogs=by_name("CATALOGNUMBER");
    const auto* year=by_name("DATE");
    check(genres && genres->values.size()==2 &&
          styles && styles->values.size()==2 &&
          labels && labels->values.size()==1 &&
          catalogs && catalogs->values.size()==2 &&
          year && year->values.size()==1 && year->values[0]=="2003" &&
          year->date_meaning==DateMeaning::EditionRelease,
          "multi-value genre/style/catalog and edition year are lossless, never first-recording date");
    const auto detailed=parse_release(R"DJ({
        "id":901,"title":"Edition details","year":2024,
        "released":"2024-02-29","country":"DE",
        "identifiers":[{"type":"Barcode","value":"001234567890"},
                       {"type":"Barcode","value":"001234567890"},
                       {"type":"Barcode","value":" 123 456 "},
                       {"type":"Matrix / Runout","value":"not a barcode"},
                       {"type":"Barcode","value":"Bad\nCode"}],
        "tracklist":[{"position":"1","title":"Track"}]
    })DJ",901);
    const auto edition_field=[](const ReleaseEdition& release,
                                std::string_view name)->const FieldEvidence* {
        for(const auto& field:release.fields)
            if(field.field==name) return &field;
        return nullptr;
    };
    const auto* released=edition_field(detailed,"RELEASEDATE");
    const auto* country=edition_field(detailed,"COUNTRY");
    const auto* barcodes=edition_field(detailed,"BARCODE");
    check(released && released->values==std::vector<std::string>{"2024-02-29"} &&
          released->date_meaning==DateMeaning::EditionRelease &&
          released->source_id=="901" &&
          released->scope==EvidenceScope::Edition &&
          country && country->values==std::vector<std::string>{"DE"} &&
          barcodes && barcodes->values==
              std::vector<std::string>({"001234567890"," 123 456 "}) &&
          !track_field(detailed.tracks[0],"BARCODE") &&
          !track_field(detailed.tracks[0],"COUNTRY"),
          "edition date, country and multivalue barcodes preserve provenance");
    for(const auto invalid:{"2023-02-29","2024-04-31","2024-00-12",
                            "2024-13-01","2024-02-00","2024-02-30",
                            "2024-2-29","2024-02","2024-02-29T00:00:00",
                            "2024-02-2X"}) {
        auto payload=std::string(R"DJ({"id":902,"title":"Invalid date","released":"DATE_VALUE","tracklist":[{"title":"Track"}]})DJ");
        payload.replace(payload.find("DATE_VALUE"),10,invalid);
        const auto parsed=parse_release(payload,902);
        check(!edition_field(parsed,"RELEASEDATE"),
              "invalid or partial release date cannot become evidence");
    }
    const auto mismatch=parse_release(R"DJ({
        "id":903,"title":"Conflicting year","year":2022,
        "released":"2024-01-01","country":"Bad\nCountry",
        "identifiers":[{"type":"Barcode","value":"Bad\nCode"}],
        "tracklist":[{"title":"Track"}]
    })DJ",903);
    check(!edition_field(mismatch,"RELEASEDATE") &&
          !edition_field(mismatch,"COUNTRY") &&
          !edition_field(mismatch,"BARCODE") &&
          edition_field(mismatch,"DATE"),
          "conflicting year and control characters cannot supply edition fields");
    check(duration_ms("4:02")==242000 &&
          duration_ms("1:02:03")==3723000 &&
          duration_ms("4:2")==-1 && duration_ms("4:60")==-1 &&
          duration_ms("999:99")==-1 && duration_ms("0:00")==-1 &&
          duration_ms("1:02:03:04")==-1 && duration_ms("")==-1,
          "strict Discogs display duration is bounded, never a CUE index");
    const auto collaboration=parse_release(R"({
        "id":777,"title":"Together","artists":[
            {"name":"Artist A","join":" & "},{"name":"Artist B","join":""}],
        "tracklist":[{"type_":"track","position":"1","title":"Shared",
            "duration":"1:02:03"}]
    })",777);
    const auto album_credit=[](const ReleaseEdition& release) {
        for(const auto& field:release.fields)
            if(field.field=="ALBUM ARTIST") return field.values;
        return std::vector<std::string>{};
    };
    check(album_credit(collaboration)==std::vector<std::string>{"Artist A & Artist B"},
          "Discogs joined album artist is a single provenance-preserving tag");
    check(collaboration.tracks.size()==1 &&
          collaboration.tracks[0].recording.primary_artist=="Artist A & Artist B" &&
          collaboration.tracks[0].recording.duration_ms==3723000 &&
          !track_field(collaboration.tracks[0],"ARTIST") &&
          collaboration.fields.size()>=2,
          "explicit multi-artist joins retained without invented separator");
    // A sampler's "Various" edition credit cannot qualify an uncredited
    // individual track. Explicit sibling track credits are still usable.
    for(const char* compilation_credit:{"Various","Various Artists",
                                        "Various (2)"}) {
        const auto json=std::string(R"({
            "id":783,"title":"Sampler","artists":[{"name":")")+
            compilation_credit+R"("}],"tracklist":[
                {"position":"1","title":"Unknown Performer"},
                {"position":"2","title":"Known Performer",
                 "artists":[{"name":"Actual DJ"}]}
            ]
        })";
        const auto sampler=parse_release(json,783);
        check(sampler.tracks.size()==2 &&
              sampler.tracks[0].recording.primary_artist.empty() &&
              !track_field(sampler.tracks[0],"ARTIST") &&
              sampler.tracks[1].recording.primary_artist=="Actual DJ" &&
              track_field(sampler.tracks[1],"ARTIST") &&
              album_credit(sampler)==
                  std::vector<std::string>{compilation_credit},
              "compilation album credit never becomes missing track ARTIST");
        LocalReleaseTrack misleading_local;
        misleading_local.source_index=44;
        misleading_local.recording.title="Unknown Performer";
        misleading_local.recording.primary_artist=compilation_credit;
        misleading_local.track_number=1;
        const auto unsafe_alignment=align_release({misleading_local},sampler);
        check(unsafe_alignment.matched==0 &&
              unsafe_alignment.unmatched==1 &&
              unsafe_alignment.tracks[0].edition_track_index==no_release_track,
              "an uncredited sampler row cannot match on album placeholder");
    }
    const auto trailing_join=parse_release(R"({
        "id":779,"title":"Trailing bad join","artists":[
            {"name":"Artist A","join":" & "},{"name":"Artist B","join":" x "}],
        "tracklist":[{"type_":"track","position":"1","title":"Shared"}]
    })",779);
    bool has_album_artist=false;
    for(const auto& field:trailing_join.fields)
        if(field.field=="ALBUM ARTIST") has_album_artist=true;
    check(!has_album_artist &&
          trailing_join.tracks[0].recording.primary_artist.empty(),
          "malformed last Discogs artist join must not fabricate a joint credit");

    const auto ambiguous_artist=parse_release(R"({
        "id":778,"title":"Unknown credit","artists":[
            {"name":"Artist A"},{"name":"Artist B"}],
        "tracklist":[{"type_":"track","position":"1","title":"Shared"}]
    })",778);
    check(album_credit(ambiguous_artist).empty(),
          "ambiguous multi-artist album credits must not become separate album artist tags");
    check(ambiguous_artist.tracks[0].recording.primary_artist.empty() &&
          !track_field(ambiguous_artist.tracks[0],"ARTIST"),
          "unqualified collaboration does not inherit a guessed artist");
    const auto ambiguous_track=parse_release(R"({
        "id":780,"title":"Ambiguous track credit",
        "tracklist":[{"type_":"track","position":"1","title":"Duet",
          "artists":[{"name":"A"},{"name":"B"}]}]
    })",780);
    check(ambiguous_track.tracks.size()==1 &&
          !track_field(ambiguous_track.tracks[0],"ARTIST") &&
          track_field(ambiguous_track.tracks[0],"TITLE"),
          "ambiguous explicit track credits are not silently joined");
    const auto medley=parse_release(R"({
        "id":2,"title":"Medley only","tracklist":[
            {"type_":"index","title":"Medley",
             "sub_tracks":[{"type_":"track","position":"B1","title":"Subtrack"}]},
            {"type_":"heading","title":"Side A"}]
    })",2);
    check(medley.tracks.size()==1 &&
          medley.tracks[0].recording.title=="Subtrack" &&
          medley.tracks[0].source_track_id=="B1" &&
          track_field(medley.tracks[0],"TITLE") &&
          track_field(medley.tracks[0],"TITLE")->source_id=="2/track/1",
          "indexed subtracks are independent read-only edition evidence");
    rejects([]{(void)parse_release(R"({
        "id":3,"title":"Partial medley","tracklist":[
            {"type_":"track","position":"1","title":"Valid"},
            {"type_":"index","title":"Broken medley",
             "sub_tracks":[{"type_":"track","position":"2"}]}]
    })",3);}, "malformed child cannot silently yield a partial edition");
    rejects([]{(void)parse_release(R"({
        "id":4,"title":"Missing index children","tracklist":[
            {"type_":"track","position":"1","title":"Valid"},
            {"type_":"index","title":"Medley"}]
    })",4);}, "incomplete index blocks the whole edition");
    rejects([]{(void)parse_release(R"({
        "id":5,"title":"Unknown audio kind","tracklist":[
            {"type_":"track","position":"1","title":"Valid"},
            {"type_":"unrecognized","title":"Unmapped"}]
    })",5);}, "unknown playable kinds cannot disappear from track review");
    const auto unsafe_hits=parse_search(R"({"results":[
        {"type":"release","id":5,"title":"Unsafe\nName"},
        {"type":"release","id":6,"title":"Valid","artist":"Bad\nArtist",
         "genre":["Good","Bad\nGenre"]}]})");
    check(unsafe_hits.hits.size()==1 &&
          unsafe_hits.hits[0].id==6 &&
          unsafe_hits.hits[0].artist.empty() &&
          unsafe_hits.hits[0].genres.size()==1 &&
          unsafe_hits.hits[0].genres[0]=="Good",
          "remote control escapes cannot enter catalog labels or tags");
    rejects([&]{(void)parse_release(sample,322);},
            "reject substituted release identity before comparison or write approval");
    rejects([]{(void)parse_release(R"({"id":1,"title":"No tracklist"})",1);},
            "do not pretend missing release tracklist is a match");
    check(std::string(attribution).find("not affiliated")!=std::string::npos &&
          data_attribution=="Data provided by Discogs.",
          "required Discogs provider-attribution strings are present");
    std::cout<<"PASS: strictly authenticated Discogs read-only URL, result, release, provenance and safety contracts\n";
}
