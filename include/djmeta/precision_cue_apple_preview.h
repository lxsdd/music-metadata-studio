#pragma once

// Apple Music share URL + official lookup JSON -> safe, in-memory CUE preview.
// Caller explicitly obtains the official API response; no implicit HTTP,
// album/audio download, filesystem write or foobar virtual-subsong operation.
#include "djmeta/precision_cue_apple.h"
#include "djmeta/precision_cue_apple_link.h"
#include "djmeta/precision_cue_assemble.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace djmeta {

struct ApplePrecisionCuePreviewInput {
    std::string album_url;
    std::string lookup_response_json;
    PrecisionCueRelease reference_release;
    std::vector<PrecisionCuePhysicalDisc> local_discs;
    std::string genre;
    std::string release_date;
};

// A malformed/incomplete Apple answer fails closed, with no partial CUE.
// An uncertain reference/match requires review and cannot silently be
// promoted to a qualified preview. This is not a write authorization.
inline PrecisionCuePreviewResult preview_apple_precision_cue_album(
    const ApplePrecisionCuePreviewInput& input) {
    PrecisionCuePreviewResult result;
    try {
        const auto link = parse_apple_precision_cue_album_link(input.album_url);
        auto release = parse_apple_precision_cue_lookup(
            input.lookup_response_json, link.collection_id);
        PrecisionCuePreviewRequest request;
        request.reference = input.reference_release;
        request.candidates.push_back(std::move(release));
        request.physical_discs = input.local_discs;
        request.genre = input.genre;
        request.release_date = input.release_date;
        return make_precision_cue_release_preview(request);
    } catch (const std::invalid_argument&) {
        result.state = PrecisionCueMatchState::Rejected;
        result.diagnostics.emplace_back("APPLE_LOOKUP_OR_LINK_INVALID");
        return result;
    } catch (const std::overflow_error&) {
        result.state = PrecisionCueMatchState::Rejected;
        result.diagnostics.emplace_back("APPLE_LOOKUP_OR_TIMING_OVERFLOW");
        return result;
    }
}

} // namespace djmeta
