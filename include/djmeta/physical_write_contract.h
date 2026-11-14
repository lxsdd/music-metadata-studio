#pragma once
// Frozen online selection for physical metadata. Pure read-only qualification.
#include "djmeta/online_physical_postimage.h"
#include <map>
#include <string>
#include <vector>
namespace djmeta::online {
struct FrozenPhysicalTagWrite {
    std::string source_path;
    PhysicalTagSourceSnapshot physical;
    OnlineFieldReviewSnapshot official;
    OnlineFieldStagedPreview reviewed;
    std::vector<std::size_t> selected;
    std::map<std::size_t,std::string> date_targets;
};
enum class PhysicalWriteGateState { Blocked, Noop, Changed };
struct PhysicalWriteGate {
    PhysicalWriteGateState state=PhysicalWriteGateState::Blocked;
    PhysicalTagPostimage postimage;
};
inline PhysicalWriteGate verify_frozen_physical_write(
    const FrozenPhysicalTagWrite& frozen, const MetadataDocument& fresh,
    const std::string& path, const std::string& physical_id,
    const std::string& source_guard) {
    if(frozen.source_path.empty() || path!=frozen.source_path ||
       !frozen.physical.qualified_physical || frozen.physical.virtual_subsong ||
       frozen.physical.host_physical_key.empty() ||
       frozen.physical.source_guard.empty() ||
       physical_id!=frozen.physical.host_physical_key ||
       source_guard!=frozen.physical.source_guard ||
       frozen.selected.empty() || frozen.selected.size()>1024 ||
       frozen.official.evidence.size()>1024)
        return {};
    const auto verified=verify_online_physical_tag_postimage(
        frozen.physical,fresh,physical_id,source_guard,frozen.official,
        frozen.selected,frozen.date_targets,frozen.reviewed);
    if(!verified.eligible)return {};
    return {verified.changed?PhysicalWriteGateState::Changed:
                             PhysicalWriteGateState::Noop,verified};
}
} // namespace djmeta::online
