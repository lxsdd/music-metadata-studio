#pragma once

// Beatport v4 CATALOG DATA ADAPTER (offline, read-only, C++20).
// Accepts bounded JSON supplied by an independently authorized host transport.
// This code NEVER obtains OAuth credentials, performs HTTP, accesses a local
// library, scrapes a site, writes tags, or treats metadata as write approval.
// Catalog paths are inert until host authorization and usage rights are met.
#include "djmeta/musicbrainz_provider.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta::online::beatport {

using Json = musicbrainz::Json;
inline constexpr std::uint64_t maximum_catalog_id = 999999999999ULL;

inline bool text_ok(std::string_view value, std::size_t maximum = 1024) {
    if (value.empty() || value.size() > maximum ||
        !valid_utf8_metadata_text(value)) return false;
    for (const unsigned char c : value)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}
inline std::string label(const Json& obj, std::string_view name,
                         std::size_t maximum = 1024) {
    const auto value = musicbrainz::str(obj, name);
    return text_ok(value, maximum) ? value : std::string{};
}
inline std::uint64_t identifier(const Json* value) {
    const auto n = musicbrainz::number_or(value);
    return n > 0 && static_cast<std::uint64_t>(n) <= maximum_catalog_id
        ? static_cast<std::uint64_t>(n) : 0;
}
inline std::uint64_t id(const Json& obj, std::string_view key = "id") {
    return identifier(obj.get(key));
}
inline std::string decimal(std::uint64_t value) {
    if (value == 0 || value > maximum_catalog_id)
        throw std::invalid_argument("Invalid Beatport catalog identifier");
    return std::to_string(value);
}
inline std::string safe_query(std::string_view query) {
    if (!text_ok(query, 150))
        throw std::invalid_argument("Invalid Beatport search query");
    return musicbrainz::encode_url_query(query);
}
// Only path generation: the host must independently validate credentials and
// legal permission before making a single HTTPS request.
inline std::string search_path(std::string_view query, unsigned page = 1) {
    if (page < 1 || page > 3)
        throw std::invalid_argument("Beatport search page limit exceeded");
    return "/v4/catalog/search/?q=" + safe_query(query) +
           "&type=tracks&per_page=20&page=" + std::to_string(page);
}
inline std::string track_path(std::uint64_t track_id) {
    return "/v4/catalog/tracks/" + decimal(track_id);
}
inline std::string release_path(std::uint64_t release_id) {
    return "/v4/catalog/releases/" + decimal(release_id);
}
inline std::string release_tracks_path(std::uint64_t release_id) {
    return "/v4/catalog/releases/" + decimal(release_id) +
           "/tracks?per_page=200";
}

inline std::string object_name(const Json& node) {
    if (node.type == Json::Type::String)
        return text_ok(node.text) ? node.text : std::string{};
    if (node.type == Json::Type::Object) return label(node, "name");
    return {};
}
inline std::vector<std::string> names(const Json* array,
                                      std::size_t maximum = 32) {
    std::vector<std::string> out;
    if (!array || array->type != Json::Type::Array) return out;
    for (const auto& item : array->array) {
        if (out.size() >= maximum) break;
        const auto value = object_name(item);
        if (!value.empty() &&
            std::find(out.begin(), out.end(), value) == out.end())
            out.push_back(value);
    }
    return out;
}
inline std::string ascii_lower(std::string_view value) {
    std::string result;
    for (unsigned char c : value) {
        result.push_back(c >= 'A' && c <= 'Z'
            ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c));
    }
    return result;
}
// Classification never strips or guesses named remixes. Unknown mix types are
// retained verbatim in VERSION for explicit user review.
inline MixKind mix_kind(std::string_view raw) {
    const auto v = ascii_lower(raw);
    if (v == "original mix" || v == "original") return MixKind::Original;
    if (v.find("remix") != std::string::npos ||
        v.find(" rmx") != std::string::npos) return MixKind::Remix;
    if (v.find("extended") != std::string::npos) return MixKind::Extended;
    if (v.find("radio") != std::string::npos) return MixKind::Radio;
    if (v.find("instrumental") != std::string::npos)
        return MixKind::Instrumental;
    if (v.find("live") != std::string::npos) return MixKind::Live;
    return MixKind::Unknown;
}
inline bool calendar_date(std::string_view date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    for (std::size_t i = 0; i < date.size(); ++i)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9'))
            return false;
    const int year = (date[0]-'0')*1000 + (date[1]-'0')*100 +
                     (date[2]-'0')*10 + (date[3]-'0');
    const int month = (date[5]-'0')*10 + (date[6]-'0');
    const int day = (date[8]-'0')*10 + (date[9]-'0');
    if (year < 1850 || year > 2200 || month < 1 || month > 12) return false;
    constexpr int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = year%4 == 0 && (year%100 != 0 || year%400 == 0);
    return day >= 1 && day <= days[month] + (month == 2 && leap ? 1 : 0);
}
inline std::string date_label(const Json& obj, std::string_view key) {
    const auto value = label(obj, key, 10);
    return calendar_date(value) ? value : std::string{};
}
inline int bounded_number(const Json& node, std::string_view key,
                          int minimum, int maximum) {
    const auto n = musicbrainz::number_or(node.get(key));
    return n >= minimum && n <= maximum ? static_cast<int>(n) : -1;
}
inline std::int64_t duration(const Json& node) {
    // v4 detail/search length_ms is milliseconds. A missing or implausible
    // value remains unknown, never seconds guessed from a title or index.
    const auto n = musicbrainz::number_or(node.get("length_ms"));
    return n >= 1000 && n <= 6*60*60*1000 ? n : -1;
}

