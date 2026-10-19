#include "djmeta/precision_cue_apple.h"
#include "djmeta/precision_cue_deezer.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta {
namespace {

[[noreturn]] void reject(const char* why) {
    throw std::invalid_argument(why);
}

std::int64_t positive_number(std::string_view s) {
    if (s.empty() || s.size() > 19) reject("invalid Apple integer");
    for (char c : s) if (c < '0' || c > '9') reject("non-integer Apple field");
    std::int64_t n = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (ec != std::errc{} || p != s.data() + s.size() || n <= 0)
        reject("Apple integer out of range");
    return n;
}

bool utf8_valid(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80u) { ++i; continue; }
        unsigned bytes = 0, cp = 0, minimum = 0;
        if ((c & 0xe0u) == 0xc0u) { bytes = 2; cp = c & 0x1fu; minimum = 0x80u; }
        else if ((c & 0xf0u) == 0xe0u) { bytes = 3; cp = c & 0x0fu; minimum = 0x800u; }
        else if ((c & 0xf8u) == 0xf0u) { bytes = 4; cp = c & 0x07u; minimum = 0x10000u; }
        else return false;
        if (bytes > text.size() - i) return false;
        for (unsigned k = 1; k < bytes; ++k) {
            const auto x = static_cast<unsigned char>(text[i + k]);
            if ((x & 0xc0u) != 0x80u) return false;
            cp = (cp << 6) | (x & 0x3fu);
        }
        if (cp < minimum || cp > 0x10ffffu ||
            (cp >= 0xd800u && cp <= 0xdfffu)) return false;
        i += bytes;
    }
    return true;
}

void put_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80u) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xc0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 63u)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xe0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 63u)));
        out.push_back(static_cast<char>(0x80u | (cp & 63u)));
    } else {
        out.push_back(static_cast<char>(0xf0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 63u)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 63u)));
        out.push_back(static_cast<char>(0x80u | (cp & 63u)));
    }
}

