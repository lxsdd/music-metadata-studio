#pragma once

// Official MusicBrainz WS/2 read-only provider contract, pure C++20.
// No network, SDK, tokens, filesystem, credentials, or writes in this layer.
// A host may execute ONLY the fixed HTTPS paths produced here after a click.
#include "djmeta/online_fields.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta::online::musicbrainz {

enum class SearchKind { Recording, Release };
struct SearchCandidate {
    SearchKind kind = SearchKind::Recording;
    std::string mbid;
    std::string title;
    std::string artist;
    std::string release_date;
    // Optional recording-only API evidence; never a tag write or automatic
    // choice between multiple legitimate ISRC identities.
    std::vector<std::string> recording_isrcs;
    std::int64_t duration_ms = -1;
    int search_score = 0; // MusicBrainz search rank, NOT match confidence.
};
struct SearchResult {
    SearchKind kind = SearchKind::Recording;
    std::vector<SearchCandidate> candidates;
};
struct Json {
    enum class Type { Null, Bool, Number, String, Object, Array };
    Type type = Type::Null;
    std::string text;
    std::map<std::string, Json> object;
    std::vector<Json> array;
    const Json* get(std::string_view key) const {
        if (type != Type::Object) return nullptr;
        auto it = object.find(std::string(key));
        return it == object.end() ? nullptr : &it->second;
    }
    std::string str() const { return type == Type::String ? text : std::string{}; }
};
class JsonParser {
public:
    explicit JsonParser(std::string_view source) : source_(source) {
        if (source.size() > 2u * 1024u * 1024u)
            throw std::invalid_argument("MusicBrainz response exceeds 2 MiB");
    }
    Json parse() {
        auto root = value(0);
        ws();
        if (pos_ != source_.size()) fail("trailing JSON");
        return root;
    }
private:
    std::string_view source_;
    std::size_t pos_ = 0;
    std::size_t nodes_ = 0;
    [[noreturn]] void fail(const char* message) const {
        throw std::invalid_argument(
            std::string("Invalid MusicBrainz JSON: ") + message);
    }
    void ws() {
        while (pos_ < source_.size() && (source_[pos_] == ' ' ||
               source_[pos_] == '\n' || source_[pos_] == '\r' ||
               source_[pos_] == '\t')) ++pos_;
    }
    bool take(char expected) {
        ws();
        if (pos_ < source_.size() && source_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }
    void need(char expected) { if (!take(expected)) fail("unexpected JSON token"); }
    static void append_unicode(std::string& out, std::uint32_t code) {
        if (code <= 0x7f) out.push_back(static_cast<char>(code));
        else if (code <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 63)));
        } else if (code <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (code & 63)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 63)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (code & 63)));
        }
    }
    std::uint32_t hex4() {
        if (pos_ + 4 > source_.size()) fail("short unicode escape");
        std::uint32_t n = 0;
        for (int i=0;i<4;++i) {
            const char c=source_[pos_++];
            n <<= 4;
            if (c >= '0' && c <= '9') n |= static_cast<unsigned>(c-'0');
            else if(c >= 'a' && c <= 'f') n |= static_cast<unsigned>(c-'a'+10);
            else if(c >= 'A' && c <= 'F') n |= static_cast<unsigned>(c-'A'+10);
            else fail("invalid unicode hex");
        }
        return n;
    }
    std::string string() {
        need('"');
        std::string result;
        while (pos_ < source_.size()) {
            unsigned char c=static_cast<unsigned char>(source_[pos_++]);
            if (c == '"') {
                if (!valid_utf8_metadata_text(result))
                    fail("invalid UTF-8");
                return result;
            }
            if (c < 0x20) fail("raw control char in string");
            if (c != '\\') { result.push_back(static_cast<char>(c)); continue; }
            if (pos_ == source_.size()) fail("short string escape");
            const char esc=source_[pos_++];
            switch(esc) {
            case '"': case '\\': case '/': result.push_back(esc); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                std::uint32_t code=hex4();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (pos_ + 2 > source_.size() ||
                        source_[pos_++] != '\\' || source_[pos_++] != 'u')
                        fail("unpaired surrogate");
                    const auto low=hex4();
                    if (low < 0xdc00 || low > 0xdfff) fail("bad surrogate pair");
                    code=0x10000 + ((code-0xd800) << 10) + (low-0xdc00);
                } else if (code >= 0xdc00 && code <= 0xdfff) {
                    fail("unpaired low surrogate");
                }
                append_unicode(result, code);
                break;
            }
            default: fail("bad string escape");
            }
        }
        fail("unterminated string");
    }
    Json value(std::size_t depth) {
        if (depth > 32 || ++nodes_ > 40000) fail("JSON nesting/node limit");
        ws();
        if (pos_ >= source_.size()) fail("missing value");
        Json out;
        const char c=source_[pos_];
        if (c == '"') { out.type=Json::Type::String; out.text=string(); }
        else if (c == '{') {
            out.type=Json::Type::Object;
            ++pos_;
            if (!take('}')) while (true) {
                const auto name=string();
                need(':');
                auto entry=value(depth+1);
                if (!out.object.emplace(name,std::move(entry)).second)
                    fail("duplicate object key");
                if (take('}')) break;
                need(',');
            }
        } else if (c == '[') {
            out.type=Json::Type::Array;
            ++pos_;
            if (!take(']')) while (true) {
                out.array.push_back(value(depth+1));
                if (take(']')) break;
                need(',');
            }
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            out.type=Json::Type::Number;
            const auto start=pos_;
            if (source_[pos_] == '-') ++pos_;
            if (pos_ >= source_.size()) fail("invalid number");
            if (source_[pos_] == '0') ++pos_;
            else {
                if (source_[pos_] < '1' || source_[pos_] > '9')
                    fail("invalid number");
                while (pos_ < source_.size() && source_[pos_] >= '0' &&
                       source_[pos_] <= '9') ++pos_;
            }
            if (pos_ < source_.size() && source_[pos_] == '.') {
                ++pos_;
                const auto start_digits=pos_;
                while (pos_ < source_.size() && source_[pos_] >= '0' &&
                       source_[pos_] <= '9') ++pos_;
                if (pos_ == start_digits) fail("invalid decimal");
            }
            if (pos_ < source_.size() &&
                (source_[pos_] == 'E' || source_[pos_] == 'e')) {
                ++pos_;
                if (pos_ < source_.size() &&
                    (source_[pos_] == '-' || source_[pos_] == '+')) ++pos_;
                const auto start_digits=pos_;
                while (pos_ < source_.size() && source_[pos_] >= '0' &&
                       source_[pos_] <= '9') ++pos_;
                if (pos_ == start_digits) fail("invalid exponent");
            }
            out.text=std::string(source_.substr(start,pos_-start));
        } else {
            const auto remaining=source_.substr(pos_);
            if (remaining.starts_with("true")) {
                out.type=Json::Type::Bool; out.text="true"; pos_+=4;
            } else if (remaining.starts_with("false")) {
                out.type=Json::Type::Bool; out.text="false"; pos_+=5;
            } else if (remaining.starts_with("null")) {
                pos_+=4;
            } else fail("unknown token");
        }
        return out;
    }
};

