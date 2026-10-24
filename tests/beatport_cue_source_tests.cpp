#include "djmeta/beatport_release_review.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main() {
    using namespace djmeta::online;
    const std::string bundle =
        "@beatport-release-json\n"
        R"({"id":987,"name":"Home EP","track_count":1})" "\n"
        "@beatport-tracks-json\n"
        R"({"count":1,"results":[{"id":321,"name":"Home",
        "mix_name":"Extended Mix","number":1,
        "artists":[{"name":"DJ Example"}],"length_ms":402500,
        "release":{"id":987,"name":"Home EP"}}]})";
    const auto edition=parse_beatport_user_release_clipboard(bundle);
    const auto cue=djmeta::inspect_cue_metadata(
        "TITLE \"Home EP\"\nREM GENRE House\nREM GENRE Trance\n"
        "REM CUSTOM Keep\nFILE \"source.flac\" WAVE\n"
        "TRACK 01 AUDIO\nTITLE \"Home (Extended Mix)\"\n"
        "PERFORMER \"DJ Example\"\nINDEX 01 00:00:00\n",
        djmeta::CueCarrierKind::EmbeddedText);
    if(cue.status!=djmeta::CueSyntaxStatus::Parsed) return 1;
    const auto rows=review_beatport_user_edition_cue(cue,edition);
    std::size_t repeated=0,originals=0;
    bool has_file=false,has_track=false;
    for(const auto& row:rows) {
        if(!is_local_cue_inventory(row)) continue;
        ++originals;
        if(row.field=="CUE Original / Album / REM GENRE") ++repeated;
        if(row.field=="CUE Original / FILE #1 / FILE") has_file=true;
        if(row.field.find("CUE Original / Track 1")!=std::string::npos)
            has_track=true;
        if(!row.candidate.values.empty() ||
           row.candidate.source_id.find("cue-line:")!=0 ||
           row.state!=FieldReviewState::Unchanged) return 2;
    }
    if(repeated!=2 || originals<6 || !has_file || !has_track) return 3;
    // Repeated existing global GENRE must not be flattened or replaced
    // solely because a provider suggested a new release genre.
    for(const auto& row:rows)
        if(row.field=="Album / REM GENRE" &&
           (row.state!=FieldReviewState::Blocked ||
            row.original_values!=std::vector<std::string>{"House","Trance"}))
            return 6;

    const auto incomplete=djmeta::inspect_cue_metadata(
        "TITLE \"Home EP\"\nREM CUSTOM Keep\n"
        "FILE \"source.flac\" WAVE\nTRACK 01 AUDIO\n"
        "INDEX 01 00:00:00\n",djmeta::CueCarrierKind::ExternalText);
    if(incomplete.status!=djmeta::CueSyntaxStatus::Parsed) return 4;
    const auto blocked=review_beatport_user_edition_cue(incomplete,edition);
    bool original_seen=false,blocked_album=false,blocked_track=false;
    for(const auto& row:blocked) {
        if(row.field=="CUE Original / Album / REM CUSTOM" &&
           is_local_cue_inventory(row)) original_seen=true;
        if(row.field=="Album / TITLE" &&
           row.state==FieldReviewState::Blocked) blocked_album=true;
        if(row.field=="Edition Track 1 / TITLE" &&
           row.state==FieldReviewState::Blocked) blocked_track=true;
    }
    if(!original_seen || !blocked_album || !blocked_track) return 5;
    std::cout<<"Beatport CUE original source and fail-closed review PASS\n";
}