struct JsonValue {
    enum class Type { Object, Array, String, Number, Literal } type = Type::Literal;
    std::map<std::string, JsonValue> fields;
    std::vector<JsonValue> items;
    std::string raw;
    const JsonValue* find(std::string_view key) const {
        const auto it = fields.find(std::string(key));
        return it == fields.end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {
        if (text.empty() || text.size() > 4 * 1024 * 1024 || !utf8_valid(text))
            reject("invalid Apple JSON length/UTF-8");
    }
    JsonValue parse() {
        auto v = value(0);
        spaces();
        if (pos_ != text_.size()) reject("trailing Apple JSON data");
        return v;
    }
private:
    void spaces() {
        while (pos_ < text_.size() &&
              (text_[pos_] == ' ' || text_[pos_] == '\n' ||
               text_[pos_] == '\r' || text_[pos_] == '\t')) ++pos_;
    }
    bool take(char c) {
        spaces();
        if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; }
        return false;
    }
    void need(char c) { if (!take(c)) reject("malformed Apple JSON punctuation"); }
    unsigned hex4() {
        if (text_.size() - pos_ < 4) reject("truncated JSON Unicode escape");
        unsigned n = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            n <<= 4;
            if (c >= '0' && c <= '9') n |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') n |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') n |= static_cast<unsigned>(c - 'A' + 10);
            else reject("invalid JSON Unicode escape");
        }
        return n;
    }
    std::string str() {
        need('"');
        std::string out;
        while (pos_ < text_.size()) {
            const auto c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return out;
            if (c < 0x20u) reject("unescaped JSON control");
            if (c != '\\') out.push_back(static_cast<char>(c));
            else {
                if (pos_ == text_.size()) reject("truncated JSON escape");
                const char e = text_[pos_++];
                switch (e) {
                    case '"': case '\\': case '/': out.push_back(e); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        unsigned cp = hex4();
                        if (cp >= 0xd800u && cp <= 0xdbffu) {
                            if (text_.size() - pos_ < 2 || text_.substr(pos_, 2) != "\\u")
                                reject("unpaired JSON surrogate");
                            pos_ += 2;
                            const unsigned lo = hex4();
                            if (lo < 0xdc00u || lo > 0xdfffu) reject("invalid JSON surrogate");
                            cp = 0x10000u + ((cp - 0xd800u) << 10) + (lo - 0xdc00u);
                        } else if (cp >= 0xdc00u && cp <= 0xdfffu) reject("unpaired JSON surrogate");
                        put_utf8(out, cp);
                        break;
                    }
                    default: reject("unknown JSON escape");
                }
            }
            if (out.size() > 8192) reject("Apple string too large");
        }
        reject("unterminated JSON string");
    }
    JsonValue value(unsigned depth) {
        if (depth > 16 || ++nodes_ > 20000) reject("Apple JSON complexity limit");
        spaces();
        if (pos_ == text_.size()) reject("truncated Apple JSON");
        JsonValue v;
        if (take('{')) {
            v.type = JsonValue::Type::Object;
            if (take('}')) return v;
            for (;;) {
                spaces();
                const auto key = str();
                need(':');
                auto child = value(depth + 1);
                if (!v.fields.emplace(key, std::move(child)).second)
                    reject("duplicate Apple JSON key");
                if (take('}')) return v;
                need(',');
            }
        }
        if (take('[')) {
            v.type = JsonValue::Type::Array;
            if (take(']')) return v;
            for (;;) {
                v.items.push_back(value(depth + 1));
                if (take(']')) return v;
                need(',');
            }
        }
        if (text_[pos_] == '"') {
            v.type = JsonValue::Type::String;
            v.raw = str();
            return v;
        }
        if (text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
            const auto begin = pos_;
            if (text_[pos_] == '-') ++pos_;
            if (pos_ == text_.size()) reject("truncated JSON number");
            if (text_[pos_] == '0') ++pos_;
            else {
                if (text_[pos_] < '1' || text_[pos_] > '9') reject("invalid JSON number");
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            }
            if (pos_ < text_.size() && text_[pos_] == '.') {
                ++pos_;
                if (pos_ == text_.size() || text_[pos_] < '0' || text_[pos_] > '9')
                    reject("invalid JSON fraction");
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            }
            if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
                ++pos_;
                if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
                if (pos_ == text_.size() || text_[pos_] < '0' || text_[pos_] > '9')
                    reject("invalid JSON exponent");
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            }
            v.type = JsonValue::Type::Number;
            v.raw = std::string(text_.substr(begin, pos_ - begin));
            return v;
        }
        for (const auto literal : {"true", "false", "null"}) {
            const std::string_view token = literal;
            if (text_.substr(pos_, token.size()) == token) {
                pos_ += token.size();
                v.raw = std::string(token);
                return v;
            }
        }
        reject("invalid Apple JSON value");
    }
    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t nodes_ = 0;
};

const JsonValue& get(const JsonValue& root, std::string_view key, JsonValue::Type type) {
    const auto* ptr = root.find(key);
    if (!ptr || ptr->type != type) reject("missing or invalid Apple field type");
    return *ptr;
}

std::string text(const JsonValue& v, std::string_view field) {
    auto x = get(v, field, JsonValue::Type::String).raw;
    if (x.empty() || x.size() > 4096 || !utf8_valid(x)) reject("invalid Apple text");
    for (unsigned char c : x) if (c < 0x20u || c == 0x7fu) reject("Apple control in metadata");
    return x;
}

std::int64_t number(const JsonValue& v, std::string_view field) {
    return positive_number(get(v, field, JsonValue::Type::Number).raw);
}

unsigned position(const JsonValue& v, std::string_view field) {
    const auto n = number(v, field);
    if (n > 99) reject("Apple disc or track index exceeds CUE range");
    return static_cast<unsigned>(n);
}

unsigned track_count(const JsonValue& v) {
    const auto n = number(v, "trackCount");
    if (n > 199) reject("Apple track count exceeds limit");
    return static_cast<unsigned>(n);
}

