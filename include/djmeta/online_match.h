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
        if (ch >= 0x80 || !std::isalnum(ch)) return {};
        result.push_back(static_cast<char>(std::toupper(ch)));
    }
    // ISO 3901 structure: two-letter country, three alphanumeric registrant,
    // two-digit year and five-digit designation. Mere 12-character length
    // would incorrectly promote malformed IDs to matching evidence.
    if (result.size() != 12) return {};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const char ch = result[i];
        const bool letter = ch >= 'A' && ch <= 'Z';
        const bool digit = ch >= '0' && ch <= '9';
        if (!(i < 2 ? letter : (i < 5 ? (letter || digit) : digit)))
            return {};
    }
    return result;
}

inline CandidateMatch compare(const RecordingIdentity& local, const Candidate& remote) {
    CandidateMatch result;
    result.provider = remote.provider;
    result.provider_track_id = remote.provider_track_id;

    const auto local_title = comparable(local.title);
    const auto remote_title = comparable(remote.identity.title);
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
    const auto local_mix_name = comparable(local.mix_name);
    const auto remote_mix_name = comparable(remote.identity.mix_name);
    if (local.mix != MixKind::Unknown &&
        remote.identity.mix != MixKind::Unknown) {
        if (local.mix != remote.identity.mix) {
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
    } else if (local.mix != remote.identity.mix) {
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

    const auto local_isrc = normalized_isrc(local.isrc);
    const auto remote_isrc = normalized_isrc(remote.identity.isrc);
    const bool same_isrc = !local_isrc.empty() && local_isrc == remote_isrc;
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
    const std::vector<Candidate>& candidates) {
    std::vector<CandidateMatch> results;
    // One provider ID denotes one remote recording. If two responses attach
    // different metadata to that ID, retaining the first result can silently
    // turn an untrusted, order-dependent candidate into a strong suggestion.
    // Deduplicate only by exact provider+ID, and fail closed on collisions.
    using SourceId = std::pair<std::string, std::string>;
    std::map<SourceId, Candidate> unique;
    std::set<SourceId> collisions;
    for (const auto& candidate : candidates) {
        if (candidate.provider.empty() || candidate.provider_track_id.empty() ||
            candidate.provider.find('\0') != std::string::npos ||
            candidate.provider_track_id.find('\0') != std::string::npos) continue;
        const SourceId id{candidate.provider, candidate.provider_track_id};
        const auto [it, inserted] = unique.emplace(id, candidate);
        if (!inserted) {
            const auto& a = it->second.identity;
            const auto& b = candidate.identity;
            if (a.title != b.title || a.primary_artist != b.primary_artist ||
                a.mix_name != b.mix_name || a.mix != b.mix ||
                a.isrc != b.isrc || a.duration_ms != b.duration_ms)
                collisions.insert(id);
        }
    }
    results.reserve(unique.size());
    for (const auto& [id, candidate] : unique) {
        auto match = compare(local, candidate);
        if (collisions.contains(id)) {
            match.decision = MatchDecision::Review;
            match.score = 0;
            match.evidence.clear();
            match.conflicts = {"provider_identity_collision"};
        }
        results.push_back(std::move(match));
    }
    std::sort(results.begin(), results.end(),
        [](const CandidateMatch& a, const CandidateMatch& b) {
            if (a.decision != b.decision)
                return static_cast<int>(a.decision) < static_cast<int>(b.decision);
            if (a.score != b.score) return a.score > b.score;
            if (a.provider != b.provider) return a.provider < b.provider;
            return a.provider_track_id < b.provider_track_id;
        });
    return results;
}

} // namespace djmeta::online
