#pragma once
// Pure, bounded, user-facing explanation of a blocked provider match.
// Never authorizes a CUE, network request, tag change or filesystem write.
#include "djmeta/precision_cue_match.h"
#include <algorithm>
#include <set>
#include <string>
#include <string_view>

namespace djmeta {
inline bool precision_cue_safe_iso_date(std::string_view date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    for (std::size_t i = 0; i < date.size(); ++i)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9'))
            return false;
    return true;
}


inline std::string precision_cue_diagnostic_identity(std::string_view input) {
    std::string result;
    bool pending_space = false;
    for (const unsigned char c : input) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pending_space = !result.empty();
        } else {
            if (pending_space) result.push_back(' ');
            pending_space = false;
            result.push_back(static_cast<char>(
                c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
        }
    }
    return result;
}

// Only numeric locations and catalog durations enter the UI: never echo
// untrusted provider track titles, which may contain unsafe control text.
inline std::string precision_cue_first_track_conflict(
    std::string_view reason, const PrecisionCueRelease& reference,
    const PrecisionCueRelease& candidate) {
    const auto count = (std::min)(reference.tracks.size(), candidate.tracks.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& a = reference.tracks[i];
        const auto& b = candidate.tracks[i];
        bool conflict = false;
        if (reason == "DISC_OR_TRACK_POSITION_MISMATCH")
            conflict = a.disc_number != b.disc_number || a.track_number != b.track_number;
        else if (reason == "TRACK_TITLE_OR_MIX_VERSION_MISMATCH")
            conflict = precision_cue_diagnostic_identity(a.title) !=
                       precision_cue_diagnostic_identity(b.title);
        else if (reason == "TRACK_DURATION_INCOMPATIBLE" ||
                 reason == "TRACK_DURATION_REVIEW") {
            if (a.duration_ms > 0 && b.duration_ms > 0) {
                const auto delta = a.duration_ms >= b.duration_ms
                    ? a.duration_ms - b.duration_ms : b.duration_ms - a.duration_ms;
                conflict = delta > (reason == "TRACK_DURATION_INCOMPATIBLE" ? 3000 : 1000);
            }
        } else if (reason == "TRACK_PERFORMER_REVIEW")
            conflict = precision_cue_diagnostic_identity(a.performer).empty() ||
                       precision_cue_diagnostic_identity(b.performer).empty() ||
                       precision_cue_diagnostic_identity(a.performer) !=
                       precision_cue_diagnostic_identity(b.performer);
        else if (reason == "REFERENCE_DURATION_UNKNOWN")
            conflict = a.duration_ms == 0;
        if (!conflict) continue;
        std::string detail = " First conflict: disc " +
            std::to_string(a.disc_number) + ", track " + std::to_string(a.track_number);
        if (reason == "DISC_OR_TRACK_POSITION_MISMATCH")
            detail += " (Apple disc " + std::to_string(b.disc_number) +
                      ", track " + std::to_string(b.track_number) + ")";
        if (reason == "TRACK_DURATION_INCOMPATIBLE" ||
            reason == "TRACK_DURATION_REVIEW")
            detail += " (Deezer " + std::to_string(a.duration_ms) +
                      " ms / Apple " + std::to_string(b.duration_ms) +
                      " ms; catalog metadata, not audio precision)";
        return detail + ".";
    }
    return {};
}

inline std::string describe_precision_cue_match_failure(
    const PrecisionCueMatchReport& report,
    const PrecisionCueRelease& reference,
    const PrecisionCueRelease& candidate) {
    if (report.state == PrecisionCueMatchState::Selected) return {};
    std::string message = "Deezer/Apple edition verification blocked the CUE preview.";
    if (report.state == PrecisionCueMatchState::Ambiguous)
        message += "\nMultiple plausible or inconsistent release identities.";
    if (report.candidates.size() != 1) return message;
    std::set<std::string> emitted; // no repeated warning flood for 99-track discs
    for (const auto& code : report.candidates.front().diagnostics) {
        if (!emitted.insert(code).second) continue;
        if (code == "RELEASE_DATE_REVIEW") {
            message += "\nRelease dates disagree";
            if (precision_cue_safe_iso_date(reference.release_date) &&
                precision_cue_safe_iso_date(candidate.release_date))
                message += ": Deezer " + reference.release_date +
                           " / Apple " + candidate.release_date;
            message += ". Choose the exact edition.";
        } else if (code == "TRACK_TITLE_OR_MIX_VERSION_MISMATCH") {
            message += "\nTrack title or mix version differs (Mixed/Original/Radio)." +
                       precision_cue_first_track_conflict(code, reference, candidate);
        } else if (code == "TRACK_DURATION_INCOMPATIBLE") {
            message += "\nTrack durations differ beyond the permitted identity window." +
                       precision_cue_first_track_conflict(code, reference, candidate);
        } else if (code == "DISC_OR_TRACK_POSITION_MISMATCH") {
            message += "\nDisc or track order differs." +
                       precision_cue_first_track_conflict(code, reference, candidate);
        } else if (code == "TRACK_COUNT_MISMATCH") {
            message += "\nTrack counts differ.";
        } else if (code == "ALBUM_EDITION_TITLE_REVIEW") {
            message += "\nAlbum or edition title differs.";
        } else if (code == "TRACK_PERFORMER_REVIEW") {
            message += "\nTrack performer differs or is missing." +
                       precision_cue_first_track_conflict(code, reference, candidate);
        } else if (code == "ALBUM_PERFORMER_REVIEW") {
            message += "\nAlbum performer differs or is missing.";
        } else if (code == "TRACK_DURATION_REVIEW") {
            message += "\nTrack durations require independent review." +
                       precision_cue_first_track_conflict(code, reference, candidate);
        } else if (code == "REFERENCE_DURATION_UNKNOWN") {
            message += "\nReference track duration is unknown." +
                       precision_cue_first_track_conflict(code, reference, candidate);
        } else if (code == "REFERENCE_PROVIDER_ID_UNVERIFIED") {
            message += "\nReference release identity is unverified.";
        } else {
            message += "\nProvider metadata is incomplete or invalid.";
        }
    }
    return message;
}
} // namespace djmeta