// Accept only a supported ISO catalog date; never infer a missing DATE.
std::string apple_album_release_date(std::string_view s) {
    if (s.size() != 10 && s.size() != 20)
        reject("unsupported Apple release date");
    const auto digit = [&](std::size_t i) {
        if (s[i] < '0' || s[i] > '9') reject("invalid Apple date digit");
        return static_cast<unsigned>(s[i] - '0');
    };
    if (s[4] != '-' || s[7] != '-') reject("invalid Apple date separators");
    const unsigned year = 1000 * digit(0) + 100 * digit(1) + 10 * digit(2) + digit(3);
    const unsigned month = 10 * digit(5) + digit(6);
    const unsigned day = 10 * digit(8) + digit(9);
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    constexpr unsigned days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (year == 0 || month == 0 || month > 12 || day == 0 ||
        day > days[month] + (month == 2 && leap ? 1u : 0u))
        reject("invalid Apple calendar date");
    if (s.size() == 20) {
        if (s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z' ||
            10 * digit(11) + digit(12) > 23 ||
            10 * digit(14) + digit(15) > 59 ||
            10 * digit(17) + digit(18) > 59)
            reject("invalid Apple UTC date");
    }
    return std::string(s.substr(0, 10));
}

std::string country(std::string_view code) {
    if (code.size() != 2) reject("invalid Apple storefront");
    std::string result;
    for (char c : code) {
        if (c >= 'a' && c <= 'z') result.push_back(static_cast<char>(c - 'a' + 'A'));
        else if (c >= 'A' && c <= 'Z') result.push_back(c);
        else reject("invalid Apple storefront");
    }
    return result;
}

} // namespace

std::string apple_precision_cue_lookup_url(std::string_view collection_id) {
    return "https://itunes.apple.com/lookup?id=" +
           std::to_string(positive_number(collection_id)) +
           "&entity=song&limit=200";
}

std::string apple_precision_cue_lookup_url(
    std::string_view collection_id, std::string_view storefront) {
    return apple_precision_cue_lookup_url(collection_id) +
           "&country=" + country(storefront);
}

std::string apple_precision_cue_album_search_url(
    std::string_view query, std::string_view storefront) {
    if (query.empty() || query.size() > 256 || !utf8_valid(query))
        reject("invalid Apple query");
    std::string escaped;
    bool meaningful = false;
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char c : query) {
        if (c < 0x20u || c == 0x7fu) reject("control in Apple query");
        if (c != ' ') meaningful = true;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' ||
            c == '-' || c == '~') escaped.push_back(static_cast<char>(c));
        else {
            escaped.push_back('%');
            escaped.push_back(hex[c >> 4]);
            escaped.push_back(hex[c & 15u]);
        }
    }
    if (!meaningful) reject("blank Apple query");
    return "https://itunes.apple.com/search?media=music&entity=album&limit=25&country=" +
           country(storefront) + "&term=" + escaped;
}

std::vector<ApplePrecisionCueAlbumHit> parse_apple_precision_cue_album_search(
    std::string_view json) {
    const auto root = JsonParser(json).parse();
    if (root.type != JsonValue::Type::Object) reject("Apple search not an object");
    const auto& list = get(root, "results", JsonValue::Type::Array).items;
    const auto& raw_count = get(root, "resultCount", JsonValue::Type::Number).raw;
    if (list.size() > 25 || raw_count != std::to_string(list.size()))
        reject("Apple search results truncated or excessive");
    std::map<std::string, bool> ids;
    std::vector<ApplePrecisionCueAlbumHit> hits;
    for (const auto& v : list) {
        if (v.type != JsonValue::Type::Object ||
            text(v, "wrapperType") != "collection")
            reject("non-album Apple search hit");
        const auto id = std::to_string(number(v, "collectionId"));
        if (!ids.emplace(id, true).second) reject("duplicate Apple album hit");
        hits.push_back({id, text(v, "collectionName"), text(v, "artistName"),
                        track_count(v)});
    }
    return hits;
}