inline std::int64_t number_or(const Json* value, std::int64_t fallback = -1) {
    if (!value || value->type != Json::Type::Number) return fallback;
    std::int64_t num=0;
    const auto p=std::from_chars(value->text.data(),
        value->text.data()+value->text.size(),num);
    return p.ec == std::errc{} &&
        p.ptr == value->text.data()+value->text.size() ? num : fallback;
}
inline std::string str(const Json& obj, std::string_view field) {
    const auto* item=obj.get(field);
    return item ? item->str() : std::string{};
}
inline bool valid_mbid(std::string_view id) {
    if (id.size() != 36) return false;
    for (std::size_t i=0;i<36;++i) {
        const char c=id[i];
        if (i==8||i==13||i==18||i==23) {
            if (c!='-') return false;
        } else if (!((c>='0'&&c<='9') || (c>='a'&&c<='f') ||
                     (c>='A'&&c<='F'))) return false;
    }
    return true;
}
inline std::string artist_credit(const Json& obj) {
    const auto* arr=obj.get("artist-credit");
    if (!arr || arr->type != Json::Type::Array) return {};
    std::string combined;
    for (const auto& piece : arr->array) {
        if (piece.type == Json::Type::String) {
            combined += piece.text;
        } else if (piece.type == Json::Type::Object) {
            auto label=str(piece,"name");
            if (label.empty()) {
                const auto* artist=piece.get("artist");
                if (artist) label=str(*artist,"name");
            }
            combined += label;
            combined += str(piece,"joinphrase");
        }
        if (combined.size()>512) return {};
    }
    return combined;
}
inline std::string encode_url_query(std::string_view value) {
    static constexpr char hex[]="0123456789ABCDEF";
    std::string out;
    for (const unsigned char c:value) {
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
            (c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')
            out.push_back(static_cast<char>(c));
        else {
            out.push_back('%');
            out.push_back(hex[c>>4]);
            out.push_back(hex[c&15]);
        }
    }
    return out;
}
// Optional ARTIST evidence must not make an otherwise valid TITLE/ISRC
// search unusable. Keep the original tag untouched but never transmit an
// invalid, control-character-bearing or unbounded Lucene term.
inline bool valid_search_term(std::string_view value) {
    if(value.empty() || value.size()>150 || !valid_utf8_metadata_text(value))
        return false;
    for(const unsigned char ch:value)
        if(ch<0x20u || ch==0x7fu) return false;
    return true;
}
inline std::string lucene_literal(std::string_view value) {
    if (!valid_search_term(value))
        throw std::invalid_argument("MusicBrainz search term invalid or too long");
    std::string out="\"";
    for (char c:value) {
        // Backslash and quote are escaped within Lucene quoted strings;
        // every ASCII control and DEL is forbidden even when UTF-8-valid.
        // Percent-encoding alone cannot make such invisible metadata a
        // qualified, user-intended provider query.
        if (static_cast<unsigned char>(c) < 0x20u ||
            static_cast<unsigned char>(c) == 0x7fu)
            throw std::invalid_argument("MusicBrainz search control character");
        if (c=='"' || c=='\\') out.push_back('\\');
        out.push_back(c);
    }
    out += '"';
    return out;
}
inline std::string make_search_path(SearchKind kind,
                                    std::string_view title,
                                    std::string_view artist) {
    const auto title_query=lucene_literal(title);
    const auto query=(kind == SearchKind::Recording ? "recording:" : "release:")+
        title_query+(artist.empty() ? "" : " AND artist:"+lucene_literal(artist));
    return std::string(kind==SearchKind::Recording
        ? "/ws/2/recording?query=" : "/ws/2/release?query=") +
        encode_url_query(query)+"&fmt=json&limit=8";
}

struct RecordingSearchAttempt {
    std::string label; // Explicit, human-readable provenance of one HTTPS request.
    std::string path;
};
struct RecordingSearchPlan {
    std::string full_title;
    std::string core_title;
    std::string artist;
    std::vector<RecordingSearchAttempt> attempts;
};

// Only a bounded plan produced by an explicit Search gesture may continue
// after an empty provider response. The host separately rechecks source,
// selected tab, cancellation generation and fresh physical/CUE snapshot.
inline constexpr bool continue_empty_search_plan(
    std::size_t attempted_index, std::size_t total,
    bool result_empty, bool context_qualified) noexcept {
    return context_qualified && result_empty &&
           total>1 && total<=8 && attempted_index<total-1;
}

// Every explicit additional Search click may inspect another bounded
// strategy, even when the previous stage returned unrelated candidates.
// Fresh sources/queries start at zero; an exhausted plan cycles to zero.
inline constexpr std::size_t next_clicked_search_attempt(
    std::size_t attempted_index, std::size_t total,
    bool same_qualified_result_view) noexcept {
    if(!same_qualified_result_view || total==0 || total>8 ||
       attempted_index>=total-1) return 0;
    return attempted_index+1;
}

// Exact recording/mix identity and search recall are separate questions.
// Remove a *known trailing* parenthetical version ONLY for discovery, never
// for metadata replacement or match approval. Unknown parentheticals stay.
inline std::string search_title_core(std::string_view full) {
    std::string trimmed(full);
    const auto end=trimmed.find_last_not_of(" \t");
    if(end==std::string::npos) return {};
    trimmed.resize(end+1);
    if(trimmed.size()<4 || (trimmed.back()!=')' && trimmed.back()!=']'))
        return trimmed;
    const char opening=trimmed.back()==')' ? '(' : '[';
    const auto pos=trimmed.find_last_of(opening);
    if(pos==std::string::npos || pos==0) return trimmed;
    std::string qualifier=trimmed.substr(pos+1,trimmed.size()-pos-2);
    for(char& c:qualifier) if(c>='A'&&c<='Z') c=static_cast<char>(c+('a'-'A'));
    static constexpr std::string_view generic[]={
        "original version","original mix","original","album version",
        "album mix","radio edit","radio version","radio mix",
        "extended version","extended mix","extended","club mix",
        "single version","single mix","instrumental","edit",
        "remastered","remaster","remastered version",
        "dj version","dj mix","dj edit"
    };
    bool known=false;
    for(auto key:generic) if(qualifier==key) {known=true;break;}
    // DJs frequently have source TITLE="Song (Remixer Remix)" while the
    // MusicBrainz recording is indexed as "Song". Broaden DISCOVERY ONLY,
    // never alter the original full title or grant an identity match.
    // Do not strip unknown parentheticals such as "(Live at the Arena)".
    if(!known) {
        for(const std::string_view suffix : {" remix", " rework"}) {
            if(qualifier.size() > suffix.size() &&
               qualifier.ends_with(suffix)) {
                const auto remixer=std::string_view(qualifier).substr(
                    0,qualifier.size()-suffix.size());
                if(remixer.find_first_not_of(" \t")!=std::string_view::npos) {
                    known=true;
                    break;
                }
            }
        }
    }
    if(!known) return trimmed;
    std::string core=trimmed.substr(0,pos);
    const auto last=core.find_last_not_of(" \t");
    if(last==std::string::npos) return trimmed;
    core.resize(last+1);
    return core;
}

// Extract a leading artist for OPTIONAL search recall when the recording
// credit is a collaboration. This is deliberately not a normalization rule:
// the complete ARTIST value and exact candidate identity are preserved.
inline std::string lead_credit_search_artist(std::string_view credit) {
    const auto first=credit.find_first_not_of(" \t");
    if(first==std::string_view::npos) return {};
    const auto last=credit.find_last_not_of(" \t");
    const std::string trimmed(credit.substr(first,last-first+1));
    std::string folded=trimmed;
    for(char& c:folded)
        if(c>='A' && c<='Z') c=static_cast<char>(c+('a'-'A'));
    static constexpr std::string_view separators[]={
        " x ", " vs. ", " vs ", " feat. ", " feat ",
        " ft. ", " ft ", " featuring ", " w/ "
    };
    std::size_t cut=std::string::npos;
    for(const auto separator:separators) {
        const auto pos=folded.find(separator);
        if(pos!=std::string::npos && (cut==std::string::npos || pos<cut))
            cut=pos;
    }
    if(cut==std::string::npos) return {};
    std::string lead=trimmed.substr(0,cut);
    const auto end=lead.find_last_not_of(" \t");
    if(end==std::string::npos) return {};
    lead.resize(end+1);
    return lead.size()>=3 ? lead : std::string{};
}

// MusicBrainz's one-request-per-second boundary is enforced by the HTTP
// worker. Staged manual discovery must wait rather than reject a valid
// second click; the UI thread must never sleep.
inline constexpr std::uint64_t minimum_request_spacing_ms=1200;
inline std::uint64_t request_spacing_wait_ms(
    std::uint64_t last_tick, std::uint64_t now_tick) noexcept {
    if(last_tick==0) return 0;
    if(now_tick<last_tick) return minimum_request_spacing_ms;
    const auto elapsed=now_tick-last_tick;
    return elapsed>=minimum_request_spacing_ms?0:
        minimum_request_spacing_ms-elapsed;
}

inline bool valid_search_isrc(std::string_view value) {
    if(value.size()!=12) return false;
    for(std::size_t i=0;i<value.size();++i) {
        const char c=value[i];
        const bool letter=(c>='A'&&c<='Z')||(c>='a'&&c<='z');
        const bool digit=(c>='0'&&c<='9');
        if((i<2 && !letter) || (i>=2 && i<5 && !letter && !digit) ||
           (i>=5 && !digit)) return false;
    }
    return true;
}

inline RecordingSearchPlan recording_search_plan(
    std::string_view full_title, std::string_view artist,
    std::string_view isrc = {}) {
    RecordingSearchPlan p;
    p.full_title=std::string(full_title);
    p.core_title=search_title_core(full_title);
    p.artist=std::string(artist);
    const auto add=[&](std::string label,std::string path) {
        for(const auto& a:p.attempts) if(a.path==path) return;
        p.attempts.push_back({std::move(label),std::move(path)});
    };
    if(valid_search_isrc(isrc)) {
        std::string code(isrc);
        for(char& ch:code) if(ch>='a'&&ch<='z') ch=static_cast<char>(ch-32);
        add("Exact ISRC identifier", "/ws/2/recording?query="+
            encode_url_query("isrc:"+code)+"&fmt=json&limit=8");
    }
    // The original TITLE may be very long or include malformed/invisible
    // bytes. Never send a lossy substring from an unsafe source. If its
    // text is clean but merely overlong, a qualified shorter mix-core is
    // still useful for read-only discovery. A valid ISRC is always allowed
    // independently; an unqualified TITLE cannot cancel that lookup.
    const bool clean_source_title=valid_utf8_metadata_text(full_title) &&
        std::none_of(full_title.begin(),full_title.end(),[](unsigned char c) {
            return c<0x20u || c==0x7fu;
        });
    const bool core_ok=clean_source_title && valid_search_term(p.core_title);
    const bool full_ok=clean_source_title && valid_search_term(p.full_title);
    if(!core_ok && !full_ok) {
        if(p.attempts.empty())
            throw std::invalid_argument("A qualified title or valid ISRC is required for discovery");
        return p;
    }
    const auto add_titles=[&](std::string_view who, std::string core_label,
                              std::string full_label) {
        if(core_ok) add(std::move(core_label),
            make_search_path(SearchKind::Recording,p.core_title,who));
        if(full_ok && (!core_ok || p.core_title!=p.full_title))
            add(std::move(full_label),
                make_search_path(SearchKind::Recording,p.full_title,who));
    };
    if(valid_search_term(p.artist)) {
        add_titles(p.artist,
            p.full_title==p.core_title ? "Original title + artist" :
                "Main title + artist (version kept for review)",
            "Full title + artist");
    }
    // Collaboration fallbacks are strictly discovery-only, never approvals.
    const auto lead=lead_credit_search_artist(p.artist);
    if(!lead.empty() && lead!=p.artist && valid_search_term(lead)) {
        add_titles(lead,
            p.full_title==p.core_title ? "Original title + lead credited artist" :
                "Main title + lead credited artist (identity review)",
            "Full title + lead credited artist (identity review)");
    }
    add_titles("",
        p.full_title==p.core_title ? "Title only" :
            "Main title only (version kept for review)",
        "Full title only");
    return p;
}

// Albums/CUE releases are often indexed with a different joined or release
// artist credit. A second *user-triggered* exact-title-only query improves
// discovery without treating missing artist evidence as a match.
inline std::vector<RecordingSearchAttempt> release_search_plan(
    std::string_view album, std::string_view artist) {
    std::vector<RecordingSearchAttempt> attempts;
    if(valid_search_term(artist))
        attempts.push_back({"Release title + album artist",
            make_search_path(SearchKind::Release,album,artist)});
    attempts.push_back({"Release title only (artist unverified)",
        make_search_path(SearchKind::Release,album,"")});
    return attempts;
}

inline std::string comparable_search_key(std::string_view v) {
    std::string s;
    bool separator=false;
    for(unsigned char c:v) {
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c>=128) {
            if(separator && !s.empty()) s.push_back(' ');
            separator=false;
            s.push_back(static_cast<char>(
                c>='A'&&c<='Z'?c+('a'-'A'):c));
        } else separator=true;
    }
    return s;
}

