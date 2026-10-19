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
    // User can explicitly type facts visible on a lawful Beatport track
    // page. No developer key, JSON response, scraping or host file write.
    const std::string fact_text =
        "@beatport-track-facts\n"
        "@provider=beatport\n"
        "@id=track:321\n"
        "@scope=recording\n"
        "TITLE=Home\n"
        "ARTIST=DJ Example\n"
        "VERSION=Extended Mix\n"
        "DURATION=6:42\n"
        "REMIXER=Mixer Q\n"
        "ISRC=GBXYZ2601234\n"
        "GENRE=Progressive House\n"
        "BPM=128\n"
        "KEY=Gm\n"
        "RELEASE_ID=987\n"
        "ALBUM=Home EP\n"
        "LABEL=Demo Label\n"
        "DATE@edition=2026-10-16\n";
    check(is_beatport_user_facts_clipboard(fact_text) &&
          !is_beatport_user_facts_clipboard(raw),
          "factual Beatport header is explicit and separate from API JSON");
    const auto facts=parse_beatport_user_facts_clipboard(fact_text);
    check(facts.id==321 && facts.release_id==987 &&
          facts.title=="Home" && facts.mix_name=="Extended Mix" &&
          facts.duration_ms==402000 && facts.bpm==128 &&
          facts.musical_key=="Gm" &&
          facts.label_name=="Demo Label" &&
          facts.release_date=="2026-10-16",
          "typed Beatport facts retain exact track/mix/edition field identities");
    const auto fact_review=review_beatport_user_track(original,402500,facts);
    check(fact_review.identity==MatchDecision::Suggested &&
          find(fact_review.fields,"GENRE")->state==FieldReviewState::NeedsReview &&
          find(fact_review.fields,"DATE")->candidate.date_meaning==
              DateMeaning::EditionRelease &&
          find(fact_review.fields,"MYTAG")->state==FieldReviewState::Unchanged,
          "typed visible Beatport facts reach qualified, read-only complete physical review");
    const auto wrong_fact_review=review_beatport_user_track(
        wrong,402500,facts);
    check(wrong_fact_review.identity==MatchDecision::Rejected &&
          find(wrong_fact_review.fields,"GENRE")->state==FieldReviewState::Blocked,
          "same Beatport facts never override a conflicting mix");
    const auto fact_change=[&](std::string old, std::string replacement) {
        auto edited=fact_text;
        const auto pos=edited.find(old);
        check(pos!=std::string::npos,"fixture replacement exists");
        edited.replace(pos,old.size(),replacement);
        return edited;
    };
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("VERSION=Extended Mix\n",""));},
        "missing mix identity blocks plain Beatport facts");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("DURATION=6:42","DURATION=6:99"));},
        "incorrect seconds never produce synthetic duration");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("DURATION=6:42","DURATION=bad"));},
        "free-text guessed duration is rejected");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("@id=track:321","@id=track:00321"));},
        "noncanonical track ID cannot masquerade as exact Beatport identity");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("@provider=beatport","@provider=discogs"));},
        "Beatport fact importer cannot reuse foreign-provider evidence");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("@scope=recording","@scope=edition"));},
        "Beatport track facts must remain recording-scoped");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("BPM=128","BPM=900"));},
        "unsafe BPM is never silently normalized");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("DATE@edition=2026-10-16","DATE@edition=2026-02-30"));},
        "unqualified calendar dates do not become release dates");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("KEY=Gm","SURPRISE=arbitrary"));},
        "unrecognized Beatport fact fields fail closed");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("VERSION=Extended Mix\n",
                    "VERSION=Extended Mix\nVERSION=Radio Edit\n"));},
        "ambiguous supplied versions do not produce track suggestions");
    const std::string crlf_facts="@beatport-track-facts\r\n"+
        fact_text.substr(fact_text.find('\n')+1);
    check(parse_beatport_user_facts_clipboard(crlf_facts).id==321,
          "Windows CRLF Beatport fact marker accepted without changing evidence");

    const std::string windows="@beatport-track-json\r\n"+
        raw.substr(raw.find('\n')+1);
    check(parse_beatport_user_json_clipboard(windows).id==321,
          "Windows CRLF clipboard marker accepted");
    check(original.fields[4].values==std::vector<std::string>{"-5.4 dB"},
          "no media metadata writes or mutation");
    std::cout<<"Beatport user clipboard read-only workflow PASS\n";
}
