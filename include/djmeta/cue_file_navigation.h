#pragma once
// Read-only navigation between independently qualified external CUE FILE
// references and their captured physical File locations source. No path,
// tag, provider ID, raw destination, write permission, or host action is
// encoded in a link. Indices are stable original captured batch order.
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace djmeta {
struct CueFileNavigation {
    std::size_t source_index=0;
    std::size_t file_index=0;
    bool carrier=false;
};
inline std::string cue_file_navigation_id(std::size_t file_index,
                                           std::size_t source_index) {
    if(file_index>=128 || source_index>=100000)
        return "cue-raw-route:unavailable";
    return "cue-raw-route:"+std::to_string(file_index+1)+"/source/"+
           std::to_string(source_index+1);
}
inline std::optional<CueFileNavigation> resolve_cue_file_navigation(
    std::string_view candidate_source_id,std::size_t carrier_index,
    std::size_t captured_tracks) noexcept {
    if(captured_tracks==0 || captured_tracks>100000 ||
       carrier_index>=captured_tracks)
        return std::nullopt;
    if(candidate_source_id=="cue-sidecar-action")
        return CueFileNavigation{carrier_index,0,true};
    constexpr std::string_view prefix="cue-raw-route:";
    constexpr std::string_view between="/source/";
    if(!candidate_source_id.starts_with(prefix))
        return std::nullopt;
    candidate_source_id.remove_prefix(prefix.size());
    const auto delim=candidate_source_id.find(between);
    if(delim==std::string_view::npos)
        return std::nullopt;
    const auto file_text=candidate_source_id.substr(0,delim);
    const auto source_text=candidate_source_id.substr(delim+between.size());
    if(file_text.empty() || file_text.size()>3 ||
       source_text.empty() || source_text.size()>6 ||
       file_text.front()=='0' || source_text.front()=='0')
        return std::nullopt;
    std::size_t file=0,source=0;
    const auto digit=[](std::string_view raw,std::size_t& value) noexcept {
        auto result=std::from_chars(raw.data(),raw.data()+raw.size(),value,10);
        return result.ec==std::errc{} && result.ptr==raw.data()+raw.size();
    };
    if(!digit(file_text,file)||!digit(source_text,source)||
       file==0 || file>128 || source==0 || source>captured_tracks)
        return std::nullopt;
    return CueFileNavigation{source-1,file-1,false};
}
} // namespace djmeta
