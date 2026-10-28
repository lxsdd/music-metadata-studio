#include "djmeta/precision_cue_assemble.h"
#include "djmeta/precision_cue_filename.h"
#include "djmeta/precision_cue_calendar_date.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace djmeta {
namespace {
constexpr std::int64_t kReviewDeltaMs = 1000;
constexpr std::int64_t kRejectDeltaMs = 3000;
constexpr std::int64_t kMaximumDiscMs = 24LL * 60 * 60 * 1000;
constexpr std::size_t kMaximumPhysicalDiscs = 8;

std::uint64_t duration_difference(std::int64_t a, std::int64_t b) {
    return a >= b ? static_cast<std::uint64_t>(a - b)
                  : static_cast<std::uint64_t>(b - a);
}
} // namespace


PrecisionCueDiscMapping resolve_precision_cue_disc_mapping(
    const std::vector<std::int64_t>& provider_disc_durations_ms,
    const std::vector<PrecisionCuePhysicalDisc>& physical_discs) {
    PrecisionCueDiscMapping result;
    const auto count = provider_disc_durations_ms.size();
    if (count == 0 || count > kMaximumPhysicalDiscs || physical_discs.size() != count) {
        result.diagnostics.emplace_back("PHYSICAL_DISC_COUNT_INVALID");
        return result;
    }
    for (const auto duration : provider_disc_durations_ms) {
        if (duration <= 0 || duration > kMaximumDiscMs) {
            result.diagnostics.emplace_back("PROVIDER_DISC_DURATION_INVALID");
            return result;
        }
    }
    std::set<std::string> names;
    for (const auto& physical : physical_discs) {
        if (!precision_cue_safe_mp3_basename(physical.mp3_basename) ||
            physical.measured_duration_ms < 0 ||
            physical.measured_duration_ms > kMaximumDiscMs) {
            result.diagnostics.emplace_back("PHYSICAL_MP3_INVALID");
            return result;
        }
        auto name = physical.mp3_basename;
        for (char& ch : name)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
        if (!names.insert(std::move(name)).second) {
            result.diagnostics.emplace_back("DUPLICATE_PHYSICAL_MP3_BASENAME");
            return result;
        }
    }
    // Enumerate at most two permutations. Never break ties by filename.
    std::vector<std::size_t> trial(count, 0), unique_assignment;
    std::vector<bool> used(count, false);
    unsigned solutions = 0;
    std::function<void(std::size_t)> search = [&](std::size_t disc) {
        if (solutions >= 2) return;
        if (disc == count) {
            ++solutions;
            if (solutions == 1) unique_assignment = trial;
            return;
        }
        for (std::size_t file = 0; file < count && solutions < 2; ++file) {
            if (used[file]) continue;
            const auto measured = physical_discs[file].measured_duration_ms;
            if (measured != 0 &&
                duration_difference(provider_disc_durations_ms[disc], measured) >
                    static_cast<std::uint64_t>(kRejectDeltaMs)) continue;
            used[file] = true;
            trial[disc] = file;
            search(disc + 1);
            used[file] = false;
        }
    };
    search(0);
    if (solutions == 0) {
        result.diagnostics.emplace_back("NO_DURATION_COMPATIBLE_DISC_ASSIGNMENT");
        return result;
    }
    if (solutions > 1) {
        result.state = PrecisionCueMatchState::Ambiguous;
        result.diagnostics.emplace_back("MULTIPLE_DURATION_COMPATIBLE_DISC_ASSIGNMENTS");
        return result;
    }
    result.physical_index_by_disc = std::move(unique_assignment);
    bool review = count > 1;
    if (review) result.diagnostics.emplace_back("DISC_IDENTITY_INFERRED_FROM_DURATION_ONLY");
    for (std::size_t disc = 0; disc < count; ++disc) {
        const auto file = result.physical_index_by_disc[disc];
        const auto measured = physical_discs[file].measured_duration_ms;
        if (file != disc)
            result.diagnostics.emplace_back("PHYSICAL_SELECTION_ORDER_NOT_DISC_ORDER");
        if (measured == 0) {
            review = true;
            result.diagnostics.emplace_back("LOCAL_DURATION_UNKNOWN_DISC_" +
                                            std::to_string(disc + 1));
        } else if (duration_difference(provider_disc_durations_ms[disc], measured) >
                   static_cast<std::uint64_t>(kReviewDeltaMs)) {
            review = true;
            result.diagnostics.emplace_back("LOCAL_DURATION_REVIEW_DISC_" +
                                            std::to_string(disc + 1));
        }
    }
    result.state = review ? PrecisionCueMatchState::Review : PrecisionCueMatchState::Selected;
    return result;
}