// Descriptive alignment, NOT automatic confidence or permission to import.
inline std::string recording_identity_label(std::string_view source_title,
    std::string_view source_artist, std::string_view candidate_title,
    std::string_view candidate_artist) {
    const auto artist=comparable_search_key(source_artist);
    const auto matched_artist=comparable_search_key(candidate_artist);
    // An absent local TITLE or ARTIST must not masquerade as confirmation:
    // distinguish exact-title/artist-unknown from version differences and
    // unrelated candidates. None of these labels is write authorization.
    const bool artist_verified=!artist.empty() && !matched_artist.empty();
    if(artist_verified && artist!=matched_artist)
        return "Artist differs";
    const auto full=comparable_search_key(source_title);
    const auto match=comparable_search_key(candidate_title);
    if(full==match && !full.empty())
        return artist_verified ? "Title+artist" : "Artist unverified";
    const auto core=comparable_search_key(search_title_core(source_title));
    const auto candidate_core=comparable_search_key(search_title_core(candidate_title));
    if(!core.empty() && core==candidate_core)
        return artist_verified ? "Version review" : "Version and artist review";
    return "Identity review";
}

inline std::string make_release_lookup_path(std::string_view mbid) {
    if (!valid_mbid(mbid))
        throw std::invalid_argument("Invalid MusicBrainz release identity");
    return "/ws/2/release/"+std::string(mbid)+
        "?inc=recordings%2Bartist-credits%2Bisrcs&fmt=json";
}

