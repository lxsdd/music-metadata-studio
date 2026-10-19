#include "djmeta/discogs_provider.h"

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
    check(edition_first.size()==7 &&
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
    check(track_search_plan("Home","DJ Example",{},"CAT 007/A").size()==5,
          "a single catalog number participates without any barcode guess");
    rejects([]{(void)edition_identifier_search_path("ISRC","USABC2601234");},
            "Discogs API does not support a fabricated ISRC search filter");
    rejects([]{(void)edition_identifier_search_path("barcode","x\r\nAuthorization: bad");},
            "barcode search forbids header injection");
    rejects([]{(void)edition_identifier_search_path("catno",std::string(151,'x'));},
            "catalog identifiers are bounded");

    const auto tagged_album=track_search_plan(
        "Only You (Extended Mix)","Matt Fax",{},{},"Contrast EP");
    check(tagged_album.size()==6 &&
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
    rejects([]{(void)track_search_plan("Valid",{}, {}, {}, "bad\nheader");},
            "malformed album hint cannot cross the native provider boundary");
    rejects([]{(void)release_title_search_path(std::string(151,'x'));},
            "album release title search is bounded");

    const auto staged=track_search_plan(
        "1000 Years (DJ Version)", "Kyau & Albert x Maywave");
    check(staged.size()==5 &&
          staged[0].path==search_path("1000 Years (DJ Version)",
                                     "Kyau & Albert x Maywave") &&
          staged[1].path.find("track=1000%20Years%20%28DJ%20Version%29")!=
              std::string::npos &&
          staged[2].path==track_search_path("1000 Years (DJ Version)") &&
          staged[3].path==track_search_path("1000 Years") &&
          staged[4].path==search_path("1000 Years") &&
          staged[0].label!=staged[2].label,
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
    const auto simple_plan=track_search_plan("Via Balearia","Estiva");
    check(simple_plan.size()==4 &&
          simple_plan[2].path==track_search_path("Via Balearia") &&
          simple_plan[3].path==search_path("Via Balearia"),
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
    rejects([]{(void)track_search_plan("", "Artist");},
            "staged query requires a valid track title");

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
          !allowed_readonly_path("/database/search?q=X&type=master&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?q=Hi%0D%0ABad&type=release&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?q=Hi&type=release&token=secret&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?q=Hi&type=release&page=2&per_page=20") &&
          !allowed_readonly_path("/database/search?track=Hi&type=release&page=1&per_page=200") &&
          !allowed_readonly_path("/database/search?barcode=123&type=release&artist=Any&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?catno=XYZ&type=release&token=secret&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?isrc=USABC2601234&type=release&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?release_title=EP&type=release&token=secret&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?release_title=EP&type=release&artist=Any&page=2&per_page=20") &&
          !allowed_readonly_path("/database/search?release_title=EP%0D&type=release&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?barcode=%30%30&type=release&page=1&per_page=20") &&
          !allowed_readonly_path("/database/search?barcode=0%0A&type=release&page=1&per_page=20") &&
          !allowed_readonly_path("/releases/0") &&
          !allowed_readonly_path("/releases/42/notes") &&
          !allowed_readonly_path("/releases/042"),
          "native Discogs host rejects foreign endpoints, tokens, control escapes and extra pages");
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
          edition.tracks.size()==3 &&
          edition.tracks[0].source_track_id=="A1" &&
          edition.tracks[0].track_number==-1 &&
          edition.tracks[0].recording.duration_ms==242000 &&
          edition.tracks[0].recording.primary_artist=="Primary Artist" &&
          edition.tracks[1].track_number==2 &&
          edition.tracks[1].recording.primary_artist=="Guest" &&
          edition.tracks[2].track_number==-1,
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
    rejects([]{(void)parse_release(R"({
        "id":2,"title":"Medley only","tracklist":[
            {"type_":"index","title":"Medley",
             "sub_tracks":[{"type_":"track","title":"Subtrack"}]},
            {"type_":"heading","title":"Side A"}]
    })",2);}, "nested index and headings cannot masquerade as track rows");
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
