#include "djmeta/track_review.h"

#include <algorithm>
#include <array>
#include <numeric>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace djmeta {
namespace {
int compare_folded(std::string_view a, std::string_view b) {
    const auto common = (std::min)(a.size(), b.size());
    for (std::size_t i=0; i<common; ++i) {
        unsigned char x = static_cast<unsigned char>(a[i]);
        unsigned char y = static_cast<unsigned char>(b[i]);
        if (x>='A' && x<='Z') x+=32;
        if (y>='A' && y<='Z') y+=32;
        if (x!=y) return x<y ? -1 : 1;
    }
    if (a.size()==b.size()) return 0;
    return a.size()<b.size() ? -1 : 1;
}
std::string upper_ascii(std::string_view input) {
    std::string out(input);
    for (char& ch:out) if (ch>='a' && ch<='z') ch=static_cast<char>(ch-'a'+'A');
    return out;
}
bool matches_track_focus(const MetadataDiffRow& row, MetadataFocus focus) {
    const bool music=is_music_metadata_field(row.field);
    return focus==MetadataFocus::All ||
        (focus==MetadataFocus::Music && music) ||
        (focus==MetadataFocus::Extended && !music);
}
}
bool is_music_metadata_field(std::string_view name) {
    // High-value DJ/foobar tag fields. Do not change engine eligibility;
    // this list is only the default UI focus, and is versioned separately.
    static constexpr std::array<std::string_view,31> fields = {
        "ALBUM", "ALBUM ARTIST", "ALBUMARTIST", "ARTIST", "BPM", "CATALOGNUMBER",
        "COMMENT", "COMPILATION", "COPYRIGHT", "DATE", "DATE_RAW", "DISCNUMBER",
        "DISCTOTAL", "GENRE", "ISRC", "LABEL", "MIX", "MIXED BY",
        "ORIGINAL ARTIST", "ORIGINAL TITLE", "PUBLISHER", "REMIX", "REMIXED BY",
        "REMIXER", "SONGWRITER", "STYLE", "TITLE", "TOTALDISCS", "TOTALTRACKS",
        "TRACKNUMBER", "VERSION"
    };
    const auto folded = upper_ascii(name);
    return std::find(fields.begin(),fields.end(),folded)!=fields.end();
}
std::vector<TrackReviewSummary> summarize_track_changes(
    std::size_t count, const std::vector<MetadataDiffRow>& proposals) {
    std::vector<TrackReviewSummary> result(count);
    for (std::size_t i=0;i<count;++i) result[i].source_index=i;
    for (const auto& proposal : proposals) {
        if (proposal.source_index >= count) continue; // invalid projection not displayed
        auto& item=result[proposal.source_index];
        if(is_music_metadata_field(proposal.field)) ++item.music_changes;
        else ++item.extended_changes;
        switch(proposal.safety) {
            case SafetyClass::Safe: ++item.safe_changes; break;
            case SafetyClass::Confident: ++item.confident_changes; break;
            case SafetyClass::Review: ++item.review_required; break;
        }
    }
    return result;
}
TrackReviewProgress summarize_track_review_progress(
    const AnalysisResult& analysis,
    const std::vector<ReviewDecision>& decisions) {
    if(analysis.proposals.size()!=decisions.size())
        throw std::invalid_argument("Review decision count does not match captured proposals.");
    TrackReviewProgress result;
    for(std::size_t i=0;i<analysis.proposals.size();++i) {
        const auto& proposal=analysis.proposals[i];
        const auto& decision=decisions[i];
        switch(decision.action) {
            case ReviewAction::Pending:
                ++result.pending_proposals;
                if(proposal.safety==SafetyClass::Review) ++result.pending_review;
                break;
            case ReviewAction::Reject: break;
            case ReviewAction::Accept:
                if(proposal.proposed_value!=proposal.original_value)
                    ++result.staged_changes;
                break;
            case ReviewAction::ManualValue:
                if(decision.manual_value!=proposal.original_value)
                    ++result.staged_changes;
                break;
            default: throw std::invalid_argument("Unknown review decision.");
        }
    }
    return result;
}

