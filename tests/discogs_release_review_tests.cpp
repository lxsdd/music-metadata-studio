#include "djmeta/discogs_release_review.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok, const char* message) {
    if(!ok) {std::cerr<<"FAIL: "<<message<<"\n"; std::exit(1);}
}
const djmeta::online::FieldReviewRow* field(
    const std::vector<djmeta::online::FieldReviewRow>& rows,
    const std::string& name) {
    const auto it=std::find_if(rows.begin(),rows.end(),
        [&](const auto& row){return row.field==name;});
    return it==rows.end()?nullptr:&*it;
}
}

int main() {
    using namespace djmeta::online;
    using namespace djmeta::online::discogs;
    djmeta::MetadataDocument originals;
    originals.fields.push_back({"GENRE",{"House","Deep House"}});
    originals.fields.push_back({"REPLAYGAIN_TRACK_GAIN",{"-4.50 dB"}});
    originals.fields.push_back({"MY CUSTOM TAG",{"Original","Other"}});
    originals.fields.push_back({"TITLE",{"My Physical Track"}});
    auto release=parse_release(R"JSON({
      "id":4242,"title":"Exact edition","year":2024,
      "genres":["House","Trance"],
      "labels":[{"name":"Label A","catno":"0012"}],
      "tracklist":[{"position":"1","title":"Track One (Extended Mix)",
                    "extraartists":[{"name":"Producer A","role":"Remix"}]},
                   {"position":"2","title":"Track Two"}]
    })JSON",4242);
    const auto rows=review_discogs_physical_edition(originals,release);
    const auto* genre=field(rows,"Album / GENRE");
    require(genre && genre->candidate.values==
        std::vector<std::string>({"House","Trance"}) &&
        genre->original_values==std::vector<std::string>({"House","Deep House"}) &&
        genre->state==FieldReviewState::NeedsReview &&
        genre->candidate.source_id=="4242",
        "edition GENRE preserves multivalues and exact provider identity");
    const auto* custom=field(rows,"Album / MY CUSTOM TAG");
    const auto* gain=field(rows,"Album / REPLAYGAIN_TRACK_GAIN");
    require(custom && gain &&
        custom->original_values==std::vector<std::string>({"Original","Other"}) &&
        gain->original_values==std::vector<std::string>({"-4.50 dB"}) &&
        custom->candidate.values.empty() && gain->candidate.values.empty() &&
        custom->state==FieldReviewState::Unchanged &&
        gain->state==FieldReviewState::Unchanged &&
        custom->reason=="discogs_local_only_no_provider_value",
        "original-only custom/multivalue and ReplayGain fields are kept, not deleted");
    const auto* remix=field(rows,"Release Track 1 / REMIXER");
    const auto* title=field(rows,"Release Track 2 / TITLE");
    require(remix && title && remix->state==FieldReviewState::Blocked &&
        title->state==FieldReviewState::Blocked &&
        remix->candidate.source_id=="4242/track/1" &&
        title->candidate.source_id=="4242/track/2",
        "physical file cannot inherit arbitrary Discogs track credits");
    auto spoofed=release;
    spoofed.fields.front().provider="musicbrainz";
    bool refused=false;
    try{(void)review_discogs_physical_edition(originals,spoofed);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,"cross-provider edition substitution refused");
    spoofed=release;
    spoofed.tracks[0].fields.front().source_id="another-release/track/1";
    const auto tainted=review_discogs_physical_edition(originals,spoofed);
    const auto* bad=field(tainted,"Release Track 1 / TITLE");
    require(bad && bad->state==FieldReviewState::Blocked &&
        bad->candidate.values.empty() &&
        bad->reason=="discogs_live_release_unverified_track_provenance",
        "tainted track provenance cannot expose falsely verified tag candidates");
    require(originals.fields[0].values==std::vector<std::string>({"House","Deep House"}) &&
        originals.fields[1].values==std::vector<std::string>({"-4.50 dB"}),
        "comparison is read-only and leaves originals unchanged");

    const auto exact=parse_release(R"JSON({
      "id":4243,"title":"Exact EP","artists":[{"name":"Primary Artist"}],
      "tracklist":[
        {"position":"1","title":"Track One (Extended Mix)","duration":"4:02",
         "extraartists":[{"name":"DJ One","role":"Remix"}]},
        {"position":"2","title":"Track Two (Extended Mix)","duration":"4:02",
         "extraartists":[{"name":"Other DJ","role":"Remix"}]}
      ]
    })JSON",4243);
    djmeta::MetadataDocument physical;
    physical.fields.push_back({"TITLE",{"Track One (Extended Mix)"}});
    physical.fields.push_back({"ARTIST",{"Primary Artist"}});
    physical.fields.push_back({"TRACKNUMBER",{"1"}});
    physical.fields.push_back({"REPLAYGAIN_TRACK_GAIN",{"-4.5 dB"}});
    physical.fields.push_back({"CUSTOM",{"First","Second"}});
    const auto verified=review_discogs_physical_edition(physical,exact,242000);
    const auto* matched_title=field(verified,"Release Track 1 / TITLE");
    const auto* matched_credit=field(verified,"Release Track 1 / REMIXER");
    const auto* other_title=field(verified,"Release Track 2 / TITLE");
    require(matched_title && matched_title->state==FieldReviewState::Unchanged &&
            matched_title->reason=="discogs_live_release_verified_track_exact_noop" &&
            matched_credit && matched_credit->state==FieldReviewState::NeedsReview &&
            matched_credit->candidate.values==std::vector<std::string>{"DJ One"} &&
            matched_credit->candidate.source_id=="4243/track/1" &&
            other_title && other_title->state==FieldReviewState::Blocked,
            "qualified physical track compares only its exact edition row");
    require(field(verified,"Album / CUSTOM") &&
            field(verified,"Album / CUSTOM")->original_values==
                std::vector<std::string>({"First","Second"}) &&
            field(verified,"Album / REPLAYGAIN_TRACK_GAIN") &&
            physical.fields[3].values==std::vector<std::string>{"-4.5 dB"},
            "matching preserves original custom multivalues and ReplayGain");
    auto with_isrc=physical;
    with_isrc.fields.push_back({"ISRC",{"USABC2601234"}});
    const auto isrc_not_in_catalog=review_discogs_physical_edition(
        with_isrc,exact,242000);
    require(field(isrc_not_in_catalog,"Release Track 1 / REMIXER")->state==
                FieldReviewState::NeedsReview &&
            field(isrc_not_in_catalog,"Album / ISRC")->original_values==
                std::vector<std::string>{"USABC2601234"},
            "one-sided local ISRC is retained but not treated as Discogs evidence");
    const auto no_duration=review_discogs_physical_edition(physical,exact);
    require(field(no_duration,"Release Track 1 / REMIXER")->state==
                FieldReviewState::Blocked,
            "title, artist and position alone never verify a mix");
    auto wrong_mix=physical;
    wrong_mix.fields[0].values={"Track One (Radio Edit)"};
    const auto wrong=review_discogs_physical_edition(wrong_mix,exact,242000);
    require(field(wrong,"Release Track 1 / REMIXER")->state==
                FieldReviewState::Blocked,
            "wrong mix never inherits a Discogs remix credit");
    auto ambiguous=exact;
    ambiguous.tracks[1]=ambiguous.tracks[0];
    ambiguous.tracks[1].source_track_id="B1";
    ambiguous.tracks[1].track_number=-1;
    for(auto& item:ambiguous.tracks[1].fields)
        item.source_id="4243/track/2";
    ambiguous.tracks[0].source_track_id="A1";
    ambiguous.tracks[0].track_number=-1;
    auto no_position=physical;
    no_position.fields[2].values={"A1"};
    const auto tied=review_discogs_physical_edition(no_position,ambiguous,242000);
    require(field(tied,"Release Track 1 / REMIXER")->state==
                FieldReviewState::Blocked,
            "equal edition candidates cannot be arbitrarily assigned");
    auto duplicated=physical;
    duplicated.fields.push_back({"title",{"Track One (Extended Mix)"}});
    const auto duplicate=review_discogs_physical_edition(duplicated,exact,242000);
    require(field(duplicate,"Release Track 1 / REMIXER")->state==
                FieldReviewState::Blocked,
            "duplicate physical identity tags fail closed");
    auto tainted_edition=exact;
    tainted_edition.tracks[0].fields.front().source_id="4243/track/99";
    const auto unsafe=review_discogs_physical_edition(
        physical,tainted_edition,242000);
    require(field(unsafe,"Release Track 1 / REMIXER")->state==
                FieldReviewState::Blocked,
            "tainted track field provenance blocks all track comparisons");
    std::cout<<"Discogs read-only physical edition review PASS\n";
}
