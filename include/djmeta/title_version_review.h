#pragma once
// Exact-source, read-only TITLE suffix vs VERSION identity diagnostic.
// This is NOT a normalizer rule, proposal, provider match or write approval.
#include "djmeta/normalizer.h"
#include "djmeta/structural_guard.h"
#include <array>
#include <string>
#include <string_view>

namespace djmeta {

enum class TitleVersionReviewState {
    NoEvidence, Candidate, Consistent, Conflict, Ambiguous
};
struct TitleVersionReview {
    TitleVersionReviewState state=TitleVersionReviewState::NoEvidence;
    std::string base_title;
    std::string suffix;
    std::string version;
};
inline constexpr char title_version_lower(char c) noexcept {
    return c>='A' && c<='Z' ? static_cast<char>(c-'A'+'a') : c;
}
inline bool title_version_equal(std::string_view a,std::string_view b) noexcept {
    if(a.size()!=b.size()) return false;
    for(std::size_t i=0;i<a.size();++i)
        if(title_version_lower(a[i])!=title_version_lower(b[i])) return false;
    return true;
}
inline std::string_view title_version_trim(std::string_view value) noexcept {
    while(!value.empty() && value.front()==' ') value.remove_prefix(1);
    while(!value.empty() && value.back()==' ') value.remove_suffix(1);
    return value;
}
inline bool title_version_known_suffix(std::string_view value) noexcept {
    constexpr std::array<std::string_view,12> names={
        "Original Version","Original Mix","Extended Mix","Radio Edit",
        "Club Mix","DJ Version","Dub Mix","Instrumental","Remix","Edit",
        "Extended Version","Radio Version"
    };
    for(const auto name:names)
        if(title_version_equal(value,name)) return true;
    return false;
}
inline TitleVersionReview review_title_version(
    const MetadataDocument& source) {
    std::string_view title,version;
    bool has_title=false,has_version=false,ambiguous_title=false,ambiguous_version=false;
    for(const auto& field:source.fields) {
        if(title_version_equal(field.name,"TITLE")) {
            if(has_title || field.values.size()!=1) ambiguous_title=true;
            else {has_title=true;title=field.values.front();}
        } else if(title_version_equal(field.name,"VERSION")) {
            if(has_version || field.values.size()!=1) ambiguous_version=true;
            else {has_version=true;version=field.values.front();}
        }
    }
    if(!has_title) return ambiguous_title
        ? TitleVersionReview{TitleVersionReviewState::Ambiguous} : TitleVersionReview{};
    if(ambiguous_title || !valid_utf8_metadata_text(title) ||
       !safe_scalar_metadata_field("TITLE",title))
        return {TitleVersionReviewState::Ambiguous};
    const auto whole=title_version_trim(title);
    if(whole.size()<5 || (whole.back()!=')' && whole.back()!=']'))
        return {};
    const char opener=whole.back()==')'?'(':'[';
    const auto start=whole.rfind(opener);
    if(start==std::string_view::npos || start<2 || whole[start-1]!=' ')
        return {};
    const auto suffix=title_version_trim(
        whole.substr(start+1,whole.size()-start-2));
    if(!title_version_known_suffix(suffix)) return {};
    const auto base=title_version_trim(whole.substr(0,start));
    if(base.empty() || base.back()==')' || base.back()==']' ||
       !safe_scalar_metadata_field("TITLE",base))
        return {TitleVersionReviewState::Ambiguous};
    TitleVersionReview result;
    result.base_title=std::string(base);
    result.suffix=std::string(suffix);
    if(ambiguous_version || (has_version &&
        (!valid_utf8_metadata_text(version) ||
         !safe_scalar_metadata_field("VERSION",version))))
        result.state=TitleVersionReviewState::Ambiguous;
    else if(!has_version)
        result.state=TitleVersionReviewState::Candidate;
    else {
        result.version=std::string(version);
        result.state=title_version_equal(suffix,title_version_trim(version))
            ? TitleVersionReviewState::Consistent
            : TitleVersionReviewState::Conflict;
    }
    return result;
}

} // namespace djmeta