std::size_t count_pending_review_proposals(
    const AnalysisResult& analysis,
    const std::vector<ReviewDecision>& decisions) {
    if (analysis.proposals.size()!=decisions.size())
        throw std::invalid_argument(
            "Review decision count does not match captured proposals.");
    std::size_t pending=0;
    for(std::size_t i=0;i<analysis.proposals.size();++i)
        if(analysis.proposals[i].safety==SafetyClass::Review &&
           decisions[i].action==ReviewAction::Pending)
            ++pending;
    return pending;
}
TrackDiffIndex index_track_diffs(
    const std::vector<MetadataDiffRow>& proposals, std::size_t track_count) {
    if(track_count==(std::numeric_limits<std::size_t>::max)())
        throw std::length_error("Track diff index count cannot overflow.");
    TrackDiffIndex result;
    result.captured_row_count=proposals.size();
    result.offsets.assign(track_count+1,0);
    for(const auto& row:proposals)
        if(row.source_index<track_count)
            ++result.offsets[row.source_index+1];
    for(std::size_t i=1;i<result.offsets.size();++i)
        result.offsets[i]+=result.offsets[i-1];
    result.row_positions.resize(result.offsets.back());
    auto cursors=result.offsets;
    for(std::size_t i=0;i<proposals.size();++i) {
        const auto source=proposals[i].source_index;
        if(source<track_count)
            result.row_positions[cursors[source]++]=i;
    }
    return result;
}
std::vector<MetadataDiffRow> selected_track_diffs(
    const std::vector<MetadataDiffRow>& all, std::size_t index,
    MetadataFocus focus) {
    std::vector<MetadataDiffRow> rows;
    for(const auto& change:all)
        if(change.source_index==index && matches_track_focus(change,focus))
            rows.push_back(change);
    return rows;
}
std::vector<MetadataDiffRow> selected_track_diffs(
    const std::vector<MetadataDiffRow>& all, const TrackDiffIndex& index,
    std::size_t source_index, MetadataFocus focus) {
    if(index.offsets.empty() || index.captured_row_count!=all.size() ||
       index.offsets.back()!=index.row_positions.size())
        throw std::invalid_argument("Track diff index does not match snapshot.");
    if(source_index>=index.offsets.size()-1) return {};
    const auto begin=index.offsets[source_index];
    const auto end=index.offsets[source_index+1];
    if(end<begin || end>index.row_positions.size())
        throw std::invalid_argument("Track diff index range is invalid.");
    std::vector<MetadataDiffRow> rows;
    rows.reserve(end-begin);
    for(std::size_t i=begin;i<end;++i) {
        const auto position=index.row_positions[i];
        if(position>=all.size() || all[position].source_index!=source_index)
            throw std::invalid_argument("Track diff index source identity changed.");
        if(matches_track_focus(all[position],focus))
            rows.push_back(all[position]);
    }
    return rows;
}
std::vector<std::size_t> filter_track_view(
    const std::vector<TrackReviewSummary>& summaries,
    const std::vector<std::size_t>& sorted_source_indices,
    TrackDiscovery discovery) {
    std::vector<std::size_t> visible;
    visible.reserve(sorted_source_indices.size());
    for (const auto index : sorted_source_indices) {
        if (index >= summaries.size()) continue;
        const auto& track = summaries[index];
        const bool changed = track.music_changes + track.extended_changes > 0;
        bool include=false;
        switch(discovery) {
            case TrackDiscovery::All: include=true; break;
            case TrackDiscovery::Changed: include=changed; break;
            case TrackDiscovery::NeedsReview: include=track.review_required>0; break;
            case TrackDiscovery::PendingAny: include=track.pending_proposals>0; break;
            case TrackDiscovery::StagedEdits: include=track.staged_changes>0; break;
            case TrackDiscovery::Reviewed:
                include=changed && track.pending_proposals==0; break;
            case TrackDiscovery::Unchanged: include=!changed; break;
            default: throw std::invalid_argument("Unknown track review filter.");
        }
        if(include) visible.push_back(index);
    }
    return visible;
}

std::vector<std::size_t> sort_track_summaries(
    const std::vector<TrackReviewSummary>& rows,
    const std::vector<std::string>& labels, int col, bool desc) {
    std::vector<std::size_t> view(rows.size());
    std::iota(view.begin(),view.end(),std::size_t{0});
    if(col<0 || col>3) return view;
    std::stable_sort(view.begin(),view.end(),[&](std::size_t a,std::size_t b){
        int comparison=0;
        if(col==0) {
            const std::string_view left=rows[a].source_index<labels.size()
                 ? std::string_view(labels[rows[a].source_index]):std::string_view{};
            const std::string_view right=rows[b].source_index<labels.size()
                 ? std::string_view(labels[rows[b].source_index]):std::string_view{};
            comparison=compare_folded(left,right);
        } else {
            const auto value=[&](const TrackReviewSummary& row) -> std::size_t {
                return col==1?row.music_changes:
                       col==2?row.extended_changes:row.review_required;
            };
            if(value(rows[a])<value(rows[b])) comparison=-1;
            if(value(rows[a])>value(rows[b])) comparison=1;
        }
        return desc?comparison>0:comparison<0;
    });
    return view;
}
} // namespace djmeta
