#include "djmeta/track_review.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

void check(bool good,const char* msg){
    if(!good){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}
}
djmeta::MetadataDiffRow diff(std::size_t i,const char* field,djmeta::SafetyClass safety){
    djmeta::MetadataDiffRow item;
    item.source_index=i;item.field=field;item.original=" old ";item.proposed="new";
    item.safety=safety;item.rule_ids="safe.trim";return item;
}
int main(){
    using namespace djmeta;
    const std::vector<MetadataDiffRow> proposals={
        diff(0,"DISCOGS_RELEASE_CREDITS",SafetyClass::Safe),
        diff(0,"TITLE",SafetyClass::Review),
        diff(0,"DATE_RAW",SafetyClass::Confident),
        diff(1,"ARTIST",SafetyClass::Safe),
        diff(1,"X_CUSTOM_FIELD",SafetyClass::Review),
        diff(1,"DISCOGS_ARTIST_PROFILE",SafetyClass::Safe)
    };
    check(is_music_metadata_field("artist") &&
          is_music_metadata_field("REMIXED BY") &&
          is_music_metadata_field("date_raw") &&
          !is_music_metadata_field("DISCOGS_RELEASE_NOTES") &&
          !is_music_metadata_field("X_CUSTOM_FIELD"),
          "default music/extended scope classification");
    const auto all=summarize_track_changes(3,proposals);
    check(all.size()==3 && all[0].music_changes==2 &&
          all[0].extended_changes==1 && all[0].review_required==1 &&
          all[1].music_changes==1 && all[1].extended_changes==2 &&
          all[1].review_required==1 && all[2].music_changes==0,
          "one summary per selected track including unchanged");
    const auto music=selected_track_diffs(proposals,0,MetadataFocus::Music);
    const auto extended=selected_track_diffs(proposals,1,MetadataFocus::Extended);
    const auto both=selected_track_diffs(proposals,1,MetadataFocus::All);
    check(music.size()==2 && music[0].field=="TITLE" &&
          music[0].safety==SafetyClass::Review &&
          extended.size()==2 && both.size()==3,
          "focus must only filter presentation and preserve provenance");
    // A source-local index must return exactly the same proposal identities,
    // provenance, focus filtering and original ordering as the flat scan.
    const auto source_index=index_track_diffs(proposals,3);
    check(source_index.offsets.size()==4 &&
          source_index.row_positions.size()==proposals.size(),
          "review index bounds are proportional to track and diff counts");
    for(std::size_t source=0;source<3;++source)
        for(const auto focus : {MetadataFocus::Music,MetadataFocus::Extended,
                                MetadataFocus::All}) {
            const auto legacy=selected_track_diffs(proposals,source,focus);
            const auto indexed=selected_track_diffs(proposals,source_index,source,focus);
            check(legacy.size()==indexed.size(),
                  "indexed track focus matches flat semantic filter");
            for(std::size_t j=0;j<legacy.size();++j)
                check(legacy[j].source_index==indexed[j].source_index &&
                      legacy[j].field==indexed[j].field &&
                      legacy[j].rule_ids==indexed[j].rule_ids &&
                      legacy[j].proposal_index==indexed[j].proposal_index,
                      "indexed diff retains proposal identity and rule provenance");
        }
    const std::vector<MetadataDiffRow> shuffled={
        diff(1,"COMMENT",SafetyClass::Review),
        diff(0,"TITLE",SafetyClass::Safe),
        diff(1,"ARTIST",SafetyClass::Confident),
        diff(90,"UNQUALIFIED",SafetyClass::Review)};
    const auto interleaved=index_track_diffs(shuffled,2);
    const auto source_one=selected_track_diffs(
        shuffled,interleaved,1,MetadataFocus::All);
    check(source_one.size()==2 && source_one[0].field=="COMMENT" &&
          source_one[1].field=="ARTIST" &&
          interleaved.row_positions.size()==3 &&
          selected_track_diffs(shuffled,interleaved,2,MetadataFocus::All).empty(),
          "interleaved source rows stay stable; out-of-range sources never leak");

    // 54,129-source fixture: this selection visits exactly its own 3 rows
    // after one O(total) index build, independent of other sources' proposals.
    constexpr std::size_t kLargeBatch=54129;
    std::vector<MetadataDiffRow> many;
    many.reserve(kLargeBatch+2);
    for(std::size_t source=0;source<kLargeBatch;++source)
        many.push_back(diff(source,"TITLE",SafetyClass::Safe));
    many.push_back(diff(29017,"X_CUSTOM_FIELD",SafetyClass::Review));
    many.push_back(diff(29017,"ARTIST",SafetyClass::Confident));
    const auto big_index=index_track_diffs(many,kLargeBatch);
    const auto selected=selected_track_diffs(
        many,big_index,29017,MetadataFocus::All);
    const auto selected_music=selected_track_diffs(
        many,big_index,29017,MetadataFocus::Music);
    check(big_index.offsets.size()==kLargeBatch+1 &&
          big_index.row_positions.size()==many.size() &&
          big_index.offsets[29018]-big_index.offsets[29017]==3 &&
          selected.size()==3 && selected_music.size()==2 &&
          selected[0].field=="TITLE" &&
          selected[1].field=="X_CUSTOM_FIELD" &&
          selected[2].field=="ARTIST",
          "54k-track lookup touches only selected source and keeps field order");
    bool stale_index_rejected=false;
    try {
        const auto shorter=std::vector<MetadataDiffRow>(many.begin(),many.end()-1);
        (void)selected_track_diffs(shorter,big_index,29017,MetadataFocus::All);
    } catch(const std::invalid_argument&) { stale_index_rejected=true; }
    check(stale_index_rejected,"snapshot length change invalidates review index");
    const auto original_source=many[29017].source_index;
    many[29017].source_index=0;
    stale_index_rejected=false;
    try {
        (void)selected_track_diffs(many,big_index,29017,MetadataFocus::All);
    } catch(const std::invalid_argument&) { stale_index_rejected=true; }
    many[29017].source_index=original_source;
    check(stale_index_rejected,"same-size row reassignment cannot cross source identities");
    check(index_track_diffs({},0).offsets.size()==1 &&
          selected_track_diffs({},index_track_diffs({},0),0,
                               MetadataFocus::All).empty(),
          "zero-track review snapshot is valid and empty");
    bool overflow_rejected=false;
    try {
        (void)index_track_diffs({},(std::numeric_limits<std::size_t>::max)());
    } catch(const std::length_error&) { overflow_rejected=true; }
    check(overflow_rejected,"overflow-sized review index refuses allocation");

    // Decision changes alter Pending/Review only for an affected source.
    // Immutable music/extended/SAFE counts and other sources remain intact.
    AnalysisResult analysis;
    analysis.proposals.resize(4);
    analysis.proposals[0].safety=SafetyClass::Review;
    analysis.proposals[1].safety=SafetyClass::Confident;
    analysis.proposals[2].safety=SafetyClass::Review;
    analysis.proposals[3].safety=SafetyClass::Review;
    std::vector<ReviewDecision> decisions(4);
    check(count_pending_review_proposals(analysis,decisions)==3,
          "initial Pending/Review count uses captured proposal classes");
    decisions[0].action=ReviewAction::Accept;
    decisions[3].action=ReviewAction::Reject;
    check(count_pending_review_proposals(analysis,decisions)==1,
          "Accept and Reject remove only their own Pending/Review counters");
    decisions[2].action=ReviewAction::ManualValue;
    check(count_pending_review_proposals(analysis,decisions)==0,
          "manual replacement removes final pending semantic review");
    decisions[0].action=ReviewAction::Pending;
    check(count_pending_review_proposals(analysis,decisions)==1,
          "Reset restores pending review on the correct proposal");
    bool invalid_count_rejected=false;
    decisions.pop_back();
    try {
        (void)count_pending_review_proposals(analysis,decisions);
    } catch(const std::invalid_argument&) { invalid_count_rejected=true; }
    check(invalid_count_rejected,
          "decision count mismatch cannot silently falsify track status");
    const std::vector<std::string> names={"Z-track","A-track","B-track"};
    const auto by_name=sort_track_summaries(all,names,0,false);
    const auto by_review=sort_track_summaries(all,names,3,true);
    check((by_name==std::vector<std::size_t>{1,2,0}) &&
          (by_review==std::vector<std::size_t>{0,1,2}),
          "stable master row sort and index mapping");
    // The native grid may reuse a filename order after preview decisions,
    // because those decisions change only counts, never source identity.
    auto modified_summaries=all;
    modified_summaries[2].review_required=100;
    modified_summaries[2].music_changes=100;
    const auto by_name_after_decision=sort_track_summaries(
        modified_summaries,names,0,false);
    const auto by_name_desc=sort_track_summaries(all,names,0,true);
    const auto by_name_desc_after_decision=sort_track_summaries(
        modified_summaries,names,0,true);
    const auto by_review_after_decision=sort_track_summaries(
        modified_summaries,names,3,true);
    check(by_name_after_decision==by_name &&
          by_name_desc==std::vector<std::size_t>({0,2,1}) &&
          by_name_desc_after_decision==by_name_desc &&
          by_review_after_decision!=by_review,
          "immutable source filename order can be cached; dynamic review count cannot");
    const std::vector<std::string> tied_names={"same","same","z"};
    const auto tied_ascending=sort_track_summaries(all,tied_names,0,false);
    const auto tied_descending=sort_track_summaries(all,tied_names,0,true);
    check(tied_ascending==std::vector<std::size_t>({0,1,2}) &&
          tied_descending==std::vector<std::size_t>({2,0,1}),
          "ascending and descending filename caches keep stable equal-name source order");
    const auto all_tracks=filter_track_view(all,by_name,TrackDiscovery::All);
    const auto changed_tracks=filter_track_view(all,by_name,TrackDiscovery::Changed);
    const auto review_tracks=filter_track_view(all,by_name,TrackDiscovery::NeedsReview);
    check(all_tracks==by_name &&
          (changed_tracks==std::vector<std::size_t>{1,0}) &&
          (review_tracks==std::vector<std::size_t>{1,0}) &&
          filter_track_view(all,{2},TrackDiscovery::NeedsReview).empty() &&
          filter_track_view(all,{999,1},TrackDiscovery::Changed)==
            std::vector<std::size_t>{1},
          "discovery retains sort identity and handles empty/invalid views");
    check(proposals[0].field=="DISCOGS_RELEASE_CREDITS" &&
          proposals[4].field=="X_CUSTOM_FIELD",
          "no mutation or loss of secondary metadata");
    std::cout<<"PASS: indexed master/detail summaries, music/extended filters and stable source IDs\n";
}
