#pragma once

#include "djmeta/cue_metadata.h"
#include "djmeta/online_release.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace djmeta::online {

// A snapshot for comparison with ONE proposed release edition.
// This is not a foobar handle, not a write target and not an approval plan.
// The CUE carrier and physical audio parent are deliberately not fabricated.
struct CueReleaseLookup {
    bool eligible = false;
    CueCarrierKind carrier = CueCarrierKind::ExternalText;
    std::vector<LocalReleaseTrack> tracks; // CUE ordinal -> stable comparison index.
    std::vector<std::string> reasons;
};

// Safe bridge into the existing recording-vs-release assignment engine.
// A global CUE TITLE is ALBUM TITLE: it must NOT fill a missing track TITLE.
// Global PERFORMER can be inherited. Missing/ambiguous fields abstain and
// reject the entire lookup instead of guessing from filenames or other tracks.
// No disk, network, fingerprint, runtime or source mutation.
inline CueReleaseLookup prepare_cue_release_lookup(
    const CueMetadataInventory& cue) {
    CueReleaseLookup result;
    result.carrier = cue.carrier;
    if (cue.status != CueSyntaxStatus::Parsed) {
        result.reasons.push_back("cue_inventory_unqualified");
        return result;
    }
    if (cue.tracks.empty()) {
        result.reasons.push_back("cue_has_no_tracks");
        return result;
    }
    if (cue.tracks.size() > 128) {
        // Same bound as online_release.h; no silent truncation.
        result.reasons.push_back("release_matching_limit_exceeded");
        return result;
    }
    for (const auto& track : cue.tracks) {
        if (track.track_type != "AUDIO" ||
            track.file_reference_index >= cue.files.size()) {
            result.reasons.push_back("cue_track_source_unqualified");
            result.tracks.clear();
            return result;
        }
        const auto title = effective_cue_field(cue, track.ordinal, "TITLE");
        const auto artist = effective_cue_field(cue, track.ordinal, "PERFORMER");
        const auto isrc = effective_cue_field(cue, track.ordinal, "ISRC");
        if (!title.present || !artist.present ||
            title.ambiguous || artist.ambiguous || isrc.ambiguous) {
            result.reasons.push_back("cue_track_identity_missing_or_ambiguous");
            result.tracks.clear();
            return result;
        }
        RecordingIdentity identity;
        identity.title = title.value;
        identity.primary_artist = artist.value;
        identity.isrc = isrc.present ? isrc.value : std::string{};
        identity.mix = MixKind::Unknown; // No unsafe guessing from title.
        identity.duration_ms = -1;       // INDEX/CD frames != full duration.
        LocalReleaseTrack row;
        row.source_index = track.ordinal;
        row.track_number = track.declared_track_number;
        row.disc_number = -1; // A new FILE is NOT proof of a new disc.
        row.recording = std::move(identity);
        result.tracks.push_back(std::move(row));
    }
    result.eligible = true;
    return result;
}

} // namespace djmeta::online
