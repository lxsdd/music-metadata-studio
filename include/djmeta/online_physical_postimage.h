#pragma once
// Cross-check official online selected physical fields with a detached
// no-op-aware host-identity-bound tag postimage. No SDK/file writer here.
#include "djmeta/online_field_staging.h"
#include "djmeta/physical_tag_postimage.h"
#include <map>
#include <set>
#include <string>
#include <vector>
namespace djmeta::online {
inline djmeta::PhysicalTagPostimage verify_online_physical_tag_postimage(
    const djmeta::PhysicalTagSourceSnapshot& source,
    const djmeta::MetadataDocument& current_tags,
    const std::string& current_physical_key,
    const std::string& current_source_guard,
    const OnlineFieldStagedPreview& selected) {
    const auto refuse=[](const char* message){
        djmeta::PhysicalTagPostimage p;p.rejection=message;return p;
    };
    if(selected.planned.fields.size()>10000)
        return refuse("ONLINE_POSTIMAGE_SIZE_LIMIT");
    std::map<std::string,std::vector<std::string>> originals, planned;
    for(const auto& field:current_tags.fields) {
        const auto key=djmeta::physical_fold_name(field.name);
        if(!originals.emplace(key,field.values).second)
            return refuse("ONLINE_DUPLICATE_ORIGINAL");
    }
    for(const auto& field:selected.planned.fields) {
        const auto key=djmeta::physical_fold_name(field.name);
        if(!planned.emplace(key,field.values).second)
            return refuse("ONLINE_DUPLICATE_POSTIMAGE");
    }
    // Missing original fields are implicit deletions and cannot be inferred
    // from a provider omission. Require every original key and ordered
    // multivalue payload unless explicitly present in changed projected set.
    for(const auto& [name, values]:originals)
        if(!planned.contains(name))
            return refuse("ONLINE_PROVIDER_OMITTED_ORIGINAL");
    std::vector<djmeta::PhysicalTagValueApproval> approvals;
    approvals.reserve(selected.planned.fields.size());
    for(const auto& field:selected.planned.fields) {
        const auto canonical=djmeta::physical_fold_name(field.name);
        const auto old=originals.find(canonical);
        if(old!=originals.end() && old->second==field.values) continue;
        approvals.push_back({
            field.name,old==originals.end()?std::vector<std::string>{}:old->second,
            field.values
        });
    }
    if(approvals.size()!=selected.explicitly_changed)
        return refuse("ONLINE_CHANGED_COUNT_DISAGREES");
    auto out=djmeta::preview_explicit_physical_tag_values(
        source,current_tags,current_physical_key,current_source_guard,approvals);
    if(!out.eligible) return out;
    if(djmeta::compare_physical_text_tags(out.planned,selected.planned)!=
       djmeta::TagWriteDecision::Unchanged)
        return refuse("ONLINE_POSTIMAGE_MISMATCH");
    return out;
}
} // namespace djmeta::online
