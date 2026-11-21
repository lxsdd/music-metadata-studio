#pragma once
// Exact-edition, source-byte-bound external CUE metadata preview -> batch item.
// A detached PlanItem is NOT a CUE file writer or SDK execution authority.
#include "djmeta/batch_plan.h"
#include "djmeta/cue_review_postimage.h"
#include "djmeta/normalizer.h"
#include <string>
#include <string_view>
#include <vector>
namespace djmeta::online {
inline std::string cue_carrier_content_fingerprint(std::string_view bytes) {
    // SHA-256 using the engine's length-prefixed document serializer.
    // Preserve every CR/LF, BOM and unknown REM byte; no normalization.
    return djmeta::fingerprint(
        djmeta::MetadataDocument{{{"CUE_SOURCE_BYTES_V1",{std::string(bytes)}}}});
}
struct CueValueBatchProjection {
    bool eligible=false;
    std::string rejection;
    djmeta::FilePlanItem planned;
    djmeta::CueValuePostimage cue;
};
inline CueValueBatchProjection stage_exact_external_cue_values_for_batch(
    const djmeta::FilePlanItem& captured_carrier,
    std::string_view raw_cue,
    const ReleaseEdition& verified_edition,
    const std::vector<std::size_t>& selected_indices) {
    const auto deny=[](const char* reason){
        CueValueBatchProjection result;result.rejection=reason;return result;
    };
    // Embedded CUESHEET and FLAC Type-5 MUST use an independent physical
    // carrier writer; no virtual subsong handle or generic file writer.
    if(captured_carrier.role!=djmeta::FileRole::ExternalCue ||
       captured_carrier.action!=djmeta::FileAction::None ||
       captured_carrier.cue_links!=djmeta::CueLinkState::Verified ||
       captured_carrier.cue_references_will_change ||
       captured_carrier.cue_values_will_change ||
       captured_carrier.source_path.empty() ||
       captured_carrier.source_key.empty() ||
       captured_carrier.physical_id.empty() ||
       captured_carrier.source_guard.empty() ||
       captured_carrier.ruleset_revision.empty() ||
       (!captured_carrier.target_path.empty() ||
        !captured_carrier.target_key.empty() ||
        !captured_carrier.target_guard.empty() ||
        !captured_carrier.target_physical_id.empty() ||
        captured_carrier.target_presence!=djmeta::TargetPresence::Unchecked) ||
       (captured_carrier.associated_audio_id.empty() &&
        captured_carrier.associated_audio_ids.empty()))
        return deny("CUE_BATCH_CARRIER_OR_DEPENDENCY_UNQUALIFIED");
    if(raw_cue.empty() || raw_cue.size()>8u*1024u*1024u ||
       selected_indices.size()>512)
        return deny("CUE_BATCH_SOURCE_OR_SELECTION_TOO_LARGE");
    const auto source_fp=cue_carrier_content_fingerprint(raw_cue);
    if(!captured_carrier.cue_source_fingerprint.empty() &&
       captured_carrier.cue_source_fingerprint!=source_fp)
        return deny("CUE_BATCH_CAPTURED_BYTES_STALE");
    // Provider identity and full-edition 1:1 mapping are recomputed from
    // the raw cue/official edition; caller-supplied completeness flags
    // cannot authorize album-global changes.
    const auto snap=capture_qualified_cue_edition_postimage(
        raw_cue,djmeta::CueCarrierKind::ExternalText,verified_edition);
    if(!snap.eligible)
        return deny("CUE_BATCH_EXACT_EDITION_UNQUALIFIED");
    const auto post=preview_selected_cue_review_postimage(
        snap,raw_cue,selected_indices);
    if(!post.eligible || post.bytes.empty() ||
       post.changed!=(post.bytes!=raw_cue))
        return deny("CUE_BATCH_SELECTED_VALUE_POSTIMAGE_REFUSED");
    const auto target_fp=cue_carrier_content_fingerprint(post.bytes);
    if(!captured_carrier.cue_postimage_fingerprint.empty() &&
       captured_carrier.cue_postimage_fingerprint!=target_fp)
        return deny("CUE_BATCH_PREVIOUS_POSTIMAGE_CHANGED");
    CueValueBatchProjection out;
    out.eligible=true;
    out.planned=captured_carrier;
    out.planned.cue_source_fingerprint=source_fp;
    out.planned.cue_postimage_fingerprint=target_fp;
    out.planned.cue_values_will_change=post.changed;
    out.cue=post;
    return out;
}
} // namespace djmeta::online
