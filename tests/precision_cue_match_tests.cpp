#include "djmeta/precision_cue_match.h"
#include "djmeta/precision_cue_match_diagnostics.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using djmeta::PrecisionCueMatchState;

namespace {

djmeta::PrecisionCueRelease release(const std::string& provider, const std::string& id) {
    djmeta::PrecisionCueRelease result;
    result.provider = provider;
    result.provider_release_id = id;
    result.album_title = "Synthetic: THIRTY (DJ Mix)";
    result.album_performer = "Various Artists";
    result.tracks = {
        {1, 1, "Gran Turismo (Mixed)", "Künstler Eins", 164307},
        {1, 2, "Loophole (Mixed)", "Artist Two", 280867},
        {2, 1, "Bebey (Mixed)", "Artist Three", 248173},
        {2, 2, "Hidden Lines (Mixed)", "Artist Four", 107667},
    };
    return result;
}

void strong_match_and_rounded_reference() {
    const auto source = release("deezer", "ref-1");
    auto apple = release("apple", "680000001");
    apple.tracks[0].duration_ms += 750;
    apple.tracks[1].duration_ms -= 1000;
    const auto report = djmeta::match_precision_cue_releases(source, {apple});
    assert(report.state == PrecisionCueMatchState::Selected);
    assert(report.selected_candidate_index == 0);
    assert(report.candidates.size() == 1);
    assert(report.candidates[0].diagnostics.empty());
    assert(djmeta::describe_precision_cue_match_failure(
        report, source, apple).empty());
}


void sourced_edition_date_review() {
    auto source = release("deezer", "synthetic-ref");
    auto apple = release("apple", "synthetic-collection");
    apple.genre = "Electronic";
    apple.release_date = "2026-10-16";
    assert(djmeta::match_precision_cue_releases(source, {apple}).state ==
           PrecisionCueMatchState::Selected);
    source.release_date = "2026-10-15";
    const auto date_report = djmeta::match_precision_cue_releases(source, {apple});
    assert(date_report.state == PrecisionCueMatchState::Review);
    const auto date_explanation = djmeta::describe_precision_cue_match_failure(
        date_report, source, apple);
    assert(date_explanation.find("Deezer 2026-10-15 / Apple 2026-10-16") !=
           std::string::npos);
    assert(!date_report.selected_candidate_index.has_value());
    source.release_date = "2026-10-16";
    assert(djmeta::match_precision_cue_releases(source, {apple}).state ==
           PrecisionCueMatchState::Selected);
    auto conflicting = apple;
    conflicting.genre = "Downtempo";
    assert(djmeta::match_precision_cue_releases(source, {apple, conflicting}).state ==
           PrecisionCueMatchState::Ambiguous);
    apple.genre = "Electronic\nInjected";
    assert(djmeta::match_precision_cue_releases(source, {apple}).state ==
           PrecisionCueMatchState::Rejected);
}

void exact_duplicate_deduplicates_only_same_id() {
    const auto source = release("deezer", "ref-1");
    const auto apple = release("apple", "680000001");
    auto report = djmeta::match_precision_cue_releases(source, {apple, apple});
    assert(report.state == PrecisionCueMatchState::Selected);
    assert(report.selected_candidate_index == 0);
    assert(report.candidates[1].state == PrecisionCueMatchState::Duplicate);
    auto other_id = apple;
    other_id.provider_release_id = "680000002";
    report = djmeta::match_precision_cue_releases(source, {apple, other_id});
    assert(report.state == PrecisionCueMatchState::Ambiguous);
    assert(!report.selected_candidate_index.has_value());
    other_id = apple;
    other_id.tracks[0].duration_ms += 1;
    report = djmeta::match_precision_cue_releases(source, {apple, other_id});
    assert(report.state == PrecisionCueMatchState::Ambiguous);
    assert(!report.selected_candidate_index.has_value());
    other_id.tracks[0].title = "Different (Radio Mix)";
    report = djmeta::match_precision_cue_releases(source, {apple, other_id});
    assert(report.state == PrecisionCueMatchState::Ambiguous);
    assert(!report.selected_candidate_index.has_value());
}

void mix_identity_and_disc_gates() {
    const auto source = release("deezer", "ref-1");
    auto apple = release("apple", "680000001");
    apple.tracks[1].title = "Loophole (Original Mix)";
    const auto mix_report = djmeta::match_precision_cue_releases(source, {apple});
    assert(mix_report.state == PrecisionCueMatchState::Rejected);
    assert(djmeta::describe_precision_cue_match_failure(mix_report, source, apple)
        .find("mix version differs") != std::string::npos);
    apple = release("apple", "680000001");
    apple.tracks[2].disc_number = 1;
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Rejected);
    apple = release("apple", "680000001");
    apple.tracks[2].track_number = 2;
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Rejected);
    apple = release("apple", "680000001");
    apple.tracks[1].duration_ms += 3001;
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Rejected);
}

