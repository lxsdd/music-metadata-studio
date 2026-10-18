#include "djmeta/precision_cue_source_consensus.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
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
    s.resolution_ms = resolution;
    return s;
}

int main() {
    using namespace djmeta;
    auto apple = source("apple", 1);
    auto mb = source("musicbrainz", 1);
    auto deezer = source("deezer", 1000);
    assert(audit_precision_cue_source_consensus({apple}).state ==
           PrecisionCueConsensusState::SingleHighResolutionSource);
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Agreed);

    mb.release.tracks[0].duration_ms += 13;
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Agreed);
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
    deezer.release.tracks[0].title = "Intro (Original)";
    assert(audit_precision_cue_source_consensus({apple, deezer}).state ==
           PrecisionCueConsensusState::Rejected);
    mb = source("musicbrainz", 1);
    mb.release.tracks[1].track_number = 3;
    assert(audit_precision_cue_source_consensus({apple, mb}).state ==
           PrecisionCueConsensusState::Rejected);
    assert(audit_precision_cue_source_consensus({apple, apple}).state ==
           PrecisionCueConsensusState::Rejected);
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
    assert(report.state == PrecisionCueConsensusState::Agreed);
    assert(report.differences.size() == 30);
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
    return 0;
}
