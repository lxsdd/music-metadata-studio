#pragma once
// Independent embedded-text CUESHEET -> detached physical SDK file_info.
// Not a virtual subsong writer and NEVER invokes metadb_io or input writers.
#include "djmeta/cue_review_postimage.h"
#include "djmeta/physical_tag_postimage.h"
#include "djmeta/write_guard.h"
#include <cstddef>
#include <string>
#include <vector>
namespace djmeta::online {
enum class EmbeddedCueSdkStatus { Blocked, ExactNoop, Detached };
struct EmbeddedCueSdkResult {
    EmbeddedCueSdkStatus status=EmbeddedCueSdkStatus::Blocked;
    std::string rejection;
};
template<class DetachedInfo,class SourceInfo,class ReadTags>
EmbeddedCueSdkResult project_embedded_cue_into_physical_sdk(
    const SourceInfo& source, DetachedInfo& detached,
    const djmeta::PhysicalTagSourceSnapshot& physical,
    const std::string& live_physical_key,
    const std::string& live_source_guard,
    const CuePostimageReviewSnapshot& snapshot,
    const std::vector<std::size_t>& selected_indices,
    const djmeta::CueValuePostimage& approved,
    ReadTags read_tags) {
    const auto fail=[](const char* why){
        return EmbeddedCueSdkResult{EmbeddedCueSdkStatus::Blocked,why};
    };
    if(!physical.qualified_physical || physical.virtual_subsong ||
       physical.host_physical_key.empty() || physical.source_guard.empty() ||
       physical.host_physical_key!=live_physical_key ||
       physical.source_guard!=live_source_guard)
        return fail("EMBEDDED_CUE_PHYSICAL_SOURCE_UNQUALIFIED");
    if(!snapshot.eligible ||
       snapshot.carrier!=djmeta::CueCarrierKind::EmbeddedText ||
       !approved.eligible)
        return fail("EMBEDDED_CUE_SNAPSHOT_UNQUALIFIED");
    const auto current=read_tags(source);
    if(djmeta::compare_physical_text_tags(current,physical.original)!=
       djmeta::TagWriteDecision::Unchanged)
        return fail("EMBEDDED_CUE_PHYSICAL_TAGS_STALE");
    const djmeta::MetadataField* carrier=nullptr;
    std::size_t carrier_index=0, index=0;
    for(const auto& field:current.fields) {
        if(djmeta::is_protected_cue_metadata(field.name)) {
            if(carrier || !djmeta::metadata_field_equals_ci(
                    field.name,"CUESHEET") ||
               field.values.size()!=1)
                return fail("EMBEDDED_CUE_CARRIER_AMBIGUOUS");
            carrier=&field;carrier_index=index;
        }
        ++index;
    }
    if(!carrier || carrier->values.front()!=snapshot.original_bytes ||
       carrier->values.front().empty() ||
       carrier->values.front().size()>8u*1024u*1024u)
        return fail("EMBEDDED_CUE_SOURCE_BYTES_CHANGED");
    const auto verified=preview_selected_cue_review_postimage(
        snapshot,carrier->values.front(),selected_indices);
    if(!verified.eligible || verified.changed!=approved.changed ||
       verified.bytes!=approved.bytes)
        return fail("EMBEDDED_CUE_SELECTED_PROPOSAL_CHANGED");
    if(!verified.changed)return {EmbeddedCueSdkStatus::ExactNoop,{}};
    if(verified.bytes.empty() ||
       verified.bytes.find('\0')!=std::string::npos)
        return fail("EMBEDDED_CUE_POSTIMAGE_NOT_SDK_TEXT");
    DetachedInfo candidate;
    candidate.copy(source);
    candidate.meta_set(carrier->name.c_str(),verified.bytes.c_str());
    auto expected=current;
    expected.fields[carrier_index].values={verified.bytes};
    const auto roundtrip=read_tags(candidate);
    if(djmeta::compare_physical_text_tags(roundtrip,expected)!=
       djmeta::TagWriteDecision::Unchanged)
        return fail("EMBEDDED_CUE_SDK_ROUNDTRIP_CHANGED");
    detached.copy(candidate);
    return {EmbeddedCueSdkStatus::Detached,{}};
}
} // namespace djmeta::online
