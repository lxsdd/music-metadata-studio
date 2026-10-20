#pragma once
// Pure, bounded, user-facing explanation of a blocked provider match.
// Never authorizes a CUE, network request, tag change or filesystem write.
#include "djmeta/precision_cue_match.h"
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

inline std::string describe_precision_cue_match_failure(
    const PrecisionCueMatchReport& report,
    const PrecisionCueRelease& reference,
    const PrecisionCueRelease& candidate) {
    if (report.state == PrecisionCueMatchState::Selected) return {};
    std::string message = "Deezer/Apple edition verification blocked the CUE preview.";
    if (report.state == PrecisionCueMatchState::Ambiguous)
        message += "\nMultiple plausible or inconsistent release identities.";
    if (report.candidates.size() != 1) return message;
    for (const auto& code : report.candidates.front().diagnostics) {
        if (code == "RELEASE_DATE_REVIEW") {
            message += "\nRelease dates disagree";
            if (precision_cue_safe_iso_date(reference.release_date) &&
                precision_cue_safe_iso_date(candidate.release_date))
                message += ": Deezer " + reference.release_date +
                           " / Apple " + candidate.release_date;
            message += ". Choose the exact edition.";
        } else if (code == "TRACK_TITLE_OR_MIX_VERSION_MISMATCH") {
            message += "\nTrack title or mix version differs (Mixed/Original/Radio).";
        } else if (code == "TRACK_DURATION_INCOMPATIBLE") {
            message += "\nTrack durations differ beyond the permitted identity window.";
        } else if (code == "DISC_OR_TRACK_POSITION_MISMATCH") {
            message += "\nDisc or track order differs.";
        } else if (code == "TRACK_COUNT_MISMATCH") {
            message += "\nTrack counts differ.";
        } else if (code == "ALBUM_EDITION_TITLE_REVIEW") {
            message += "\nAlbum or edition title differs.";
        } else if (code == "TRACK_PERFORMER_REVIEW") {
            message += "\nTrack performer differs or is missing.";
        } else if (code == "ALBUM_PERFORMER_REVIEW") {
            message += "\nAlbum performer differs or is missing.";
        } else if (code == "TRACK_DURATION_REVIEW") {
            message += "\nTrack durations require independent review.";
        } else if (code == "REFERENCE_DURATION_UNKNOWN") {
            message += "\nReference track duration is unknown.";
        } else if (code == "REFERENCE_PROVIDER_ID_UNVERIFIED") {
            message += "\nReference release identity is unverified.";
        } else {
            message += "\nProvider metadata is incomplete or invalid.";
        }
    }
    return message;
}
} // namespace djmeta
