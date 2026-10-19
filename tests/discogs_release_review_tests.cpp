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
    std::cout<<"Discogs read-only physical edition review PASS\n";
}
