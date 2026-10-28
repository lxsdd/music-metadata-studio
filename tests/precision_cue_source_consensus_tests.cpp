#include "djmeta/precision_cue_source_consensus.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>

static djmeta::PrecisionCueDurationSource source(const char* provider, unsigned resolution) {
    djmeta::PrecisionCueDurationSource s;
    s.release.provider = provider;
    s.release.provider_release_id = provider;
    s.release.album_title = "Synthetic Mix (DJ Mix)";
    s.release.album_performer = "Various Artists";
    s.release.tracks = {
        {1, 1, "Intro (Mixed)", "Artist One", 61234},
        {1, 2, "Finale (Mixed)", "Artist Two", 123456}
    };
    if (std::string(provider) == "deezer" || std::string(provider) == "Deezer") {
        s.release.tracks[0].duration_ms = 61000;
        s.release.tracks[1].duration_ms = 123000;
    }
    s.resolution_ms = resolution;
    return s;
}

int main() {
    using namespace djmeta;
    // The consensus API is callable independently of the strict matcher.
    // Equal-looking but deceptive provider fields must NEVER corroborate
    // frame timing, even if BOTH alleged sources contain the same bad text.
    for (const auto bad : {
        "\xC2\x85", "\xE2\x80\xAE", "\xE2\x81\xA6",
        "\xE2\x80\xA8", "\xEF\xBB\xBF", "\xEF\xB7\x90",
        "\xED\xA0\x80", "\xC0\xAF"
    }) {
        auto primary = source("apple", 1);
        auto second = source("musicbrainz", 1);
        primary.release.album_title += bad;
        second.release.album_title += bad;
        assert(audit_precision_cue_source_consensus({primary, second}).state ==
               PrecisionCueConsensusState::Rejected);
        primary = source("apple", 1);
        second = source("musicbrainz", 1);
        second.release.tracks[1].title += bad;
        assert(audit_precision_cue_source_consensus({primary, second}).state ==
               PrecisionCueConsensusState::Rejected);
        second = source("musicbrainz", 1);
        second.release.provider_release_id += bad;
        assert(audit_precision_cue_source_consensus({primary, second}).state ==
               PrecisionCueConsensusState::Rejected);
    }
    auto international_primary = source("apple", 1);
    auto international_second = source("musicbrainz", 1);
    international_primary.release.album_title =
        international_second.release.album_title = "München – 東京";
    international_primary.release.tracks[0].title =
        international_second.release.tracks[0].title = "Café 👩‍🎤";
    assert(audit_precision_cue_source_consensus({
        international_primary, international_second}).state ==
        PrecisionCueConsensusState::Agreed);

    auto apple = source("apple", 1);
    auto mb = source("musicbrainz", 1);
    auto deezer = source("deezer", 1000);
    // 14 ms is coarser than a 75-fps frame (13.333... ms): it must
    // not count as independent frame-resolution corroboration.
    auto borderline = source("musicbrainz", 14);
    assert(audit_precision_cue_source_consensus({apple, borderline}).state ==
           PrecisionCueConsensusState::SingleHighResolutionSource);
    borderline.resolution_ms = 13;
    assert(audit_precision_cue_source_consensus({apple, borderline}).state ==
           PrecisionCueConsensusState::Agreed);
    auto coarse_primary = source("apple", 14);
    assert(audit_precision_cue_source_consensus({coarse_primary, mb}).state ==
           PrecisionCueConsensusState::Review);
    coarse_primary.resolution_ms = 13;
    assert(audit_precision_cue_source_consensus({coarse_primary, mb}).state ==
           PrecisionCueConsensusState::Agreed);
    assert(audit_precision_cue_source_consensus({apple}).state ==
           PrecisionCueConsensusState::SingleHighResolutionSource);
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Agreed);
    // A changed final track does NOT move an INDEX 01 start marker. It
    // remains a separate tail-duration REVIEW, never "one-frame CUE".
    mb.release.tracks[1].duration_ms += 14;
    auto tail_report = audit_precision_cue_source_consensus({apple, mb});
    assert(tail_report.state == PrecisionCueConsensusState::Review);
    assert(tail_report.diagnostics[0] == "DISC_TAIL_DURATION_DISAGREEMENT_REVIEW");
    assert(tail_report.differences[1].disc_terminal_track);
    assert(tail_report.differences[1].signed_start_frame_delta == 0);
    assert(tail_report.differences[1].signed_end_frame_delta == 1);
    mb.release.tracks[1].duration_ms -= 14;
    // Regression: a +1/-1 ms tail still differs even when both the
    // terminal end and every INDEX 01 quantize to identical frames.
    for (const auto tail_delta : {1, -1}) {
        mb.release.tracks[1].duration_ms += tail_delta;
        const auto subframe_tail = audit_precision_cue_source_consensus({apple, mb});
        assert(subframe_tail.state == PrecisionCueConsensusState::Review);
        assert(subframe_tail.diagnostics[0] == "DISC_TAIL_DURATION_DISAGREEMENT_REVIEW");
        assert(subframe_tail.differences[1].signed_delta_ms == tail_delta);
        assert(subframe_tail.differences[1].signed_start_frame_delta == 0);
        assert(subframe_tail.differences[1].signed_end_frame_delta == 0);
        mb.release.tracks[1].duration_ms -= tail_delta;
    }
    // Compensated sub-frame differences are not an exact-duration match.
    // Keep the compensated pair NON-terminal: a changed final track has
    // its own separate, higher-priority tail-duration diagnostic.
    apple.release.tracks.push_back({1, 3, "Third Track", "Artist Three", 90000});
    mb.release.tracks.push_back({1, 3, "Third Track", "Artist Three", 90000});
    mb.release.tracks[0].duration_ms += 1;
    mb.release.tracks[1].duration_ms -= 1;
    const auto cancelled_subframe = audit_precision_cue_source_consensus({apple, mb});
    assert(cancelled_subframe.state == PrecisionCueConsensusState::Review);
    assert(cancelled_subframe.diagnostics[0] ==
           "SUBFRAME_TRACK_DURATION_DISAGREEMENT_REVIEW");
    assert(cancelled_subframe.differences[0].signed_end_frame_delta == 0);
    assert(cancelled_subframe.differences[1].signed_start_frame_delta == 0);
    assert(cancelled_subframe.differences[1].signed_end_frame_delta == 0);
    mb.release.tracks[0].duration_ms -= 1;
    mb.release.tracks[1].duration_ms += 1;
    apple.release.tracks.pop_back();
    mb.release.tracks.pop_back();
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Agreed);
    // Disc one tail is also NOT a start marker for disc two.
    mb.release.tracks.push_back({2, 1, "Bonus Outro", "Artist Three", 90000});
    apple.release.tracks.push_back({2, 1, "Bonus Outro", "Artist Three", 90000});
    mb.release.tracks[1].duration_ms += 14;
    tail_report = audit_precision_cue_source_consensus({apple, mb});
    assert(tail_report.state == PrecisionCueConsensusState::Review);
    assert(tail_report.diagnostics[0] == "DISC_TAIL_DURATION_DISAGREEMENT_REVIEW");
    assert(tail_report.differences[2].signed_start_frame_delta == 0);
    assert(tail_report.differences[1].disc_terminal_track);
    assert(tail_report.differences[2].disc_terminal_track);
    apple.release.tracks.pop_back();
    mb.release.tracks.pop_back();
    mb.release.tracks[1].duration_ms -= 14;
    const auto initial_coarse = audit_precision_cue_source_consensus({apple, deezer});
    assert(initial_coarse.differences.size() == 2);
    assert(initial_coarse.differences[0].signed_delta_ms == -234);
    assert(initial_coarse.differences[0].signed_cumulative_delta_ms == -234);
    assert(initial_coarse.differences[1].signed_delta_ms == -456);
    assert(initial_coarse.differences[1].signed_cumulative_delta_ms == -690);
    assert(initial_coarse.differences[0].signed_start_cumulative_delta_ms == 0);
    assert(initial_coarse.differences[1].signed_start_cumulative_delta_ms == -234);
    assert(initial_coarse.differences[0].signed_start_frame_delta == 0);
    // This coarse source's arithmetic frame delta is NEVER frame proof.
    assert(initial_coarse.differences[1].signed_start_frame_delta ==
           initial_coarse.differences[0].signed_end_frame_delta);
    assert(audit_precision_cue_source_consensus({apple}).differences.empty());
    // Both trusted release inputs must start at disc 1, track 1. Reject a
    // shifted first disc even when both providers share the same bad data.
    auto missing_first_disc = apple;
    for (auto& track : missing_first_disc.release.tracks)
        track.disc_number = 2;
    assert(audit_precision_cue_source_consensus({missing_first_disc}).state ==
           PrecisionCueConsensusState::Rejected);
    assert(audit_precision_cue_source_consensus({missing_first_disc, mb}).state ==
           PrecisionCueConsensusState::Rejected);
    auto missing_first_track = apple;
    missing_first_track.release.tracks[0].track_number = 2;
    assert(audit_precision_cue_source_consensus({missing_first_track}).state ==
           PrecisionCueConsensusState::Rejected);

    // Conflict in two known publication dates is evidence of another
    // edition even if titles, positions and 75-fps boundaries coincide.
    apple.release.release_date = "2026-10-16";
    mb.release.release_date = "2026-10-16";
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Agreed);
    mb.release.release_date = "2026-10-15";
    const auto date_conflict = audit_precision_cue_source_consensus({apple, mb});
    assert(date_conflict.state == PrecisionCueConsensusState::Rejected);
    assert(date_conflict.diagnostics[0] == "DUPLICATE_OR_DIFFERENT_RELEASE_EDITION");
    mb.release.release_date.clear(); // Unknown never invents another date.
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Agreed);
    apple.release.release_date.clear();
    // Independent frame clocks do not certify conflicting known edition
    // classifications. Missing genre is unknown, not contradictory.
    apple.release.genre = "Electronic";
    mb.release.genre = "Ambient";
    const auto genre_conflict = audit_precision_cue_source_consensus({apple, mb});
    assert(genre_conflict.state == PrecisionCueConsensusState::Rejected);
    assert(genre_conflict.diagnostics[0] == "DUPLICATE_OR_DIFFERENT_RELEASE_EDITION");
    mb.release.genre = "  eLeCtRoNiC  ";
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Agreed);
    mb.release.genre.clear();
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Agreed);
    apple.release.genre.clear();
    // Two Apple collection IDs are not two independent timing providers.
    auto other_apple = source("APPLE", 1);
    other_apple.release.provider_release_id = "alternate-collection";
    auto repeated = audit_precision_cue_source_consensus({apple, other_apple});
    assert(repeated.state == PrecisionCueConsensusState::Rejected);
    assert(repeated.diagnostics.size() == 1);
    assert(repeated.diagnostics[0] == "REPEATED_PROVIDER_NOT_INDEPENDENT");
    // Deezer whole-second durations cannot be relabelled as millisecond timing.
    auto fake_precision = source("Deezer", 1);
    auto spoof = audit_precision_cue_source_consensus({fake_precision});
    assert(spoof.state == PrecisionCueConsensusState::Rejected);
    assert(spoof.diagnostics[0] == "DEEZER_SECONDS_NOT_FRAME_PRECISION");
    spoof = audit_precision_cue_source_consensus({apple, fake_precision});
    assert(spoof.state == PrecisionCueConsensusState::Rejected);
    assert(spoof.diagnostics[0] == "DEEZER_SECONDS_NOT_FRAME_PRECISION");
    // One fractional millisecond is sufficient to disqualify alleged Deezer
    // seconds as independently corroborating provider evidence.
    auto false_seconds = source("deezer", 1000);
    false_seconds.release.tracks[0].duration_ms += 1;
    auto false_seconds_report = audit_precision_cue_source_consensus({apple, false_seconds});
    assert(false_seconds_report.state == PrecisionCueConsensusState::Rejected);
    assert(false_seconds_report.diagnostics[0] == "DEEZER_DURATION_NOT_WHOLE_SECONDS");
    false_seconds_report = audit_precision_cue_source_consensus({false_seconds});
    assert(false_seconds_report.state == PrecisionCueConsensusState::Rejected);

    mb.release.tracks[0].duration_ms += 13;
    auto single_frame = audit_precision_cue_source_consensus({apple, mb});
    assert(single_frame.state == PrecisionCueConsensusState::Review);
    assert(single_frame.diagnostics[0] == "ONE_FRAME_BOUNDARY_AMBIGUITY_REVIEW");
    mb.release.tracks[0].duration_ms += 15;
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Review);
    mb = source("musicbrainz", 1);
    mb.release.tracks[0].duration_ms += 13;
    mb.release.tracks[1].duration_ms += 13;
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Review); // cumulative drift
    assert(audit_precision_cue_source_consensus({apple, deezer}).state ==
           PrecisionCueConsensusState::SingleHighResolutionSource);
    deezer.release.tracks[0].duration_ms = 61000;
    assert(audit_precision_cue_source_consensus({apple, deezer}).state ==
           PrecisionCueConsensusState::SingleHighResolutionSource);
    // A clearly incompatible whole-second duration is not corroboration.
    deezer.release.tracks[0].duration_ms = 65000;
    auto coarse_conflict = audit_precision_cue_source_consensus({apple, deezer});
    assert(coarse_conflict.state == PrecisionCueConsensusState::Review);
    assert(coarse_conflict.diagnostics.size() == 1);
    assert(coarse_conflict.diagnostics[0] == "COARSE_REFERENCE_DURATION_CONFLICT");
    deezer.release.tracks[0].duration_ms = 61000;
    deezer.release.tracks[0].title = "Intro (Original)";
    assert(audit_precision_cue_source_consensus({apple, deezer}).state ==
           PrecisionCueConsensusState::Rejected);
    mb = source("musicbrainz", 1);
    mb.release.tracks[1].track_number = 3;
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Rejected);
    repeated = audit_precision_cue_source_consensus({apple, apple});
    assert(repeated.state == PrecisionCueConsensusState::Rejected);
    assert(repeated.diagnostics[0] == "REPEATED_PROVIDER_NOT_INDEPENDENT");
    apple.resolution_ms = 1000;
    assert(audit_precision_cue_source_consensus({apple}).state ==
           PrecisionCueConsensusState::Review);
    assert(audit_precision_cue_source_consensus({}).state ==
           PrecisionCueConsensusState::Rejected);

    // 30 purely synthetic tracks: 16 on disc 1 and 14 on disc 2.
    apple = source("apple", 1);
    mb = source("musicbrainz", 1);
    apple.release.tracks.clear();
    mb.release.tracks.clear();
    for (unsigned disc = 1; disc <= 2; ++disc) {
        const unsigned count = disc == 1 ? 16 : 14;
        for (unsigned track = 1; track <= count; ++track) {
            apple.release.tracks.push_back({disc, track, "Synthetic Track",
                                            "Synthetic Artist", 180000});
            mb.release.tracks.push_back({disc, track, "Synthetic Track",
                                         "Synthetic Artist", 180000});
        }
    }
    mb.release.tracks[1].duration_ms += 14;
    mb.release.tracks[2].duration_ms -= 14;
    mb.release.tracks[17].duration_ms += 14;
    mb.release.tracks[18].duration_ms -= 14;
    const auto report = audit_precision_cue_source_consensus({apple, mb});
    assert(report.state == PrecisionCueConsensusState::Review);
    assert(report.diagnostics[0] == "ONE_FRAME_BOUNDARY_AMBIGUITY_REVIEW");
    assert(report.differences.size() == 30);
    // Track deltas accumulate only within each disc. The second disc must
    // start with a fresh zero offset, never inherit disc one's rounding.
    assert(report.differences[0].signed_cumulative_delta_ms == 0);
    assert(report.differences[1].signed_cumulative_delta_ms == 14);
    assert(report.differences[2].signed_cumulative_delta_ms == 0);
    assert(report.differences[16].signed_cumulative_delta_ms == 0);
    assert(report.differences[17].signed_cumulative_delta_ms == 14);
    assert(report.differences[18].signed_cumulative_delta_ms == 0);
    // CUE INDEX 01 starts use the PREVIOUS track end, with a fresh
    // zero offset for every disc; do not mislabel end deltas as starts.
    assert(report.differences[0].signed_start_cumulative_delta_ms == 0);
    assert(report.differences[1].signed_start_cumulative_delta_ms == 0);
    assert(report.differences[2].signed_start_cumulative_delta_ms == 14);
    assert(report.differences[16].signed_start_cumulative_delta_ms == 0);
    assert(report.differences[17].signed_start_cumulative_delta_ms == 0);
    assert(report.differences[18].signed_start_cumulative_delta_ms == 14);
    assert(report.differences[0].signed_start_frame_delta == 0);
    assert(report.differences[1].signed_start_frame_delta == 0);
    assert(report.differences[2].signed_start_frame_delta == 1);
    assert(report.differences[3].signed_start_frame_delta == 0);
    assert(report.differences[16].signed_start_frame_delta == 0);
    assert(report.differences[17].signed_start_frame_delta == 0);
    assert(report.differences[18].signed_start_frame_delta == 1);
    assert(report.differences[19].signed_start_frame_delta == 0);
    unsigned exact = 0, one_frame = 0;
    for (const auto& diff : report.differences) {
        if (diff.signed_end_frame_delta == 0) ++exact;
        else if (diff.signed_end_frame_delta == 1 ||
                 diff.signed_end_frame_delta == -1) ++one_frame;
    }
    assert(exact == 28 && one_frame == 2);
    mb.release.tracks[18].duration_ms += 100;
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Review);

    // End-to-end native admission contract: independently matched Deezer
    // seconds never become Apple CUE frames when cumulative drift is unsafe.
    apple = source("apple", 1);
    deezer = source("deezer", 1000);
    apple.release.tracks.clear();
    deezer.release.tracks.clear();
    for (unsigned track = 1; track <= 30; ++track) {
        apple.release.tracks.push_back({1, track, "Synthetic Mixed Track",
                                        "Synthetic Artist", 180500});
        deezer.release.tracks.push_back({1, track, "Synthetic Mixed Track",
                                         "Synthetic Artist", 180000});
    }
    assert(match_precision_cue_releases(deezer.release, {apple.release}).state ==
           PrecisionCueMatchState::Selected);
    assert(audit_precision_cue_source_consensus({apple, deezer}).state ==
           PrecisionCueConsensusState::SingleHighResolutionSource);
    // All 30 per-track deviations remain within the matcher threshold,
    // but their aggregate drift makes the generated CUE unsafe.
    for (auto& track : apple.release.tracks) track.duration_ms = 180999;
    assert(match_precision_cue_releases(deezer.release, {apple.release}).state ==
           PrecisionCueMatchState::Review);
    const auto cumulative_conflict =
        audit_precision_cue_source_consensus({apple, deezer});
    assert(cumulative_conflict.state == PrecisionCueConsensusState::Review);
    assert(cumulative_conflict.diagnostics[0] ==
           "COARSE_REFERENCE_DURATION_CONFLICT");
    assert(cumulative_conflict.first_coarse_conflict.has_value());
    const auto first = *cumulative_conflict.first_coarse_conflict;
    assert(first.disc_number == 1 && first.track_number == 3);
    assert(first.signed_track_delta_ms == -999);
    assert(first.signed_cumulative_delta_ms == -2997);
    assert(cumulative_conflict.differences[2].signed_cumulative_delta_ms == -2997);
    assert(first.allowed_track_delta_ms == 1500);
    assert(first.allowed_cumulative_delta_ms == 2500);
    assert(!audit_precision_cue_source_consensus({
        source("apple", 1), source("deezer", 1000)
    }).first_coarse_conflict.has_value());

    // Every selected Apple track is exactly 180 seconds here; Deezer's
    // corresponding 179 seconds is a VALID whole-second catalog result.
    // Per-track differences stay below the rejection threshold, but
    // accumulated boundary drift must require review.
    for (auto& track : apple.release.tracks) track.duration_ms = 180000;
    deezer = source("deezer", 1000);
    deezer.release.tracks = apple.release.tracks;
    // Use real whole-second Deezer evidence, not synthetic 179100 ms.
    for (auto& track : deezer.release.tracks) track.duration_ms -= 1000;
    coarse_conflict = audit_precision_cue_source_consensus({apple, deezer});
    assert(coarse_conflict.state == PrecisionCueConsensusState::Review);
    assert(coarse_conflict.diagnostics[0] == "COARSE_REFERENCE_DURATION_CONFLICT");
    assert(coarse_conflict.first_coarse_conflict.has_value());
    assert(coarse_conflict.first_coarse_conflict->track_number == 3);
    assert(coarse_conflict.first_coarse_conflict->signed_track_delta_ms == -1000);
    assert(coarse_conflict.differences[2].signed_cumulative_delta_ms == -3000);

    // Standard CUE supports at most 99 tracks per disc; this native preview
    // supports at most eight explicitly mapped physical MP3 discs.
    apple = source("apple", 1);
    apple.release.tracks.clear();
    for (unsigned track = 1; track <= 99; ++track)
        apple.release.tracks.push_back({1, track, "Synthetic Track", "Artist", 1000});
    assert(audit_precision_cue_source_consensus({apple}).state ==
           PrecisionCueConsensusState::SingleHighResolutionSource);
    apple.release.tracks.push_back({1, 100, "Overflow", "Artist", 1000});
    assert(audit_precision_cue_source_consensus({apple}).state ==
           PrecisionCueConsensusState::Rejected);
    apple.release.tracks.pop_back();
    for (unsigned disc = 2; disc <= 8; ++disc)
        apple.release.tracks.push_back({disc, 1, "Synthetic Track", "Artist", 1000});
    assert(audit_precision_cue_source_consensus({apple}).state ==
           PrecisionCueConsensusState::SingleHighResolutionSource);
    apple.release.tracks.push_back({9, 1, "Ninth Disc", "Artist", 1000});
    assert(audit_precision_cue_source_consensus({apple}).state ==
           PrecisionCueConsensusState::Rejected);

    apple = source("apple", 1);
    constexpr std::int64_t twelve_hours = 12LL * 60 * 60 * 1000;
    apple.release.tracks[0].duration_ms = twelve_hours;
    apple.release.tracks[1].duration_ms = twelve_hours;
    assert(audit_precision_cue_source_consensus({apple}).state ==
           PrecisionCueConsensusState::SingleHighResolutionSource);
    ++apple.release.tracks[1].duration_ms;
    assert(audit_precision_cue_source_consensus({apple}).state ==
           PrecisionCueConsensusState::Rejected);
    apple.release.tracks[1].disc_number = 2;
    apple.release.tracks[1].track_number = 1;
    assert(audit_precision_cue_source_consensus({apple}).state ==
           PrecisionCueConsensusState::SingleHighResolutionSource);
    return 0;
}
