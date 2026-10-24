#pragma once

// P0 proof of concept: pure, read-only, provider-independent candidate ranking.
// No network, SDK, metadata mutation, authentication, or file I/O.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace djmeta::online {

enum class MixKind { Unknown, Original, Extended, Radio, Remix, Live, Instrumental };
enum class MatchDecision { Suggested, Review, Rejected };

struct RecordingIdentity {
    std::string title;
    std::string primary_artist;
    std::string mix_name; // Named remixer/edition if explicitly known.
    MixKind mix = MixKind::Unknown;
    std::string isrc;
    std::int64_t duration_ms = -1; // -1 means unknown, never zero-guess.
};

struct Candidate {
    std::string provider;
    std::string provider_track_id;
    RecordingIdentity identity;
};

// User-tunable display preference for equally substantiated candidates.
// Provider origin is not evidence of recording identity and cannot turn a
// rejected/review-only candidate into a suggested match.
struct ProviderPreferences {
    std::vector<std::string> preferred_order{"beatport", "crates", "discogs", "deezer", "musicbrainz"};
};

inline std::size_t preference_index(
    std::string_view provider, const ProviderPreferences& preferences) {
    const auto it = std::find(preferences.preferred_order.begin(),
                              preferences.preferred_order.end(), provider);
    return static_cast<std::size_t>(it - preferences.preferred_order.begin());
}

struct CandidateMatch {
    std::string provider;
    std::string provider_track_id;
    MatchDecision decision = MatchDecision::Review;
    int score = 0; // Synthetic P0 weight only; NOT calibrated confidence.
    std::vector<std::string> evidence;
    std::vector<std::string> conflicts;
};

// ASCII-only, deliberately conservative search comparison, not a metadata
// normalization rule. Avoids undefined locale-dependent transformations and
// never changes source text or transliterates non-ASCII bytes.
inline std::string comparable(std::string_view raw) {
    std::string out;
    bool pending_space = false;
    for (unsigned char ch : raw) {
        if (ch == 0) return {}; // Invalid search token.
        if (ch < 0x80 && (std::isspace(ch) || std::ispunct(ch))) {
            pending_space = !out.empty();
        } else {
            if (pending_space) out.push_back(' ');
            pending_space = false;
            out.push_back(ch < 0x80
                ? static_cast<char>(std::tolower(ch))
                : static_cast<char>(ch));
        }
    }
    return out;
}

