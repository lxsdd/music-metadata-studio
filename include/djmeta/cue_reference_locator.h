#pragma once
// Read-only Studio File-organization preflight of EXISTING external CUE FILE
// paths. The path is a candidate to probe, not proof of physical association,
// canonical destination, file safety or any right to write.
#include "djmeta/structural_guard.h"
#include "djmeta/batch_preview.h"
#include <string>
#include <string_view>

namespace djmeta {

struct CueSiblingLocator {
    std::string source_candidate;
    std::string reason; // stable, user-visible fail-closed diagnostic
    bool eligible = false;
    std::string source_parent; // exact CUE directory, separately probed by foobar
    bool nested = false; // safe relative subfolders only, never traversal
};

inline CueSiblingLocator preview_cue_sibling_locator(
    std::string_view cue_locator, std::string_view raw_filename) {
    if (cue_locator.empty() || raw_filename.empty() ||
        raw_filename.size() > 1024 ||
        raw_filename == "." || raw_filename == "..")
        return {{}, "CUE_FILE_PATH_UNQUALIFIED", false, {}, false};
    if (!valid_utf8_metadata_text(cue_locator) ||
        !valid_utf8_metadata_text(raw_filename))
        return {{}, "CUE_FILE_ENCODING_UNQUALIFIED", false, {}, false};
    // A FILE entry may be in a relative subfolder of its actual CUE carrier
    // (e.g. FILE "CD1/01.wav"). Keep this lexical step read-only: never
    // resolve "..", drive/UNC paths, ADS, URI escapes, reserved devices,
    // Windows-ambiguous spelling or Unicode direction overrides.
    // The shared FileOps raw-relative validator supplies the SAME Windows
    // component rules; a second OS-level native parent-chain inspection
    // below the selected CUE directory is mandatory before showing PRESENT.
    if(raw_filename.find('%')!=std::string_view::npos)
        return {{}, "CUE_FILE_UNSAFE_OR_NESTED_PATH", false, {}, false};
    std::string relative(raw_filename);
    for(char& c:relative) if(c=='\\') c='/';
    std::size_t first=0;
    while(first<relative.size()) {
        const auto sep=relative.find('/',first);
        const auto last=sep==std::string::npos?relative.size():sep;
        const auto part=std::string_view(relative).substr(first,last-first);
        if(part.empty() || part=="." || part=="..")
            return {{}, "CUE_FILE_UNSAFE_OR_NESTED_PATH", false, {}, false};
        if(part.back()=='.' || part.back()==' ')
            return {{}, "CUE_FILE_AMBIGUOUS_WINDOWS_NAME", false, {}, false};
        std::string stem;
        for(const char c:part) {
            if(c=='.') break;
            stem+=c>='a'&&c<='z'
                ? static_cast<char>(c-'a'+'A') : c;
        }
        while(!stem.empty() && stem.back()==' ') stem.pop_back();
        const bool com_lpt=stem.starts_with("COM") || stem.starts_with("LPT");
        const auto suffix=stem.size()>=3
            ? std::string_view(stem).substr(3) : std::string_view{};
        const bool digit=suffix.size()==1 && suffix.front()>='1' &&
            suffix.front()<='9';
        const bool superscript=suffix=="\xC2\xB9" ||
            suffix=="\xC2\xB2" || suffix=="\xC2\xB3";
        if(stem=="CON" || stem=="PRN" || stem=="AUX" || stem=="NUL" ||
           (com_lpt && (digit || superscript)))
            return {{}, "CUE_FILE_RESERVED_DEVICE", false, {}, false};
        if(sep==std::string::npos) break;
        first=sep+1;
    }
    if(!raw_relative_path_lexically_safe(relative))
        return {{}, "CUE_FILE_UNSAFE_OR_NESTED_PATH", false, {}, false};
    const auto split=cue_locator.find_last_of("/\\");
    if(split==std::string_view::npos || split+1>=cue_locator.size())
        return {{}, "CUE_PARENT_LOCATOR_UNQUALIFIED", false, {}, false};
    const auto parent=std::string(cue_locator.substr(0,split+1));
    // file:// locators use '/', whereas native Windows paths use '\\'.
    // Never normalize the CUE parent itself or infer an alternative root.
    if(cue_locator.substr(0,7)=="file://") {
        return {parent+relative,"CUE_FILE_HOST_PROBE_REQUIRED",true,
                parent,relative.find('/')!=std::string::npos};
    }
    const char native_sep=cue_locator[split];
    for(char& c:relative) if(c=='/') c=native_sep;
    return {parent+relative,"CUE_FILE_HOST_PROBE_REQUIRED",true,
            parent,relative.find(native_sep)!=std::string::npos};
}

} // namespace djmeta
