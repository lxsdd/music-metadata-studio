#pragma once
// Adapter from visible, exact Discogs edition rows to immutable field evidence.
// Search hits, CUE carriers, unverified track associations and local inventory
// are NOT valid inputs for the physical-only staged preview.
#include "djmeta/online_fields.h"
#include "djmeta/online_field_staging.h"
#include "djmeta/discogs_release_review.h"
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
inline std::optional<std::size_t> independently_verified_discogs_track(
    const MetadataDocument& original, const ReleaseEdition& edition,
    std::int64_t duration_ms) {
    if(duration_ms<=0 || duration_ms>36000000 || edition.provider!="discogs" ||
       discogs_edition_identifier_conflict(original,edition))
        return std::nullopt;
    const auto local=discogs_physical_identity(original,duration_ms);
    if(!local) return std::nullopt;
    const auto alignment=align_release({*local},edition);
    if(alignment.tracks.size()!=1) return std::nullopt;
    const auto& match=alignment.tracks.front();
    if(match.decision!=MatchDecision::Suggested ||
       !match.conflicts.empty() ||
       !discogs_edition_track_provenance_trusted(
           edition,match.edition_track_index))
        return std::nullopt;
    return match.edition_track_index;
}

inline std::vector<FieldEvidence> physical_discogs_stageable_evidence(
    const ReleaseEdition& edition,
    const MetadataDocument& physical_originals,
    std::int64_t physical_duration_ms,
    const std::vector<FieldReviewRow>& reviewed_physical_rows) {
    if (edition.provider!="discogs" ||
        !valid_evidence_origin(edition.edition_id,512))
        throw std::invalid_argument("No exact verified Discogs edition");
    std::vector<FieldEvidence> allowed;
    // Recalculate a uniquely suggested, conflict-free physical match. Do
    // not trust a UI reason/status string alone as proof of a match.
    const auto confirmed=independently_verified_discogs_track(
        physical_originals,edition,physical_duration_ms);
    if(!confirmed) return allowed;
    const auto exact_track_id=edition.edition_id+"/track/"+
        std::to_string(*confirmed+1);
    for(const auto& row:reviewed_physical_rows) {
        const auto album=physical_discogs_edition_field_index(edition,row);
        const bool track=verified_physical_discogs_track_field(edition,row) &&
                         row.candidate.source_id==exact_track_id;
        if(!album && !track) continue;
        if(std::find(allowed.begin(),allowed.end(),row.candidate)==allowed.end())
            allowed.push_back(row.candidate);
    }
    return allowed;
}

inline std::optional<std::size_t> physical_discogs_stageable_field_index(
    const ReleaseEdition& edition,
    const std::vector<FieldEvidence>& allowed,
    const FieldReviewRow& detail, bool explicit_date_target=false) {
    // Each rendered album row must correspond to an exact edition field.
    // Track rows additionally require the independent physical alignment.
    const auto album=physical_discogs_edition_field_index(edition,detail);
    if(!album && !verified_physical_discogs_track_field(edition,detail))
        return std::nullopt;
    // No implicit DATE -> edition-date replacement. This field requires an
    // independent named target policy, not just inclusion in the preview.
    if(detail.state==FieldReviewState::NeedsReview &&
       online_date_requires_explicit_target(detail.candidate) &&
       !explicit_date_target)
        return std::nullopt;
    std::optional<std::size_t> result;
    for(std::size_t i=0;i<allowed.size();++i) {
        if(allowed[i]!=detail.candidate) continue;
        if(result) return std::nullopt;
        result=i;
    }
    return result;
}

// A master date is NOT an edition tag. It may join an exact edition's
// detached preview only with a separately authenticated master detail and
// the master ID linked by that exact edition. Selection still requires a
// uniquely qualified physical track (checked by the caller on fresh info).
inline bool verified_discogs_master_year(
    const ReleaseEdition& edition,const FieldEvidence& candidate) {
    if(edition.provider!="discogs" || edition.master_id.empty() ||
       edition.master_id.size()>12) return false;
    std::uint64_t id=0;
    const auto [end,ec]=std::from_chars(
        edition.master_id.data(),
        edition.master_id.data()+edition.master_id.size(),id);
    if(ec!=std::errc{} ||
       end!=edition.master_id.data()+edition.master_id.size() ||
       !id || id>999999999999ULL ||
       edition.master_id!=std::to_string(id)) return false;
    if(candidate.provider!="discogs" ||
       candidate.source_id!="master:"+edition.master_id ||
       candidate.scope!=EvidenceScope::Release ||
       candidate.field!="DATE" ||
       candidate.date_meaning!=DateMeaning::OriginalRelease ||
       candidate.values.size()!=1 ||
       candidate.values.front().size()!=4) return false;
    const auto& year=candidate.values.front();
    int number=0;
    const auto [last,error]=std::from_chars(
        year.data(),year.data()+year.size(),number);
    return error==std::errc{} && last==year.data()+year.size() &&
           number>=1850 && number<=2200;
}

inline std::optional<std::size_t> physical_discogs_master_field_index(
    const ReleaseEdition& edition,const std::vector<FieldEvidence>& allowed,
    const FieldReviewRow& row,bool explicit_date_target=false) {
    if(row.field!="Discogs Master / DATE" ||
       !row.reason.starts_with("discogs_live_master_year_verified_") ||
       row.state==FieldReviewState::Blocked ||
       (row.state==FieldReviewState::NeedsReview && !explicit_date_target) ||
       !verified_discogs_master_year(edition,row.candidate))
        return std::nullopt;
    std::optional<std::size_t> found;
    for(std::size_t i=0;i<allowed.size();++i)
        if(allowed[i]==row.candidate) {
            if(found) return std::nullopt;
            found=i;
        }
    return found;
}

struct DiscogsDatePair {
    std::size_t master_index=0;
    std::size_t edition_index=0;
};

// Prefer a full edition release date when the official edition supplies it,
// falling back to its year. Never infer one from the other, and never combine
// two different dates into one target tag.
inline std::optional<DiscogsDatePair> discogs_dual_date_indices(
    const ReleaseEdition& edition,
    const std::vector<FieldEvidence>& allowed) {
    std::optional<std::size_t> master,edition_year,edition_full;
    for(std::size_t i=0;i<allowed.size();++i) {
        const auto& candidate=allowed[i];
        if(verified_discogs_master_year(edition,candidate)) {
            if(master) return std::nullopt;
            master=i;
        }
        if(candidate.provider=="discogs" &&
           candidate.source_id==edition.edition_id &&
           candidate.scope==EvidenceScope::Edition &&
           candidate.date_meaning==DateMeaning::EditionRelease &&
           candidate.values.size()==1 &&
           std::find(edition.fields.begin(),edition.fields.end(),
                     candidate)!=edition.fields.end()) {
            auto* slot=candidate.field=="RELEASEDATE"?&edition_full:
                       candidate.field=="DATE"?&edition_year:nullptr;
            if(slot) {
                if(*slot) return std::nullopt;
                *slot=i;
            }
        }
    }
    if(!master || (!edition_full && !edition_year)) return std::nullopt;
    return DiscogsDatePair{*master,edition_full?*edition_full:*edition_year};
}

} // namespace djmeta::online