PrecisionCueDiscMapping validate_precision_cue_partial_disc_mapping(
    const std::vector<std::int64_t>& provider_disc_durations_ms,
    const std::vector<PrecisionCuePhysicalDisc>& physical_discs,
    const std::vector<std::size_t>& chosen_physical_index_by_disc) {
    PrecisionCueDiscMapping result;
    const auto baseline = resolve_precision_cue_disc_mapping(
        provider_disc_durations_ms, physical_discs);
    if (baseline.state == PrecisionCueMatchState::Rejected) return baseline;
    const auto count = provider_disc_durations_ms.size();
    if (chosen_physical_index_by_disc.size() != count) {
        result.diagnostics.emplace_back("PARTIAL_DISC_MAPPING_COUNT_INVALID");
        return result;
    }
    std::vector<bool> seen(count, false);
    bool incomplete = false;
    for (std::size_t disc = 0; disc < count; ++disc) {
        const auto file = chosen_physical_index_by_disc[disc];
        if (file == count) {
            incomplete = true;
            continue;
        }
        if (file > count || seen[file]) {
            result.diagnostics.emplace_back("PARTIAL_DISC_MAPPING_DUPLICATE_OR_INVALID");
            return result;
        }
        seen[file] = true;
        const auto measured = physical_discs[file].measured_duration_ms;
        if (measured != 0 &&
            duration_difference(provider_disc_durations_ms[disc], measured) >
                static_cast<std::uint64_t>(kRejectDeltaMs)) {
            result.diagnostics.emplace_back("PARTIAL_DISC_MAPPING_DURATION_INCOMPATIBLE");
            return result;
        }
    }
    // A locally valid selection may still consume the only MP3 that another
    // disc can use. Prove that at least one complete permutation remains.
    // The bounded search has at most 8! leaves and never chooses for the user.
    std::function<bool(std::size_t)> completion_exists = [&](std::size_t disc) {
        if (disc == count) return true;
        if (chosen_physical_index_by_disc[disc] != count)
            return completion_exists(disc + 1);
        for (std::size_t file = 0; file < count; ++file) {
            if (seen[file]) continue;
            const auto measured = physical_discs[file].measured_duration_ms;
            if (measured != 0 &&
                duration_difference(provider_disc_durations_ms[disc], measured) >
                    static_cast<std::uint64_t>(kRejectDeltaMs)) continue;
            seen[file] = true;
            const bool possible = completion_exists(disc + 1);
            seen[file] = false;
            if (possible) return true;
        }
        return false;
    };
    if (!completion_exists(0)) {
        result.diagnostics.emplace_back("PARTIAL_DISC_MAPPING_NO_COMPLETION");
        return result;
    }
    result.state = PrecisionCueMatchState::Review;
    if (incomplete) {
        result.diagnostics.emplace_back("PARTIAL_DISC_MAPPING_INCOMPLETE");
        return result;
    }
    return review_precision_cue_disc_mapping(
        provider_disc_durations_ms, physical_discs, chosen_physical_index_by_disc);
}

