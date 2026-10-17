#include "djmeta/precision_cue_match.h"
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
    assert(djmeta::match_precision_cue_releases(source, {apple}).state == PrecisionCueMatchState::Rejected);
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

} // namespace

int main() {
    strong_match_and_rounded_reference();
    exact_duplicate_deduplicates_only_same_id();
    mix_identity_and_disc_gates();
    incomplete_or_conflicting_evidence_review();
    malformed_provider_data_fail_closed();
    normalization_preserves_mix_versions();
    return 0;
}
