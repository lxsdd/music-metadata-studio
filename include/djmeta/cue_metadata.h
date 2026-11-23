#pragma once

// Read-only view of the *actual* CUE text carrier, independent of foobar's
// projected virtual file_info. Shared by external and embedded textual CUEs.
// All byte offsets refer to the original input; no writer is exposed here.
#include "djmeta/external_cue.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

enum class CueCarrierKind { ExternalText, EmbeddedText };
enum class CueFieldScope { AlbumGlobal, TrackLocal };

struct CueMetadataField {
    std::string name;       // Explicit CUE semantic key, e.g. TITLE or REM GENRE.
    std::string value;      // Exact decoded UTF-8 content, no alias normalization.
    CueFieldScope scope = CueFieldScope::AlbumGlobal;
    std::size_t line_number = 0;
    std::size_t value_begin = 0; // start of value bytes in original CUE.
    std::size_t value_end = 0;   // end (exclusive), no enclosing quotes.
    bool quoted = false;
};

struct CueTrackMetadata {
    std::size_t ordinal = 0; // Position in this CUE, not filename or foobar subsong ID.
    int declared_track_number = 0;
    std::string track_type; // AUDIO, MODE1/2352 etc; not a tag field.
    std::size_t file_reference_index = static_cast<std::size_t>(-1);
    std::size_t line_number = 0;
    std::vector<CueMetadataField> local_fields;
};

struct CueMetadataInventory {
    CueSyntaxStatus status = CueSyntaxStatus::Invalid;
    CueTextEncoding encoding = CueTextEncoding::Unknown;
    CueCarrierKind carrier = CueCarrierKind::ExternalText;
    std::vector<ExternalCueReference> files;
    std::vector<CueMetadataField> globals;
    std::vector<CueTrackMetadata> tracks;
    std::vector<std::string> diagnostics; // Reason codes; never raw personal tag values.
    std::size_t source_bytes = 0;
};

// C1: no paths, file reads, SDK operations, timestamps, writes or networking.
// For EmbeddedText, the caller must supply the qualified exact raw CUE text
// from the physical carrier. The parser never infers that a virtual foobar
// handle is a legitimate direct write target.
CueMetadataInventory inspect_cue_metadata(
    std::string_view source,
    CueCarrierKind carrier);

// Returns an effective *read-only* track field. Track-local overrides
// album-global; when both are absent, no value is guessed. Duplicated tokens
// yield ambiguous=true and no effective field rather than choosing arbitrarily.
struct CueEffectiveField {
    bool present = false;
    bool inherited = false;
    bool ambiguous = false;
    std::string value;
    std::size_t source_line_number = 0;
};
CueEffectiveField effective_cue_field(
    const CueMetadataInventory& inventory,
    std::size_t ordinal,
    std::string_view cue_field_name);

// C1 scoring uses textual CUE TITLE/PERFORMER and positions as evidence;
// release matching remains advisory, not metadata approval.
} // namespace djmeta