void incomplete_or_conflicting_evidence_review() {
    auto source = release("deezer", "ref-1");
    auto apple = release("apple", "680000001");
    source.tracks[0].duration_ms = 0;
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Review);
    source = release("deezer", "ref-1");
    apple.tracks[0].duration_ms += 1500;
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Review);
    apple = release("apple", "680000001");
    apple.album_title = "Synthetic: THIRTY (Deluxe)";
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Review);
    apple = release("apple", "680000001");
    apple.tracks[0].performer = "Different Artist";
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Review);
    apple = release("apple", "680000001");
    source.album_performer.clear();
    apple.album_performer.clear();
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Review);
    source = release("deezer", "ref-1");
    apple = release("apple", "680000001");
    source.provider_release_id.clear();
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Review);
    source = release("deezer", "ref-1");
    apple = release("apple", "680000001");
    auto second = apple;
    second.provider_release_id = "680000002";
    second.album_title = "Synthetic: THIRTY (Deluxe)";
    assert(djmeta::match_precision_cue_releases(source, {apple, second}).state == PrecisionCueMatchState::Ambiguous);
}

void malformed_provider_data_fail_closed() {
    auto source = release("deezer", "ref-1");
    auto apple = release("apple", "680000001");
    source.tracks[0].title = "\xc0\xaf";
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Rejected);
    source = release("deezer", "ref-1");
    apple.tracks[0].title = "Bad\nTitle";
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Rejected);
    apple = release("apple", "680000001");
    apple.provider_release_id.clear();
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Rejected);
    apple = release("apple", "680000001");
    apple.tracks[1].duration_ms = 0;
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Rejected);
    apple = release("apple", "680000001");
    apple.tracks[1].track_number = 1;
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Rejected);
    apple = release("apple", "680000001");
    std::vector<djmeta::PrecisionCueRelease> excessive(201, apple);
    assert(djmeta::match_precision_cue_releases(source, excessive).state == PrecisionCueMatchState::Rejected);
}

void normalization_preserves_mix_versions() {
    auto source = release("deezer", "ref-1");
    auto apple = release("apple", "680000001");
    source.album_title = "  SYNTHETIC:  THIRTY (DJ Mix) ";
    source.tracks[0].title = " GRAN TURISMO (Mixed) ";
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Selected);
    apple.tracks[0].title = "Gran Turismo (Radio Edit)";
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Rejected);
}