struct TrackEvidence {
    std::uint64_t id = 0;
    std::uint64_t release_id = 0;
    std::string title;                  // Beatport base name, NOT the mix name.
    std::string mix_name;
    std::vector<std::string> artists;   // Ordered credits, never joined on guess.
    std::vector<std::string> remixers;
    std::string isrc;
    std::int64_t duration_ms = -1;
    int bpm = -1;
    int track_number = -1;
    std::string genre;
    std::string subgenre;
    std::string musical_key;
    std::string release_title;
    std::string label_name;
    std::string catalog_number;
    std::string upc;
    std::string release_date;           // Edition-release, not recording origin.
    std::string publish_date;           // Digital publication, NOT first release.
};

inline TrackEvidence parse_track_object(const Json& obj) {
    if (obj.type != Json::Type::Object)
        throw std::invalid_argument("Beatport track must be an object");
    TrackEvidence out;
    out.id = id(obj);
    out.title = label(obj, "name", 2048);
    if (!out.id || out.title.empty())
        throw std::invalid_argument("Missing Beatport track identity");
    out.mix_name = label(obj, "mix_name", 512);
    out.artists = names(obj.get("artists"));
    out.remixers = names(obj.get("remixers"));
    out.isrc = label(obj, "isrc", 24);
    out.duration_ms = duration(obj);
    out.bpm = bounded_number(obj, "bpm", 30, 300);
    out.track_number = bounded_number(obj, "number", 1, 256);
    if (const auto* genre = obj.get("genre")) out.genre = object_name(*genre);
    if (const auto* sub = obj.get("sub_genre")) out.subgenre = object_name(*sub);
    if (const auto* key = obj.get("key")) out.musical_key = object_name(*key);
    out.catalog_number = label(obj, "catalog_number");
    out.release_date = date_label(obj, "new_release_date");
    out.publish_date = date_label(obj, "publish_date");
    if (const auto* rel = obj.get("release");
        rel && rel->type == Json::Type::Object) {
        out.release_id = id(*rel);
        out.release_title = label(*rel, "name", 2048);
        if (const auto* label_object = rel->get("label"))
            out.label_name = object_name(*label_object);
        out.upc = label(*rel, "upc", 128);
    }
    return out;
}

inline TrackEvidence parse_track(std::string_view json,
                                  std::uint64_t expected_id) {
    decimal(expected_id);
    const auto root = musicbrainz::JsonParser(json).parse();
    const auto result = parse_track_object(root);
    if (result.id != expected_id)
        throw std::invalid_argument("Beatport track identity mismatch");
    return result;
}
inline Candidate to_candidate(const TrackEvidence& track) {
    RecordingIdentity identity;
    identity.title = track.title;
    // Two independent credits do not establish a canonical collaboration
    // separator. Preserve them in field-level proposals instead.
    if (track.artists.size() == 1) identity.primary_artist = track.artists.front();
    identity.mix_name = track.mix_name;
    identity.mix = mix_kind(track.mix_name);
    identity.isrc = track.isrc;
    identity.duration_ms = track.duration_ms;
    return {"beatport", decimal(track.id), std::move(identity)};
}
inline std::vector<FieldEvidence> fields(const TrackEvidence& track) {
    const auto recording = decimal(track.id);
    const auto edition = track.release_id
        ? std::to_string(track.release_id) : recording;
    std::vector<FieldEvidence> result;
    const auto add = [&](std::string name, std::vector<std::string> values,
                         EvidenceScope scope = EvidenceScope::Recording,
                         DateMeaning meaning = DateMeaning::NotDate) {
        if (!values.empty())
            result.push_back({std::move(name), std::move(values), "beatport",
                scope == EvidenceScope::Recording ? recording : edition,
                scope, meaning});
    };
    add("TITLE", {track.title});
    add("ARTIST", track.artists);
    add("VERSION", track.mix_name.empty() ? std::vector<std::string>{}
                                           : std::vector<std::string>{track.mix_name});
    add("REMIXER", track.remixers);
    add("ISRC", track.isrc.empty() ? std::vector<std::string>{}
                                   : std::vector<std::string>{track.isrc});
    if (track.bpm > 0) add("BPM", {std::to_string(track.bpm)});
    if (!track.musical_key.empty()) add("KEY", {track.musical_key});
    if (!track.genre.empty()) add("GENRE", {track.genre});
    if (!track.subgenre.empty()) add("STYLE", {track.subgenre});
    if (!track.release_title.empty())
        add("ALBUM", {track.release_title}, EvidenceScope::Edition);
    if (!track.label_name.empty())
        add("LABEL", {track.label_name}, EvidenceScope::Edition);
    if (!track.catalog_number.empty())
        add("CATALOGNUMBER", {track.catalog_number}, EvidenceScope::Edition);
    if (!track.upc.empty())
        add("BARCODE", {track.upc}, EvidenceScope::Edition);
    if (track.track_number > 0)
        add("TRACKNUMBER", {std::to_string(track.track_number)}, EvidenceScope::Edition);
    if (!track.release_date.empty())
        add("DATE", {track.release_date}, EvidenceScope::Edition,
            DateMeaning::EditionRelease);
    if (!track.publish_date.empty())
        add("PUBLISH_DATE", {track.publish_date}, EvidenceScope::Edition,
            DateMeaning::DigitalPublication);
    return result;
}