// ISRC identity is an identifier, not natural language: optional separators
// should not turn the same 12-character code into two different recordings.
inline std::string normalized_isrc(std::string_view raw) {
    std::string result;
    for (unsigned char ch : raw) {
        if (ch == '-' || ch == ' ' || ch == '\t') continue;
        if (ch >= 0x80 ||
            !((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9'))) return {};
        result.push_back(ch >= 'a' && ch <= 'z'
            ? static_cast<char>(ch - ('a' - 'A'))
            : static_cast<char>(ch));
    }
    // ISO 3901: two-letter country, three alphanumeric registrant,
    // two-digit year and five-digit designation. Length alone is insufficient.
    if (result.size() != 12) return {};
    const auto letter = [](char c) { return c >= 'A' && c <= 'Z'; };
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!letter(result[0]) || !letter(result[1])) return {};
    for (std::size_t i = 2; i < 5; ++i)
        if (!letter(result[i]) && !digit(result[i])) return {};
    for (std::size_t i = 5; i < 12; ++i)
        if (!digit(result[i])) return {};
    return result;
}

// Beatport stores the base title and the version in separate fields; many
// local DJ tags append the version to TITLE instead. Extract only an explicit
// terminal (... Mix/Edit/Remix/etc.) annotation. Other parentheses stay in the
// base title so unrelated compositions cannot become accidental matches.
struct TitleMixAnnotation {
    std::string_view base;
    std::string_view version;
    MixKind kind = MixKind::Unknown;
};
inline MixKind classify_mix_annotation(std::string_view name) {
    const auto value = comparable(name);
    if (value == "original mix" || value == "original") return MixKind::Original;
    if (value.find("remix") != std::string::npos ||
        value.ends_with(" rmx")) return MixKind::Remix;
    if (value.find("extended") != std::string::npos) return MixKind::Extended;
    if (value.find("radio") != std::string::npos) return MixKind::Radio;
    if (value.find("instrumental") != std::string::npos)
        return MixKind::Instrumental;
    if (value.find("live") != std::string::npos) return MixKind::Live;
    return MixKind::Unknown;
}
inline TitleMixAnnotation split_title_mix(std::string_view title) {
    TitleMixAnnotation output{title, {}, MixKind::Unknown};
    while (!title.empty() && title.back() == ' ') title.remove_suffix(1);
    if (title.empty() || title.back() != ')') return output;
    const auto opening = title.rfind('(');
    if (opening == std::string_view::npos || opening == 0 ||
        title.size() - opening > 132) return output;
    const auto suffix = title.substr(opening+1, title.size()-opening-2);
    if (suffix.empty() || suffix.find('(') != std::string_view::npos ||
        suffix.find(')') != std::string_view::npos ||
        comparable(suffix).empty()) return output;
    const auto kind = classify_mix_annotation(suffix);
    const auto normalized = comparable(suffix);
    const bool named_mix = normalized.ends_with(" mix") ||
                           normalized.ends_with(" edit") ||
                           normalized.ends_with(" version") ||
                           normalized.ends_with(" dub");
    if (kind == MixKind::Unknown && !named_mix) return output;
    auto base = title.substr(0, opening);
    while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
    if (base.empty() || comparable(base).empty()) return output;
    return {base, suffix, kind};
}

inline CandidateMatch compare(const RecordingIdentity& local, const Candidate& remote) {
    CandidateMatch result;
    result.provider = remote.provider;
    result.provider_track_id = remote.provider_track_id;

    const auto local_annotation = split_title_mix(local.title);
    const auto remote_annotation = split_title_mix(remote.identity.title);
    const auto local_title = comparable(local_annotation.base);
    const auto remote_title = comparable(remote_annotation.base);
    const auto local_artist = comparable(local.primary_artist);
    const auto remote_artist = comparable(remote.identity.primary_artist);
    if (local_title.empty() || remote_title.empty() ||
        local_title != remote_title) {
        result.conflicts.push_back("title_unverified");
        result.decision = MatchDecision::Rejected;
        return result;
    }
    result.score += 36;
    result.evidence.push_back("title_exact");

    if (local_artist.empty() || remote_artist.empty() ||
        local_artist != remote_artist) {
        result.conflicts.push_back("primary_artist_unverified");
        result.decision = MatchDecision::Rejected;
        return result;
    }
    result.score += 30;
    result.evidence.push_back("primary_artist_exact");

    bool ambiguous = false;
    const auto local_mix_name = comparable(local.mix_name.empty()
        ? local_annotation.version : std::string_view(local.mix_name));
    const auto remote_mix_name = comparable(remote.identity.mix_name.empty()
        ? remote_annotation.version : std::string_view(remote.identity.mix_name));
    const auto local_mix = local.mix != MixKind::Unknown
        ? local.mix : local_annotation.kind;
    const auto remote_mix = remote.identity.mix != MixKind::Unknown
        ? remote.identity.mix : remote_annotation.kind;
    // Conflicting TITLE suffix versus a separately tagged VERSION is a
    // source-local disagreement. Never interpret it as stronger evidence.
    if ((!local.mix_name.empty() && !local_annotation.version.empty() &&
         comparable(local.mix_name) != comparable(local_annotation.version)) ||
        (!remote.identity.mix_name.empty() &&
         !remote_annotation.version.empty() &&
         comparable(remote.identity.mix_name) !=
             comparable(remote_annotation.version)) ||
        (local.mix != MixKind::Unknown &&
         local_annotation.kind != MixKind::Unknown &&
         local.mix != local_annotation.kind) ||
        (remote.identity.mix != MixKind::Unknown &&
         remote_annotation.kind != MixKind::Unknown &&
         remote.identity.mix != remote_annotation.kind)) {
        ambiguous = true;
        result.conflicts.push_back("title_version_annotation_conflict");
    }
    if (local_mix != MixKind::Unknown &&
        remote_mix != MixKind::Unknown) {
        if (local_mix != remote_mix) {
            result.conflicts.push_back("mix_type_conflict");
            result.decision = MatchDecision::Rejected;
            return result;
        }
        if (!local_mix_name.empty() && !remote_mix_name.empty() &&
            local_mix_name != remote_mix_name) {
            result.conflicts.push_back("named_mix_conflict");
            result.decision = MatchDecision::Rejected;
            return result;
        }
        result.score += 10;
        result.evidence.push_back("mix_type_exact");
    } else if (local_mix != remote_mix) {
        ambiguous = true;
        result.conflicts.push_back("mix_type_unverified");
    }
    if (!local_mix_name.empty() && !remote_mix_name.empty() &&
        local_mix_name != remote_mix_name) {
        ambiguous = true;
        result.conflicts.push_back("named_mix_unverified");
    }
    if (local_mix_name.empty() != remote_mix_name.empty()) {
        ambiguous = true;
        result.conflicts.push_back("named_mix_missing_on_one_side");
    }

    // Matching ISRC alone does not establish the mix or remix identity.
    if (local_mix == MixKind::Unknown &&
        remote_mix == MixKind::Unknown &&
        (local_mix_name.empty() || remote_mix_name.empty() ||
         local_mix_name != remote_mix_name)) {
        ambiguous = true;
        result.conflicts.push_back("mix_identity_unverified");
    }
    if (local_mix == MixKind::Remix &&
        remote_mix == MixKind::Remix &&
        local_mix_name.empty() && remote_mix_name.empty()) {
        ambiguous = true;
        result.conflicts.push_back("remix_identity_unverified");
    }

    const auto local_isrc = normalized_isrc(local.isrc);
    const auto remote_isrc = normalized_isrc(remote.identity.isrc);
    const bool same_isrc = !local_isrc.empty() && local_isrc == remote_isrc;
    if ((!local.isrc.empty() && local_isrc.empty()) ||
        (!remote.identity.isrc.empty() && remote_isrc.empty())) {
        ambiguous = true;
        result.conflicts.push_back("invalid_isrc_unverified");
    } else if (!same_isrc && (local_isrc.empty() != remote_isrc.empty())) {
        ambiguous = true;
        result.conflicts.push_back("isrc_missing_on_one_side");
    }
    if (same_isrc) {
        result.score += 18;
        result.evidence.push_back("isrc_exact_not_conclusive");
    } else if (!local_isrc.empty() && !remote_isrc.empty()) {
        ambiguous = true;
        result.conflicts.push_back("isrc_conflict");
    }

    bool close_duration = false;
    if (local.duration_ms >= 0 && remote.identity.duration_ms >= 0) {
        const auto delta = local.duration_ms >= remote.identity.duration_ms
            ? local.duration_ms - remote.identity.duration_ms
            : remote.identity.duration_ms - local.duration_ms;
        if (delta > 90000) {
            result.conflicts.push_back("duration_far_conflict");
            result.decision = MatchDecision::Rejected;
            return result;
        }
        if (delta <= 3000) {
            result.score += 6;
            close_duration = true;
            result.evidence.push_back("duration_within_3s");
        } else if (delta > 10000) {
            ambiguous = true;
            result.conflicts.push_back("duration_review");
        } else {
            result.evidence.push_back("duration_within_10s");
        }
    } else {
        result.evidence.push_back("duration_missing");
    }

    // Never treat a score as a calibrated probability or tag-write approval.
    // Strong suggestion requires exact song/artist plus independent evidence.
    if (!ambiguous && result.score >= 82 && (same_isrc || close_duration)) {
        result.decision = MatchDecision::Suggested;
    } else {
        result.decision = MatchDecision::Review;
    }
    return result;
}

inline std::vector<CandidateMatch> rank(
    const RecordingIdentity& local,
    const std::vector<Candidate>& candidates,
    const ProviderPreferences& preferences = {}) {
    std::vector<CandidateMatch> results;
    // Duplicate provider IDs must not silently select the first payload.
    // Identical repeats are harmless; any conflicting recording payload makes
    // the entire provider identity unusable, independent of input order.
    // Exact byte equality is intentional: an unverified representation change
    // is not grounds for an automatic metadata suggestion.
    const auto same_identity = [](const RecordingIdentity& a,
                                  const RecordingIdentity& b) {
        return a.title == b.title &&
               a.primary_artist == b.primary_artist &&
               a.mix_name == b.mix_name &&
               a.mix == b.mix &&
               a.isrc == b.isrc &&
               a.duration_ms == b.duration_ms;
    };
    using Key = std::pair<std::string, std::string>;
    std::map<Key, std::size_t> first;
    std::set<Key> conflicting;
    std::map<Key, RecordingIdentity> originals;
    for (const auto& candidate : candidates) {
        if (candidate.provider.empty() || candidate.provider_track_id.empty())
            continue;
        const Key key{candidate.provider, candidate.provider_track_id};
        const auto [it, inserted] = first.emplace(key, results.size());
        if (inserted) {
            results.push_back(compare(local, candidate));
            originals.emplace(key, candidate.identity);
        } else {
            if (!same_identity(originals.at(key), candidate.identity))
                conflicting.insert(key);
        }
    }
    for (const auto& key : conflicting) {
        auto& match = results[first.at(key)];
        match.decision = MatchDecision::Rejected;
        match.score = 0;
        match.evidence.clear();
        match.conflicts.push_back("provider_identity_conflict");
    }
    std::sort(results.begin(), results.end(),
        [&preferences](const CandidateMatch& a, const CandidateMatch& b) {
            if (a.decision != b.decision)
                return static_cast<int>(a.decision) < static_cast<int>(b.decision);
            if (a.score != b.score) return a.score > b.score;
            // Presentation tie-break only. The score, safety decision and
            // complete provenance remain unchanged for every provider.
            const auto ap = preference_index(a.provider, preferences);
            const auto bp = preference_index(b.provider, preferences);
            if (ap != bp) return ap < bp;
            if (a.provider != b.provider) return a.provider < b.provider;
            return a.provider_track_id < b.provider_track_id;
        });
    return results;
}

} // namespace djmeta::online
