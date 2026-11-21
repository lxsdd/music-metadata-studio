#pragma once
// Provider-neutral, read-only SEARCH hint from one physical ARTIST vector.
// Never a canonical credit, recording-match proof or tag-edit suggestion.
#include "djmeta/online_fields.h"
#include <string>

namespace djmeta::online {

// One physical ARTIST field may legitimately carry multiple distinct DJ
// collaborators. Include their existing values in stored order with spaces,
// not a fabricated feat./x/vs relationship. A duplicate field name, missing,
// malformed or oversized credit makes the optional hint absent. TITLE and
// other metadata must be handled separately and never silently truncated.
inline std::string physical_collaborator_search_hint(
    const MetadataDocument& source) {
    bool seen=false;
    std::string query;
    for(const auto& field:source.fields) {
        if(ascii_upper_field(field.name)!="ARTIST") continue;
        if(seen || field.values.empty() || field.values.size()>8) return {};
        seen=true;
        for(const auto& value:field.values) {
            if(value.empty() || value.size()>150 ||
               !valid_utf8_metadata_text(value)) return {};
            for(const unsigned char ch:value)
                if(ch<0x20 || ch==0x7f) return {};
            const auto extra=value.size()+(query.empty()?0:1);
            if(extra>150-query.size()) return {};
            if(!query.empty()) query.push_back(' ');
            query+=value;
        }
    }
    return query;
}

} // namespace djmeta::online