struct SearchResult {
    std::vector<TrackEvidence> hits;
    bool has_more = false; // Search pages are bounded; no implicit pagination.
};
inline const Json* result_list(const Json& root, std::string_view primary,
                               std::string_view alternate) {
    const auto* first = root.get(primary);
    const auto* second = root.get(alternate);
    if ((first && second) || (!first && !second))
        throw std::invalid_argument("Ambiguous or missing Beatport result list");
    const auto* chosen = first ? first : second;
    if (chosen->type != Json::Type::Array)
        throw std::invalid_argument("Invalid Beatport result list type");
    return chosen;
}
inline SearchResult parse_search(std::string_view json) {
    const auto root = musicbrainz::JsonParser(json).parse();
    const auto* list = result_list(root, "tracks", "results");
    SearchResult result;
    if (list->array.size() > 200)
        throw std::invalid_argument("Unbounded Beatport search response");
    for (const auto& obj : list->array) {
        if (result.hits.size() >= 20) { result.has_more = true; break; }
        try {
            auto row = parse_track_object(obj);
            result.hits.push_back(std::move(row));
        } catch (const std::invalid_argument&) {
            // A malformed hit is not evidence; preserve other valid hits.
        }
    }
    return result;
}

// A release and its independently fetched track page are separate entities.
// Refuse partial tracklists and mismatched release identities rather than
// inventing tracks/positions or silently recommending a wrong edition.
inline ReleaseEdition parse_edition(std::string_view release_json,
                                    std::string_view tracks_json,
                                    std::uint64_t expected_release_id) {
    decimal(expected_release_id);
    const auto release = musicbrainz::JsonParser(release_json).parse();
    if (id(release) != expected_release_id ||
        label(release, "name", 2048).empty())
        throw std::invalid_argument("Beatport release identity mismatch");
    ReleaseEdition edition;
    edition.provider = "beatport";
    edition.edition_id = decimal(expected_release_id);
    const auto add = [&](std::string name, std::vector<std::string> values,
                         DateMeaning meaning = DateMeaning::NotDate) {
        if (!values.empty())
            edition.fields.push_back({std::move(name), std::move(values),
                "beatport", edition.edition_id, EvidenceScope::Edition, meaning});
    };
    add("ALBUM", {label(release, "name", 2048)});
    add("ALBUM ARTIST", names(release.get("artists")));
    if (const auto* lbl = release.get("label")) {
        const auto value = object_name(*lbl);
        if (!value.empty()) add("LABEL", {value});
    }
    const auto upc = label(release, "upc", 128);
    if (!upc.empty()) add("BARCODE", {upc});
    const auto date = date_label(release, "new_release_date");
    if (!date.empty()) add("DATE", {date}, DateMeaning::EditionRelease);
    const auto track_data = musicbrainz::JsonParser(tracks_json).parse();
    const auto* list = result_list(track_data, "results", "tracks");
    if (list->array.empty() || list->array.size() > 200)
        throw std::invalid_argument("Incomplete or excessive Beatport release tracks");
    const int count = bounded_number(release, "track_count", 1, 200);
    if (count > 0 && static_cast<std::size_t>(count) != list->array.size())
        throw std::invalid_argument("Partial Beatport release track page");
    for (const auto& object : list->array) {
        auto track = parse_track_object(object);
        if (track.release_id != 0 && track.release_id != expected_release_id)
            throw std::invalid_argument("Foreign track in Beatport release");
        EditionTrack row;
        row.source_track_id = decimal(track.id);
        row.recording = to_candidate(track).identity;
        row.track_number = track.track_number;
        row.fields = fields(track);
        edition.tracks.push_back(std::move(row));
    }
    return edition;
}

} // namespace djmeta::online::beatport
