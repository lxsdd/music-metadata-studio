#pragma once
// Physical textual tag postimage planner: SOURCE-ONLY and NO file_info writer.
// A successful postimage is NOT a permission to invoke SDK metadata updates.
#include "djmeta/normalizer.h"
#include "djmeta/structural_guard.h"
#include "djmeta/write_guard.h"
#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace djmeta {
struct PhysicalTagSourceSnapshot {
    bool qualified_physical=false;
    bool virtual_subsong=false;
    std::string host_physical_key;
    std::string source_guard;
    MetadataDocument original;
};
struct PhysicalTagValueApproval {
    std::string field_name;
    std::vector<std::string> expected_values; // empty means absent/insert
    std::vector<std::string> replacement_values; // nonempty: deletions blocked
};
struct PhysicalTagPostimage {
    bool eligible=false;
    bool changed=false;
    MetadataDocument planned;
    std::size_t explicitly_changed=0;
    std::size_t exact_noops=0;
    std::string rejection;
};
inline std::string physical_fold_name(std::string name){
    for(char& c:name) if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return name;
}
inline bool physical_tag_immutable(std::string_view name){
    // ReplayGain has additional real-world fields (e.g. reference loudness,
    // track/album range); protecting only the four common gain/peak keys
    // allows an unqualified normalization rule to mutate the rest.
    const auto upper=physical_fold_name(std::string(name));
    return is_protected_cue_metadata(name) ||
        upper.starts_with("replaygain_") ||
        upper=="metadata_block_picture" ||
        upper=="coverart" || upper=="coverartmime";
}
inline PhysicalTagPostimage preview_explicit_physical_tag_values(
    const PhysicalTagSourceSnapshot& snapshot,
    const MetadataDocument& current_tags,
    const std::string& current_physical_key,
    const std::string& current_source_guard,
    const std::vector<PhysicalTagValueApproval>& explicit_approvals){
    const auto refuse=[](const char* why){
        PhysicalTagPostimage result;result.rejection=why;return result;
    };
    if(!snapshot.qualified_physical || snapshot.virtual_subsong ||
       snapshot.host_physical_key.empty() || snapshot.source_guard.empty() ||
       current_physical_key!=snapshot.host_physical_key ||
       current_source_guard!=snapshot.source_guard)
        return refuse("PHYSICAL_SOURCE_IDENTITY_UNQUALIFIED");
    if(current_tags.fields.size()>10000 || snapshot.original.fields.size()>10000 ||
       explicit_approvals.size()>1024)
        return refuse("PHYSICAL_TAG_COUNT_UNQUALIFIED");
    // Preserve original multivalue ordering, including all unapproved custom
    // names. Host enumeration order and ASCII case differences are not writes.
    if(compare_physical_text_tags(snapshot.original,current_tags)!=
        TagWriteDecision::Unchanged)
        return refuse("PHYSICAL_TAG_SOURCE_STALE_OR_INVALID");
    PhysicalTagPostimage out;
    out.planned=current_tags;
    std::map<std::string,std::size_t> source_names;
    for(std::size_t i=0;i<current_tags.fields.size();++i)
        source_names.emplace(physical_fold_name(current_tags.fields[i].name),i);
    std::set<std::string> approved;
    for(const auto& item:explicit_approvals){
        if(item.field_name.empty() || item.field_name.size()>128 ||
           !valid_utf8_metadata_text(item.field_name))
            return refuse("PHYSICAL_FIELD_NAME_UNQUALIFIED");
        // Standard foobar keys include ALBUM ARTIST and user-defined fields
        // with interior spaces. Reject only controls and edge whitespace,
        // not valid single spaces inside the name.
        if(item.field_name.front()==' ' || item.field_name.back()==' ')
            return refuse("PHYSICAL_FIELD_NAME_UNQUALIFIED");
        for(unsigned char c:item.field_name)
            if(c<0x20 || c==0x7f)
                return refuse("PHYSICAL_FIELD_NAME_UNQUALIFIED");
        if(physical_tag_immutable(item.field_name))
            return refuse("PHYSICAL_PROTECTED_FIELD_REQUIRES_SEPARATE_WRITER");
        const auto canonical=physical_fold_name(item.field_name);
        if(!approved.insert(canonical).second)
            return refuse("PHYSICAL_DUPLICATE_APPROVAL");
        if(item.replacement_values.empty() || item.replacement_values.size()>256)
            return refuse("PHYSICAL_DELETION_OR_VALUE_LIMIT");
        for(const auto& value:item.replacement_values)
            if(value.size()>65536 || value.find('\0')!=std::string::npos ||
               !valid_utf8_metadata_text(value))
                return refuse("PHYSICAL_REPLACEMENT_INVALID_UTF8");
        const auto existing=source_names.find(canonical);
        const std::vector<std::string> old=existing==source_names.end()
            ? std::vector<std::string>{}
            : out.planned.fields[existing->second].values;
        if(old!=item.expected_values)
            return refuse("PHYSICAL_ORIGINAL_FIELD_STALE");
        if(old==item.replacement_values) {
            ++out.exact_noops;continue;
        }
        if(existing!=source_names.end()) {
            out.planned.fields[existing->second].values=item.replacement_values;
        } else {
            source_names.emplace(canonical,out.planned.fields.size());
            out.planned.fields.push_back(
                {item.field_name,item.replacement_values});
        }
        ++out.explicitly_changed;
    }
    const auto diff=compare_physical_text_tags(current_tags,out.planned);
    if(diff==TagWriteDecision::Unqualified)
        return refuse("PHYSICAL_POSTIMAGE_UNQUALIFIED");
    out.changed=diff==TagWriteDecision::Changed;
    if(out.changed != (out.explicitly_changed>0))
        return refuse("PHYSICAL_POSTIMAGE_DIFFERENCE_MISMATCH");
    out.eligible=true;
    return out;
}
} // namespace djmeta