PrecisionCueRelease parse_apple_precision_cue_lookup(
    std::string_view json, std::string_view expected_collection_id) {
    const auto id = positive_number(expected_collection_id);
    const auto root = JsonParser(json).parse();
    if (root.type != JsonValue::Type::Object) reject("Apple lookup not an object");
    const auto& list = get(root, "results", JsonValue::Type::Array).items;
    const auto n = number(root, "resultCount");
    if (list.size() < 2 || list.size() > 200 ||
        n != static_cast<std::int64_t>(list.size()))
        reject("Apple lookup incomplete");
    PrecisionCueRelease release;
    release.provider = "apple";
    release.provider_release_id = std::to_string(id);
    std::map<std::pair<unsigned, unsigned>, PrecisionCueReleaseTrack> positions;
    std::map<unsigned, unsigned> counts;
    // Reject an overlong release disc before it reaches the native preview.
    std::map<unsigned, std::int64_t> elapsed_by_disc;
    unsigned disc_count = 0, album_count = 0;
    bool found_album = false, saw_track_count = false;
    for (const auto& row : list) {
        if (row.type != JsonValue::Type::Object) reject("invalid Apple result");
        const auto wrapper = text(row, "wrapperType");
        if (number(row, "collectionId") != id) reject("mixed Apple release IDs");
        if (wrapper == "collection") {
            if (found_album) reject("duplicate Apple collection row");
            found_album = true;
            const auto album = text(row, "collectionName");
            const auto artist = text(row, "artistName");
            if ((!release.album_title.empty() && release.album_title != album) ||
                (!release.album_performer.empty() && release.album_performer != artist))
                reject("conflicting Apple album row");
            release.album_title = album;
            release.album_performer = artist;
            album_count = track_count(row);
            if (const auto* date = row.find("releaseDate")) {
                if (date->type != JsonValue::Type::String)
                    reject("invalid Apple album release date type");
                release.release_date =
                    apple_album_release_date(text(row, "releaseDate"));
            }
            if (const auto* genre = row.find("primaryGenreName")) {
                if (genre->type != JsonValue::Type::String)
                    reject("invalid Apple genre type");
                release.genre = text(row, "primaryGenreName");
            }
            continue;
        }
        if (wrapper != "track" || text(row, "kind") != "song")
            reject("unsupported Apple track type");
        const auto album = text(row, "collectionName");
        if (!release.album_title.empty() && release.album_title != album)
            reject("different Apple track album");
        release.album_title = album;
        if (const auto* performer = row.find("collectionArtistName")) {
            if (performer->type != JsonValue::Type::String)
                reject("invalid Apple album artist field");
            auto artist = text(row, "collectionArtistName");
            if (!release.album_performer.empty() && release.album_performer != artist)
                reject("conflicting Apple album artist");
            release.album_performer = std::move(artist);
        }
        const auto disc = position(row, "discNumber");
        const auto track = position(row, "trackNumber");
        const auto ms = number(row, "trackTimeMillis");
        if (disc > 8) reject("Apple release exceeds eight supported discs");
        auto& elapsed = elapsed_by_disc[disc];
        constexpr std::int64_t kMaximumDiscMs = 24LL * 60 * 60 * 1000;
        if (ms > kMaximumDiscMs - elapsed)
            reject("Apple disc cumulative duration exceeds 24 hours");
        elapsed += ms;
        if (const auto* dc = row.find("discCount")) {
            if (dc->type != JsonValue::Type::Number) reject("invalid Apple disc count");
            const auto d = position(row, "discCount");
            if (disc_count && disc_count != d) reject("conflicting Apple disc counts");
            disc_count = d;
        }
        if (const auto* tc = row.find("trackCount")) {
            if (tc->type != JsonValue::Type::Number) reject("invalid Apple track count");
            const auto t = track_count(row);
            const auto [it, added] = counts.emplace(disc, t);
            if (!added && it->second != t) reject("conflicting Apple disc track count");
            saw_track_count = true;
        }
        PrecisionCueReleaseTrack info{disc, track, text(row, "trackName"),
                                      text(row, "artistName"), ms};
        if (!positions.emplace(std::make_pair(disc, track), std::move(info)).second)
            reject("duplicate Apple disc-track index");
    }
    if (!found_album || positions.empty() || positions.size() != album_count)
        reject("Apple album track list is incomplete");
    unsigned previous_disc = 0, previous_track = 0;
    for (auto& entry : positions) {
        auto& track = entry.second;
        if (track.disc_number == previous_disc) {
            if (track.track_number != previous_track + 1)
                reject("gap in Apple track positions");
        } else if (track.disc_number != previous_disc + 1 ||
                   track.track_number != 1) reject("gap in Apple disc positions");
        previous_disc = track.disc_number;
        previous_track = track.track_number;
        release.tracks.push_back(std::move(track));
    }
    if (disc_count && previous_disc != disc_count)
        reject("Apple disc list incomplete");
    if (saw_track_count) {
        if (counts.size() != previous_disc) reject("missing Apple disc track count");
        std::map<unsigned, unsigned> actual;
        for (const auto& t : release.tracks) ++actual[t.disc_number];
        bool per_disc = true, whole_album = true;
        for (const auto& [disc, declared] : counts) {
            if (declared != actual[disc]) per_disc = false;
            if (declared != release.tracks.size()) whole_album = false;
        }
        if (!per_disc && !whole_album) reject("incomplete Apple disc data");
    }
    return release;
}


