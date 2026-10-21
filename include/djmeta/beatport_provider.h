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
#include <optional>
#include <stdexcept>
#include <set>
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
struct SearchAttempt {
    std::string reason; // Human-readable disclosure of the bounded query.
    std::string path;   // Inert until an authorized host transport exists.
};
// Shared lowercase helper is implemented further below for parsing and
// mix classification. Forward-declare it for the search-only planner.
inline std::string ascii_lower(std::string_view value);
// A canonical ISRC can be used as a FIRST, bounded official catalog
// search term by a future authorized transport. It identifies a recording,
// NOT an exact commercial mix/edition; all results must still pass the
// separate title, mix, artist and physical-duration verifier.
inline std::optional<std::string> canonical_search_isrc(std::string_view raw) {
    if(raw.empty() || raw.size()>18) return std::nullopt;
    std::string result;
    for(unsigned char c:raw) {
        if(c=='-') continue;
        if(c>='a' && c<='z') c=static_cast<unsigned char>(c-('a'-'A'));
        if(!((c>='A' && c<='Z') || (c>='0' && c<='9')))
            return std::nullopt;
        result.push_back(static_cast<char>(c));
    }
    if(result.size()!=12) return std::nullopt;
    for(std::size_t i=0;i<12;++i) {
        const char c=result[i];
        if(i<2 && (c<'A' || c>'Z')) return std::nullopt;
        if(i>=5 && (c<'0' || c>'9')) return std::nullopt;
    }
    return result;
}
// Search-only fallback: featured-artist suffixes often make strict catalog
// queries return no results, even where a matching base track exists. Keep
// the complete original identity in the independent matcher; NEVER use the
// shortened search string to authorize a different recording or mix.
inline std::string search_title_without_feature_credit(std::string_view title) {
    if(!text_ok(title,150)) return {};
    const auto folded=ascii_lower(title);
    constexpr std::string_view markers[]={
        " feat. "," feat "," ft. "," ft "," featuring "};
    std::size_t earliest=std::string_view::npos;
    for(const auto marker:markers) {
        const auto at=folded.find(marker);
        if(at!=std::string_view::npos &&
           (earliest==std::string_view::npos || at<earliest))
            earliest=at;
    }
    if(earliest==std::string_view::npos || earliest==0)
        return std::string(title);
    // Never strip everything or perform fuzzy provider-match repairs.
    return std::string(title.substr(0,earliest));
}
// Beatport v4 search has historically rejected bracket-heavy queries.
// Search artist + full BASE TITLE, then base TITLE alone; ONLY after those
// attempts offer an optional feat/ft-cleaned fallback search. Exact version
// and collaborator credit matching remains an independent hard gate.
inline std::vector<SearchAttempt> track_search_plan(
    std::string_view tagged_title, std::string_view tagged_artist = {},
    std::string_view tagged_isrc = {}) {
    if (!text_ok(tagged_title, 150) ||
        (!tagged_artist.empty() && !text_ok(tagged_artist, 150)))
        throw std::invalid_argument("Invalid Beatport track search identity");
    const auto annotated=split_title_mix(tagged_title);
    const auto title=annotated.version.empty()
        ? tagged_title : annotated.base;
    std::vector<SearchAttempt> result;
    if(const auto isrc=canonical_search_isrc(tagged_isrc))
        result.push_back({"ISRC first (recording only; mix requires verification)",
                          search_path(*isrc)});
    if (!tagged_artist.empty() &&
        tagged_artist.size() + 1 + title.size() <= 150) {
        result.push_back({"Artist and base title",
            search_path(std::string(tagged_artist) + " " + std::string(title))});
    }
    const auto title_path=search_path(title);
    if (std::none_of(result.begin(),result.end(),
            [&](const SearchAttempt& step){return step.path==title_path;}))
        result.push_back({"Base title without artist restriction", title_path});
    const auto without_feat=search_title_without_feature_credit(title);
    if(!without_feat.empty() && without_feat!=title) {
        if(!tagged_artist.empty() &&
           tagged_artist.size()+1+without_feat.size()<=150) {
            const auto path=search_path(std::string(tagged_artist)+" "+without_feat);
            if(std::none_of(result.begin(),result.end(),
                [&](const SearchAttempt& step){return step.path==path;}))
                result.push_back({"Explicit featured-artist search fallback",path});
        }
        const auto path=search_path(without_feat);
        if(std::none_of(result.begin(),result.end(),
            [&](const SearchAttempt& step){return step.path==path;}))
            result.push_back({"Base-title-only fallback; identity still verified",path});
    }
    return result;
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
    const auto total = musicbrainz::number_or(root.get("count"));
    result.has_more = total > static_cast<std::int64_t>(list->array.size());
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
    // A partial or teaser page is not a verified edition. A missing count
    // cannot prove completeness, even when the visible array looks plausible.
    const int count = bounded_number(release, "track_count", 1, 200);
    if (count < 1 || static_cast<std::size_t>(count) != list->array.size())
        throw std::invalid_argument("Unqualified or partial Beatport tracklist");
    const auto* reported = track_data.get("count");
    if (reported && (reported->type != Json::Type::Number ||
                     musicbrainz::number_or(reported) != count))
        throw std::invalid_argument("Beatport page count contradicts edition");
    const auto* page = track_data.get("page");
    if (page && (page->type != Json::Type::Number ||
                 musicbrainz::number_or(page) != 1))
        throw std::invalid_argument("Beatport tracks are not the first page");
    const auto* next = track_data.get("next");
    const auto* previous = track_data.get("previous");
    if ((next && next->type != Json::Type::Null) ||
        (previous && previous->type != Json::Type::Null))
        throw std::invalid_argument("Beatport tracklist is paginated");
    std::set<std::uint64_t> seen_ids;
    std::set<int> seen_positions;
    for (const auto& object : list->array) {
        auto track = parse_track_object(object);
        if (track.release_id != expected_release_id)
            throw std::invalid_argument("Beatport track release identity missing or foreign");
        if (!seen_ids.insert(track.id).second)
            throw std::invalid_argument("Duplicate Beatport track identity");
        if (track.track_number > 0 &&
            !seen_positions.insert(track.track_number).second)
            throw std::invalid_argument("Duplicate Beatport edition position");
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