void eight_disc_99_track_contract() {
    auto reference = release("deezer", "synthetic-maximum");
    auto candidate = release("apple", "synthetic-maximum");
    reference.tracks.clear();
    candidate.tracks.clear();
    for (unsigned disc = 1; disc <= 8; ++disc) {
        for (unsigned track = 1; track <= 99; ++track) {
            reference.tracks.push_back({disc, track, "Synthetic Mix Track",
                                        "Synthetic Performer", 1000});
            candidate.tracks.push_back({disc, track, "Synthetic Mix Track",
                                        "Synthetic Performer", 1000});
        }
    }
    const auto maximum = djmeta::match_precision_cue_releases(reference, {candidate});
    assert(maximum.state == PrecisionCueMatchState::Selected);
    assert(maximum.selected_candidate_index == 0);
    assert(reference.tracks.size() == 792);

    auto malformed = reference;
    malformed.tracks[98].track_number = 100;
    assert(djmeta::match_precision_cue_releases(malformed, {candidate}).state ==
           PrecisionCueMatchState::Rejected);
    malformed = candidate;
    malformed.tracks[98].track_number = 100;
    assert(djmeta::match_precision_cue_releases(reference, {malformed}).state ==
           PrecisionCueMatchState::Rejected);

    reference.tracks.clear();
    candidate.tracks.clear();
    for (unsigned disc = 1; disc <= 9; ++disc) {
        reference.tracks.push_back({disc, 1, "Synthetic Track", "Performer", 1000});
        candidate.tracks.push_back({disc, 1, "Synthetic Track", "Performer", 1000});
    }
    assert(djmeta::match_precision_cue_releases(reference, {candidate}).state ==
           PrecisionCueMatchState::Rejected);

    reference.tracks.pop_back();
    candidate.tracks.pop_back();
    candidate.tracks[7].disc_number = 7;
    assert(djmeta::match_precision_cue_releases(reference, {candidate}).state ==
           PrecisionCueMatchState::Rejected);
}

void cumulative_mixed_edition_drift_gates() {
    auto deezer = release("deezer", "synthetic-whole-second");
    auto apple = release("apple", "synthetic-ms");
    deezer.tracks.clear();
    apple.tracks.clear();
    // Positive fixture: independently rounded seconds may deviate within
    // the conservative per-disc envelope, without an invented audio claim.
    for (unsigned disc = 1; disc <= 2; ++disc) {
        for (unsigned track = 1; track <= 30; ++track) {
            deezer.tracks.push_back(
                {disc, track, "Synthetic Mixed", "Artist", 180000});
            apple.tracks.push_back(
                {disc, track, "Synthetic Mixed", "Artist", 180500});
        }
    }
    auto report = djmeta::match_precision_cue_releases(deezer, {apple});
    assert(report.state == PrecisionCueMatchState::Selected);
    assert(report.selected_candidate_index == 0);

    // Negative fixture: 999ms individually looks acceptable, but cumulative
    // +2997ms already exceeds the 2500ms allowance at disc 1, track 3.
    for (auto& track : apple.tracks) track.duration_ms = 180999;
    report = djmeta::match_precision_cue_releases(deezer, {apple});
    assert(report.state == PrecisionCueMatchState::Review);
    assert(!report.selected_candidate_index.has_value());
    assert(djmeta::describe_precision_cue_match_failure(report, deezer, apple)
        .find("disc 1, track 3 (candidate minus reference 2997 ms, "
              "allowed +/-2500 ms)") != std::string::npos);

    // Valid opposing catalog rounding errors cancel instead of accumulating.
    for (auto& track : apple.tracks)
        track.duration_ms = track.track_number % 2 ? 180900 : 179100;
    report = djmeta::match_precision_cue_releases(deezer, {apple});
    assert(report.state == PrecisionCueMatchState::Selected);

    // Disc 2 starts at zero regardless of the last boundary on disc 1.
    for (auto& track : apple.tracks)
        track.duration_ms = track.disc_number == 1 ? 180500 : 180000;
    for (auto& track : apple.tracks)
        if (track.disc_number == 2 && track.track_number <= 3)
            track.duration_ms = 180900;
    report = djmeta::match_precision_cue_releases(deezer, {apple});
    assert(report.state == PrecisionCueMatchState::Review);
    assert(djmeta::describe_precision_cue_match_failure(report, deezer, apple)
        .find("disc 2, track 3 (candidate minus reference 2700 ms, "
              "allowed +/-2500 ms)") != std::string::npos);

    // Unknown reference timing is already REVIEW, not a spurious cumulative
    // rejection fabricated from a missing duration.
    deezer.tracks[0].duration_ms = 0;
    report = djmeta::match_precision_cue_releases(deezer, {apple});
    assert(report.state == PrecisionCueMatchState::Review);
}

