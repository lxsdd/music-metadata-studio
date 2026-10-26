#include "djmeta/musicbrainz_provider.h"
#include "djmeta/musicbrainz_search_review.h"
#include "djmeta/musicbrainz_physical_review.h"
#include "djmeta/musicbrainz_batch_review.h"
#include "djmeta/physical_album_batch_scope.h"
#include "djmeta/cue_online_bridge.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool yes, const char* msg) {
    if (!yes) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
template<typename Function> void rejects(Function fn,const char* why) {
    bool rejected=false;
    try { fn(); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,why);
}
constexpr const char* first="11111111-1111-1111-1111-111111111111";
constexpr const char* second="22222222-2222-2222-2222-222222222222";
}
int main() {
    using namespace djmeta;
    using namespace djmeta::online;
    using namespace djmeta::online::musicbrainz;
    check(explicit_physical_album_term("ALBUM:  Via Balearia  ")==
              std::optional<std::string>{"Via Balearia"} &&
          !explicit_physical_album_term("Via Balearia") &&
          !explicit_physical_album_term("album-title") &&
          !explicit_physical_album_term("recording: 1000 Years"),
          "album prefix alone selects the explicit physical release search");
    rejects([] { (void)explicit_physical_album_term("Album:  "); },
            "empty physical album search fails before any official request");
    check(request_spacing_wait_ms(0,100)==0 &&
          request_spacing_wait_ms(100,100)==1200 &&
          request_spacing_wait_ms(100,101)==1199 &&
          request_spacing_wait_ms(100,1299)==1 &&
          request_spacing_wait_ms(100,1300)==0 &&
          request_spacing_wait_ms(100,1600)==0 &&
          request_spacing_wait_ms(500,200)==1200,
          "MusicBrainz worker reserves a bounded, cancelable request slot");
    check(next_clicked_search_attempt(0,4,true)==1 &&
          next_clicked_search_attempt(1,4,true)==2 &&
          next_clicked_search_attempt(3,4,true)==0 &&
          next_clicked_search_attempt(0,4,false)==0 &&
          next_clicked_search_attempt(0,0,true)==0 &&
          next_clicked_search_attempt(0,9,true)==0,
          "explicit Search click advances on irrelevant hits, cycles after final stage");
    check(continue_empty_search_plan(0,4,true,true) &&
          continue_empty_search_plan(2,4,true,true) &&
          !continue_empty_search_plan(3,4,true,true) &&
          !continue_empty_search_plan(0,4,false,true) &&
          !continue_empty_search_plan(0,4,true,false) &&
          !continue_empty_search_plan(0,0,true,true) &&
          !continue_empty_search_plan(0,9,true,true) &&
          !continue_empty_search_plan(4,4,true,true),
          "a clicked MusicBrainz search can continue only on empty bounded qualified stages");
    const auto request=make_search_path(SearchKind::Recording,
        "Björk: It's \"Alive\" & 東京", "Artist / Guest");
    check(request.starts_with("/ws/2/recording?query=") &&
          request.find("fmt=json&limit=25") != std::string::npos &&
          request.find("Björk") == std::string::npos &&
          request.find("%22") != std::string::npos &&
          request.find("%26") != std::string::npos &&
          request.find("%E6%9D%B1%E4%BA%AC") != std::string::npos,
          "recording search encoded, Lucene quoted, bounded and UTF-8");
    // Browser URL intake is pure, fixed-host, explicit and read-only.
    const auto direct_recording=parse_exact_entity_url(
        std::string("  HTTPS://WWW.MUSICBRAINZ.ORG/recording/")+first+"  ");
    const auto direct_release=parse_exact_entity_url(
        std::string("https://musicbrainz.org/release/")+second+"/");
    check(direct_recording && direct_recording->kind==SearchKind::Recording &&
          direct_recording->mbid==first &&
          direct_release && direct_release->kind==SearchKind::Release &&
          direct_release->mbid==second &&
          !parse_exact_entity_url("Björk - Original Version") &&
          !parse_exact_entity_url("Album: Release 2026"),
          "exact MusicBrainz browser URLs select only explicit recording/release IDs");
    const auto exact_recording_path=make_exact_recording_search_path(first);
    check(exact_recording_path.starts_with("/ws/2/recording?query=") &&
          exact_recording_path.find("rid%3A"+std::string(first))!=std::string::npos &&
          qualified_musicbrainz_request_path(exact_recording_path) &&
          qualified_musicbrainz_request_path(
              make_release_lookup_path(direct_release->mbid)),
          "exact recording and release URLs rebuild official fixed-host routes");
    for(const auto& bad : {
        "http://musicbrainz.org/recording/11111111-1111-1111-1111-111111111111",
        "https://evil.example/recording/11111111-1111-1111-1111-111111111111",
        "https://musicbrainz.org.evil.example/release/11111111-1111-1111-1111-111111111111",
        "https://user@musicbrainz.org/release/11111111-1111-1111-1111-111111111111",
        "https://musicbrainz.org:443/release/11111111-1111-1111-1111-111111111111",
        "https://musicbrainz.org/release-group/11111111-1111-1111-1111-111111111111",
        "https://musicbrainz.org/track/11111111-1111-1111-1111-111111111111",
        "https://musicbrainz.org/release/11111111-1111-1111-1111-111111111111?inc=evil",
        "https://musicbrainz.org/release/11111111-1111-1111-1111-111111111111#fragment",
        "https://musicbrainz.org/release/11111111-1111-1111-1111-111111111111/extra",
        "https://musicbrainz.org/recording/not-an-mbid",
        "https://musicbrainz.org/release/%31%31%31%31",
        "https://musicbrainz.org\\release\\11111111-1111-1111-1111-111111111111",
        "musicbrainz.org/release/11111111-1111-1111-1111-111111111111"
    }) rejects([&] { (void)parse_exact_entity_url(bad); },
        "invalid MusicBrainz URL must not fall back to arbitrary search");
    rejects([&] { (void)make_exact_recording_search_path("not-a-uuid"); },
        "invalid recording ID cannot reach official HTTPS transport");
    // Execute the REAL native WinHTTP path allowlist in ordinary CTest.
    // Previously the label-bearing release lookup generated by the provider
    // was rejected by a separate native predicate before the HTTP request.
    check(qualified_musicbrainz_request_path(
              make_release_lookup_path(first)) &&
          qualified_musicbrainz_request_path(request) &&
          qualified_musicbrainz_request_path(
              make_search_path(SearchKind::Release,"Album","Artist")) &&
          qualified_musicbrainz_request_path(
              recording_search_plan("Known Song","Artist","USRC17607839")
                  .attempts.front().path),
          "official release/recording/ISRC producer paths pass native allowlist");
    const std::string official_release=make_release_lookup_path(first);
    for(const std::string& malicious:{
        std::string{}, std::string("https://musicbrainz.org")+official_release,
        std::string("//musicbrainz.org")+official_release,
        std::string("/ws/2/release/")+first+
            "?inc=recordings%2Bartist-credits%2Bisrcs&fmt=json",
        official_release+"&evil=1",
        std::string("/ws/2/release/../")+first+
            std::string(release_lookup_request_suffix),
        std::string("/ws/2/release?query=Known%0DInjected")+
            std::string(search_request_suffix),
        std::string("/ws/2/release?query=Known%0AInjected")+
            std::string(search_request_suffix),
        std::string("/ws/2/release?query=Known%00Injected")+
            std::string(search_request_suffix),
        std::string("/ws/2/recording?query=Text%7FInjected")+
            std::string(search_request_suffix),
        std::string("/ws/2/recording?query=Text%zzInjected")+
            std::string(search_request_suffix),
        std::string("/ws/2/recording?query=Text%aaInjected")+
            std::string(search_request_suffix),
        std::string("/ws/2/recording?query=Track")+
            std::string(search_request_suffix)+"#fragment",
        std::string("/ws/2/recording?query=")+
            std::string(search_request_suffix),
        std::string("/ws/2/recording?query=Track")+
            "&fmt=json&limit=100",
    }) {
        check(!qualified_musicbrainz_request_path(malicious),
              "unapproved host, endpoint, identity or query rejected");
    }
    check(make_search_path(SearchKind::Release,"Album","").find("/ws/2/release?")==0,
          "release search without unknown artist is permitted");
    const auto album_stages=release_search_plan("Live Set","Artist feat. Guest");
    check(album_stages.size()==4 &&
          album_stages[0].path==make_search_path(
              SearchKind::Release,"Live Set","Artist feat. Guest") &&
          album_stages[1].path==make_search_path(
              SearchKind::Release,"Live Set","Artist") &&
          album_stages[2].path==make_search_path(
              SearchKind::Release,"Live Set","Guest") &&
          album_stages[3].path==make_search_path(
              SearchKind::Release,"Live Set","") &&
          album_stages[1].label.find("identity review")!=std::string::npos &&
          album_stages[2].label.find("identity review")!=std::string::npos &&
          album_stages[3].label.find("unverified")!=std::string::npos &&
          std::all_of(album_stages.begin(),album_stages.end(),
              [](const auto& stage) {
                  return qualified_musicbrainz_request_path(stage.path);
              }),
          "CUE release stages retain full credit, try each credited artist, then title-only without asserting an edition match");
    check(valid_release_search_barcode("4006381333931") &&
          valid_release_search_barcode("036000291452") &&
          !valid_release_search_barcode("4006381333932") &&
          !valid_release_search_barcode("036000291453") &&
          !valid_release_search_barcode("40063813339X1") &&
          !valid_release_search_barcode("4006381333931\n") &&
          !valid_release_search_barcode("12345"),
          "only check-digit-valid UPC-A/EAN-13 becomes a release identifier");
    const auto catalog_stages=release_search_plan(
        "Live Set","Artist feat. Guest","4006381333931");
    check(catalog_stages.size()==5 &&
          catalog_stages.front().label.find("barcode")!=std::string::npos &&
          catalog_stages.front().path==
              std::string("/ws/2/release?query=barcode%3A4006381333931")+
                  std::string(search_request_suffix) &&
          qualified_musicbrainz_request_path(catalog_stages.front().path) &&
          std::equal(album_stages.begin(),album_stages.end(),
              catalog_stages.begin()+1,
              [](const auto& a,const auto& b) {return a.path==b.path;}),
          "validated CUE media barcode adds one bounded exact read-only search stage before every credited-artist strategy");
    const auto barcode_only=release_search_plan(
        "","Artist feat. Guest","4006381333931");
    check(barcode_only.size()==1 &&
          barcode_only.front().path==catalog_stages.front().path &&
          barcode_only.front().path.find("artist")==std::string::npos,
          "missing CUE album title permits only exact barcode discovery");
    const auto invalid_catalog_stages=release_search_plan(
        "Live Set","Artist feat. Guest","4006381333932");
    check(invalid_catalog_stages.size()==album_stages.size() &&
          invalid_catalog_stages.front().path==album_stages.front().path,
          "invalid CUE media barcode never leaks into an official query");

    const auto album_without_artist=release_search_plan("Live Set","");
    check(album_without_artist.size()==1 &&
          album_without_artist[0].path==album_stages[3].path,
          "missing release artist never repeats the same provider URL");
    // Real collaboration names often include an ampersand INSIDE one
    // artist's name. Only explicit X/feat delimiters may split credits.
    const auto dj_collaboration=release_search_plan(
        "1000 Years","Kyau & Albert X Maywave");
    check(dj_collaboration.size()==4 &&
          dj_collaboration[0].path==make_search_path(
              SearchKind::Release,"1000 Years","Kyau & Albert X Maywave") &&
          dj_collaboration[1].path==make_search_path(
              SearchKind::Release,"1000 Years","Kyau & Albert") &&
          dj_collaboration[2].path==make_search_path(
              SearchKind::Release,"1000 Years","Maywave") &&
          dj_collaboration[3].path==make_search_path(
              SearchKind::Release,"1000 Years","") &&
          dj_collaboration[1].label.find("identity review")!=std::string::npos &&
          dj_collaboration[2].label.find("identity review")!=std::string::npos,
          "joined DJ album artist recall retains duo ampersand and requires edition review");
    const auto single_artist=release_search_plan("Album","Kyau & Albert");
    check(single_artist.size()==2 &&
          single_artist[0].path==make_search_path(
              SearchKind::Release,"Album","Kyau & Albert") &&
          single_artist[1].path==make_search_path(
              SearchKind::Release,"Album",""),
          "ampersand inside a credited artist must never be split");
    const auto malformed_partner=release_search_plan(
        "Album","Valid Artist feat. "+std::string(160,'X'));
    check(malformed_partner.size()==1 &&
          malformed_partner[0].path==make_search_path(
              SearchKind::Release,"Album",""),
          "overlong joined credit cannot leak partial invented identities");
    rejects([]{(void)release_search_plan("","Artist");},
            "empty release title cannot fall back to broad artist-only search");
    const std::string too_long_artist(151,'A');
    const auto safe_album_long_artist=release_search_plan(
        "Qualified CUE album",too_long_artist);
    check(safe_album_long_artist.size()==1 &&
          safe_album_long_artist[0].path==make_search_path(
              SearchKind::Release,"Qualified CUE album",""),
          "overlong CUE artist preserves safe release-title-only search");
    const auto safe_album_control=release_search_plan("Qualified CUE album",
        std::string("Artist\nInjected"));
    check(safe_album_control.size()==1 &&
          safe_album_control[0].path==safe_album_long_artist[0].path,
          "ASCII controls in optional CUE artist never become provider query");
    check(make_release_lookup_path(first)==std::string("/ws/2/release/")+first+
          std::string(release_lookup_request_suffix) &&
          release_lookup_request_suffix.find("%2Blabels")!=std::string_view::npos &&
          search_request_suffix=="&fmt=json&limit=25",
          "provider release lookup and native allowlist share official label-bearing contract");
    rejects([]{(void)make_release_lookup_path("../ws/2/recording");},
            "invalid MBIDs never become URL paths");
    rejects([]{(void)make_search_path(SearchKind::Release,"Album\nOther","");},
            "CRLF injection in search rejected");
    rejects([]{(void)make_search_path(SearchKind::Recording,
                                     std::string("Track\x01",6),"");},
            "hidden ASCII control in source TITLE must not reach MusicBrainz");
    rejects([]{(void)make_search_path(SearchKind::Recording,
                                     std::string("Track\x7f",6),"");},
            "DEL byte in source TITLE must not reach MusicBrainz");
    rejects([]{(void)make_search_path(SearchKind::Release,"","");},
            "empty search title rejected");

    const auto klf=recording_search_plan(
        "What Time Is Love (Original Version)","The KLF");
    check(klf.core_title=="What Time Is Love" && klf.attempts.size()==4 &&
          klf.attempts[0].label.starts_with("Main title + artist") &&
          klf.attempts[0].path==make_search_path(
              SearchKind::Recording,"What Time Is Love","The KLF") &&
          klf.attempts[2].path==make_search_path(
              SearchKind::Recording,"What Time Is Love",""),
          "KLF regression: main title+artist before manually broader search");
    // Successive official search stages retain exact IDs and provenance
    // without treating duplicate results as independent editions.
    SearchResult first_stage;
    first_stage.kind=SearchKind::Recording;
    first_stage.candidates.push_back({SearchKind::Recording,first,
        "What Time Is Love","The KLF","",{},231000,96,"",false});
    SearchResult second_stage;
    second_stage.kind=SearchKind::Recording;
    second_stage.candidates.push_back({SearchKind::Recording,first,
        "What Time Is Love","The KLF","",{},231000,84,"",false});
    second_stage.candidates.push_back({SearchKind::Recording,second,
        "What Time Is Love (Original Version)","The KLF","",{},231500,81,"",false});
    const SearchResult empty_stage{SearchKind::Recording,{}};
    const auto accumulated_first=merge_search_stage_results(
        empty_stage,first_stage,"Main title + artist",false);
    const auto accumulated_second=merge_search_stage_results(
        accumulated_first.result,second_stage,"Full title + artist",true);
    check(accumulated_first.added==1 &&
          accumulated_second.added==1 && accumulated_second.repeated==1 &&
          accumulated_second.result.candidates.size()==2 &&
          accumulated_second.result.candidates[0].mbid==first &&
          accumulated_second.result.candidates[0].search_score==96 &&
          accumulated_second.result.candidates[0].discovery_strategy==
              "Main title + artist" &&
          accumulated_second.result.candidates[1].discovery_strategy==
              "Full title + artist" &&
          !accumulated_second.result.candidates[0].repeated_identity_conflict,
          "multiple successful stages retain unique IDs, first rank and provenance");
    auto conflicting_stage=second_stage;
    conflicting_stage.candidates[0].title="What Time Is Love (different)";
    const auto conflicting=merge_search_stage_results(
        accumulated_first.result,conflicting_stage,"Title only",true);
    check(conflicting.conflicting==1 &&
          conflicting.result.candidates[0].repeated_identity_conflict &&
          conflicting.result.candidates[0].title=="What Time Is Love" &&
          conflicting.result.candidates[0].discovery_strategy=="Main title + artist",
          "same MBID inconsistent snippets flagged without replacing first evidence");
    const auto no_new=merge_search_stage_results(
        accumulated_second.result,empty_stage,"Empty fallback",true);
    check(no_new.added==0 && no_new.result.candidates.size()==2,
          "empty later stage does not erase earlier search results");
    const auto restart=merge_search_stage_results(
        accumulated_second.result,first_stage,"Restarted title",false);
    check(restart.result.candidates.size()==1 &&
          restart.result.candidates[0].discovery_strategy=="Restarted title",
          "new query or exhausted-plan restart discards previous stage results");
    SearchResult release_stage;
    release_stage.kind=SearchKind::Release;
    release_stage.candidates.push_back({SearchKind::Release,first,"Album","Artist",
        "",{},-1,0,"",false});
    rejects([&] {(void)merge_search_stage_results(
        accumulated_first.result,release_stage,"Other kind",true);},
        "recording and release cannot share accumulated browser");
    SearchResult invalid_stage=first_stage;
    invalid_stage.candidates[0].mbid="not-an-mbid";
    rejects([&] {(void)merge_search_stage_results(
        empty_stage,invalid_stage,"Invalid",false);},
        "invalid MBID cannot enter accumulated search results");
    rejects([&] {(void)merge_search_stage_results(
        empty_stage,first_stage,"Stage\nInjected",false);},
        "untrusted stage label cannot become user-visible provenance");
    check(search_title_core("Track (Unknown Remixer Club Dub)")==
              "Track (Unknown Remixer Club Dub)" &&
          search_title_core("Track (Extended Mix)")=="Track",
          "unknown remix preserved, generic version only stripped for search");
    // Additional DJ mixes often share a base recording with an officially
    // indexed title that omits its version. Broad searches never change tags,
    // discard the full-title attempt, or assert an exact version match.
    for(const std::string_view suffix : {
            std::string_view("Vocal Mix"),std::string_view("Dub Mix"),
            std::string_view("Instrumental Mix"),std::string_view("Main Mix"),
            std::string_view("Club Edit"),std::string_view("Extended Edit"),
            std::string_view("Original Club Mix"),
            std::string_view("Original Extended Mix"),
            std::string_view("Extended Club Mix")}) {
        const std::string full="Via Balearia ("+std::string(suffix)+")";
        const auto plan=recording_search_plan(full,"Estiva");
        check(plan.full_title==full && plan.core_title=="Via Balearia" &&
              plan.attempts.size()==4 &&
              plan.attempts[0].path==make_search_path(
                  SearchKind::Recording,"Via Balearia","Estiva") &&
              plan.attempts[1].path==make_search_path(
                  SearchKind::Recording,full,"Estiva") &&
              qualified_musicbrainz_request_path(plan.attempts[0].path) &&
              qualified_musicbrainz_request_path(plan.attempts[1].path) &&
              recording_identity_label(full,"Estiva","Via Balearia","Estiva")==
                  "Version review",
              "DJ suffix broadened for official discovery, never auto-matched");
    }
    check(search_title_core("Title (Live at the Arena)")==
              "Title (Live at the Arena)" &&
          search_title_core("Title (20th Anniversary Edition)")==
              "Title (20th Anniversary Edition)" &&
          search_title_core("Title [Unverified Bonus Track]")==
              "Title [Unverified Bonus Track]",
          "unknown edition and live qualifiers are never stripped");
    // Two recognized decorations can obscure a title already present in
    // the official recording index. Exact version evidence and identity
    // remain unmodified and the full-title query is still emitted.
    const auto stacked_version=recording_search_plan(
        "1000 Years (DJ Version) [Remastered]", "Kyau & Albert X Maywave");
    check(stacked_version.core_title=="1000 Years" &&
          stacked_version.full_title=="1000 Years (DJ Version) [Remastered]" &&
          stacked_version.attempts.size()==7 &&
          stacked_version.attempts[0].path==make_search_path(
              SearchKind::Recording,"1000 Years","Kyau & Albert X Maywave") &&
          stacked_version.attempts[1].path==make_search_path(
              SearchKind::Recording,stacked_version.full_title,
              "Kyau & Albert X Maywave") &&
          stacked_version.attempts[4].path==make_search_path(
              SearchKind::Recording,"1000 Years","Maywave") &&
          qualified_musicbrainz_request_path(stacked_version.attempts[0].path),
          "stacked generic versions broaden discovery without extra requests");
    check(search_title_core("Track - Extended Mix [Remastered]")=="Track" &&
          search_title_core("Track (Original Mix) - Radio Edit")=="Track" &&
          search_title_core("Track (John Smith Remix) (Remastered)")=="Track" &&
          search_title_core("Track (Live at the Arena) (Extended Mix)")==
              "Track (Live at the Arena)" &&
          search_title_core("Track (Extended Mix) [Unknown Edition]")==
              "Track (Extended Mix) [Unknown Edition]" &&
          search_title_core("Track (Extended Mix) [Remastered] (Radio Edit)")==
              "Track (Extended Mix)",
          "bounded suffix peeling stops on unknown qualifiers and after two recognized versions");
    const auto named_remix=recording_search_plan(
        "Only You (John Smith Remix)", "Matt Fax");
    check(named_remix.core_title=="Only You" &&
          named_remix.full_title=="Only You (John Smith Remix)" &&
          named_remix.attempts.size()==4 &&
          named_remix.attempts[0].path==make_search_path(
              SearchKind::Recording,"Only You","Matt Fax") &&
          named_remix.attempts[1].path==make_search_path(
              SearchKind::Recording,"Only You (John Smith Remix)","Matt Fax") &&
          recording_identity_label(
              "Only You (John Smith Remix)","Matt Fax",
              "Only You","Matt Fax")=="Version review",
          "named DJ remix gains recall fallback but never an exact version match");
    // Discogger's harder native corpus includes a credited main mix and an
    // original edit. The exact track remains a distinct query and a base
    // title hit is only a version-review candidate, never an exact mix.
    for(const auto& fixture : {
            std::pair{"Jóga (Howie B Main Mix)","Jóga"},
            std::pair{"Eple (Original Edit)","Eple"}}) {
        const auto plan=recording_search_plan(fixture.first,"Björk");
        check(plan.full_title==fixture.first &&
              plan.core_title==fixture.second &&
              plan.attempts.size()==4 &&
              plan.attempts[0].path==make_search_path(
                  SearchKind::Recording,fixture.second,"Björk") &&
              plan.attempts[1].path==make_search_path(
                  SearchKind::Recording,fixture.first,"Björk") &&
              recording_identity_label(fixture.first,"Björk",
                  fixture.second,"Björk")=="Version review",
              "credited main mix/original edit broadens lookup only");
    }
    const auto named_mix=recording_search_plan(
        "Insomnia (Monster Mix)","Faithless");
    check(named_mix.full_title=="Insomnia (Monster Mix)" &&
          named_mix.core_title=="Insomnia" &&
          named_mix.attempts.size()==4 &&
          named_mix.attempts[0].path==make_search_path(
              SearchKind::Recording,"Insomnia","Faithless") &&
          named_mix.attempts[1].path==make_search_path(
              SearchKind::Recording,"Insomnia (Monster Mix)","Faithless") &&
          recording_identity_label(
              "Insomnia (Monster Mix)","Faithless",
              "Insomnia","Faithless")=="Version review",
          "named bracketed mix expands read-only search, never approves identity");
    check(search_title_core("Insomnia (Live Mix)")==
              "Insomnia (Live Mix)" &&
          search_title_core("Insomnia (Unknown Club Dub)")==
              "Insomnia (Unknown Club Dub)",
          "ambiguous live/dub annotations stay exact");
    check(search_title_core("Jóga - Howie B Main Mix")==
              "Jóga - Howie B Main Mix" &&
          search_title_core("Jóga (Howie B Live Main Mix)")==
              "Jóga (Howie B Live Main Mix)" &&
          search_title_core("Jóga (Howie B Club Dub)")==
              "Jóga (Howie B Club Dub)",
          "non-bracketed names and ambiguous live mixes stay exact");
    check(search_title_core("Title [Artist Rework]")=="Title" &&
          search_title_core("Title [Extended Mix]")=="Title" &&
          search_title_core("Title [Live at the Arena]")==
              "Title [Live at the Arena]" &&
          search_title_core("Title (Unknown Remixer Club Dub)")==
              "Title (Unknown Remixer Club Dub)",
          "known bracket/version suffix is discovery-only; unknown qualifiers survive");
    // DJ file TITLEs commonly use a hyphen/en dash/em dash instead of
    // parenthetical qualifiers. Search both core and full title; retain
    // unresolved version identity and exact original tags for review.
    for(const std::string& full : {
            std::string("Via Balearia - Extended Mix"),
            std::string("Via Balearia \xE2\x80\x93 Radio Edit"),
            std::string("Via Balearia \xE2\x80\x94 Original Version"),
            std::string("Via Balearia - Dub Mix"),
            std::string("Via Balearia - Club Edit")}) {
        const auto plan=recording_search_plan(full,"Estiva");
        check(plan.full_title==full && plan.core_title=="Via Balearia" &&
              plan.attempts.size()==4 &&
              plan.attempts[0].path==make_search_path(
                  SearchKind::Recording,"Via Balearia","Estiva") &&
              plan.attempts[1].path==make_search_path(
                  SearchKind::Recording,full,"Estiva") &&
              qualified_musicbrainz_request_path(plan.attempts[0].path) &&
              qualified_musicbrainz_request_path(plan.attempts[1].path) &&
              recording_identity_label(full,"Estiva",
                  "Via Balearia","Estiva")=="Version review",
              "dash-delimited DJ version expands read-only discovery, not identity");
    }
    check(search_title_core("Title - Live at the Arena")==
              "Title - Live at the Arena" &&
          search_title_core("Title - John Smith Remix")==
              "Title - John Smith Remix" &&
          search_title_core("Title - 20th Anniversary Edition")==
              "Title - 20th Anniversary Edition" &&
          search_title_core("Title - ")=="Title -" &&
          search_title_core("Title - Extended Mix (Live)")==
              "Title - Extended Mix (Live)",
          "unknown hyphenated segments remain complete exact titles");
    const auto collab=recording_search_plan(
        "1000 Years (Dj Version)","Kyau & Albert X Maywave");
    check(collab.core_title=="1000 Years" &&
          collab.full_title=="1000 Years (Dj Version)" &&
          collab.attempts.size()==7 &&
          collab.attempts[0].path==make_search_path(
              SearchKind::Recording,"1000 Years","Kyau & Albert X Maywave") &&
          collab.attempts[2].path==make_search_path(
              SearchKind::Recording,"1000 Years","Kyau & Albert") &&
          collab.attempts[4].path==make_search_path(
              SearchKind::Recording,"1000 Years","Maywave") &&
          collab.attempts[5].path==make_search_path(
              SearchKind::Recording,"1000 Years","") &&
          recording_identity_label("1000 Years (Dj Version)",
              "Kyau & Albert X Maywave","1000 Years","Kyau & Albert")==
              "Artist differs",
          "DJ version and split collaboration improve search only, never authorize a match");
    check(lead_credit_search_artist("Artist feat. Guest")=="Artist" &&
          lead_credit_search_artist("Artist vs. Guest")=="Artist" &&
          lead_credit_search_artist("Artist w/ Guest")=="Artist" &&
          lead_credit_search_artist("Artist & Guest").empty() &&
          lead_credit_search_artist("A x B").empty(),
          "only explicit collaboration separators add a bounded leading-artist search");
    check(partner_credit_search_artist("Kyau & Albert X Maywave")=="Maywave" &&
          partner_credit_search_artist("Artist feat. Guest")=="Guest" &&
          partner_credit_search_artist("Artist & Guest").empty() &&
          partner_credit_search_artist("A x B").empty() &&
          partner_credit_search_artist("Artist x Guest feat. Singer").empty(),
          "second artist is an optional unique credit, never a nested or invented artist");
    const auto bounded_collab=recording_search_plan(
        "1000 Years (Dj Version)","Kyau & Albert X Maywave","USRC17607839");
    check(bounded_collab.attempts.size()==8 &&
          bounded_collab.attempts[0].label=="Exact ISRC identifier" &&
          bounded_collab.attempts[5].path==make_search_path(
              SearchKind::Recording,"1000 Years","Maywave") &&
          std::all_of(bounded_collab.attempts.begin(),bounded_collab.attempts.end(),
             [](const auto& attempt) {
                 return qualified_musicbrainz_request_path(attempt.path);
             }),
          "ISRC, exact/full, lead/partner and title-only are capped at eight allowed requests");
    const auto poisoned_artist=recording_search_plan(
        "Known Track","Lead Artist x Guest\nPrivate");
    check(poisoned_artist.attempts.size()==1 &&
          poisoned_artist.attempts[0].path==make_search_path(
              SearchKind::Recording,"Known Track",""),
          "malformed original collaboration cannot leak a derived artist query");
    const auto safe_recording=recording_search_plan("Known Track",too_long_artist);
    check(safe_recording.artist==too_long_artist &&
          safe_recording.attempts.size()==1 &&
          safe_recording.attempts[0].path==make_search_path(
              SearchKind::Recording,"Known Track",""),
          "overlong ARTIST stays intact as original and cannot disable title-only discovery");
    const auto safe_control_artist=recording_search_plan(
        "Known Track",std::string("Guest\nInjected"));
    check(safe_control_artist.attempts.size()==1 &&
          safe_control_artist.attempts[0].path==safe_recording.attempts[0].path,
          "control-character ARTIST cannot reach official HTTPS search path");
    const auto invalid_utf8_artist=recording_search_plan(
        "Known Track",std::string(1,static_cast<char>(0xff)));
    check(invalid_utf8_artist.attempts.size()==1 &&
          invalid_utf8_artist.attempts[0].path==safe_recording.attempts[0].path,
          "invalid UTF-8 ARTIST falls back to qualified title-only discovery");
    rejects([&]{(void)make_search_path(
        SearchKind::Recording,"Known Track",too_long_artist);},
        "direct query construction still rejects overlong artist");
    const auto by_isrc=recording_search_plan("Track (Radio Edit)","Artist","usrc17607839");
    check(by_isrc.attempts.size()==5 &&
          by_isrc.attempts[0].label=="Exact ISRC identifier" &&
          by_isrc.attempts[0].path.find("isrc%3AUSRC17607839")!=std::string::npos,
          "valid ISRC prioritized ahead of title-only fallback");
    check(!valid_search_isrc("../bad-1234") &&
          recording_search_plan("Track","Artist","../bad-1234").attempts.size()==2,
          "invalid ISRC never submitted; query count bounded");
    const std::string long_title(200,'T');
    const auto long_isrc=recording_search_plan(
        long_title,"Artist","USRC17607839");
    check(long_isrc.attempts.size()==1 &&
          long_isrc.attempts[0].label=="Exact ISRC identifier",
          "overlong TITLE cannot suppress valid ISRC-only discovery");
    rejects([&]{(void)recording_search_plan(long_title,"Artist");},
            "overlong TITLE without valid ISRC has no unsafe broad fallback");
    const auto hidden_title=recording_search_plan(
        std::string("Track\nHidden"),"Artist","USRC17607839");
    check(hidden_title.attempts.size()==1 &&
          hidden_title.attempts[0].label=="Exact ISRC identifier",
          "hidden TITLE controls cannot be silently stripped into Lucene");
    const auto long_remix_title=std::string(70,'X')+
        " ("+std::string(95,'Y')+" Remix)";
    const auto long_remix=recording_search_plan(long_remix_title,"Artist");
    check(long_remix.core_title==std::string(70,'X') &&
          long_remix.attempts.size()==2 &&
          long_remix.attempts[0].path==make_search_path(
              SearchKind::Recording,std::string(70,'X'),"Artist") &&
          long_remix.attempts[1].path==make_search_path(
              SearchKind::Recording,std::string(70,'X'),""),
          "qualified main title remains searchable even when full remix suffix exceeds bound");
    const auto id_only=recording_search_plan("","Artist","usrc17607839");
    check(id_only.full_title.empty() && id_only.core_title.empty() &&
          id_only.attempts.size()==1 &&
          id_only.attempts[0].label=="Exact ISRC identifier" &&
          id_only.attempts[0].path.find("isrc%3AUSRC17607839")!=std::string::npos,
          "missing TITLE with valid ISRC searches identifier only, without a broad artist query");
    const auto whitespace_title=recording_search_plan(" \t","Artist","USRC17607839");
    check(whitespace_title.attempts.size()==1 &&
          whitespace_title.attempts[0].path==id_only.attempts[0].path,
          "blank TITLE whitespace cannot create an unconstrained provider search");
    rejects([]{(void)recording_search_plan("","Artist","");},
            "missing title and missing identifier are rejected");
    rejects([]{(void)recording_search_plan("","Artist","../bad-1234");},
            "invalid ISRC does not bypass empty-title denial");
    rejects([]{(void)recording_search_plan(" \t","Artist","");},
            "whitespace-only title with no identifier remains invalid");
    check(recording_identity_label("Track","","Track","Artist")==
              "Artist unverified" &&
          recording_identity_label("Track","Artist","Track","")==
              "Artist unverified" &&
          recording_identity_label("Track","","Track","")==
              "Artist unverified" &&
          recording_identity_label("Track","   ","Track","Artist")==
              "Artist unverified" &&
          recording_identity_label("Track","Artist","Track","Other Artist")==
              "Artist differs",
          "title equality never invents artist confirmation for incomplete metadata");
    check(recording_identity_label("Track","","Different Song","Artist")==
              "Identity review" &&
          recording_identity_label("","", "Candidate Title","Artist")==
              "Identity review" &&
          recording_identity_label("Track (Extended Mix)","","Track","Artist")==
              "Version and artist review" &&
          recording_identity_label("Track","Artist","Track (Extended Mix)","")==
              "Version and artist review",
          "unknown artist never hides missing, unrelated or changed recording title");
    check(recording_identity_label("What Time Is Love (Original Version)","The KLF",
          "What Time Is Love?","The KLF")=="Version review" &&
          recording_identity_label("What Time Is Love (Original Version)","The KLF",
          "What Time Is Love?","Other Artist")=="Artist differs" &&
          recording_identity_label("What Time Is Love?","The KLF",
          "What Time Is Love?","The KLF")=="Title+artist",
          "artist/version conflicts cannot be treated as an approved identity");
    // More than 8 strongly similar DJ versions can rank ahead of the correct
    // mix. Use the official bounded 25-result search window with no additional
    // request, preserving API relevance order, every exact MBID, and a hard cap.
    std::string wider_json="{\"recordings\":[";
    for(int i=1;i<=30;++i) {
        if(i>1) wider_json+=",";
        const auto ordinal=std::to_string(i);
        const std::string mbid_suffix=
            std::string(12-ordinal.size(),'0')+ordinal;
        wider_json+="{\"id\":\"00000000-0000-0000-0000-"+
            mbid_suffix+"\",\"title\":\"Only You (Mix "+ordinal+
            ")\",\"artist-credit\":[{\"name\":\"Matt Fax\"}]}";
    }
    wider_json+="]}";
    const auto wider=parse_search(wider_json,SearchKind::Recording);
    check(max_search_candidates==25 && wider.candidates.size()==25 &&
          wider.candidates[0].title=="Only You (Mix 1)" &&
          wider.candidates[8].title=="Only You (Mix 9)" &&
          wider.candidates[24].title=="Only You (Mix 25)",
          "bounded 25-result window retains the ninth and later legitimate mixes");
    const auto wider_release_path=make_search_path(
        SearchKind::Release,"Known edition","Artist");
    check(wider_release_path.ends_with(search_request_suffix) &&
          make_search_path(SearchKind::Recording,"Known track","Artist")
              .ends_with(search_request_suffix) &&
          recording_search_plan("Known track","Artist","USRC17607839")
              .attempts.front().path.ends_with(search_request_suffix),
          "search, release and ISRC discovery share one HTTP query suffix");
    const std::string recording_json=R"MB({
       "created":"2026-10-09","recording-count":2,
       "recordings":[
         {"id":"11111111-1111-1111-1111-111111111111",
          "title":"Bj\u00f6rk & \u6771\u4eac",
          "score":99,"length":215540,
          "artist-credit":[{"name":"Alpha","joinphrase":" feat. "},{"name":"Beta"}],
          "isrcs":["gbabc2600001","GBABC2600001","USRC17607839",
                   "invalid/ISRC"],
          "releases":[{"id":"ignored"}]},
         {"id":"22222222-2222-2222-2222-222222222222",
          "title":"Track (Radio Edit)",
          "score":55,"length":200000,
          "artist-credit":[{"name":"Other Artist"}]},
         {"id":"invalid/redirect","title":"Bad","artist-credit":[{"name":"X"}]}
       ]
    })MB";
    const auto found=parse_search(recording_json,SearchKind::Recording);
    check(found.candidates.size()==2 &&
          found.candidates[0].mbid==first &&
          found.candidates[0].title=="Björk & 東京" &&
          found.candidates[0].artist=="Alpha feat. Beta" &&
          found.candidates[0].duration_ms==215540 &&
          found.candidates[0].search_score==99 &&
          found.candidates[1].search_score==55 &&
          found.candidates[0].recording_isrcs==
              std::vector<std::string>({"GBABC2600001","USRC17607839"}) &&
          found.candidates[1].recording_isrcs.empty(),
          "official recording response preserves Unicode, artist roles, duration, score and distinct ISRC evidence");
    const auto unsafe_isrc_result=parse_search(R"({
        "recordings":[{"id":"11111111-1111-1111-1111-111111111111",
         "title":"Title","artist-credit":[{"name":"Artist"}],
         "isrcs":23}]
    })",SearchKind::Recording);
    check(unsafe_isrc_result.candidates.size()==1 &&
          unsafe_isrc_result.candidates[0].recording_isrcs.empty(),
          "malformed ISRC source cannot be inferred or changed");
    const auto edition_isrc_noise=parse_search(R"({
        "releases":[{"id":"11111111-1111-1111-1111-111111111111",
         "title":"Edition","artist-credit":[{"name":"Artist"}],
         "isrcs":["USRC17607839"]}]
    })",SearchKind::Release);
    check(edition_isrc_noise.candidates.size()==1 &&
          edition_isrc_noise.candidates[0].recording_isrcs.empty(),
          "edition ISRC-like data cannot masquerade as recording identifier");
    const auto repeated=parse_search(R"({
        "recordings":[
            {"id":"11111111-1111-1111-1111-111111111111",
             "title":"Version A","artist-credit":[{"name":"DJ"}],"score":99},
            {"id":"11111111-1111-1111-1111-111111111111",
             "title":"Version A repeated","artist-credit":[{"name":"DJ"}],"score":70},
            {"id":"22222222-2222-2222-2222-222222222222",
             "title":"Version A","artist-credit":[{"name":"DJ"}],"score":80}
        ]
    })",SearchKind::Recording);
    check(repeated.candidates.size()==2 &&
          repeated.candidates[0].mbid==first &&
          repeated.candidates[0].title=="Version A" &&
          repeated.candidates[0].search_score==99 &&
          repeated.candidates[1].mbid==second,
          "same MBID is deduplicated but same-name distinct recording retained in API order");
    const auto repeated_releases=parse_search(R"({
        "releases":[
            {"id":"11111111-1111-1111-1111-111111111111",
             "title":"Edition","artist-credit":[{"name":"DJ"}]},
            {"id":"11111111-1111-1111-1111-111111111111",
             "title":"Repeated edition","artist-credit":[{"name":"DJ"}]}
        ]
    })",SearchKind::Release);
    check(repeated_releases.candidates.size()==1 &&
          repeated_releases.candidates[0].title=="Edition",
          "duplicate edition identity cannot produce two selectable release rows");
    // A MusicBrainz search hit may omit release artist-credit even though its
    // edition MBID is still valid for a user-initiated exact release lookup.
    // Never hide that release or invent a performer from local metadata.
    const auto no_artist_releases=parse_search(R"({
        "releases":[
            {"id":"11111111-1111-1111-1111-111111111111",
             "title":"Uncredited DJ Compilation","score":91},
            {"id":"22222222-2222-2222-2222-222222222222",
             "title":"Uncredited Live Edition","artist-credit":[],"date":"2026-10"},
            {"id":"33333333-3333-3333-3333-333333333333",
             "title":"","artist-credit":[]}
        ]
    })",SearchKind::Release);
    check(no_artist_releases.candidates.size()==2 &&
          no_artist_releases.candidates[0].mbid==first &&
          no_artist_releases.candidates[0].artist.empty() &&
          no_artist_releases.candidates[0].search_score==91 &&
          no_artist_releases.candidates[1].mbid==second &&
          no_artist_releases.candidates[1].artist.empty() &&
          no_artist_releases.candidates[1].release_date=="2026-10" &&
          recording_identity_label("Uncredited DJ Compilation","Local",
              no_artist_releases.candidates[0].title,
              no_artist_releases.candidates[0].artist)=="Artist unverified" &&
          make_release_lookup_path(no_artist_releases.candidates[0].mbid)==
              make_release_lookup_path(first),
          "artist-unknown releases stay discoverable only as unverified edition IDs");
    const auto no_artist_recordings=parse_search(R"({
        "recordings":[
            {"id":"11111111-1111-1111-1111-111111111111",
             "title":"No Artist Evidence"},
            {"id":"22222222-2222-2222-2222-222222222222",
             "title":"Qualified Recording","artist-credit":[{"name":"DJ"}]}
        ]
    })",SearchKind::Recording);
    check(no_artist_recordings.candidates.size()==1 &&
          no_artist_recordings.candidates[0].mbid==second &&
          no_artist_recordings.candidates[0].artist=="DJ",
          "recording search still rejects uncredited identities");
    check(parse_search(R"({"releases":[]})",SearchKind::Release).candidates.empty(),
          "empty permitted source response never fabricates a hit");
    const auto release_search=std::string(R"MB({
        "releases":[{"id":"11111111-1111-1111-1111-111111111111",
        "title":"Album Edition","date":"2001-06-15","score":91,
        "artist-credit":[{"name":"Example Artist"}]}]
    })MB");
    const auto releases=parse_search(release_search,SearchKind::Release);
    check(releases.candidates.size()==1&&
          releases.candidates[0].kind==SearchKind::Release&&
          releases.candidates[0].release_date=="2001-06-15",
          "release search retains explicit release-edition date");


    // Search results must not display an impossible release calendar value
    // as verified edition evidence; the candidate identity stays available.
    const auto search_with_date=[&](std::string_view date) {
        auto json=release_search;
        const auto at=json.find("\"date\":\"2001-06-15\"");
        check(at!=std::string::npos,"synthetic release date fixture is stable");
        json.replace(at,std::string("\"date\":\"2001-06-15\"").size(),
                     "\"date\":\""+std::string(date)+"\"");
        return parse_search(json,SearchKind::Release);
    };
    for(const char* invalid:{"0000","2026-02-29","2026-04-31",
                             "1900-02-29","2026-13","2026/02/20"}) {
        const auto result=search_with_date(invalid);
        check(result.candidates.size()==1 &&
              result.candidates[0].release_date.empty(),
              "invalid MusicBrainz search calendar date is not edition evidence");
    }
    for(const char* valid:{"2026","2026-02","2024-02-29","2000-02-29"}) {
        const auto result=search_with_date(valid);
        check(result.candidates.size()==1 &&
              result.candidates[0].release_date==valid,
              "valid MusicBrainz search date retains its original precision");
    }
    const auto edition_json=std::string(R"MB({
      "id":"11111111-1111-1111-1111-111111111111",
      "title":"Album Edition", "date":"2001-06-15",
      "artist-credit":[{"name":"Example Artist"}],
      "media":[
       {"position":1,"tracks":[
         {"position":1,"title":"First (Extended Mix)","length":420000,
          "artist-credit":[{"name":"Example Artist"}],
          "recording":{"id":"22222222-2222-2222-2222-222222222222",
             "title":"First (Extended Mix)",
             "isrcs":["GBABC2600001"],
             "artist-credit":[{"name":"Example Artist"}]}},
         {"position":2,"title":"Second","length":301000,
          "recording":{"id":"22222222-2222-2222-2222-222222222222",
             "title":"Second","artist-credit":[{"name":"Example Artist"}]}}]},
       {"position":2,"tracks":[
         {"position":1,"title":"Third","length":240000,
          "recording":{"id":"22222222-2222-2222-2222-222222222222",
             "title":"Third","artist-credit":[{"name":"Guest Artist"}]}}]}
      ]
    })MB");
    const auto edition=parse_release_lookup(edition_json,first);
    // End-to-end parsed official edition -> exact physical track match ->
    // grouped read-only local/album/track details. No physical stage is exposed.
    const auto physical_release=parse_release_lookup(R"MB({
      "id":"11111111-1111-1111-1111-111111111111",
      "title":"Synthetic Edition", "date":"2026-10-21",
      "barcode":"1234567890123",
      "artist-credit":[{"name":"Example Artist"}],
      "media":[{"position":1,"tracks":[
        {"id":"22222222-2222-2222-2222-222222222222",
         "position":1,"title":"Skyline (Extended Mix)","length":420000,
         "artist-credit":[{"name":"Example Artist"}],
         "recording":{"title":"Skyline (Extended Mix)",
           "artist-credit":[{"name":"Example Artist"}]}},
        {"id":"33333333-3333-3333-3333-333333333333",
         "position":2,"title":"Other (Extended Mix)","length":240000,
         "artist-credit":[{"name":"Example Artist"}],
         "recording":{"title":"Other (Extended Mix)",
           "artist-credit":[{"name":"Example Artist"}]}}
      ]}]
    })MB",first);
    const MetadataDocument physical_tags{{
        {"TITLE",{"Skyline (Extended Mix)"}},
        {"ARTIST",{"Example Artist"}},
        {"TRACKNUMBER",{"1"}},
        {"DISCNUMBER",{"1"}},
        {"BARCODE",{"1234567890123"}},
        {"DJ_CUSTOM",{"Keep","All","Values"}},
        {"REPLAYGAIN_TRACK_GAIN",{"-5 dB"}}
    }};
    const auto originals_copy=physical_tags;
    const auto details=review_musicbrainz_physical_edition(
        physical_tags,physical_release,420000);
    const auto get_row=[](const auto& rows,std::string_view name) {
        return std::find_if(rows.begin(),rows.end(),[&](const auto& row) {
            return row.field==name;
        });
    };
    check(get_row(details,"Album / ALBUM")!=details.end() &&
          get_row(details,"Album / ALBUM")->state==
              FieldReviewState::NeedsReview &&
          get_row(details,"Release Track 1 / MATCH")->state==
              FieldReviewState::Unchanged &&
          get_row(details,"Release Track 1 / TITLE")->state==
              FieldReviewState::Unchanged &&
          get_row(details,"Release Track 2 / MATCH")->state==
              FieldReviewState::Blocked &&
          get_row(details,"Release Track 2 / TITLE")->state==
              FieldReviewState::Blocked &&
          get_row(details,"Physical / DJ_CUSTOM")->original_values==
              std::vector<std::string>({"Keep","All","Values"}) &&
          get_row(details,"Physical / REPLAYGAIN_TRACK_GAIN")!=details.end() &&
          physical_tags==originals_copy,
          "physical edition comparison preserves custom/ReGain vectors and only "
          "one direct exact occurrence promotes read-only album and track evidence");
    // One explicit two-file album selection: complete one-to-one exact
    // edition proof is visible, with every original vector retained.
    auto second_tags=physical_tags;
    second_tags.fields[0].values={"Other (Extended Mix)"};
    second_tags.fields[2].values={"2"};
    second_tags.fields.push_back({"DJ_NOTES",{"second","source"}});
    const auto batch_before=second_tags;
    const auto batch=review_musicbrainz_physical_batch({
        {0,physical_tags,420000,true},
        {1,second_tags,240000,true}},physical_release);
    check(batch.complete && batch.matched==2 &&
          get_row(batch.rows,"Batch / MATCH")->state==
              FieldReviewState::Unchanged &&
          get_row(batch.rows,"Physical #1 / MATCH")->state==
              FieldReviewState::Unchanged &&
          get_row(batch.rows,"Physical #2 / MATCH")->state==
              FieldReviewState::Unchanged &&
          get_row(batch.rows,"Physical #2 / Original DJ_NOTES")->original_values==
              std::vector<std::string>({"second","source"}) &&
          get_row(batch.rows,"Physical #1 / Original REPLAYGAIN_TRACK_GAIN")!=
              batch.rows.end() &&
          second_tags==batch_before && physical_tags==originals_copy,
          "whole physical album comparison preserves all original vectors");
    // The same exact edition also qualifies when both original ALBUM
    // fields are already populated; neither path mutates existing tags.
    auto first_tagged=physical_tags;
    auto second_tagged=second_tags;
    first_tagged.fields.push_back({"ALBUM",{"Synthetic Edition"}});
    second_tagged.fields.push_back({"ALBUM",{"Synthetic Edition"}});
    const auto already_tagged=review_musicbrainz_physical_batch({
        {0,first_tagged,420000,true},
        {1,second_tagged,240000,true}},physical_release);
    check(already_tagged.complete && already_tagged.matched==2 &&
          get_row(already_tagged.rows,"Batch / ALBUM")->state==
              FieldReviewState::Unchanged,
          "already tagged exact physical album is an immutable no-op comparison");
    const auto partial=review_musicbrainz_physical_batch({
        {0,physical_tags,420000,true},
        {1,second_tags,240000,false}},physical_release);
    check(!partial.complete && partial.matched==0 &&
          get_row(partial.rows,"Physical #2 / MATCH")->reason==
              "musicbrainz_physical_batch_NOT_PHYSICAL_SOURCE" &&
          get_row(partial.rows,"Batch / ALBUM")->state==
              FieldReviewState::Blocked &&
          get_row(partial.rows,"Physical #2 / MATCH")->state==
              FieldReviewState::Blocked,
          "unqualified physical source blocks batch album identity");
    auto wrong_album=second_tags;
    wrong_album.fields.push_back({"ALBUM",{"Different Edition"}});
    auto album_first=physical_tags;
    album_first.fields.push_back({"ALBUM",{"Synthetic Edition"}});
    const auto mixed=review_musicbrainz_physical_batch({
        {0,album_first,420000,true},
        {1,wrong_album,240000,true}},physical_release);
    check(!mixed.complete && mixed.matched==0 &&
          get_row(mixed.rows,"Physical #2 / MATCH")->reason==
              "musicbrainz_physical_batch_CONFLICTING_ALBUM",
          "mixed album identity shows source-specific blocker");
    auto duplicate_album=wrong_album;
    duplicate_album.fields.push_back({"ALBUM",{"Synthetic Edition"}});
    const auto ambiguous_album=review_musicbrainz_physical_batch({
        {0,physical_tags,420000,true},
        {1,duplicate_album,240000,true}},physical_release);
    check(!ambiguous_album.complete && ambiguous_album.matched==0 &&
          get_row(ambiguous_album.rows,"Physical #2 / MATCH")->reason==
              "musicbrainz_physical_batch_AMBIGUOUS_ALBUM",
          "duplicate ALBUM field identifies source-specific blocker");
    auto oversized_sources=std::vector<PhysicalBatchTrack>(129,
        {0,physical_tags,420000,true});
    rejects([&]{(void)review_musicbrainz_physical_batch(
        oversized_sources,physical_release);
    },"physical album selection over 128 cannot scan unbounded library");
    auto disputed_batch=second_tags;
    disputed_batch.fields[4].values={"different barcode"};
    const auto conflict=review_musicbrainz_physical_batch({
        {0,physical_tags,420000,true},
        {1,disputed_batch,240000,true}},physical_release);
    check(!conflict.complete && conflict.matched==0 &&
          get_row(conflict.rows,"Physical #2 / MATCH")->reason==
              "musicbrainz_physical_batch_EDITION_IDENTIFIER_CONFLICT",
          "hidden second-source edition identifier exposes exact blocker");
    auto wrong_mix_batch=second_tags;
    wrong_mix_batch.fields[0].values={"Other (Radio Edit)"};
    const auto remix=review_musicbrainz_physical_batch({
        {0,physical_tags,420000,true},
        {1,wrong_mix_batch,240000,true}},physical_release);
    check(!remix.complete && remix.matched<2 &&
          get_row(remix.rows,"Physical #2 / MATCH")->candidate.values[0].find(
              "title/mix/number/duration")!=std::string::npos,
          "wrong mix names matching ambiguity without proposing a replacement");
    rejects([&]{(void)review_musicbrainz_physical_batch({
        {0,physical_tags,420000,true},
        {0,second_tags,240000,true}},physical_release);
    },"duplicate physical source identity rejected");
    rejects([&]{(void)review_musicbrainz_physical_batch({
        {0,physical_tags,420000,true}},physical_release);
    },"single source is not a physical album batch");
    auto tainted=physical_release;
    tainted.tracks[1].fields[0].source_id=first;
    const auto forged_batch=review_musicbrainz_physical_batch({
        {0,physical_tags,420000,true},
        {1,second_tags,240000,true}},tainted);
    check(!forged_batch.complete,
          "foreign release-track MBID never qualifies physical album");
    auto no_id=physical_release;
    no_id.tracks[0].source_track_id.clear();
    no_id.tracks[0].fields.clear();
    const auto missed=review_musicbrainz_physical_edition(
        physical_tags,no_id,420000);
    check(get_row(missed,"Album / ALBUM")->state==FieldReviewState::Blocked &&
          get_row(missed,"Release Track 1 / MATCH")->state==FieldReviewState::Blocked,
          "recording title/artist/duration cannot replace absent release track ID");
    auto forged_track=physical_release;
    forged_track.tracks[0].fields[0].source_id=first; // release != track
    const auto forged=review_musicbrainz_physical_edition(
        physical_tags,forged_track,420000);
    check(get_row(forged,"Album / ALBUM")->state==FieldReviewState::Blocked,
          "wrong-MBID track field blocks matched album evidence");
    auto wrong_barcode=physical_tags;
    wrong_barcode.fields[4].values={"9999999999999"};
    const auto disputed=review_musicbrainz_physical_edition(
        wrong_barcode,physical_release,420000);
    check(get_row(disputed,"Album / ALBUM")->state==FieldReviewState::Blocked,
          "conflicting local barcode blocks entire edition identity promotion");
    auto mismatched_mix=physical_tags;
    mismatched_mix.fields[0].values={"Skyline (Radio Edit)"};
    const auto wrong_mix=review_musicbrainz_physical_edition(
        mismatched_mix,physical_release,420000);
    check(get_row(wrong_mix,"Album / ALBUM")->state==FieldReviewState::Blocked,
          "same artist but wrong mix never becomes exact physical edition match");
    rejects([&]{auto mixed=physical_release;
        mixed.fields[0].source_id=second;
        (void)review_musicbrainz_physical_edition(physical_tags,mixed,420000);
    },"foreign edition field source is never trusted in physical comparison");

    check(edition.provider=="musicbrainz"&&edition.edition_id==first &&
          edition.fields.size()==3 &&
          edition.fields[2].date_meaning==DateMeaning::EditionRelease &&
          edition.tracks.size()==3 &&
          edition.tracks[0].source_track_id.empty() &&
          edition.tracks[1].source_track_id.empty() &&
          edition.tracks[0].recording.isrc=="GBABC2600001" &&
          edition.tracks[1].disc_number==1 && edition.tracks[1].track_number==2 &&
          edition.tracks[2].disc_number==2 && edition.tracks[2].track_number==1,
          "release lookup preserves exact edition, recording, date/disc/track scope");


    // Release-level ALBUM ARTIST must never be copied into a missing
    // track-level ARTIST. A partial compilation remains inspectable and
    // individually credited sibling tracks stay matchable. The uncredited
    // track must not receive a fabricated artist or automatic association.
    const auto compilation=parse_release_lookup(R"MB({
      "id":"11111111-1111-1111-1111-111111111111",
      "title":"Mixed Artists",
      "artist-credit":[{"name":"Various Artists"}],
      "media":[{"tracks":[
        {"position":1,"title":"Credited Song",
         "artist-credit":[{"name":"Actual Track Performer"}],
         "recording":{"title":"Credited Song",
                      "artist-credit":[{"name":"Recording Performer"}]}},
        {"position":2,"title":"Uncredited Song",
         "recording":{"title":"Uncredited Song"}},
        {"position":3,"title":"Recording Song",
         "recording":{"title":"Recording Song",
                      "artist-credit":[{"name":"Actual Recording Performer"}]}}
      ]}]
    })MB",first);
    check(compilation.fields.size()==2 &&
          compilation.fields[1].field=="ALBUM ARTIST" &&
          compilation.fields[1].values==std::vector<std::string>{"Various Artists"} &&
          compilation.tracks.size()==3 &&
          compilation.tracks[0].recording.primary_artist=="Actual Track Performer" &&
          compilation.tracks[1].recording.primary_artist.empty() &&
          compilation.tracks[2].recording.primary_artist=="Actual Recording Performer",
          "compilation retains release artist separately from track/recording artist");
    LocalReleaseTrack local_uncredited;
    local_uncredited.source_index=17;
    local_uncredited.recording.title="Uncredited Song";
    local_uncredited.recording.primary_artist="Unknown Local Performer";
    local_uncredited.disc_number=1;
    local_uncredited.track_number=2;
    const auto uncredited_alignment=align_release({local_uncredited},compilation);
    check(uncredited_alignment.matched==0 &&
          uncredited_alignment.unmatched==1 &&
          uncredited_alignment.tracks.size()==1 &&
          uncredited_alignment.tracks[0].edition_track_index==no_release_track,
          "missing provider track credit cannot be auto-matched via Various Artists");



    // The exact-edition parser previously accepted any nonempty DATE,
    // unlike the search display. Both paths now share one calendar gate.
    const auto lookup_with_date=[&](std::string_view date) {
        auto json=edition_json;
        const auto at=json.find("\"date\":\"2001-06-15\"");
        check(at!=std::string::npos,"synthetic edition date fixture is stable");
        json.replace(at,std::string("\"date\":\"2001-06-15\"").size(),
                     "\"date\":\""+std::string(date)+"\"");
        return parse_release_lookup(json,first);
    };
    for(const char* invalid:{"0000","2026-02-29","2026-04-31",
                             "1900-02-29","2026-13","2026/02/20"}) {
        const auto result=lookup_with_date(invalid);
        check(result.fields.size()==2 && result.tracks.size()==3,
              "invalid exact-edition DATE omitted without losing tracklist");
    }
    for(const char* valid:{"2026","2026-02","2024-02-29","2000-02-29"}) {
        const auto result=lookup_with_date(valid);
        check(result.fields.size()==3 &&
              result.fields[2].field=="DATE" &&
              result.fields[2].values==std::vector<std::string>{valid} &&
              result.fields[2].scope==EvidenceScope::Edition &&
              result.fields[2].date_meaning==DateMeaning::EditionRelease,
              "valid exact-edition DATE remains typed and lossless");
    }
    const auto enriched_release_json=std::string(R"MB({
      "id":"11111111-1111-1111-1111-111111111111",
      "title":"Album With More Than Two Tags",
      "artist-credit":[{"name":"Album Performer"}],
      "date":"2026-10-20",
      "barcode":"0001234567895",
      "label-info":[
        {"catalog-number":"001-A",
         "label":{"id":"22222222-2222-2222-2222-222222222222","name":"Label Alpha"}},
        {"catalog-number":"001-B","label":{"name":"Label Beta"}},
        {"catalog-number":"001-A","label":{"name":"Label Alpha"}},
        {"catalog-number":null,"label":null}
      ],
      "media":[{"position":1,"tracks":[{"position":1,
          "title":"Track (Original Version)","length":180000,
          "artist-credit":[{"name":"Album Performer"}]}]}]
    })MB");
    const auto enriched=parse_release_lookup(enriched_release_json,first);
    check(enriched.fields.size()==6 &&
          enriched.fields[3].field=="LABEL" &&
          enriched.fields[3].values==
              std::vector<std::string>({"Label Alpha","Label Beta"}) &&
          enriched.fields[4].field=="CATALOGNUMBER" &&
          enriched.fields[4].values==
              std::vector<std::string>({"001-A","001-B"}) &&
          enriched.fields[5].field=="BARCODE" &&
          enriched.fields[5].values==
              std::vector<std::string>({"0001234567895"}) &&
          enriched.fields[3].scope==EvidenceScope::Edition &&
          enriched.fields[3].source_id==first &&
          enriched.tracks.size()==1,
          "verified edition loads label, catalog and barcode multivalues with exact provenance");
    const auto missing_label=parse_release_lookup(R"MB({
      "id":"11111111-1111-1111-1111-111111111111",
      "title":"No Label", "artist-credit":[{"name":"Artist"}],
      "label-info":null, "barcode":"invalid-barcode",
      "media":[{"tracks":[{"title":"A","artist-credit":[{"name":"Artist"}]}]}]
    })MB",first);
    check(missing_label.fields.size()==2 &&
          missing_label.fields[0].field=="ALBUM" &&
          missing_label.fields[1].field=="ALBUM ARTIST",
          "unknown label and invalid barcode do not create invented tag evidence");
    rejects([&]{(void)parse_release_lookup(R"MB({
      "id":"11111111-1111-1111-1111-111111111111",
      "label-info":[{"catalog-number":"Unsafe\nCatalog"}],
      "media":[{"tracks":[{"title":"A","artist-credit":[{"name":"Artist"}]}]}]
    })MB",first);},"control-bearing catalogue identity rejected at exact release boundary");

    // Official release track IDs are distinct from recording IDs. A recording
    // may recur on an edition without becoming a duplicate release-track ID.
    const auto identified_json=std::string(R"MB({
      "id":"11111111-1111-1111-1111-111111111111",
      "media":[{"tracks":[
        {"id":"33333333-3333-3333-3333-333333333333","position":1,
         "title":"One","artist-credit":[{"name":"Artist"}],
         "recording":{"id":"22222222-2222-2222-2222-222222222222"}},
        {"id":"44444444-4444-4444-4444-444444444444","position":2,
         "title":"Two","artist-credit":[{"name":"Artist"}],
         "recording":{"id":"22222222-2222-2222-2222-222222222222"}}
      ]}]
    })MB");
    const auto identified=parse_release_lookup(identified_json,first);
    const auto duplicate_track_id=std::string(R"({
        "id":"11111111-1111-1111-1111-111111111111",
        "media":[{"tracks":[
          {"id":"33333333-3333-3333-3333-333333333333",
           "title":"First","artist-credit":[{"name":"Artist"}]},
          {"id":"33333333-3333-3333-3333-333333333333",
           "title":"Second","artist-credit":[{"name":"Artist"}]}
        ]}]
    })");
    rejects([&]{(void)parse_release_lookup(duplicate_track_id,first);},
            "duplicate release-track MBID is ambiguous and cannot identify two tracks");
    const auto forged_track_id=std::string(R"({
        "id":"11111111-1111-1111-1111-111111111111",
        "media":[{"tracks":[
          {"id":"../release/foreign",
           "title":"First","artist-credit":[{"name":"Artist"}]}
        ]}]
    })");
    rejects([&]{(void)parse_release_lookup(forged_track_id,first);},
            "invalid provider release-track MBID rejected without path normalization");
    const auto invalid_isrc_release=std::string(R"({
        "id":"11111111-1111-1111-1111-111111111111",
        "media":[{"tracks":[
          {"title":"First","artist-credit":[{"name":"Artist"}],
           "recording":{"isrcs":["NOT/AN/ISRC"]}}
        ]}]
    })");
    const auto no_unverified_isrc=parse_release_lookup(invalid_isrc_release,first);
    check(no_unverified_isrc.tracks.size()==1 &&
          no_unverified_isrc.tracks[0].recording.isrc.empty(),
          "invalid optional provider ISRC never participates in edition-track matching");
    check(identified.tracks.size()==2 &&
          identified.tracks[0].source_track_id==
              "33333333-3333-3333-3333-333333333333" &&
          identified.tracks[1].source_track_id==
              "44444444-4444-4444-4444-444444444444",
          "release track IDs remain distinct despite shared recording MBID");
    check(identified.tracks[0].fields.size()==2 &&
          identified.tracks[0].fields[0].field=="TITLE" &&
          identified.tracks[0].fields[0].values==std::vector<std::string>{"One"} &&
          identified.tracks[0].fields[1].field=="ARTIST" &&
          identified.tracks[0].fields[1].values==std::vector<std::string>{"Artist"} &&
          identified.tracks[0].fields[0].provider=="musicbrainz" &&
          identified.tracks[0].fields[0].source_id==identified.tracks[0].source_track_id &&
          identified.tracks[0].fields[0].scope==EvidenceScope::Edition &&
          identified.tracks[1].fields[0].source_id==identified.tracks[1].source_track_id,
          "exact TITLE/ARTIST evidence belongs to each distinct release-track occurrence");
    check(edition.tracks[0].fields.empty() &&
          compilation.tracks[1].fields.empty() &&
          enriched.tracks[0].fields.empty(),
          "missing release-track IDs do not synthesize edition-scoped track fields");
    const auto recording_fallback=parse_release_lookup(R"MB({
      "id":"11111111-1111-1111-1111-111111111111",
      "media":[{"tracks":[{
        "id":"33333333-3333-3333-3333-333333333333",
        "recording":{"title":"Recording-only title",
          "artist-credit":[{"name":"Recording-only artist"}]}
      }]}]
    })MB",first);
    check(recording_fallback.tracks.size()==1 &&
          recording_fallback.tracks[0].recording.title=="Recording-only title" &&
          recording_fallback.tracks[0].recording.primary_artist=="Recording-only artist" &&
          recording_fallback.tracks[0].fields.empty(),
          "recording fallback assists matching but never impersonates edition-track fields");
    std::vector<LocalReleaseTrack> repeated_recording(2);
    repeated_recording[0].source_index=0;
    repeated_recording[0].recording.title="One";
    repeated_recording[0].recording.primary_artist="Artist";
    repeated_recording[0].disc_number=1;
    repeated_recording[0].track_number=1;
    repeated_recording[1].source_index=1;
    repeated_recording[1].recording.title="Two";
    repeated_recording[1].recording.primary_artist="Artist";
    repeated_recording[1].disc_number=1;
    repeated_recording[1].track_number=2;
    const auto repeated_alignment=align_release(repeated_recording,identified);
    check(repeated_alignment.matched==2 && repeated_alignment.unmatched==0,
          "repeated recording MBID does not invalidate separate release tracks");

    rejects([&]{(void)parse_release_lookup(edition_json,second);},
            "mismatched edition ID is a blocker");
    rejects([]{(void)JsonParser("{\"x\":1,\"x\":2}").parse();},
            "ambiguous duplicate JSON keys rejected");
    rejects([]{(void)JsonParser("\"\\uD800\"").parse();},
            "unpaired surrogate rejected");
    rejects([]{(void)JsonParser("{\"recordings\":[1,]}").parse();},
            "trailing comma rejected");
    rejects([]{(void)parse_search(
        R"({"releases":[{"id":"evil/traversal","title":"T","artist-credit":[{"name":"A"}]}]})",
        SearchKind::Recording);},
        "wrong official resource schema rejected");
    rejects([]{(void)JsonParser(std::string(2u*1024u*1024u+1,'x')).parse();},
            "oversized JSON response rejected");
    const auto cue=inspect_cue_metadata(
        "TITLE \"Album Edition\"\nPERFORMER \"Example Artist\"\n"
        "FILE \"disc.flac\" WAVE\nTRACK 01 AUDIO\nTITLE \"First (Extended Mix)\"\n"
        "PERFORMER \"Example Artist\"\nTRACK 02 AUDIO\nTITLE Second\n"
        "TRACK 03 AUDIO\nTITLE Third\nPERFORMER \"Guest Artist\"\n",
        CueCarrierKind::ExternalText);
    const auto local=prepare_cue_release_lookup(cue);
    check(local.eligible,"qualified synthetic CUE release");
    const auto assigned=align_release(local.tracks,edition);
    check(assigned.matched==3 && assigned.unmatched==0,
          "real provider release edition feeds existing whole-CUE matching engine");


    // A search-hit TITLE is NOT a substitute for the existing ALBUM field;
    // release artist credit is album-artist evidence, not recording ARTIST.
    const MetadataDocument source_tags{{
        {"TITLE", {"Track (Extended Mix)"}},
        {"ARTIST", {"Track Performer"}},
        {"ALBUM", {"Existing Album"}},
        {"ALBUM ARTIST", {"Album Group", "Featured Artist"}},
        {"GENRE", {"Trance", "Progressive"}},
        {"X_PRIVATE_FIELD", {"Preserve exact custom value"}}
    }};
    const auto untouched_tags=source_tags;
    const auto release_hits=review_musicbrainz_search_hit_fields(
        source_tags,first,true,"New Album","Album Group");
    check(release_hits.size()==2 &&
          release_hits[0].field=="ALBUM" &&
          release_hits[0].original_values==std::vector<std::string>{"Existing Album"} &&
          release_hits[0].candidate.values==std::vector<std::string>{"New Album"} &&
          release_hits[0].candidate.scope==EvidenceScope::Edition &&
          release_hits[0].state==FieldReviewState::NeedsReview &&
          release_hits[1].field=="ALBUM ARTIST" &&
          release_hits[1].original_values==
              std::vector<std::string>({"Album Group","Featured Artist"}) &&
          release_hits[1].state==FieldReviewState::NeedsReview,
          "release search maps ALBUM and multivalue ALBUM ARTIST without track aliasing");
    const auto recording_hits=review_musicbrainz_search_hit_fields(
        source_tags,second,false,"Track (Extended Mix)","Track Performer");
    check(recording_hits.size()==2 &&
          recording_hits[0].field=="TITLE" &&
          recording_hits[0].state==FieldReviewState::Unchanged &&
          recording_hits[0].candidate.scope==EvidenceScope::Recording &&
          recording_hits[1].field=="ARTIST" &&
          recording_hits[1].state==FieldReviewState::Unchanged,
          "recording search maps only TITLE and ARTIST and detects exact no-op");
    const auto no_remote_artist=review_musicbrainz_search_hit_fields(
        source_tags,first,true,"Existing Album","");
    check(no_remote_artist.size()==1 &&
          no_remote_artist[0].field=="ALBUM" &&
          no_remote_artist[0].state==FieldReviewState::Unchanged &&
          source_tags==untouched_tags,
          "provider-absent artist never proposes a deletion and originals are immutable");
    const auto no_local_album=review_musicbrainz_search_hit_fields(
        MetadataDocument{{{"TITLE",{"Existing Track"}}}},
        first,true,"New Album","");
    check(no_local_album.size()==1 &&
          no_local_album[0].field=="ALBUM" &&
          no_local_album[0].original_values.empty() &&
          no_local_album[0].reason=="musicbrainz_live_search_new_field_proposal",
          "missing local ALBUM is not falsely compared against TITLE");


    const auto edition_date=musicbrainz_search_edition_date(first,"2026-10-21");
    check(edition_date.has_value() &&
          edition_date->candidate.date_meaning==DateMeaning::EditionRelease &&
          edition_date->candidate.scope==EvidenceScope::Edition &&
          edition_date->field=="EDITION DATE (read-only)" &&
          edition_date->candidate.field!="DATE" &&
          edition_date->reason=="musicbrainz_live_search_edition_date_display_only",
          "search snippet date has typed edition meaning, never a physical DATE proposal");
    check(musicbrainz_search_edition_date(first,"2026-10").has_value() &&
          musicbrainz_search_edition_date(first,"2026").has_value() &&
          !musicbrainz_search_edition_date(first,"2026-10-21-22") &&
          !musicbrainz_search_edition_date(first,"2026/10/21") &&
          !musicbrainz_search_edition_date(first,"") &&
          !musicbrainz_search_edition_date("","2026-10-21"),
          "only bounded, explicitly sourced MusicBrainz edition dates appear as evidence");

    // Malformed calendar facts are not evidence, even when the remote
    // search response otherwise carries a well-formed edition MBID.
    for(const char* invalid : {
        "0000", "2026-00", "2026-13", "2026-00-01",
        "2026-02-29", "2024-02-30", "2026-04-31",
        "1900-02-29", "2026-01-00", "2026-12-32"})
        check(!musicbrainz_search_edition_date(first,invalid),
              "invalid partial/full calendar dates cannot become MB edition evidence");
    for(const char* valid : {
        "0001", "2026", "2026-02", "2026-04-30",
        "2024-02-29", "2000-02-29", "1900-02-28"})
        check(musicbrainz_search_edition_date(first,valid).has_value(),
              "valid partial dates and Gregorian leap days remain available");


    // MusicBrainz's documented offset/count allow a bounded explicit
    // next-page action without confusing an unrelated recording or edition.
    const auto first_page=make_search_path(
        SearchKind::Recording,"What Time Is Love","The KLF");
    const auto second_page=musicbrainz_search_page_path(first_page,2);
    const auto third_page=musicbrainz_search_page_path(first_page,3);
    const auto fourth_page=musicbrainz_search_page_path(first_page,4);
    check(second_page==first_page+"&offset=25" &&
          third_page==first_page+"&offset=50" &&
          fourth_page==first_page+"&offset=75" &&
          qualified_musicbrainz_request_path(second_page) &&
          qualified_musicbrainz_request_path(third_page) &&
          qualified_musicbrainz_request_path(fourth_page),
          "native fixed-host allowlist admits only bounded official search pages");
    for(const auto& malicious:{
        first_page+"&offset=0",first_page+"&offset=100",
        first_page+"&offset=25&offset=50",
        first_page+"&offset=25&token=secret",
        first_page+"&offset=-25",first_page+"&offset=025",
        first_page+"&offset=25#evil"})
        check(!qualified_musicbrainz_request_path(malicious),
              "offset injection and extra query parameters are rejected");
    rejects([&]{(void)musicbrainz_search_page_path(first_page,1);},
            "page one must use original path");
    rejects([&]{(void)musicbrainz_search_page_path(first_page,5);},
            "unbounded page five is blocked");
    rejects([&]{(void)musicbrainz_search_page_path(second_page,3);},
            "cannot paginate a previously paged URL");
    rejects([&]{(void)musicbrainz_search_page_path(
                make_release_lookup_path(first),2);},
            "exact edition lookup cannot be paged as a search");
    std::string page_json="{\"count\":75,\"offset\":0,\"recordings\":[";
    for(int i=0;i<25;++i) {
        if(i)page_json+=",";
        const std::string last=(i<10?"0":"")+std::to_string(i);
        page_json+="{\"id\":\"11111111-1111-1111-1111-1111111111"+
            last+"\",\"title\":\"Synthetic Recording\",\"artist-credit\":[{\"name\":\"Synthetic Artist\"}]}";
    }
    page_json+="]}";
    const auto parsed_page=parse_search(page_json,SearchKind::Recording);
    check(parsed_page.candidates.size()==25 &&
          parsed_page.page_items==25 &&
          parsed_page.total_count==75 && parsed_page.offset==0 &&
          musicbrainz_has_next_page(parsed_page,1) &&
          !musicbrainz_has_next_page(parsed_page,2),
          "only exact first-page offset/count permits explicit next page");
    auto page_two=page_json;
    const auto off=page_two.find("\"offset\":0");
    check(off!=std::string::npos,"fixture offset present");
    page_two.replace(off,10,"\"offset\":25");
    const auto parsed_two=parse_search(page_two,SearchKind::Recording);
    check(musicbrainz_has_next_page(parsed_two,2) &&
          !musicbrainz_has_next_page(parsed_two,1),
          "second-page metadata is source-position bound");
    auto page_three=page_two;
    const auto off_two=page_three.find("\"offset\":25");
    check(off_two!=std::string::npos,"second fixture offset present");
    page_three.replace(off_two,11,"\"offset\":50");
    check(!musicbrainz_has_next_page(
              parse_search(page_three,SearchKind::Recording),3),
          "last populated official page stops pagination");
    auto missing_count=page_json;
    const auto count_pos=missing_count.find("\"count\":75,");
    check(count_pos!=std::string::npos,"fixture count present");
    missing_count.erase(count_pos,11);
    check(!musicbrainz_has_next_page(
              parse_search(missing_count,SearchKind::Recording),1),
          "missing count cannot authorize a speculative next-page request");
    auto truncated=page_json;
    truncated.replace(truncated.find("\"count\":75"),10,"\"count\":100000000");
    check(!musicbrainz_has_next_page(
              parse_search(truncated,SearchKind::Recording),1),
          "unbounded provider count cannot drive navigation");
    // Cross-provider physical batch eligibility is independent of
    // online exact-release matching and never inspects source metadata.
    const auto two_physical=review_physical_album_batch_scope(2,
        {{0,true,true,false},{1,true,true,false}},
        "musicbrainz",first);
    check(two_physical.eligible && two_physical.rows.empty(),
          "two captured physical files are eligible for exact album alignment");
    const auto mixed=review_physical_album_batch_scope(3,
        {{0,true,true,false},{1,true,false,false},{2,false,true,true}},
        "musicbrainz",first);
    check(!mixed.eligible && mixed.rows.size()==3 &&
          mixed.rows[0].field=="Batch / MATCH" &&
          online_evidence_scope_label(mixed.rows[0].candidate.scope)=="exact edition" &&
          mixed.rows[0].state==FieldReviewState::Blocked &&
          mixed.rows[1].reason=="album_batch_scope_VIRTUAL_SUBSONG" &&
          mixed.rows[2].reason=="album_batch_scope_EXTERNAL_CUE_CARRIER" &&
          mixed.rows[2].candidate.provider=="musicbrainz" &&
          mixed.rows[2].candidate.field=="MATCH_DIAGNOSTIC",
          "mixed physical/CUE selection produces bounded, nonstageable source reasons");
    check(mixed.rows[0].candidate.provider=="musicbrainz" &&
          mixed.rows[0].reason=="album_batch_scope_UNQUALIFIED_SELECTION" &&
          mixed.rows[1].candidate.field=="MATCH_DIAGNOSTIC",
          "scope explanation is local evidence, not an online tag suggestion");
    const auto oversized=review_physical_album_batch_scope(
        54129,{},"musicbrainz",first);
    check(!oversized.eligible && oversized.rows.size()==1 &&
          oversized.rows[0].reason=="album_batch_scope_SELECTION_TOO_LARGE",
          "54k-file selection is rejected without source enumeration");
    rejects([&]{(void)review_physical_album_batch_scope(
        2,{{1,true,true,false},{0,true,true,false}},"musicbrainz",first);},
        "reordered captured source identity must not be interpreted as a visible row");
    rejects([&]{(void)review_physical_album_batch_scope(
        2,{{0,true,true,false}},"musicbrainz",first);},
        "missing source coverage fails closed");
    rejects([&]{(void)review_physical_album_batch_scope(
        129,{{0,true,true,false}},"musicbrainz",first);},
        "oversized scope refuses partial source inspection");
    std::cout << "PASS: official MusicBrainz query encoding, bounded JSON, artist roles and release alignment\n";
}
