#include "djmeta/precision_cue_optional_provider_review.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>

using namespace djmeta;
static PrecisionCueRelease release(const char* provider) {
    PrecisionCueRelease r;
    r.provider = provider;
    r.provider_release_id = provider;
    r.album_title = "Synthetic Continuous Mix (DJ Mix)";
    r.album_performer = "Various Artists";
    r.tracks = {
        {1, 1, "Track One (Mixed)", "Artist One", 61234},
        {1, 2, "Track Two (Mixed)", "Artist Two", 123456},
        {2, 1, "Track Three (Mixed)", "Artist Three", 80000}
    };
    return r;
}
int main() {
    const auto apple = release("apple");
    PrecisionCueOptionalSource s{
        release("spotify"), PrecisionCueOptionalFormat::SpotifyWebMilliseconds, true};
    auto r = audit_precision_cue_optional_source(apple, s);
    assert(r.state == PrecisionCueOptionalState::ReviewOnly);
    assert(r.comparison.state == PrecisionCueConsensusState::Agreed);
    assert(r.field_unit_ms == 1 && r.comparison_resolution_ms == 1);
    assert(r.comparison.differences.size() == 3);
    assert(r.comparison.differences[2].signed_start_cumulative_delta_ms == 0);
    assert(r.comparison.differences[0].signed_start_frame_delta == 0);
    assert(r.comparison.differences[2].signed_start_frame_delta == 0);
    // Independent end-of-disc tail vs actual INDEX 01 marker evidence.
    s.release.tracks[1].duration_ms += 14;
    r = audit_precision_cue_optional_source(apple, s);
    assert(r.state == PrecisionCueOptionalState::ReviewOnly);
    assert(r.comparison.state == PrecisionCueConsensusState::Review);
    assert(r.comparison.diagnostics[0] == "DISC_TAIL_DURATION_DISAGREEMENT_REVIEW");
    assert(r.comparison.differences[1].signed_start_frame_delta == 0);
    assert(r.comparison.differences[2].signed_start_frame_delta == 0);
    s.release.tracks[1].duration_ms -= 14;
    s.release.tracks[0].duration_ms += 14;
    r = audit_precision_cue_optional_source(apple, s);
    assert(r.comparison.state == PrecisionCueConsensusState::Review);
    assert(r.comparison.differences[1].signed_start_cumulative_delta_ms == 14);
    assert(r.comparison.differences[1].signed_start_frame_delta == 1);
    assert(r.comparison.differences[2].signed_start_frame_delta == 0);
    s.offline_user_reviewed = false;
    assert(audit_precision_cue_optional_source(apple, s).state ==
        PrecisionCueOptionalState::Blocked);
    s.offline_user_reviewed = true;
    s.release.tracks[0].title = "Track One (Original Mix)";
    assert(audit_precision_cue_optional_source(apple, s).state ==
        PrecisionCueOptionalState::Blocked);
    s.release = release("amazon_music");
    s.format = PrecisionCueOptionalFormat::AmazonWebSeconds;
    for (auto& t : s.release.tracks)
        t.duration_ms = (t.duration_ms / 1000) * 1000;
    r = audit_precision_cue_optional_source(apple, s);
    assert(r.state == PrecisionCueOptionalState::ReviewOnly);
    assert(r.comparison.state == PrecisionCueConsensusState::SingleHighResolutionSource);
    assert(r.field_unit_ms == 1000 && r.comparison_resolution_ms == 1000);
    s.release.tracks[0].duration_ms += 1;
    r = audit_precision_cue_optional_source(apple, s);
    assert(std::string(r.diagnostic) == "AMAZON_WEB_DURATION_MUST_BE_WHOLE_SECONDS");
    s.format = PrecisionCueOptionalFormat::AmazonDeviceApproximateMilliseconds;
    s.release = release("amazon_music");
    r = audit_precision_cue_optional_source(apple, s);
    assert(r.state == PrecisionCueOptionalState::ReviewOnly);
    assert(r.field_unit_ms == 1 && r.comparison_resolution_ms == 1000);
    assert(r.comparison.state == PrecisionCueConsensusState::SingleHighResolutionSource);
    s.release.tracks[1].duration_ms += 5000;
    r = audit_precision_cue_optional_source(apple, s);
    assert(r.comparison.state == PrecisionCueConsensusState::Review);
    s.release.provider = "youtube_music";
    assert(audit_precision_cue_optional_source(apple, s).state ==
        PrecisionCueOptionalState::Blocked);
    s.release = release("amazon_music");
    s.release.release_date = "2026-10-26";
    auto apple_edition = apple;
    apple_edition.release_date = "2026-10-27";
    assert(audit_precision_cue_optional_source(apple_edition, s).state ==
        PrecisionCueOptionalState::Blocked);
    assert(audit_precision_cue_optional_source(release("deezer"), s).state ==
        PrecisionCueOptionalState::Blocked);
    // Full 2-disc synthetic album, no audio, credentials or network.
    auto thirty_apple = release("apple");
    auto thirty_spotify = release("spotify");
    thirty_apple.tracks.clear();
    thirty_spotify.tracks.clear();
    for (unsigned disc = 1; disc <= 2; ++disc) {
        for (unsigned i = 1; i <= 15; ++i) {
            const auto t = PrecisionCueReleaseTrack{
                disc, i, "Synthetic Mix (Mixed)", "Artist", 180000};
            thirty_apple.tracks.push_back(t);
            thirty_spotify.tracks.push_back(t);
        }
    }
    s.release = thirty_spotify;
    s.format = PrecisionCueOptionalFormat::SpotifyWebMilliseconds;
    r = audit_precision_cue_optional_source(thirty_apple, s);
    assert(r.comparison.differences.size() == 30);
    assert(r.comparison.state == PrecisionCueConsensusState::Agreed);
    s.release.tracks[0].duration_ms += 14;
    s.release.tracks[16].duration_ms -= 14;
    r = audit_precision_cue_optional_source(thirty_apple, s);
    assert(r.comparison.state == PrecisionCueConsensusState::Review);
    assert(r.comparison.differences[15].signed_start_cumulative_delta_ms == 0);
    assert(r.comparison.differences[16].signed_start_cumulative_delta_ms == 0);
    assert(r.comparison.differences[0].signed_start_frame_delta == 0);
    assert(r.comparison.differences[1].signed_start_frame_delta == 1);
    assert(r.comparison.differences[15].signed_start_frame_delta == 0);
    assert(r.comparison.differences[16].signed_start_frame_delta == 0);
    assert(r.comparison.differences[17].signed_start_frame_delta == -1);
    assert(r.comparison.differences[18].signed_start_frame_delta == -1);
    s.release.tracks[22].title = "Not this edition";
    assert(audit_precision_cue_optional_source(thirty_apple, s).state ==
        PrecisionCueOptionalState::Blocked);
    return 0;
}