inline SearchResult parse_search(std::string_view json, SearchKind kind) {
    const auto root=JsonParser(json).parse();
    if (root.type != Json::Type::Object)
        throw std::invalid_argument("MusicBrainz search response must be an object");
    const auto* arr=root.get(kind==SearchKind::Recording?"recordings":"releases");
    if (!arr || arr->type != Json::Type::Array)
        throw std::invalid_argument("MusicBrainz response has no expected candidates");
    SearchResult output;
    output.kind=kind;
    // Provider relevance order is meaningful; retain the FIRST valid
    // occurrence of each exact MBID, not multiple rows with one identity.
    // Never deduplicate by title/artist: versions and editions may share names.
    std::set<std::string> observed_mbids;
    for (const auto& raw:arr->array) {
        if (output.candidates.size() == 8) break;
        if (raw.type != Json::Type::Object) continue;
        SearchCandidate row;
        row.kind=kind;
        row.mbid=str(raw,"id");
        row.title=str(raw,"title");
        row.artist=artist_credit(raw);
        row.release_date=str(raw,"date");
        // Search results do not guarantee isrcs. If MusicBrainz explicitly
        // supplies them, keep distinct valid identifiers as read-only
        // recording evidence. An unsupported/oversized list stays absent;
        // it must never be guessed from an edition or written to the source.
        if (kind==SearchKind::Recording) {
            const auto* isrcs=raw.get("isrcs");
            if (isrcs && isrcs->type==Json::Type::Array &&
                isrcs->array.size()<=32) {
                std::set<std::string> unique;
                for (const auto& item:isrcs->array) {
                    if (item.type!=Json::Type::String ||
                        !valid_search_isrc(item.text)) continue;
                    auto code=item.text;
                    for (auto& ch:code)
                        if (ch>='a' && ch<='z')
                            ch=static_cast<char>(ch-('a'-'A'));
                    unique.insert(std::move(code));
                }
                row.recording_isrcs.assign(unique.begin(),unique.end());
            }
        }
        const auto length=number_or(raw.get("length"));
        if (length>=0 && length<=10LL*60*60*1000) row.duration_ms=length;
        const auto score=number_or(raw.get("score"),0);
        row.search_score=static_cast<int>((std::clamp<std::int64_t>)(score,0,100));
        if (!valid_mbid(row.mbid) ||
            row.title.empty() || row.artist.empty() ||
            row.title.size()>1024 || row.artist.size()>512 ||
            !observed_mbids.insert(row.mbid).second) continue;
        output.candidates.push_back(std::move(row));
    }
    return output;
}