void release_cumulative_24h_contract() {
    auto reference = release("deezer", "synthetic-long-reference");
    auto candidate = release("apple", "synthetic-long-candidate");
    reference.tracks = {
        {1, 1, "Long First", "Artist", 12LL * 3600 * 1000},
        {1, 2, "Long Second", "Artist", 12LL * 3600 * 1000}
    };
    candidate.tracks = reference.tracks;
    assert(djmeta::match_precision_cue_releases(reference, {candidate}).state ==
           PrecisionCueMatchState::Selected);
    ++candidate.tracks[1].duration_ms;
    assert(djmeta::match_precision_cue_releases(reference, {candidate}).state ==
           PrecisionCueMatchState::Rejected);
    reference.tracks[1].duration_ms += 1;
    candidate.tracks = reference.tracks;
    assert(djmeta::match_precision_cue_releases(reference, {candidate}).state ==
           PrecisionCueMatchState::Rejected);
    reference.tracks[1].disc_number = 2;
    reference.tracks[1].track_number = 1;
    candidate.tracks = reference.tracks;
    assert(djmeta::match_precision_cue_releases(reference, {candidate}).state ==
           PrecisionCueMatchState::Selected);
}


void native_conflict_diagnostics_are_bounded_and_located() {
    auto deezer = release("deezer", "synthetic-deezer");
    auto apple = release("apple", "synthetic-apple");
    apple.tracks[1].title = "Loophole (Original Mix)";
    auto report = djmeta::match_precision_cue_releases(deezer, {apple});
    auto message = djmeta::describe_precision_cue_match_failure(report, deezer, apple);
    assert(report.state == PrecisionCueMatchState::Rejected);
    assert(message.find("First conflict: disc 1, track 2") != std::string::npos);
    assert(message.find("Original Mix") == std::string::npos);

    apple = release("apple", "synthetic-apple");
    // Both releases remain structurally valid: the disc boundary differs.
    apple.tracks[2].disc_number = 1;
    apple.tracks[2].track_number = 3;
    apple.tracks[3].track_number = 1;
    report = djmeta::match_precision_cue_releases(deezer, {apple});
    message = djmeta::describe_precision_cue_match_failure(report, deezer, apple);
    assert(report.state == PrecisionCueMatchState::Rejected);
    assert(message.find("disc 2, track 1 (Apple disc 1, track 3)") != std::string::npos);

    apple = release("apple", "synthetic-apple");
    apple.tracks[2].duration_ms += 3001;
    report = djmeta::match_precision_cue_releases(deezer, {apple});
    message = djmeta::describe_precision_cue_match_failure(report, deezer, apple);
    assert(message.find("disc 2, track 1 (Deezer 248173 ms / Apple 251174 ms") !=
           std::string::npos);
    assert(message.find("not audio precision") != std::string::npos);

    apple = release("apple", "synthetic-apple");
    apple.tracks[3].duration_ms += 1500;
    report = djmeta::match_precision_cue_releases(deezer, {apple});
    message = djmeta::describe_precision_cue_match_failure(report, deezer, apple);
    assert(report.state == PrecisionCueMatchState::Review);
    assert(message.find("disc 2, track 2 (Deezer 107667 ms / Apple 109167 ms") !=
           std::string::npos);

    apple = release("apple", "synthetic-apple");
    for (auto& track : apple.tracks) track.performer = "Other Artist";
    report = djmeta::match_precision_cue_releases(deezer, {apple});
    message = djmeta::describe_precision_cue_match_failure(report, deezer, apple);
    const auto first = message.find("Track performer differs or is missing.");
    assert(first != std::string::npos);
    assert(message.find("Track performer differs or is missing.", first + 1) ==
           std::string::npos);
    assert(message.find("disc 1, track 1") != std::string::npos);
    assert(message.size() < 512);
}

} // namespace

int main() {
    strong_match_and_rounded_reference();
    exact_duplicate_deduplicates_only_same_id();
    sourced_edition_date_review();
    native_conflict_diagnostics_are_bounded_and_located();
    mix_identity_and_disc_gates();
    incomplete_or_conflicting_evidence_review();
    malformed_provider_data_fail_closed();
    normalization_preserves_mix_versions();
    eight_disc_99_track_contract();
    release_cumulative_24h_contract();
    cumulative_mixed_edition_drift_gates();
    return 0;
}
