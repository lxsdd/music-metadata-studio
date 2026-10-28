#pragma once

// Read-only export of the currently selected online comparison fields.
// Deliberately isolated from metadata postimages and SDK tag/file writers.
// The caller must validate the source epoch and the exact visible row IDs.
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta_foobar {

struct NativeFieldEvidenceReceiptRow {
    std::wstring field, origin, source_id, scope, date_meaning, reason, status;
    std::vector<std::wstring> original_values, evidence_values;
};

inline constexpr bool native_evidence_receipt_allowed(
    bool find_tab, bool live, bool current_source, bool stale, bool refreshing,
    bool request_pending, int selected_groups, int selected_fields,
    std::size_t group, std::size_t group_count) noexcept {
    return find_tab && live && current_source && !stale && !refreshing &&
        !request_pending && selected_groups==1 &&
        selected_fields>0 && selected_fields<=128 && group<group_count;
}

inline constexpr bool native_evidence_copy_hotkey(
    unsigned virtual_key, bool ctrl, bool alt, bool shift) noexcept {
    return virtual_key==static_cast<unsigned>('C') && ctrl && !alt && !shift;
}

// Never flatten multivalues with a literal "|" or allow CR/LF/NUL/bidi
// controls to spoof an extra source, field or receipt section.
inline std::wstring native_evidence_receipt_escape(std::wstring_view raw) {
    if(raw.size()>16384) throw std::length_error("Field exceeds receipt limit");
    std::wstring out;
    out.reserve(raw.size());
    for(wchar_t ch:raw) {
        switch(ch) {
        case L'\r':out+=L"[CR]";break;
        case L'\n':out+=L"[LF]";break;
        case L'\t':out+=L"[TAB]";break;
        case L'\0':out+=L"[NUL]";break;
        case L'\x1b':out+=L"[ESC]";break;
        case L'\x7f':out+=L"[DEL]";break;
        case L'\u200e':out+=L"[LRM]";break;
        case L'\u200f':out+=L"[RLM]";break;
        case L'\u202a':out+=L"[LRE]";break;
        case L'\u202b':out+=L"[RLE]";break;
        case L'\u202c':out+=L"[PDF]";break;
        case L'\u202d':out+=L"[LRO]";break;
        case L'\u202e':out+=L"[RLO]";break;
        case L'\u2066':out+=L"[LRI]";break;
        case L'\u2067':out+=L"[RLI]";break;
        case L'\u2068':out+=L"[FSI]";break;
        case L'\u2069':out+=L"[PDI]";break;
        default:
            // Invisible Unicode format controls and line separators must not
            // impersonate another source, field or value in a copied receipt.
            // The marker records the exact code point; tags remain unchanged.
            if ((ch>=L'\u0080' && ch<=L'\u009f') ||
                ch==L'\u034f' || ch==L'\u061c' || ch==L'\u180e' ||
                (ch>=L'\u200b' && ch<=L'\u200d') ||
                ch==L'\u2028' || ch==L'\u2029' ||
                (ch>=L'\u2060' && ch<=L'\u206f') ||
                ch==L'\ufeff' || (ch>=L'\ufff9' && ch<=L'\ufffb')) {
                static constexpr wchar_t hex[]=L"0123456789ABCDEF";
                const unsigned code=static_cast<unsigned>(ch);
                out+=L"[U+";
                for(int shift=12;shift>=0;shift-=4)
                    out.push_back(hex[(code>>shift)&15]);
                out+=L"]";
            } else if(ch<0x20) {
                static constexpr wchar_t hex[]=L"0123456789ABCDEF";
                out+=L"[U+00";
                out.push_back(hex[(static_cast<unsigned>(ch)>>4)&15]);
                out.push_back(hex[static_cast<unsigned>(ch)&15]);
                out+=L"]";
            } else out.push_back(ch);
        }
    }
    return out;
}

inline std::optional<std::wstring> native_render_evidence_receipt(
    std::size_t physical_source,
    const std::vector<NativeFieldEvidenceReceiptRow>& fields) {
    if(fields.empty() || fields.size()>128 || physical_source>=2147483647u)
        return std::nullopt;
    std::wstring result=L"Music Metadata Studio - read-only field evidence\r\n"
        L"Physical source #: "+std::to_wstring(physical_source+1)+L"\r\n";
    const auto line=[&](std::wstring_view name,std::wstring_view value) {
        result.append(name);
        result+=native_evidence_receipt_escape(value);
        result+=L"\r\n";
        if(result.size()>1024u*1024u)
            throw std::length_error("Receipt exceeds 1 MiB");
    };
    try {
        for(std::size_t i=0;i<fields.size();++i) {
            const auto& f=fields[i];
            if(f.field.empty() || f.origin.empty() ||
               f.original_values.size()>128 || f.evidence_values.size()>128)
                return std::nullopt;
            result+=L"\r\nField #"+std::to_wstring(i+1)+L"\r\n";
            line(L"Field: ",f.field);
            line(L"Origin: ",f.origin);
            line(L"Source ID: ",f.source_id);
            line(L"Scope: ",f.scope);
            line(L"Date meaning: ",f.date_meaning);
            line(L"Reason: ",f.reason);
            line(L"Displayed status: ",f.status);
            result+=L"Original value count: "+
                std::to_wstring(f.original_values.size())+L"\r\n";
            for(std::size_t j=0;j<f.original_values.size();++j)
                line(L"Original["+std::to_wstring(j+1)+L"]: ",f.original_values[j]);
            result+=L"Evidence value count: "+
                std::to_wstring(f.evidence_values.size())+L"\r\n";
            for(std::size_t j=0;j<f.evidence_values.size();++j)
                line(L"Evidence["+std::to_wstring(j+1)+L"]: ",f.evidence_values[j]);
        }
        result+=L"\r\nNo tag, CUE or file change was authorized or performed.\r\n";
        if(result.size()>1024u*1024u || result.find(L'\0')!=std::wstring::npos)
            return std::nullopt;
        return result;
    } catch(const std::length_error&) {
        return std::nullopt;
    }
}

} // namespace djmeta_foobar
