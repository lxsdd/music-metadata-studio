#pragma once
// User-visible, read-only reason why an explicitly captured foobar selection
// cannot be matched as one physical release. This is a scope gate, NOT a
// second edition matcher, a provider match, or approval to stage fields.
#include "djmeta/online_fields.h"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta::online {
struct PhysicalAlbumBatchSourceScope {
    std::size_t source_index=0; // captured source ordinal, never sorted UI row
    bool physical_qualified=false;
    bool whole_file=false;      // foobar subsong == 0, not virtual CUE
    bool external_cue_locator=false;
};
struct PhysicalAlbumBatchScopeReview {
    bool eligible=false;
    std::vector<FieldReviewRow> rows; // empty for a qualified selection
};
inline PhysicalAlbumBatchScopeReview review_physical_album_batch_scope(
    std::size_t selected_count,
    const std::vector<PhysicalAlbumBatchSourceScope>& captured,
    std::string_view provider,std::string_view edition_id) {
    if((provider!="discogs" && provider!="musicbrainz") ||
       edition_id.empty() || edition_id.size()>512)
        throw std::invalid_argument("Invalid physical album scope provenance");
    PhysicalAlbumBatchScopeReview out;
    if(selected_count<=1) {
        if(!captured.empty()) throw std::invalid_argument("Single-source album scope");
        return out;
    }
    if(selected_count>128 && !captured.empty())
        throw std::invalid_argument("Oversized album scope must not enumerate sources");
    if(selected_count<=128 && captured.size()!=selected_count)
        throw std::invalid_argument("Album scope missing captured sources");
    auto diagnostic=[&](std::string field,std::string reason,
                        std::string description) {
        FieldReviewRow row;
        row.field=std::move(field);
        row.candidate.field="MATCH_DIAGNOSTIC";
        row.candidate.provider=std::string(provider);
        row.candidate.source_id=std::string(edition_id);
        row.candidate.scope=EvidenceScope::Edition;
        row.candidate.values={std::move(description)};
        row.state=FieldReviewState::Blocked;
        row.reason="album_batch_scope_"+reason;
        out.rows.push_back(std::move(row));
    };
    if(selected_count>128) {
        diagnostic("Batch / MATCH","SELECTION_TOO_LARGE",
            "Album selection has "+std::to_string(selected_count)+
            " sources; choose 2-128 physical files for one exact-edition match. "
            "No library scan or bulk approval.");
        return out;
    }
    std::size_t blocked=0;
    for(std::size_t i=0;i<captured.size();++i) {
        const auto& source=captured[i];
        if(source.source_index!=i)
            throw std::invalid_argument("Album scope source ordinal changed");
        std::string reason;
        if(source.external_cue_locator) reason="EXTERNAL_CUE_CARRIER";
        else if(!source.whole_file) reason="VIRTUAL_SUBSONG";
        else if(!source.physical_qualified) reason="NOT_PHYSICAL";
        if(!reason.empty()) {
            ++blocked;
            diagnostic("Batch / Source #"+std::to_string(i+1),reason,
                "Captured source #"+std::to_string(i+1)+": "+reason+
                "; compare this source separately. No tag/CUE changes.");
        }
    }
    if(blocked) {
        // Make the batch failure visible even with collapsed detail groups.
        out.rows.insert(out.rows.begin(),FieldReviewRow{});
        auto& summary=out.rows.front();
        summary.field="Batch / MATCH";
        summary.candidate.field="MATCH_DIAGNOSTIC";
        summary.candidate.provider=std::string(provider);
        summary.candidate.source_id=std::string(edition_id);
        summary.candidate.scope=EvidenceScope::Edition;
        summary.candidate.values={
            std::to_string(blocked)+"/"+std::to_string(selected_count)+
            " selected sources not qualified as separate physical files; "
            "whole-album alignment skipped, never staged"};
        summary.state=FieldReviewState::Blocked;
        summary.reason="album_batch_scope_UNQUALIFIED_SELECTION";
        return out;
    }
    out.eligible=true;
    return out;
}
} // namespace djmeta::online