inline ReleaseEdition parse_release_lookup(std::string_view json,
                                           std::string_view required_mbid) {
    if (!valid_mbid(required_mbid))
        throw std::invalid_argument("Missing verified release MBID");
    const auto obj=JsonParser(json).parse();
    if (obj.type!=Json::Type::Object ||
        str(obj,"id")!=required_mbid)
        throw std::invalid_argument("Unexpected MusicBrainz release identity");
    ReleaseEdition edition;
    edition.provider="musicbrainz";
    edition.edition_id=std::string(required_mbid);
    const auto title=str(obj,"title");
    const auto artist=artist_credit(obj);
    if (!title.empty())
        edition.fields.push_back({"ALBUM",{title},"musicbrainz",edition.edition_id,
                                  EvidenceScope::Edition,DateMeaning::NotDate});
    if (!artist.empty())
        edition.fields.push_back({"ALBUM ARTIST",{artist},"musicbrainz",edition.edition_id,
                                  EvidenceScope::Edition,DateMeaning::NotDate});
    const auto date=str(obj,"date");
    if (!date.empty())
        edition.fields.push_back({"DATE",{date},"musicbrainz",edition.edition_id,
                                  EvidenceScope::Edition,DateMeaning::EditionRelease});
    const auto* media=obj.get("media");
    if (!media || media->type!=Json::Type::Array)
        throw std::invalid_argument("MusicBrainz release missing track media");
    std::size_t disc=0;
    // A release-track MBID identifies ONE exact edition position. The
    // recording MBID may legitimately recur; never confuse the two.
    std::set<std::string> release_track_ids;
    for (const auto& medium:media->array) {
        if (medium.type!=Json::Type::Object) continue;
        ++disc;
        const auto* tracks=medium.get("tracks");
        if (!tracks || tracks->type!=Json::Type::Array)
            throw std::invalid_argument("MusicBrainz release has incomplete tracklist");
        for (const auto& raw:tracks->array) {
            if (edition.tracks.size()==256)
                throw std::invalid_argument("MusicBrainz release too many tracks");
            if (raw.type!=Json::Type::Object)
                throw std::invalid_argument("MusicBrainz malformed release track");
            EditionTrack row;
            const auto* rec=raw.get("recording");
            // The release track MBID identifies this edition position; the recording
            // MBID identifies audio and may legitimately recur across tracks.
            // Never substitute a recording ID for a missing release-track ID.
            row.source_track_id=str(raw,"id");
            if(!row.source_track_id.empty() &&
               (!valid_mbid(row.source_track_id) ||
                !release_track_ids.insert(row.source_track_id).second))
                throw std::invalid_argument(
                    "MusicBrainz release has ambiguous track MBID");
            row.recording.title=str(raw,"title");
            if (row.recording.title.empty()&&rec)
                row.recording.title=str(*rec,"title");
            row.recording.primary_artist=artist_credit(raw);
            if (row.recording.primary_artist.empty()&&rec)
                row.recording.primary_artist=artist_credit(*rec);
            if (row.recording.primary_artist.empty())
                row.recording.primary_artist=artist;
            const auto duration=number_or(raw.get("length"),
                                           rec?number_or(rec->get("length")):-1);
            row.recording.duration_ms=(duration>=0&&duration<=10LL*60*60*1000)?
                duration:-1;
            const auto position=number_or(raw.get("position"));
            row.disc_number=static_cast<int>(disc);
            row.track_number=position>=1&&position<=999?
                static_cast<int>(position):-1;
            if (rec) {
                const auto* isrc=rec->get("isrcs");
                if (isrc&&isrc->type==Json::Type::Array&&
                    isrc->array.size()==1&&isrc->array.front().type==Json::Type::String&&
                    valid_search_isrc(isrc->array.front().text))
                    row.recording.isrc=isrc->array.front().text;
            }
            if (row.recording.title.empty()||row.recording.primary_artist.empty())
                throw std::invalid_argument("MusicBrainz release track identity incomplete");
            edition.tracks.push_back(std::move(row));
        }
    }
    if (edition.tracks.empty())
        throw std::invalid_argument("MusicBrainz release tracklist empty");
    return edition;
}
} // namespace djmeta::online::musicbrainz
