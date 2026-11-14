#pragma once
// Detached foobar file_info postimage: never calls metadb_io/input writer.
// Source adapter is SDK-independent and checked with both fake and real
// SDK file_info implementations. Approval provenance must already be checked.
#include "djmeta/physical_tag_postimage.h"
#include <string>
#include <utility>
#include <vector>
namespace djmeta {
enum class PhysicalSdkPostimageStatus { Blocked, ExactNoop, Detached };
struct PhysicalSdkPostimageResult {
    PhysicalSdkPostimageStatus status=PhysicalSdkPostimageStatus::Blocked;
    std::string rejection;
};
inline std::vector<PhysicalTagValueApproval> physical_sdk_changed_field_approvals(
    const MetadataDocument& original,
    const MetadataDocument& planned) {
    std::vector<PhysicalTagValueApproval> result;
    for(const auto& field:planned.fields) {
        std::vector<std::string> before;
        for(const auto& source:original.fields)
            if(physical_fold_name(source.name)==physical_fold_name(field.name)) {
                before=source.values;
                break;
            }
        if(before!=field.values)
            result.push_back({field.name,before,field.values});
    }
    return result;
}
// Both source and result may be foobar SDK file_info instances, but the
// detached object must be a concrete file_info_impl (never the live handle).
// Ineligible/unchanged proposals leave the output untouched.
template<typename DetachedInfo,typename SourceInfo,typename ReadTags>
PhysicalSdkPostimageResult project_explicit_physical_sdk_postimage(
    const SourceInfo& source, DetachedInfo& detached,
    const PhysicalTagSourceSnapshot& captured,
    const std::string& live_physical_key,
    const std::string& live_source_guard,
    const std::vector<PhysicalTagValueApproval>& approvals,
    const PhysicalTagPostimage& reviewed,
    ReadTags read_tags) {
    const auto fail=[](const char* why) {
        return PhysicalSdkPostimageResult{
            PhysicalSdkPostimageStatus::Blocked,why};
    };
    if(!reviewed.eligible)return fail("SDK_PHYSICAL_REVIEW_UNQUALIFIED");
    const auto current=read_tags(source);
    const auto fresh=preview_explicit_physical_tag_values(
        captured,current,live_physical_key,live_source_guard,approvals);
    if(!fresh.eligible || fresh.changed!=reviewed.changed ||
       fresh.explicitly_changed!=reviewed.explicitly_changed ||
       compare_physical_text_tags(fresh.planned,reviewed.planned)!=
           TagWriteDecision::Unchanged)
        return fail("SDK_PHYSICAL_EVIDENCE_OR_POSTIMAGE_CHANGED");
    if(!fresh.changed)
        return {PhysicalSdkPostimageStatus::ExactNoop,{}};
    // Construct a new SDK object; source and caller output are never touched
    // until every multivalue/custom/protected-field comparison passes.
    DetachedInfo candidate;
    candidate.copy(source);
    for(const auto& edit:approvals) {
        if(edit.expected_values==edit.replacement_values)continue;
        const auto index=candidate.meta_set(
            edit.field_name.c_str(),edit.replacement_values.front().c_str());
        for(std::size_t j=1;j<edit.replacement_values.size();++j)
            candidate.meta_add_value(index,edit.replacement_values[j].c_str());
    }
    const auto sdk_reflected=read_tags(candidate);
    if(compare_physical_text_tags(sdk_reflected,fresh.planned)!=
       TagWriteDecision::Unchanged)
        return fail("SDK_PHYSICAL_MULTIVALUE_ROUNDTRIP_CHANGED");
    detached.copy(candidate);
    return {PhysicalSdkPostimageStatus::Detached,{}};
}
} // namespace djmeta
