#pragma once
// Adapter from visible, exact Discogs edition rows to immutable field evidence.
// Search hits, CUE carriers, unverified track associations and local inventory
// are NOT valid inputs for the physical-only staged preview.
#include "djmeta/online_fields.h"
#include <cstddef>
#include <charconv>
#include <vector>
#include <set>
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


// Discogs track fields are physical candidates ONLY after the existing
// duration/position/mix-aware alignment marked a single exact release track
// as verified. This helper crosschecks the UI row, source ID and the provider's
// actual edition-track fields; a search snippet cannot forge that association.
inline bool verified_physical_discogs_track_field(
    const ReleaseEdition& edition, const FieldReviewRow& detail) {
    constexpr std::string_view prefix="Release Track ";
    constexpr std::string_view separator=" / ";
    if (edition.provider!="discogs" ||
        detail.state==FieldReviewState::Blocked ||
        !detail.field.starts_with(prefix) ||
        !detail.reason.starts_with("discogs_live_release_verified_track_") ||
        detail.candidate.provider!="discogs" ||
        detail.candidate.scope!=EvidenceScope::Edition ||
        detail.candidate.values.empty())
        return false;
    const auto text=std::string_view(detail.field);
    const auto slash=text.find(separator,prefix.size());
    if(slash==std::string_view::npos) return false;
    const auto digits=text.substr(prefix.size(),slash-prefix.size());
    std::size_t ordinal=0;
    const auto [end,ec]=std::from_chars(digits.data(),
                                       digits.data()+digits.size(),ordinal);
    if(ec!=std::errc{} || end!=digits.data()+digits.size() ||
       ordinal==0 || ordinal>edition.tracks.size() ||
       digits!=std::to_string(ordinal) ||
       text.substr(slash+separator.size())!=detail.candidate.field)
        return false;
    const auto& track=edition.tracks[ordinal-1];
    const auto exact_track_source=edition.edition_id+"/track/"+
                                  std::to_string(ordinal);
    if(detail.candidate.source_id!=exact_track_source ||
       track.fields.empty()) return false;
    // The entire edition-track inventory must have trusted provenance; a
    // single correct-looking TITLE cannot mask another poisoned credit.
    for(const auto& field:track.fields)
        if(field.provider!="discogs" ||
           field.source_id!=exact_track_source ||
           field.scope!=EvidenceScope::Edition)
            return false;
    return std::find(track.fields.begin(),track.fields.end(),
                     detail.candidate)!=track.fields.end();
}

// Stageable evidence is independent of the current sorted/native list row.
// Only source-verified track rows constructed by the physical aligner can
// supply recording credits. It is still NOT a permission to write files.
inline std::vector<FieldEvidence> physical_discogs_stageable_evidence(
    const ReleaseEdition& edition,
    const std::vector<FieldReviewRow>& verified_physical_rows) {
    if (edition.provider!="discogs" ||
        !valid_evidence_origin(edition.edition_id,512))
        throw std::invalid_argument("No exact verified Discogs edition");
    std::vector<FieldEvidence> allowed=edition.fields;
    for(const auto& row:verified_physical_rows) {
        if(!verified_physical_discogs_track_field(edition,row)) continue;
        if(std::find(allowed.begin(),allowed.end(),row.candidate)==allowed.end())
            allowed.push_back(row.candidate);
    }
    return allowed;
}

inline std::optional<std::size_t> physical_discogs_stageable_field_index(
    const ReleaseEdition& edition,
    const std::vector<FieldEvidence>& allowed,
    const FieldReviewRow& detail) {
    // Each rendered album row must correspond to an exact edition field.
    // Track rows additionally require the independent physical alignment.
    const auto album=physical_discogs_edition_field_index(edition,detail);
    if(!album && !verified_physical_discogs_track_field(edition,detail))
        return std::nullopt;
    std::optional<std::size_t> result;
    for(std::size_t i=0;i<allowed.size();++i) {
        if(allowed[i]!=detail.candidate) continue;
        if(result) return std::nullopt;
        result=i;
    }
    return result;
}

} // namespace djmeta::online
