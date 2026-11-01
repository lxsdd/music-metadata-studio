#pragma once
// Lossless, bounded final human-readable approval for the QA-gated SDK writer.
// No I/O or write permission is granted by constructing this message.
#include "djmeta/physical_tag_postimage.h"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

inline std::string physical_write_display_text(std::string_view input,
                                               bool escape_backslash=true) {
    if (input.find('\0')!=std::string_view::npos ||
        !valid_utf8_metadata_text(input))
        throw std::invalid_argument("PHYSICAL_CONFIRM_INVALID_UTF8");
    static constexpr char hex[]="0123456789ABCDEF";
    std::string out;
    out.reserve(input.size());
    for (unsigned char c:input) {
        if (c=='\\' && escape_backslash) out+="\\\\";
        else if (c=='\r') out+="\\r";
        else if (c=='\n') out+="\\n";
        else if (c=='\t') out+="\\t";
        else if (c<0x20 || c==0x7f) {
            out+="\\x";
            out+=hex[c>>4];
            out+=hex[c&15];
        } else out+=static_cast<char>(c);
    }
    return out;
}

inline std::string format_physical_write_confirmation(
    const std::string& source_path,
    const std::vector<PhysicalTagValueApproval>& changed_fields) {
    // MessageBox must show *every* old/new value, never a truncated subset.
    // If it cannot, refuse submission and require a smaller selection.
    constexpr std::size_t maximum_bytes=24000;
    if(source_path.empty() || changed_fields.empty() ||
       changed_fields.size()>1024)
        throw std::invalid_argument("PHYSICAL_CONFIRM_EMPTY_OR_UNBOUNDED");
    std::string message;
    const auto append=[&message](std::string_view value) {
        if(value.size()>maximum_bytes ||
           message.size()>maximum_bytes-value.size())
            throw std::length_error(
                "Physical write confirmation too large. Select fewer fields.");
        message.append(value);
    };
    append("FINAL PHYSICAL TAG WRITE APPROVAL\n\nFile: ");
    append(physical_write_display_text(source_path,false));
    append("\nChanged fields: ");
    append(std::to_string(changed_fields.size()));
    append("\n\n");
    for(const auto& field:changed_fields) {
        if(field.field_name.empty() ||
           field.expected_values==field.replacement_values ||
           field.replacement_values.empty())
            throw std::invalid_argument("PHYSICAL_CONFIRM_INVALID_CHANGE");
        append(physical_write_display_text(field.field_name));
        append("\n  BEFORE:");
        if(field.expected_values.empty()) append(" [absent]\n");
        else {
            append("\n");
            for(std::size_t i=0;i<field.expected_values.size();++i) {
                append("    [");append(std::to_string(i+1));append("] ");
                append(physical_write_display_text(field.expected_values[i]));
                append("\n");
            }
        }
        append("  AFTER:\n");
        for(std::size_t i=0;i<field.replacement_values.size();++i) {
            append("    [");append(std::to_string(i+1));append("] ");
            append(physical_write_display_text(field.replacement_values[i]));
            append("\n");
        }
        append("\n");
    }
    append("Only the listed physical tags may change. All other tags, "
           "ReplayGain, CUE and audio must remain unchanged.\n"
           "The SDK will reread and revalidate the source before writing.\n\n"
           "Submit this one-file write?");
    return message;
}
} // namespace djmeta
