#include "djmeta/precision_cue.h"
#include "djmeta/precision_cue_boundary_audit.h"

// GitHub CI uses Release: keep test assertions enabled on every platform.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

djmeta::PrecisionCueDisc synthetic_disc() {
    djmeta::PrecisionCueDisc disc;
    disc.album_title = "Synthetic Mix";
    disc.album_performer = "Various Artists";
    disc.audio_file_basename = "02. Synthetic Mix.mp3";
    disc.release_date = "2026-01-01";
    disc.genre = "Dance";
    disc.disc_number = 2;
    disc.total_discs = 2;
    disc.tracks = {
        {"Opening", "DJ One", 19},
        {"Transition", "DJ Two", 19},
        {"Finale", "DJ Three", 123456},
    };
    return disc;
}

template<typename Callback>
void must_reject(Callback callback) {
    bool rejected = false;
    try {
        callback();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_integer_cumulative_rounding() {
    const auto result = djmeta::make_precision_cue(synthetic_disc());
    assert(result.boundaries.size() == 3);
    assert(result.boundaries[0].cue_frame == 0);
    assert(result.boundaries[1].cue_frame == 1); // cumulative 19 ms
    assert(result.boundaries[2].cue_frame == 3); // cumulative 38 ms; NOT 1+1=2
    assert(result.total_duration_ms == 123494);
    assert(result.external_cue_utf8.find("INDEX 01 00:00:01") != std::string::npos);
    assert(result.external_cue_utf8.find("INDEX 01 00:00:03") != std::string::npos);
    assert(result.external_cue_utf8.find("FILE \"02. Synthetic Mix.mp3\" WAVE") != std::string::npos);
    assert(result.external_cue_utf8.find("REM DISCNUMBER 2") != std::string::npos);
    assert(result.external_cue_utf8.find("REM TOTALDISCS 2") != std::string::npos);
}

void test_long_indices_and_unicode() {
    assert(djmeta::cue_frame_to_index(60LL * 75 * 75 + 14) == "75:00:14");
    assert(djmeta::cue_frame_to_index(69LL * 60 * 75 + 16 * 75 + 50) == "69:16:50");
    auto disc = synthetic_disc();
    disc.tracks[1].title = "Über den Wolken – 東京";
    const auto first = djmeta::make_precision_cue(disc);
    const auto second = djmeta::make_precision_cue(disc);
    assert(first.external_cue_utf8 == second.external_cue_utf8);
    assert(first.external_cue_utf8.find("Über den Wolken – 東京") != std::string::npos);
}

void test_frame_classification() {
    using enum djmeta::PrecisionCueComparison;
    assert(djmeta::classify_cue_delta(100, 100) == ExactFrame);
    assert(djmeta::classify_cue_delta(100, 101) == WithinOneFrame);
    assert(djmeta::classify_cue_delta(101, 100) == WithinOneFrame);
    assert(djmeta::classify_cue_delta(100, 102) == Review);
    assert(djmeta::classify_cue_delta(-1, 100) == Review);
}

void test_generated_utf8_validation() {
    auto d = synthetic_disc();
    d.album_title = std::string("bad ") + char(0xc3) + '(';
    must_reject([&] { (void)djmeta::make_precision_cue(d); });

    d = synthetic_disc();
    d.tracks[0].title =
        std::string("bad ") + char(0xed) + char(0xa0) + char(0x80);
    must_reject([&] { (void)djmeta::make_precision_cue(d); });

    d = synthetic_disc();
    d.audio_file_basename = std::string("bad ") + char(0xff) + ".mp3";
    must_reject([&] { (void)djmeta::make_precision_cue(d); });

    d = synthetic_disc();
    d.album_title = "München – 東京";
    const auto cue = djmeta::make_precision_cue(d);
    assert(cue.external_cue_utf8.find(d.album_title) != std::string::npos);
}

void test_invalid_input_fail_closed() {
    const auto good = synthetic_disc();
    {
        auto d = good;
        d.audio_file_basename = "../music.mp3";
        must_reject([&] { (void)djmeta::make_precision_cue(d); });
    }
    {
        auto d = good;
        d.audio_file_basename = "C:\\Music\\music.mp3";
        must_reject([&] { (void)djmeta::make_precision_cue(d); });
    }
    {
        auto d = good;
        d.tracks[0].duration_ms = 0;
        must_reject([&] { (void)djmeta::make_precision_cue(d); });
    }
    {
        auto d = good;
        d.tracks[1].duration_ms = -100;
        must_reject([&] { (void)djmeta::make_precision_cue(d); });
    }
    {
        auto d = good;
        d.tracks[0].duration_ms = 1;
        d.tracks[1].duration_ms = 1;
        must_reject([&] { (void)djmeta::make_precision_cue(d); }); // same CUE frame
    }
    {
        auto d = good;
        d.tracks[0].title = "Unsafe \n Newline";
        must_reject([&] { (void)djmeta::make_precision_cue(d); });
    }
    {
        auto d = good;
        d.tracks[0].title = "Unsafe \"quote\"";
        must_reject([&] { (void)djmeta::make_precision_cue(d); });
    }
    {
        auto d = good;
        d.total_discs = 1;
        must_reject([&] { (void)djmeta::make_precision_cue(d); });
    }
    {
        auto d = good;
        d.tracks.clear();
        must_reject([&] { (void)djmeta::make_precision_cue(d); });
    }
    {
        auto d = good;
        d.tracks = std::vector<djmeta::PrecisionCueTrack>(100, {"Synthetic", "Artist", 1000});
        must_reject([&] { (void)djmeta::make_precision_cue(d); });
    }
    // Reject reserved Windows device names and unsafe MP3 basenames.
    for (const auto& name : std::vector<std::string>{
        "CON.mp3", "Lpt1.MP3", "AUX.demo.mp3", "NUL.mp3",
        "COM9.mp3", "bad?.mp3", "bad<name>.mp3",
        "foo|bar.mp3", ".leading.mp3", " leading.mp3",
        "file:name.mp3", "file*.mp3"
    }) {
        auto d = good;
        d.audio_file_basename = name;
        must_reject([&] { (void)djmeta::make_precision_cue(d); });
    }
    must_reject([] { (void)djmeta::cue_frame_to_index(-1); });
    must_reject([] { (void)djmeta::milliseconds_to_cue_frame(-1); });
}


void test_boundary_audit_multidisc_synthetic() {
    using djmeta::PrecisionCueAuditState;
    using djmeta::PrecisionCueReferenceBoundary;
    using djmeta::PrecisionCueDisc;

    // Synthetic 16+14 release: verify positive and negative one-frame deltas
    // without shipping private reference cue sheets or any audio.
    for (unsigned disc_number = 1; disc_number <= 2; ++disc_number) {
        PrecisionCueDisc disc;
        disc.album_title = "Synthetic Continuous Mix";
        disc.album_performer = "Synthetic Ensemble";
        disc.audio_file_basename =
            "Synthetic Disc " + std::to_string(disc_number) + ".mp3";
        disc.disc_number = disc_number;
        disc.total_discs = 2;
        const unsigned count = disc_number == 1 ? 16 : 14;
        for (unsigned i = 0; i < count; ++i) {
            disc.tracks.push_back({
                "Synthetic Track " + std::to_string(i + 1),
                "Synthetic Performer",
                static_cast<std::int64_t>(139000 + i * 23)
            });
        }
        const auto generated = djmeta::make_precision_cue(disc);
        std::vector<PrecisionCueReferenceBoundary> reference;
        for (const auto& boundary : generated.boundaries) {
            reference.push_back({boundary.track_number, boundary.cue_frame});
        }
        const auto exact = djmeta::audit_precision_cue_boundaries(generated, reference);
        assert(exact.state == PrecisionCueAuditState::ExactFrames);
        assert(exact.exact_count == count);
        assert(exact.within_one_frame_count == 0);
        assert(exact.review_count == 0);

        const unsigned changed = disc_number == 1 ? 8 : 4;
        // Opposite signed deltas in the two independently timed mixes.
        if (disc_number == 1) --reference[changed].cue_frame;
        else ++reference[changed].cue_frame;
        const auto one_frame = djmeta::audit_precision_cue_boundaries(generated, reference);
        assert(one_frame.state == PrecisionCueAuditState::WithinOneFrame);
        assert(one_frame.exact_count == count - 1);
        assert(one_frame.within_one_frame_count == 1);
        assert(one_frame.review_count == 0);
        assert(one_frame.differences[changed].signed_delta_frames ==
               (disc_number == 1 ? 1 : -1));

        // More than a single CUE frame is REVIEW, never silently exact.
        if (disc_number == 1) --reference[changed].cue_frame;
        else ++reference[changed].cue_frame;
        const auto review = djmeta::audit_precision_cue_boundaries(generated, reference);
        assert(review.state == PrecisionCueAuditState::Review);
        assert(review.review_count == 1);

        // Invalid reference structure must reject with NO partial results.
        reference[0].cue_frame = 1;
        const auto bad_start = djmeta::audit_precision_cue_boundaries(generated, reference);
        assert(bad_start.state == PrecisionCueAuditState::Rejected);
        assert(bad_start.differences.empty());

        reference[0].cue_frame = 0;
        reference[1].track_number = 1;
        const auto wrong_number = djmeta::audit_precision_cue_boundaries(generated, reference);
        assert(wrong_number.state == PrecisionCueAuditState::Rejected);
        assert(wrong_number.differences.empty());

        reference[1].track_number = 2;
        reference.pop_back();
        const auto missing_track = djmeta::audit_precision_cue_boundaries(generated, reference);
        assert(missing_track.state == PrecisionCueAuditState::Rejected);
        assert(missing_track.differences.empty());
    }
}

} // namespace

int main() {
    test_integer_cumulative_rounding();
    test_long_indices_and_unicode();
    test_frame_classification();
    test_boundary_audit_multidisc_synthetic();
    test_invalid_input_fail_closed();
    test_generated_utf8_validation();
    return 0;
}