std::string deezer_precision_cue_album_api_url(std::string_view album_id) {
    return "https://api.deezer.com/album/" +
           std::to_string(positive_number(album_id));
}

std::size_t deezer_precision_cue_declared_track_count(
    std::string_view json, std::string_view expected_album_id) {
    const auto root = JsonParser(json).parse();
    if (root.type != JsonValue::Type::Object ||
        number(root, "id") != positive_number(expected_album_id))
        reject("Deezer album ID mismatch");
    const auto total = number(root, "nb_tracks");
    if (total > 8 * 99) reject("Deezer track count exceeds CUE limits");
    (void)get(root, "tracks", JsonValue::Type::Object);
    return static_cast<std::size_t>(total);
}

PrecisionCueRelease parse_deezer_precision_cue_album_pages(
    std::string_view json, std::string_view expected_album_id,
    const std::vector<std::string>& page_jsons) {
    const auto expected = positive_number(expected_album_id);
    const auto root = JsonParser(json).parse();
    if (root.type != JsonValue::Type::Object)
        reject("Deezer album is not an object");
    if (number(root, "id") != expected)
        reject("Deezer album ID mismatch");
    const auto total = number(root, "nb_tracks");
    if (total > 8 * 99) reject("Deezer track count exceeds CUE limits");
    const auto& album_artist = get(root, "artist", JsonValue::Type::Object);
    const auto& tracks = get(root, "tracks", JsonValue::Type::Object);
    const auto& initial = get(tracks, "data", JsonValue::Type::Array).items;
    if (const auto* reported = tracks.find("total")) {
        if (reported->type != JsonValue::Type::Number ||
            positive_number(reported->raw) != total)
            reject("Deezer pagination total mismatch");
    }
    if (const auto* next = tracks.find("next")) {
        if (next->type != JsonValue::Type::Literal &&
            next->type != JsonValue::Type::String)
            reject("Deezer malformed pagination cursor");
        if (next->type == JsonValue::Type::Literal && next->raw != "null")
            reject("Deezer malformed pagination cursor");
        if (page_jsons.empty() && next->type == JsonValue::Type::String)
            reject("Deezer paginated tracklist requires review");
    }
    std::vector<JsonValue> assembled;
    if (!page_jsons.empty()) {
        // Never follow provider-supplied URLs. Fetch only fixed page indices.
        const auto expected_pages = (static_cast<std::size_t>(total) + 99u) / 100u;
        if (page_jsons.size() != expected_pages || expected_pages > 8)
            reject("Deezer missing or excess track pages");
        for (std::size_t page_index = 0; page_index < page_jsons.size(); ++page_index) {
            const auto page = JsonParser(page_jsons[page_index]).parse();
            if (page.type != JsonValue::Type::Object ||
                number(page, "total") != total)
                reject("Deezer track page total mismatch");
            const auto& rows = get(page, "data", JsonValue::Type::Array).items;
            const auto remaining = static_cast<std::size_t>(total) - assembled.size();
            const auto expected_rows = (std::min)(remaining, std::size_t{100});
            if (rows.size() != expected_rows)
                reject("Deezer track page gap or truncation");
            assembled.insert(assembled.end(), rows.begin(), rows.end());
            const auto* next = page.find("next");
            const bool more = assembled.size() < static_cast<std::size_t>(total);
            if (more && (!next || next->type != JsonValue::Type::String ||
                         next->raw.empty()))
                reject("Deezer missing next-page marker");
            if (!more && next &&
                (next->type != JsonValue::Type::Literal || next->raw != "null"))
                reject("Deezer unexpected final-page marker");
        }
        if (initial.size() > assembled.size())
            reject("Deezer album page prefix too long");
        for (std::size_t i = 0; i < initial.size(); ++i) {
            const auto& a = initial[i];
            const auto& b = assembled[i];
            if (a.type != JsonValue::Type::Object ||
                b.type != JsonValue::Type::Object ||
                number(a, "id") != number(b, "id") ||
                position(a, "disk_number") != position(b, "disk_number") ||
                position(a, "track_position") != position(b, "track_position") ||
                number(a, "duration") != number(b, "duration") ||
                text(a, "title") != text(b, "title") ||
                text(get(a, "artist", JsonValue::Type::Object), "name") !=
                    text(get(b, "artist", JsonValue::Type::Object), "name"))
                reject("Deezer album/track-page identity changed");
        }
    }
    const auto& data = page_jsons.empty() ? initial : assembled;
    if (data.size() != static_cast<std::size_t>(total))
        reject("Deezer incomplete album tracklist");
    PrecisionCueRelease release;
    release.provider = "deezer";
    release.provider_release_id = std::to_string(expected);
    release.album_title = text(root, "title");
    release.album_performer = text(album_artist, "name");
    std::map<std::pair<unsigned, unsigned>, PrecisionCueReleaseTrack> ordered;
    std::map<std::int64_t, bool> unique_ids;
    std::map<unsigned, std::int64_t> elapsed_by_disc;
    for (const auto& row : data) {
        if (row.type != JsonValue::Type::Object)
            reject("Invalid Deezer track object");
        const auto id = number(row, "id");
        if (!unique_ids.emplace(id, true).second)
            reject("Duplicate Deezer track ID");
        const auto disc = position(row, "disk_number");
        const auto index = position(row, "track_position");
        if (disc > 8) reject("Deezer disc exceeds CUE limit");
        const auto seconds = number(row, "duration");
        constexpr std::int64_t kMaximumDiscMs = 24LL * 3600 * 1000;
        const auto duration_ms = seconds * 1000;
        auto& elapsed = elapsed_by_disc[disc];
        if (duration_ms > kMaximumDiscMs - elapsed)
            reject("Deezer disc cumulative duration exceeds 24 hours");
        elapsed += duration_ms;
        const auto& artist = get(row, "artist", JsonValue::Type::Object);
        PrecisionCueReleaseTrack track{
            disc, index, text(row, "title"), text(artist, "name"),
            duration_ms
        };
        if (!ordered.emplace(std::make_pair(disc, index),
                             std::move(track)).second)
            reject("Duplicate Deezer disc/track position");
    }
    unsigned prior_disc = 0, prior_track = 0;
    for (auto& entry : ordered) {
        auto& track = entry.second;
        if (track.disc_number == prior_disc) {
            if (track.track_number != prior_track + 1)
                reject("Deezer track-position gap");
        } else if (track.disc_number != prior_disc + 1 ||
                   track.track_number != 1)
            reject("Deezer disc-position gap");
        prior_disc = track.disc_number;
        prior_track = track.track_number;
        release.tracks.push_back(std::move(track));
    }
    return release;
}

PrecisionCueRelease parse_deezer_precision_cue_album(
    std::string_view json, std::string_view expected_album_id) {
    return parse_deezer_precision_cue_album_pages(json, expected_album_id, {});
}

} // namespace djmeta
