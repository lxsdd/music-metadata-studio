#pragma once

#include "djmeta/external_cue.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

// Input identities and destination-relative FILE names are supplied by the
// foobar host. The shared core NEVER guesses path equivalence or probes disk.
struct CueHostReference {
    std::size_t reference_index = 0;
    std::string expected_filename;
    std::string resolved_source_key;
    std::string proposed_filename;
    bool uniquely_resolved = false;
    bool destination_reference_verified = false;
    // Verified canonical identity of the FILE target relative to the planned
    // future CUE location and final host filename policy (not raw TF output).
    // Required even for unselected audio and must match an explicitly
    // selected CueSelectedAudio::target_key.
    std::string verified_destination_key;
};

struct CueSelectedAudio {
    std::string physical_id;
    std::string source_key;
    std::string target_key;
};

struct CueAssociationPlan {
    bool qualified = false; // purely advisory, not permission to write
    bool changes_references = false;
    std::vector<std::string> diagnostics;
    std::vector<std::string> related_selected_audio_ids;
    std::vector<CueReferenceRename> changes;
    std::string original_cue_fingerprint;
    std::string proposed_cue_fingerprint;
    std::string proposed_cue_bytes;
};

// Full coverage is required even when a reference is unchanged, as a moved
// .cue may otherwise leave an unselected audio target behind. The host must
// verify each proposed path resolves to the originally identified audio in
// the new layout. No file access, no writes, no custom path resolution.
CueAssociationPlan qualify_external_cue_association(
    std::string_view raw_cue_bytes,
    const std::vector<CueHostReference>& host_references,
    const std::vector<CueSelectedAudio>& selected_audios);

} // namespace djmeta
