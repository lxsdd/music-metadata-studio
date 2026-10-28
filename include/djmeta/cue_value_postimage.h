#pragma once
// Strict in-memory CUE value postimage: no SDK, filesystem or writer.
#include "djmeta/cue_metadata.h"
#include "djmeta/structural_guard.h"
#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
namespace djmeta {
struct CueValueChange {
    std::optional<std::size_t> track; // null = album-global
    std::string token, original, proposed;
};
struct CueValuePostimage {
    bool eligible=false, changed=false;
    std::string bytes;
    std::string rejection;
};
inline bool cue_value_editable(std::string_view n,bool track){
    if(track)return n=="TITLE" || n=="PERFORMER" || n=="SONGWRITER" ||
                    n=="ISRC" || n=="REM DATE" || n=="REM COMMENT";
    return n=="TITLE" || n=="PERFORMER" || n=="REM GENRE" ||
           n=="REM DATE" || n=="REM COMMENT" ||
           n=="REM DISCNUMBER" || n=="REM TOTALDISCS";
}
inline CueValuePostimage preview_cue_value_postimage(
    std::string_view raw,CueCarrierKind kind,
    const std::vector<CueValueChange>& changes){
    const auto fail=[](const char* why){
        CueValuePostimage r;r.rejection=why;return r;
    };
    if(raw.empty() || raw.size()>8u*1024u*1024u || changes.size()>2048)
        return fail("CUE_SIZE_LIMIT");
    const auto original=inspect_cue_metadata(raw,kind);
    if(original.status!=CueSyntaxStatus::Parsed ||
       (original.encoding!=CueTextEncoding::Utf8 &&
        original.encoding!=CueTextEncoding::Utf8Bom))
        return fail("CUE_SOURCE_UNQUALIFIED");
    struct Span{std::size_t begin,end;std::string replacement;};
    std::vector<Span> spans;
    std::set<std::pair<std::size_t,std::string>> seen;
    for(const auto& c:changes){
        const bool track=c.track.has_value();
        if(!cue_value_editable(c.token,track))
            return fail("CUE_FIELD_UNSUPPORTED");
        if(track && (*c.track>=original.tracks.size() ||
           original.tracks[*c.track].track_type!="AUDIO"))
            return fail("CUE_TRACK_UNQUALIFIED");
        if(c.original.empty() || c.proposed.empty() ||
           c.proposed.size()>8192 || !valid_utf8_metadata_text(c.proposed) ||
           has_structured_line_break(c.proposed))
            return fail("CUE_VALUE_UNQUALIFIED");
        for(unsigned char ch:c.proposed)
            if(ch<0x20 || ch==0x7f || ch=='"')
                return fail("CUE_VALUE_UNQUALIFIED");
        if(!seen.emplace(track?*c.track+1:0,c.token).second)
            return fail("CUE_DUPLICATE_EDIT");
        const auto& fields=track?original.tracks[*c.track].local_fields
                                :original.globals;
        const CueMetadataField* match=nullptr;
        unsigned count=0;
        for(const auto& f:fields)
            if(f.name==c.token){++count;match=&f;}
        if(count!=1 || !match)
            return fail("CUE_AMBIGUOUS_FIELD");
        if(match->value!=c.original ||
           match->value_begin>match->value_end ||
           match->value_end>raw.size() ||
           raw.substr(match->value_begin,match->value_end-match->value_begin)
             !=c.original)
            return fail("CUE_STALE_VALUE");
        if(!match->quoted && (c.proposed.front()==' ' ||
            c.proposed.back()==' ' || c.proposed.front()=='\t' ||
            c.proposed.back()=='\t'))
            return fail("CUE_UNQUOTED_WHITESPACE");
        spans.push_back({match->value_begin,match->value_end,c.proposed});
    }
    std::sort(spans.begin(),spans.end(),
        [](const Span& a,const Span& b){return a.begin>b.begin;});
    std::size_t previous=raw.size();
    for(const auto& s:spans){
        if(s.begin>s.end || s.end>previous)
            return fail("CUE_SPANS_OVERLAP");
        previous=s.begin;
    }
    std::string post(raw);
    for(const auto& s:spans)
        post.replace(s.begin,s.end-s.begin,s.replacement);
    if(post.size()>8u*1024u*1024u)
        return fail("CUE_RESULT_TOO_LARGE");
    const auto updated=inspect_cue_metadata(post,kind);
    if(updated.status!=CueSyntaxStatus::Parsed ||
       updated.encoding!=original.encoding ||
       updated.carrier!=original.carrier ||
       updated.files.size()!=original.files.size() ||
       updated.tracks.size()!=original.tracks.size())
        return fail("CUE_STRUCTURE_CHANGED");
    for(std::size_t i=0;i<original.files.size();++i){
        const auto& a=original.files[i];const auto& b=updated.files[i];
        if(a.filename!=b.filename || a.file_type!=b.file_type ||
           a.line_number!=b.line_number)
            return fail("CUE_FILES_CHANGED");
    }
    for(std::size_t i=0;i<original.tracks.size();++i){
        const auto& a=original.tracks[i];const auto& b=updated.tracks[i];
        if(a.ordinal!=b.ordinal ||
           a.declared_track_number!=b.declared_track_number ||
           a.track_type!=b.track_type ||
           a.file_reference_index!=b.file_reference_index ||
           a.line_number!=b.line_number)
            return fail("CUE_TRACKS_CHANGED");
    }
    const auto compare=[&](const std::vector<CueMetadataField>& a,
        const std::vector<CueMetadataField>& b,
        std::optional<std::size_t> track) {
        if(a.size()!=b.size())return false;
        for(std::size_t i=0;i<a.size();++i){
            if(a[i].name!=b[i].name || a[i].scope!=b[i].scope ||
               a[i].line_number!=b[i].line_number ||
               a[i].quoted!=b[i].quoted)return false;
            std::string expected=a[i].value;
            for(const auto& c:changes)
                if(c.track==track && c.token==a[i].name)
                    expected=c.proposed;
            if(b[i].value!=expected)return false;
        }
        return true;
    };
    if(!compare(original.globals,updated.globals,std::nullopt))
        return fail("CUE_GLOBALS_CHANGED");
    for(std::size_t i=0;i<original.tracks.size();++i)
        if(!compare(original.tracks[i].local_fields,
                    updated.tracks[i].local_fields,i))
            return fail("CUE_METADATA_CHANGED");
    CueValuePostimage out;
    out.eligible=true;out.changed=post!=raw;out.bytes=std::move(post);
    return out;
}
} // namespace djmeta
