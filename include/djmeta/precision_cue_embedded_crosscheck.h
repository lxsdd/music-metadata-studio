#pragma once
// Pure consistency check: foobar physical file_info vs bounded raw tag scan.
// Agreement is DIAGNOSTIC ONLY, never permission to update embedded metadata.
#include "djmeta/precision_cue_embedded_inventory.h"
#include "djmeta/precision_cue_embedded_probe.h"

#include <string_view>

namespace djmeta {
enum class PrecisionCueEmbeddedCrosscheckState {
    NoCueObserved,
    TextualAgreementReadOnly,
    ProtectedNativeBinary,
    ReviewRequired
};
struct PrecisionCueEmbeddedCrosscheck {
    PrecisionCueEmbeddedCrosscheckState state =
        PrecisionCueEmbeddedCrosscheckState::ReviewRequired;
    std::string_view diagnostic = "EMBEDDED_CARRIER_REVIEW_REQUIRED";
};
inline PrecisionCueEmbeddedCrosscheck crosscheck_precision_cue_embedded(
    const PrecisionCueEmbeddedTextObservation& foobar,
    const PrecisionCueEmbeddedProbe& raw) noexcept {
    using Native = PrecisionCueEmbeddedState;
    using Text = PrecisionCueEmbeddedTextState;
    using State = PrecisionCueEmbeddedCrosscheckState;
    if (raw.state == Native::Unqualified ||
        raw.state == Native::MultipleOrConflicting ||
        foobar.state == Text::NeedsReview)
        return {State::ReviewRequired, "EMBEDDED_MALFORMED_OR_MULTIPLE_SOURCES"};
    if (raw.state == Native::NoneDetected && foobar.state == Text::Absent)
        return {State::NoCueObserved, "NO_SUPPORTED_EMBEDDED_CUE_IN_EITHER_VIEW"};
    if (raw.state == Native::TextualPresent && foobar.state == Text::SyntaxOnly)
        return {State::TextualAgreementReadOnly,
                "PHYSICAL_CUESHEET_TAG_CORROBORATED_NOT_WRITABLE"};
    if (raw.state == Native::NativeBinaryPresent && foobar.state == Text::Absent)
        return {State::ProtectedNativeBinary,
                "BINARY_CUE_PRESERVE_ONLY"};
    // Metadata hidden by a virtual CUE view, unsupported textual encoding,
    // stale foobar cache, or unexpected container layout must never be treated
    // as a qualified absence or an editable embedded CUE.
    return {State::ReviewRequired,
            "FOOBAR_PHYSICAL_TAG_AND_CONTAINER_DISAGREE"};
}
} // namespace djmeta