PrecisionCueDiscMapping review_precision_cue_disc_mapping(
    const std::vector<std::int64_t>& provider_disc_durations_ms,
    const std::vector<PrecisionCuePhysicalDisc>& physical_discs,
    const std::vector<std::size_t>& reviewed_physical_index_by_disc) {
    PrecisionCueDiscMapping result;
    const auto baseline = resolve_precision_cue_disc_mapping(
        provider_disc_durations_ms, physical_discs);
    if (baseline.state == PrecisionCueMatchState::Rejected) return baseline;
    const auto count = provider_disc_durations_ms.size();
    if (reviewed_physical_index_by_disc.size() != count) {
        result.diagnostics.emplace_back("EXPLICIT_DISC_MAPPING_COUNT_INVALID");
        return result;
    }
    std::vector<bool> seen(count, false);
    for (std::size_t disc = 0; disc < count; ++disc) {
        const auto file = reviewed_physical_index_by_disc[disc];
        if (file >= count || seen[file]) {
            result.diagnostics.emplace_back("EXPLICIT_DISC_MAPPING_NOT_A_PERMUTATION");
            return result;
        }
        seen[file] = true;
        const auto measured = physical_discs[file].measured_duration_ms;
        if (measured != 0 &&
            duration_difference(provider_disc_durations_ms[disc], measured) >
                static_cast<std::uint64_t>(kRejectDeltaMs)) {
            result.diagnostics.emplace_back("EXPLICIT_DISC_MAPPING_DURATION_INCOMPATIBLE");
            return result;
        }
    }
    result.state = PrecisionCueMatchState::Review;
    result.physical_index_by_disc = reviewed_physical_index_by_disc;
    result.diagnostics.emplace_back("EXPLICIT_DISC_MAPPING_REQUIRES_IDENTITY_REVIEW");
    for (std::size_t disc = 0; disc < count; ++disc)
        if (physical_discs[reviewed_physical_index_by_disc[disc]].measured_duration_ms == 0)
            result.diagnostics.emplace_back(
                "LOCAL_DURATION_UNKNOWN_DISC_" + std::to_string(disc + 1));
    return result;
}

