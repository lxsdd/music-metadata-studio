#include "djmeta/discogs_release_review.h"
#include "djmeta/online_field_ui_selection.h"
#include "djmeta/online_field_staging.h"
#include "djmeta/native_candidate_labels.h"
#include "djmeta/discogs_search_hints.h"

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
    // Regression: even an unusable local inventory cannot prevent a
    // deliberately entered, canonical exact-edition URL. The comparison
    // still blocks accidental physical-track assignment and keeps originals.
    djmeta::MetadataDocument messy;
    messy.fields.push_back({"TITLE",{"A","B"}});
    messy.fields.push_back({"ARTIST",{"Somebody"}});
    messy.fields.push_back({"ARTIST",{"Another"}});
    messy.fields.push_back({"ALBUM",{"Version 1"}});
    messy.fields.push_back({"ALBUM",{"Version 2"}});
    messy.fields.push_back({"BARCODE",{"0001234567","0001234568"}});
    messy.fields.push_back({"CATALOGNUMBER",{"EX001"}});
    messy.fields.push_back({"MY CUSTOM",{"Keep","Both"}});
    messy.fields.push_back({"VERSION",{"Extended Mix","Radio Edit"}});
    auto hints=physical_search_hints(messy);
    require(hints.title.empty() && hints.artist.empty() &&
            hints.album.empty() && hints.barcode.empty() &&
            hints.catalog_number=="EX001" && hints.version.empty(),
            "ambiguous local optional hints abstain independently without throwing");
    const auto release_url=std::string(
        "https://www.discogs.com/release/38012673-Estiva-Via-Balearia");
    const auto expected_id=release_id_from_url(release_url);
    require(expected_id==38012673 &&
            allowed_readonly_path(release_path(expected_id)),
            "user-pasted problem release takes fixed official read-only API route");
    const auto example=parse_release(R"JSON({
        "id":38012673,"title":"Synthetic offline edition",
        "tracklist":[{"position":"1","title":"A","duration":"4:02"}]
    })JSON",expected_id);
    const auto read_only=review_discogs_physical_edition(messy,example,242000);
    require(field(read_only,"Physical / MY CUSTOM") &&
            field(read_only,"Physical / MY CUSTOM")->original_values==
                std::vector<std::string>({"Keep","Both"}) &&
            field(read_only,"Release Track 1 / MATCH") &&
            field(read_only,"Release Track 1 / MATCH")->state==FieldReviewState::Blocked &&
            !discogs_physical_identity(messy,242000),
            "offline exact-URL chain keeps local data but blocks ambiguous track match");
    djmeta::MetadataDocument separate_mix;
    separate_mix.fields={{"TITLE",{"1000 Years"}},
                         {"ARTIST",{"Kyau & Albert x Maywave"}},
                         {"VERSION",{"DJ Version"}}};
    hints=physical_search_hints(separate_mix);
    const auto stages=track_search_plan(
        hints.title,hints.artist,{}, {}, {},true,hints.version);
    require(hints.version=="DJ Version" && stages.size()==10 &&
            stages.front().path==track_search_path(
                "1000 Years (DJ Version)","Kyau & Albert x Maywave") &&
            stages[4].path==track_search_path("1000 Years","Maywave") &&
            stages[5].path==search_path("1000 Years","Maywave") &&
            allowed_readonly_path(stages.front().path) &&
            allowed_readonly_path(stages[4].path) &&
            allowed_readonly_path(stages[5].path),
            "qualified physical VERSION reaches fixed official mix-aware search");
    separate_mix.fields.push_back({"VERSION",{"Radio Edit"}});
    require(physical_search_hints(separate_mix).version.empty(),
            "conflicting VERSION fields cannot choose a remix arbitrarily");
    djmeta::MetadataDocument only_album;
    only_album.fields={{"TITLE",{"Bad\nTitle"}},{"ALBUM",{"Valid Album"}},
                       {"ARTIST",{"Artist"}}};
    hints=physical_search_hints(only_album);
    require(hints.title.empty() && hints.album=="Valid Album" &&
            hints.artist=="Artist" &&
            !track_search_plan(hints.title,hints.artist,{}, {},hints.album).empty(),
            "invalid track title cannot suppress qualified album fallback");
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
        genre->state==FieldReviewState::Blocked &&
        genre->reason.starts_with(
            "discogs_live_release_edition_source_unverified_") &&
        genre->candidate.source_id=="4242",
        "edition GENRE preserves multivalues and exact provider identity");
    const auto* custom=field(rows,"Physical / MY CUSTOM TAG");
    const auto* gain=field(rows,"Physical / REPLAYGAIN_TRACK_GAIN");
    require(custom && gain &&
        custom->original_values==std::vector<std::string>({"Original","Other"}) &&
        gain->original_values==std::vector<std::string>({"-4.50 dB"}) &&
        custom->candidate.values.empty() && gain->candidate.values.empty() &&
        custom->state==FieldReviewState::Unchanged &&
        gain->state==FieldReviewState::Unchanged &&
        custom->reason=="discogs_local_only_no_provider_value" &&
        custom->candidate.provider=="local-physical" &&
        custom->candidate.source_id=="physical-original" &&
        online_evidence_origin_label(*custom)=="Local physical" &&
        gain->candidate.provider=="local-physical" &&
        !field(rows,"Album / MY CUSTOM TAG"),
        "original custom/multivalue and ReplayGain keep local provenance");
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
    const auto* matched_album=field(verified,"Album / ALBUM");
    require(matched_album && matched_album->state==FieldReviewState::NeedsReview &&
            matched_album->candidate.provider=="discogs" &&
            matched_album->candidate.source_id=="4243" &&
            !matched_album->reason.starts_with(
                "discogs_live_release_edition_source_unverified_"),
            "a uniquely aligned physical recording may review its exact edition album");
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
    // End-to-end *detached* staging of unique verified track credits:
    // the other release track must NEVER be admitted by index or title.
    const auto permitted=physical_discogs_stageable_evidence(exact,verified);
    const auto verified_remixer=physical_discogs_stageable_field_index(
        exact,permitted,*matched_credit);
    const auto verified_title=physical_discogs_stageable_field_index(
        exact,permitted,*matched_title);
    require(verified_remixer.has_value() && verified_title.has_value() &&
            *verified_remixer>=exact.fields.size() &&
            !physical_discogs_stageable_field_index(exact,permitted,*other_title),
            "only uniquely verified edition-track credits appear in allowed staging");
    const auto snapshot=capture_online_field_review(physical,permitted,true,true);
    const auto staged_remixer=stage_selected_online_fields(
        snapshot,physical,{*verified_remixer});
    require(staged_remixer.explicitly_changed==1 &&
            staged_remixer.planned.fields.back()==
                djmeta::MetadataField{"REMIXER",{"DJ One"}} &&
            staged_remixer.planned.fields[3]==physical.fields[3] &&
            staged_remixer.planned.fields[4]==physical.fields[4] &&
            physical.fields.size()==5,
            "explicit verified REMIXER preview preserves ReplayGain and custom values");
    const auto one_noop=stage_selected_online_fields(
        snapshot,physical,{*verified_title});
    require(one_noop.exact_noops==1 && one_noop.explicitly_changed==0 &&
            one_noop.planned==physical,
            "identical verified TITLE cannot rewrite or stage a physical change");
    auto forged_track=*matched_credit;
    forged_track.candidate.source_id="4243/track/2";
    require(!physical_discogs_stageable_field_index(exact,permitted,forged_track),
            "cross-track recording credit cannot enter staging");
    forged_track=*matched_credit;
    forged_track.field="Release Track 2 / REMIXER";
    require(!physical_discogs_stageable_field_index(exact,permitted,forged_track),
            "visible track ordinal must match its provider field source");
    forged_track=*matched_credit;
    forged_track.reason="discogs_live_release_track_unassigned";
    require(!physical_discogs_stageable_field_index(exact,permitted,forged_track),
            "unassigned evidence cannot borrow a verified candidate value");

    require(field(verified,"Physical / CUSTOM") &&
            field(verified,"Physical / CUSTOM")->original_values==
                std::vector<std::string>({"First","Second"}) &&
            field(verified,"Physical / REPLAYGAIN_TRACK_GAIN") &&
            physical.fields[3].values==std::vector<std::string>{"-4.5 dB"},
            "matching preserves original custom multivalues and ReplayGain");
    const auto* diagnosis=field(verified,"Release Track 1 / MATCH");
    const auto* other_diagnosis=field(verified,"Release Track 2 / MATCH");
    require(diagnosis && diagnosis->candidate.field=="MATCH_DIAGNOSTIC" &&
            diagnosis->state==FieldReviewState::Unchanged &&
            diagnosis->reason=="discogs_live_match_suggested_readonly" &&
            !diagnosis->candidate.values.empty() &&
            other_diagnosis && other_diagnosis->state==FieldReviewState::Blocked &&
            other_diagnosis->candidate.source_id=="4243/track/2",
            "per-track native provenance and match evidence remain review-only");

    auto with_isrc=physical;
    with_isrc.fields.push_back({"ISRC",{"USABC2601234"}});
    const auto isrc_not_in_catalog=review_discogs_physical_edition(
        with_isrc,exact,242000);
    require(field(isrc_not_in_catalog,"Release Track 1 / REMIXER")->state==
                FieldReviewState::NeedsReview &&
            field(isrc_not_in_catalog,"Physical / ISRC")->original_values==
                std::vector<std::string>{"USABC2601234"},
            "one-sided local ISRC is retained but not treated as Discogs evidence");
    const auto no_duration=review_discogs_physical_edition(physical,exact);
    require(field(no_duration,"Release Track 1 / REMIXER")->state==
                FieldReviewState::Blocked &&
            field(no_duration,"Release Track 1 / MATCH")->reason==
                "discogs_live_match_duration_unavailable",
            "missing duration cannot verify a remix; rationale is visible");
    auto wrong_mix=physical;
    wrong_mix.fields[0].values={"Track One (Radio Edit)"};
    const auto wrong=review_discogs_physical_edition(wrong_mix,exact,242000);
    require(field(wrong,"Album / ALBUM") &&
            field(wrong,"Album / ALBUM")->state==FieldReviewState::Blocked,
            "wrong physical remix cannot propose even album-level edition tags");
    const auto* wrong_diagnosis=field(wrong,"Release Track 1 / MATCH");
    require(field(wrong,"Release Track 1 / REMIXER")->state==
                FieldReviewState::Blocked &&
            wrong_diagnosis && wrong_diagnosis->state==FieldReviewState::Blocked &&
            wrong_diagnosis->reason=="discogs_live_match_other_or_conflicting_track" &&
            !wrong_diagnosis->candidate.values.empty(),
            "wrong remix remains blocked with a concrete identity conflict");
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
                FieldReviewState::Blocked &&
            field(duplicate,"Release Track 1 / MATCH")->reason==
                "discogs_live_match_local_identity_ambiguous",
            "duplicate physical identity tags fail closed with rationale");
    auto tainted_edition=exact;
    tainted_edition.tracks[0].fields.front().source_id="4243/track/99";
    const auto unsafe=review_discogs_physical_edition(
        physical,tainted_edition,242000);
    require(field(unsafe,"Release Track 1 / MATCH")->reason==
                "discogs_live_match_track_provenance_unqualified" &&
            field(unsafe,"Release Track 1 / REMIXER")->state==
                FieldReviewState::Blocked,
            "tainted track field provenance blocks all track comparisons");
    const auto tainted_allowed=physical_discogs_stageable_evidence(
        tainted_edition,unsafe);
    require(tainted_allowed.size()==tainted_edition.fields.size() &&
            !physical_discogs_stageable_field_index(
                tainted_edition,tainted_allowed,
                *field(unsafe,"Release Track 1 / REMIXER")),
            "tainted source fields can never enter the physically stageable inventory");
    const auto* tainted_album=field(unsafe,"Album / ALBUM");
    require(tainted_album && tainted_album->state==FieldReviewState::Blocked &&
            tainted_album->reason.starts_with(
                "discogs_live_release_edition_source_unverified_"),
            "tainted matched track provenance also blocks edition-wide album changes");
    auto tainted_scope=exact;
    tainted_scope.tracks[0].fields.front().scope=EvidenceScope::Recording;
    const auto wrong_scope=review_discogs_physical_edition(
        physical,tainted_scope,242000);
    require(field(wrong_scope,"Album / ALBUM")->state==FieldReviewState::Blocked &&
            field(wrong_scope,"Release Track 1 / MATCH")->reason==
                "discogs_live_match_track_provenance_unqualified",
            "wrong matched track evidence scope cannot promote album fields");
    // Actual foobar owner-data display labels share these exact helpers
    // with portable release/track provenance and field comparison tests.
    auto discogs_group=*wrong_diagnosis;
    discogs_group.field="Release Track 1";
    discogs_group.reason="discogs_live_release_group";
    require(native_candidate_source_label(discogs_group)=="Discogs API" &&
            native_candidate_status_label(discogs_group)=="Unmatched",
            "unverified Discogs version is visibly unmatched with official API provenance");
    auto physical_group=*field(verified,"Physical / CUSTOM");
    physical_group.reason="local_physical_group";
    require(native_candidate_source_label(physical_group)=="Local physical" &&
            native_candidate_status_label(physical_group)=="Keep original",
            "custom multivalue physical source remains keep-original in native review");
    std::cout<<"Discogs read-only physical edition review PASS\n";
}
