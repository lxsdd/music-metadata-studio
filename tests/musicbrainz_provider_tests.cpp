#include "djmeta/musicbrainz_provider.h"
#include "djmeta/cue_online_bridge.h"

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
    check(request_spacing_wait_ms(0,100)==0 &&
          request_spacing_wait_ms(100,100)==1200 &&
          request_spacing_wait_ms(100,101)==1199 &&
          request_spacing_wait_ms(100,1299)==1 &&
          request_spacing_wait_ms(100,1300)==0 &&
          request_spacing_wait_ms(100,1600)==0 &&
          request_spacing_wait_ms(500,200)==1200,
          "MusicBrainz worker reserves a bounded, cancelable request slot");
    const auto request=make_search_path(SearchKind::Recording,
        "Björk: It's \"Alive\" & 東京", "Artist / Guest");
    check(request.starts_with("/ws/2/recording?query=") &&
          request.find("fmt=json&limit=8") != std::string::npos &&
          request.find("Björk") == std::string::npos &&
          request.find("%22") != std::string::npos &&
          request.find("%26") != std::string::npos &&
          request.find("%E6%9D%B1%E4%BA%AC") != std::string::npos,
          "recording search encoded, Lucene quoted, bounded and UTF-8");
    check(make_search_path(SearchKind::Release,"Album","").find("/ws/2/release?")==0,
          "release search without unknown artist is permitted");
    check(make_release_lookup_path(first)==std::string("/ws/2/release/")+first+
          "?inc=recordings%2Bartist-credits%2Bisrcs&fmt=json",
          "release lookup only uses MBID to prevent URL injection");
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
    check(search_title_core("Track (Unknown Remixer Club Dub)")==
              "Track (Unknown Remixer Club Dub)" &&
          search_title_core("Track (Extended Mix)")=="Track",
          "unknown remix preserved, generic version only stripped for search");
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
    check(search_title_core("Title [Artist Rework]")=="Title" &&
          search_title_core("Title [Extended Mix]")=="Title" &&
          search_title_core("Title [Live at the Arena]")==
              "Title [Live at the Arena]" &&
          search_title_core("Title (Unknown Remixer Club Dub)")==
              "Title (Unknown Remixer Club Dub)",
          "known bracket/version suffix is discovery-only; unknown qualifiers survive");
    const auto collab=recording_search_plan(
        "1000 Years (Dj Version)","Kyau & Albert X Maywave");
    check(collab.core_title=="1000 Years" &&
          collab.full_title=="1000 Years (Dj Version)" &&
          collab.attempts.size()==6 &&
          collab.attempts[0].path==make_search_path(
              SearchKind::Recording,"1000 Years","Kyau & Albert X Maywave") &&
          collab.attempts[2].path==make_search_path(
              SearchKind::Recording,"1000 Years","Kyau & Albert") &&
          collab.attempts[4].path==make_search_path(
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
    const auto by_isrc=recording_search_plan("Track (Radio Edit)","Artist","usrc17607839");
    check(by_isrc.attempts.size()==5 &&
          by_isrc.attempts[0].label=="Exact ISRC identifier" &&
          by_isrc.attempts[0].path.find("isrc%3AUSRC17607839")!=std::string::npos,
          "valid ISRC prioritized ahead of title-only fallback");
    check(!valid_search_isrc("../bad-1234") &&
          recording_search_plan("Track","Artist","../bad-1234").attempts.size()==2,
          "invalid ISRC never submitted; query count bounded");
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
    check(recording_identity_label("What Time Is Love (Original Version)","The KLF",
          "What Time Is Love?","The KLF")=="Version review" &&
          recording_identity_label("What Time Is Love (Original Version)","The KLF",
          "What Time Is Love?","Other Artist")=="Artist differs" &&
          recording_identity_label("What Time Is Love?","The KLF",
          "What Time Is Love?","The KLF")=="Title+artist",
          "artist/version conflicts cannot be treated as an approved identity");
    const std::string recording_json=R"MB({
       "created":"2026-10-09","recording-count":2,
       "recordings":[
         {"id":"11111111-1111-1111-1111-111111111111",
          "title":"Bj\u00f6rk & \u6771\u4eac",
          "score":99,"length":215540,
          "artist-credit":[{"name":"Alpha","joinphrase":" feat. "},{"name":"Beta"}],
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
          found.candidates[1].search_score==55,
          "official recording response preserves Unicode, artist roles, duration, score");
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
    check(identified.tracks.size()==2 &&
          identified.tracks[0].source_track_id==
              "33333333-3333-3333-3333-333333333333" &&
          identified.tracks[1].source_track_id==
              "44444444-4444-4444-4444-444444444444",
          "release track IDs remain distinct despite shared recording MBID");
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

    std::cout << "PASS: official MusicBrainz query encoding, bounded JSON, artist roles and release alignment\n";
}