PrecisionCuePreviewResult make_precision_cue_release_preview(
    const PrecisionCuePreviewRequest& request) {
    PrecisionCuePreviewResult result;
    // Manual REM DATE is untrusted input: reject impossible calendar claims
    // before a source-matched preview can carry them into Studio review.
    if (!precision_cue_valid_calendar_date(request.release_date)) {
        result.diagnostics.emplace_back("CUE_DATE_INVALID");
        return result;
    }
    const auto match = match_precision_cue_releases(request.reference, request.candidates);
    result.state = match.state;
    result.diagnostics = match.diagnostics;
    if (match.state != PrecisionCueMatchState::Selected ||
        !match.selected_candidate_index ||
        *match.selected_candidate_index >= request.candidates.size()) {
        result.diagnostics.emplace_back("RELEASE_NOT_UNAMBIGUOUSLY_MATCHED");
        return result;
    }
    const auto& selected = request.candidates[*match.selected_candidate_index];
    // A caller-supplied REM GENRE/DATE must not masquerade as Apple catalog
    // evidence. Empty request fields inherit only actually sourced metadata.
    const auto header_origin = [](const std::string& requested,
                                  const std::string& catalog) {
        if (requested.empty()) return catalog.empty()
            ? PrecisionCueHeaderFieldOrigin::Absent
            : PrecisionCueHeaderFieldOrigin::SelectedCatalog;
        if (catalog.empty()) return PrecisionCueHeaderFieldOrigin::ManualUnverified;
        return requested == catalog
            ? PrecisionCueHeaderFieldOrigin::SelectedCatalog
            : PrecisionCueHeaderFieldOrigin::ConflictsWithSelectedCatalog;
    };
    result.header_provenance = {
        header_origin(request.genre, selected.genre),
        header_origin(request.release_date, selected.release_date)
    };
    const auto flag_header_review = [&](PrecisionCueHeaderFieldOrigin origin,
                                        const char* field) {
        if (origin == PrecisionCueHeaderFieldOrigin::ManualUnverified) {
            result.diagnostics.emplace_back(std::string(field) + "_MANUAL_UNVERIFIED_REVIEW");
            return true;
        }
        if (origin == PrecisionCueHeaderFieldOrigin::ConflictsWithSelectedCatalog) {
            result.diagnostics.emplace_back(std::string(field) + "_CATALOG_CONFLICT_REVIEW");
            return true;
        }
        return false;
    };
    const bool genre_review = flag_header_review(result.header_provenance.genre, "CUE_GENRE");
    const bool date_review = flag_header_review(result.header_provenance.release_date, "CUE_DATE");
    // A year/month-only catalog reference can identify the same release as
    // a full-date candidate without proving its missing calendar fields.
    // Preserve the exact selected catalog REM DATE, but do not elevate the
    // standalone single-disc preview to qualified before explicit review.
    // The matcher deliberately permits a compatible partial date for search;
    // this is the separate preview/commit provenance boundary.
    const bool sourced_date_precision_review =
        !request.reference.release_date.empty() &&
        !selected.release_date.empty() &&
        request.reference.release_date.size() != selected.release_date.size() &&
        precision_cue_calendar_dates_compatible(
            request.reference.release_date, selected.release_date);
    if (sourced_date_precision_review)
        result.diagnostics.emplace_back("CUE_RELEASE_DATE_PRECISION_REVIEW");
    const bool header_requires_review =
        genre_review || date_review || sourced_date_precision_review;
    if (selected.tracks.empty()) {
        result.state = PrecisionCueMatchState::Rejected;
        result.diagnostics.emplace_back("EMPTY_RELEASE");
        return result;
    }
    const unsigned disc_count = selected.tracks.back().disc_number;
    if (disc_count == 0 || disc_count > kMaximumPhysicalDiscs || request.physical_discs.size() != disc_count) {
        result.state = PrecisionCueMatchState::Rejected;
        result.diagnostics.emplace_back("PHYSICAL_DISC_COUNT_MISMATCH");
        return result;
    }


    std::vector<std::int64_t> provider_totals(disc_count, 0);
    for (const auto& track : selected.tracks) {
        if (track.disc_number == 0 || track.disc_number > disc_count ||
            track.duration_ms <= 0 ||
            track.duration_ms > kMaximumDiscMs - provider_totals[track.disc_number - 1]) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("PROVIDER_DISC_DURATION_INVALID");
            return result;
        }
        provider_totals[track.disc_number - 1] += track.duration_ms;
    }
    const auto mapping = request.reviewed_physical_index_by_disc.empty()
        ? resolve_precision_cue_disc_mapping(provider_totals, request.physical_discs)
        : review_precision_cue_disc_mapping(
            provider_totals, request.physical_discs,
            request.reviewed_physical_index_by_disc);
    result.diagnostics.insert(result.diagnostics.end(),
                              mapping.diagnostics.begin(), mapping.diagnostics.end());
    if (mapping.state == PrecisionCueMatchState::Rejected ||
        mapping.state == PrecisionCueMatchState::Ambiguous) {
        result.state = mapping.state;
        return result;
    }
    std::vector<PrecisionCuePlan> staged;
    staged.reserve(disc_count);
    for (unsigned number = 1; number <= disc_count; ++number) {
        const auto& physical = request.physical_discs[
            mapping.physical_index_by_disc[number - 1]];
        PrecisionCueDisc disc;
        disc.album_title = selected.album_title;
        disc.album_performer = selected.album_performer;
        disc.audio_file_basename = physical.mp3_basename;
        disc.genre = request.genre.empty() ? selected.genre : request.genre;
        disc.release_date = request.release_date.empty()
            ? selected.release_date : request.release_date;
        disc.disc_number = number;
        disc.total_discs = disc_count;
        for (const auto& track : selected.tracks)
            if (track.disc_number == number)
                disc.tracks.push_back({track.title, track.performer, track.duration_ms});
        if (disc.tracks.empty()) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("EMPTY_DISC_TRACKLIST");
            return result;
        }
        try {
            staged.push_back(make_precision_cue(disc));
        } catch (const std::exception&) {
            result.state = PrecisionCueMatchState::Rejected;
            result.diagnostics.emplace_back("CUE_STRUCTURE_INVALID");
            return result;
        }
    }
    result.discs = std::move(staged);
    result.physical_index_by_disc = mapping.physical_index_by_disc;
    result.physical_source_identity_by_selection =
        request.physical_source_identity_by_selection;
    result.provider = selected.provider;
    result.provider_release_id = selected.provider_release_id;
    result.state = header_requires_review
        ? PrecisionCueMatchState::Review : mapping.state;
    result.preview_qualified = mapping.state == PrecisionCueMatchState::Selected &&
        !header_requires_review;
    return result;
}

} // namespace djmeta
