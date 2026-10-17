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
          {"position":"1-03","title":"Complex position"}
        ]
    })DJ";
    const auto edition=parse_release(sample,321);
    check(edition.provider=="discogs" && edition.edition_id=="321" &&
          edition.tracks.size()==3 &&
          edition.tracks[0].source_track_id=="A1" &&
          edition.tracks[0].track_number==-1 &&
          edition.tracks[0].recording.primary_artist=="Primary Artist" &&
          edition.tracks[1].track_number==2 &&
          edition.tracks[1].recording.primary_artist=="Guest" &&
          edition.tracks[2].track_number==-1,
          "preserve side/position and direct artist provenance without inventing track numbers");
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
    rejects([&]{(void)parse_release(sample,322);},
            "reject substituted release identity before comparison or write approval");
    rejects([]{(void)parse_release(R"({"id":1,"title":"No tracklist"})",1);},
            "do not pretend missing release tracklist is a match");
    check(std::string(attribution).find("not affiliated")!=std::string::npos &&
          data_attribution=="Data provided by Discogs.",
          "required Discogs provider-attribution strings are present");
    std::cout<<"PASS: strictly authenticated Discogs read-only URL, result, release, provenance and safety contracts\n";
}
