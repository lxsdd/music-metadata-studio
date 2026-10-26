#pragma once
// Read-only inventory of the *textual* CUESHEET metadata field as foobar
// exposes it for a physical file. This is NOT a decoder for FLAC Type-5
// binary cuesheets and NEVER authorizes embedded metadata writes.
#include "djmeta/external_cue.h"
#include "djmeta/normalizer.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

enum class PrecisionCueEmbeddedTextState {
    Absent,
    SyntaxOnly, // A FILE inventory exists, but TRACK/INDEX and audio are NOT verified.
    NeedsReview
};

struct PrecisionCueEmbeddedTextObservation {
    PrecisionCueEmbeddedTextState state = PrecisionCueEmbeddedTextState::Absent;
    std::size_t file_reference_count = 0;
    std::vector<std::string> diagnostics; // Stable codes; never private tag values.
};

inline bool precision_cue_embedded_tag_name(std::string_view name) noexcept {
    constexpr std::string_view expected = "CUESHEET";
    if (name.size() != expected.size()) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto c = name[i];
        const auto lower = c >= 'A' && c <= 'Z'
            ? static_cast<char>(c + ('a' - 'A')) : c;
        if (lower != "cuesheet"[i]) return false;
    }
    return true;
}

// No writes, no source mutation, no guessing that an ID3 TXXX value or FLAC
// Type-5 block is safely editable. Unknown encoding/multiple values fail
// closed for the inventory. "Absent" means no TEXTUAL CUESHEET field was
// exposed by this metadata snapshot; not proof that no other cue format exists.
inline PrecisionCueEmbeddedTextObservation inspect_precision_cue_embedded_text(
    const MetadataDocument& physical_metadata) {
    PrecisionCueEmbeddedTextObservation result;
    const std::string* value = nullptr;
    for (const auto& field : physical_metadata.fields) {
        if (!precision_cue_embedded_tag_name(field.name)) continue;
        if (value != nullptr || field.values.size() != 1) {
            result.state = PrecisionCueEmbeddedTextState::NeedsReview;
            result.diagnostics.emplace_back("EMBEDDED_CUESHEET_DUPLICATE_OR_MULTIVALUE");
            return result;
        }
        value = &field.values.front();
    }
    if (value == nullptr) return result;
    if (value->empty()) {
        result.state = PrecisionCueEmbeddedTextState::NeedsReview;
        result.diagnostics.emplace_back("EMBEDDED_CUESHEET_EMPTY");
        return result;
    }
    const auto syntax = inspect_external_cue(*value);
    result.file_reference_count = syntax.references.size();
    if (syntax.status != CueSyntaxStatus::Parsed) {
        result.state = PrecisionCueEmbeddedTextState::NeedsReview;
        result.diagnostics.emplace_back("EMBEDDED_CUESHEET_SYNTAX_OR_ENCODING_REVIEW");
        return result;
    }
    result.state = PrecisionCueEmbeddedTextState::SyntaxOnly;
    result.diagnostics.emplace_back("EMBEDDED_CUESHEET_TEXT_PRESENT_READ_ONLY");
    return result;
}

} // namespace djmeta
