#include "djmeta/beatport_manual_intake.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool yes,const char* msg) {
    if(!yes) {std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}
}
template<typename F> void rejects(F run,const char* msg) {
    bool refused=false;
    try{run();}catch(const std::invalid_argument&){refused=true;}
    check(refused,msg);
}
const djmeta::online::FieldReviewRow* find(
    const std::vector<djmeta::online::FieldReviewRow>& rows,
    const std::string& field) {
    const auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& row){
        return row.field==field;
    });
    return it==rows.end()?nullptr:&*it;
}
}
int main() {
    using namespace djmeta::online;
    const std::string raw=R"JSON(@beatport-track-json
{"id":321,"name":"Home","mix_name":"Extended Mix",
 "artists":[{"name":"DJ Example"}],
 "remixers":[{"name":"Mixer Q"}],"length_ms":402500,
 "isrc":"GBXYZ2601234","genre":{"name":"Progressive House"},
 "new_release_date":"2026-10-16","release":{"id":987,"name":"Home EP"}}
)JSON";
    check(is_beatport_user_json_clipboard(raw) &&
          !is_beatport_user_json_clipboard(R"({"id":321})"),
          "only explicitly selected user JSON is imported");
    const auto track=parse_beatport_user_json_clipboard(raw);
    check(track.id==321 && track.release_id==987 &&
          track.mix_name=="Extended Mix" && track.duration_ms==402500,
          "manual structured DJ recording and edition identity retained");
    djmeta::MetadataDocument original;
    original.fields.push_back({"TITLE",{"Home (Extended Mix)"}});
    original.fields.push_back({"ARTIST",{"DJ Example"}});
    original.fields.push_back({"ISRC",{"GBXYZ2601234"}});
    original.fields.push_back({"GENRE",{"Trance","Progressive House"}});
    original.fields.push_back({"REPLAYGAIN_TRACK_GAIN",{"-5.4 dB"}});
    original.fields.push_back({"MYTAG",{"One","Two"}});
    const auto verified=review_beatport_user_track(original,402500,track);
    check(verified.identity==MatchDecision::Suggested,
          "complete mix, title, artist, ISRC and host duration allow manual review");
    const auto* genre=find(verified.fields,"GENRE");
    const auto* date=find(verified.fields,"DATE");
    const auto* remixer=find(verified.fields,"REMIXER");
    check(genre && genre->state==FieldReviewState::NeedsReview &&
          genre->original_values==std::vector<std::string>({"Trance","Progressive House"}) &&
          genre->candidate.values==std::vector<std::string>({"Progressive House"}) &&
          genre->reason.starts_with("manual_beatport_readonly_") &&
          date && date->candidate.date_meaning==DateMeaning::EditionRelease &&
          date->candidate.source_id=="987" &&
          remixer && remixer->candidate.values==std::vector<std::string>{"Mixer Q"} &&
          online_evidence_origin_label(*remixer)=="Pasted: beatport",
          "Beatport import is marked user-supplied, full-field and version-aware");
    const auto* gain=find(verified.fields,"REPLAYGAIN_TRACK_GAIN");
    const auto* custom=find(verified.fields,"MYTAG");
    check(gain && gain->state==FieldReviewState::Unchanged &&
          gain->candidate.values.empty() && custom &&
          custom->original_values==std::vector<std::string>({"One","Two"}),
          "unknown tags and ReplayGain preserved without proposed deletions");
    auto wrong=original;
    wrong.fields[0].values={"Home (Radio Edit)"};
    const auto blocked=review_beatport_user_track(wrong,402500,track);
    check(blocked.identity==MatchDecision::Rejected &&
          find(blocked.fields,"GENRE")->state==FieldReviewState::Blocked &&
          find(blocked.fields,"DATE")->state==FieldReviewState::Blocked,
          "wrong mix blocks all imported recording and edition proposals");
    auto wrong_artist=original;
    wrong_artist.fields[1].values={"Another DJ"};
    check(review_beatport_user_track(wrong_artist,402500,track).identity==
          MatchDecision::Rejected,"wrong artist cannot suggest Beatport metadata");
    auto missing=original;
    missing.fields[0].values={"Home"};
    check(review_beatport_user_track(missing,402500,track).identity!=
          MatchDecision::Suggested,
          "unverified explicit mix stays blocked despite same base title");
    auto dup=original;
    dup.fields.push_back({"title",{"Home (Extended Mix)"}});
    const auto duplicated=review_beatport_user_track(dup,402500,track);
    check(duplicated.identity==MatchDecision::Rejected &&
          find(duplicated.fields,"GENRE")->state==FieldReviewState::Blocked,
          "ambiguous physical identity does not silently select one TITLE");
    rejects([]{(void)parse_beatport_user_json_clipboard(R"({"id":3})");},
            "missing explicit manual import header");
    rejects([]{(void)parse_beatport_user_json_clipboard("@beatport-track-json\n{}");},
            "missing Beatport track ID and title");
    rejects([]{(void)parse_beatport_user_json_clipboard(
        "@beatport-track-json\n{\"id\":321,\"name\":\"Home\"}");},
        "incomplete artist/duration cannot masquerade as qualified Beatport track");
    rejects([]{(void)parse_beatport_user_json_clipboard(
        "@beatport-track-json\n{\"id\":321,\"name\":\"Bad\\nControl\","
        "\"artists\":[{\"name\":\"DJ\"}],\"length_ms\":402500}");},
        "control-bearing track title rejected");
    rejects([]{(void)parse_beatport_user_json_clipboard(
        std::string("@beatport-track-json\n")+std::string(65536,'x'));},
        "oversized user JSON rejected before parsing");
    const std::string windows="@beatport-track-json\r\n"+
        raw.substr(raw.find('\n')+1);
    check(parse_beatport_user_json_clipboard(windows).id==321,
          "Windows CRLF clipboard marker accepted");
    check(original.fields[4].values==std::vector<std::string>{"-5.4 dB"},
          "no media metadata writes or mutation");
    std::cout<<"Beatport user clipboard read-only workflow PASS\n";
}
