#pragma once
// Adapter from visible, exact Discogs edition rows to immutable field evidence.
// Search hits, CUE carriers, unverified track associations and local inventory
// are NOT valid inputs for the physical-only staged preview.
#include "djmeta/online_fields.h"
#include <cstddef>
#include <optional>
#include <string>

namespace djmeta::online {

inline std::optional<std::size_t> physical_discogs_edition_field_index(
    const ReleaseEdition& edition, const FieldReviewRow& detail) {
    if (edition.provider!="discogs" ||
        !valid_evidence_origin(edition.edition_id,512) ||
        !detail.field.starts_with("Album / ") ||
        !detail.reason.starts_with("discogs_live_release_edition_") ||
        detail.state==FieldReviewState::Blocked ||
        detail.candidate.provider!="discogs" ||
        detail.candidate.source_id!=edition.edition_id ||
        detail.candidate.scope!=EvidenceScope::Edition ||
        detail.candidate.values.empty())
        return std::nullopt;
    std::optional<std::size_t> found;
    for(std::size_t i=0;i<edition.fields.size();++i) {
        if(edition.fields[i]==detail.candidate) {
            if(found) return std::nullopt;
            found=i;
        }
    }
    return found;
}

} // namespace djmeta::online
