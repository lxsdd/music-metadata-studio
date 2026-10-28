#pragma once
// CUE + MusicBrainz exact-edition qualification: read-only, no writer.
#include "djmeta/cue_online_bridge.h"
#include "djmeta/cue_release_review.h"
#include "djmeta/musicbrainz_provider.h"
#include <algorithm>
#include <set>
#include <string>
#include <vector>
namespace djmeta::online {
// A search result, a release MBID and a plausible partial match do not
// authorize CUE album-wide changes. Require the complete, conflict-free,
// one-to-one exact-edition tracklist and independently verified track MBIDs.
inline bool qualified_musicbrainz_cue_album_alignment(
    const djmeta::CueMetadataInventory& cue,
    const ReleaseEdition& edition,
    const ReleaseAlignment& alignment) {
    if(cue.status!=djmeta::CueSyntaxStatus::Parsed ||
       edition.provider!="musicbrainz" ||
       !musicbrainz::valid_mbid(edition.edition_id) ||
       alignment.provider!=edition.provider ||
       alignment.edition_id!=edition.edition_id)
        return false;
    if(cue_media_barcode_conflict(cue,edition)) return false;
    const auto local=prepare_cue_release_lookup(cue);
    if(!local.eligible || local.tracks.empty() ||
       local.tracks.size()!=edition.tracks.size() ||
       alignment.tracks.size()!=local.tracks.size())
        return false;
    for(const auto& e:edition.fields)
        if(e.provider!="musicbrainz" || e.source_id!=edition.edition_id ||
           e.scope!=EvidenceScope::Edition)
            return false;
    std::set<std::string> remote_ids;
    for(const auto& remote:edition.tracks){
        if(!musicbrainz::valid_mbid(remote.source_track_id) ||
           !remote_ids.insert(remote.source_track_id).second)
            return false;
        // A recording fallback can support matching but cannot establish the
        // distinct, exact release-track title needed for an album-wide CUE proposal.
        if(remote.fields.empty() || remote.fields.size()>64 ||
           std::count_if(remote.fields.begin(),remote.fields.end(),
               [](const FieldEvidence& field) {
                   return field.field=="TITLE";
               })!=1 ||
           !std::any_of(remote.fields.begin(),remote.fields.end(),
               [](const FieldEvidence& field) {
                   return field.field=="TITLE" && field.values.size()==1 &&
                       !field.values.front().empty();
               }))
            return false;
        for(const auto& field:remote.fields)
            if(field.provider!="musicbrainz" ||
               field.source_id!=remote.source_track_id ||
               field.scope!=EvidenceScope::Edition)
                return false;
    }
    std::set<std::size_t> local_ids,matched_ids;
    for(const auto& match:alignment.tracks){
        if(match.decision!=MatchDecision::Suggested ||
           !match.conflicts.empty() ||
           match.source_index>=local.tracks.size() ||
           match.edition_track_index>=edition.tracks.size() ||
           !local_ids.insert(match.source_index).second ||
           !matched_ids.insert(match.edition_track_index).second)
            return false;
    }
    return local_ids.size()==local.tracks.size() &&
           matched_ids.size()==edition.tracks.size();
}
} // namespace djmeta::online
