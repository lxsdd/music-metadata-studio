#include "djmeta/precision_cue.h"
#include "djmeta/precision_cue_foobar_path.h"
#include "djmeta/precision_cue_boundary_audit.h"
#include "djmeta/precision_cue_physical_bounds.h"
#include "djmeta/precision_cue_lookup_budget.h"
#include "djmeta/precision_cue_http_status.h"
#include "djmeta/precision_cue_window_geometry.h"
#include "djmeta/precision_cue_request_revision.h"

// GitHub CI uses Release: keep test assertions enabled on every platform.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <limits>
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

void test_foobar_scheme_diagnostics_are_bounded() {
    using djmeta::precision_cue_foobar_scheme_kind;
    using namespace std::string_view_literals;
    assert(precision_cue_foobar_scheme_kind(R"(D:\\Music\\Synthetic.mp3)") == "none"sv);
    assert(precision_cue_foobar_scheme_kind("unpack://private/archive/Synthetic.mp3") ==
           "virtual-unpack"sv);
    assert(precision_cue_foobar_scheme_kind("https://private/Synthetic.mp3") ==
           "remote-http"sv);
    assert(precision_cue_foobar_scheme_kind("file://D:/Music/Synthetic.mp3") ==
           "local-file"sv);
    assert(precision_cue_foobar_scheme_kind("proprietary://private/Synthetic.mp3") ==
           "unrecognized"sv);
    // The diagnostic never reflects a filename, provider URL or arbitrary scheme.
    assert(precision_cue_foobar_scheme_kind("proprietary://secret-1") ==
           precision_cue_foobar_scheme_kind("proprietary://secret-2"));
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

void test_quantization_audit_exact_integer_offsets() {
    const auto plan = djmeta::make_precision_cue(synthetic_disc());
    assert(plan.boundaries.size() == 3);
    assert(djmeta::precision_cue_quantization_offset_microseconds(plan.boundaries[0]) == 0);
    // 19ms -> 1 frame = 13.333...ms; signed offset is -5666.667us.
    assert(djmeta::precision_cue_quantization_offset_microseconds(plan.boundaries[1]) == -5667);
    // 38ms -> 3 frames = 40ms; signed offset is +2000us.
    assert(djmeta::precision_cue_quantization_offset_microseconds(plan.boundaries[2]) == 2000);
    auto invalid = plan.boundaries[1];
    invalid.cue_frame += 1;
    must_reject([&] { (void)djmeta::precision_cue_quantization_offset_microseconds(invalid); });
    invalid = plan.boundaries[1];
    invalid.elapsed_ms = -1;
    must_reject([&] { (void)djmeta::precision_cue_quantization_offset_microseconds(invalid); });
    // A forged enormous elapsed time must not overflow the signed
    // intermediate products before the preview rejects it.
    invalid = plan.boundaries[1];
    invalid.elapsed_ms = std::numeric_limits<std::int64_t>::max();
    invalid.cue_frame = 0;
    must_reject([&] { (void)djmeta::precision_cue_quantization_offset_microseconds(invalid); });
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
    d.album_title = std::string("bad ") + static_cast<char>(static_cast<unsigned char>(0xc3)) + '(';
    must_reject([&] { (void)djmeta::make_precision_cue(d); });

    d = synthetic_disc();
    d.tracks[0].title =
        std::string("bad ") + static_cast<char>(static_cast<unsigned char>(0xed)) + static_cast<char>(static_cast<unsigned char>(0xa0)) + static_cast<char>(static_cast<unsigned char>(0x80));
    must_reject([&] { (void)djmeta::make_precision_cue(d); });

    d = synthetic_disc();
    d.audio_file_basename = std::string("bad ") + static_cast<char>(static_cast<unsigned char>(0xff)) + ".mp3";
    must_reject([&] { (void)djmeta::make_precision_cue(d); });

    d = synthetic_disc();
    d.album_title = "München – 東京";
    const auto cue = djmeta::make_precision_cue(d);
    assert(cue.external_cue_utf8.find(d.album_title) != std::string::npos);
}

void test_unicode_control_and_bidi_safety() {
    // Preserve legitimate Unicode, including combining marks and ZWJ emoji.
    auto disc = synthetic_disc();
    disc.album_title = "München – 東京";
    disc.album_performer = "Кирилл";
    disc.tracks[0].title = "Café 👩‍🎤";
    const auto accepted = djmeta::make_precision_cue(disc);
    assert(accepted.external_cue_utf8.find(disc.tracks[0].title) != std::string::npos);

    const std::vector<std::string> unsafe = {
        "\xC2\x85",          // U+0085 NEL (Unicode C1)
        "\xE2\x80\xAE",      // U+202E right-to-left override
        "\xE2\x80\xA8",      // U+2028 line separator
        "\xE2\x81\xA6",      // U+2066 bidi isolate
        "\xD8\x9C",          // U+061C Arabic letter mark
        "\xEF\xBB\xBF",      // U+FEFF embedded BOM
        "\xEF\xB7\x90",      // U+FDD0 noncharacter
        "\xF4\x8F\xBF\xBF",  // U+10FFFF noncharacter
        "\xC0\xAF",          // overlong UTF-8
        "\xED\xA0\x80"       // UTF-8 surrogate
    };
    for (const auto& bad : unsafe) {
        disc = synthetic_disc();
        disc.tracks[0].title = "Synthetic" + bad + "Track";
        must_reject([&] { (void)djmeta::make_precision_cue(disc); });
        disc = synthetic_disc();
        disc.album_title = "Edition" + bad;
        must_reject([&] { (void)djmeta::make_precision_cue(disc); });
        disc = synthetic_disc();
        disc.audio_file_basename = "disc" + bad + ".mp3";
        must_reject([&] { (void)djmeta::make_precision_cue(disc); });
    }
}

void test_calendar_date_cue_headers() {
    auto disc = synthetic_disc();
    for (const auto* valid : {"2026", "2026-02", "2024-02-29",
                              "2000-02-29", "1900-02-28"}) {
        disc.release_date = valid;
        const auto plan = djmeta::make_precision_cue(disc);
        assert(plan.external_cue_utf8.find(
            std::string("REM DATE \"") + valid + "\"\n") != std::string::npos);
    }
    for (const auto* invalid : {"0000", "2026-00", "2026-13",
                                "2026-02-30", "2025-02-29", "1900-02-29",
                                "2026-04-31", "2026-10-16T00:00:00Z"}) {
        disc.release_date = invalid;
        must_reject([&] { (void)djmeta::make_precision_cue(disc); });
    }
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

        // Each disc's final INDEX 01 must precede its OWN physical end.
        // A late but still in-bounds marker is reviewable, while an
        // at-end/after-end reference fails closed with no partial rows.
        const auto total_frames = djmeta::milliseconds_to_cue_frame(
            generated.total_duration_ms);
        auto late_reference = reference;
        late_reference.back().cue_frame = total_frames - 1;
        const auto late_review = djmeta::audit_precision_cue_boundaries(
            generated, late_reference);
        assert(late_review.state == PrecisionCueAuditState::Review);
        assert(late_review.review_count == 1);
        for (const auto outside_frame : {total_frames, total_frames + 1}) {
            late_reference.back().cue_frame = outside_frame;
            const auto rejected = djmeta::audit_precision_cue_boundaries(
                generated, late_reference);
            assert(rejected.state == PrecisionCueAuditState::Rejected);
            assert(rejected.differences.empty());
        }

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


void test_read_only_edition_scoped_cue_diff() {
    using namespace djmeta;
    // Entirely synthetic 2-disc continuous mix: no private CUE or audio.
    PrecisionCueRelease deezer, apple;
    deezer.provider = "deezer";
    deezer.provider_release_id = "synthetic-reference";
    deezer.album_title = "Synthetic Continuous Mix (Mixed)";
    deezer.album_performer = "Synthetic Ensemble";
    apple = deezer;
    apple.provider = "apple";
    apple.provider_release_id = "synthetic-high-resolution";
    for (unsigned disc = 1; disc <= 2; ++disc) {
        for (unsigned track = 1; track <= (disc == 1 ? 16u : 14u); ++track) {
            const auto title = "Synthetic Track " + std::to_string(track) + " (Mixed)";
            deezer.tracks.push_back({disc, track, title, "Synthetic Performer", 180000});
            apple.tracks.push_back({disc, track, title, "Synthetic Performer", 180000});
        }
    }
    assert(match_precision_cue_releases(deezer, {apple}).state ==
           PrecisionCueMatchState::Selected);
    for (unsigned disc = 1; disc <= 2; ++disc) {
        PrecisionCueDisc source;
        source.album_title = apple.album_title;
        source.album_performer = apple.album_performer;
        source.audio_file_basename = "Synthetic Disc " + std::to_string(disc) + ".mp3";
        source.disc_number = disc;
        source.total_discs = 2;
        for (const auto& track : apple.tracks) {
            if (track.disc_number == disc)
                source.tracks.push_back({track.title, track.performer, track.duration_ms});
        }
        const auto plan = make_precision_cue(source);
        std::vector<PrecisionCueReferenceBoundary> markers;
        for (const auto& boundary : plan.boundaries)
            markers.push_back({boundary.track_number, boundary.cue_frame});
        const auto exact = review_precision_cue_edition_boundaries(
            deezer, {apple}, 0, disc, plan, markers);
        assert(exact.state == PrecisionCueEditionDiffState::ExactFramePreviewOnly);
        assert(exact.boundaries.exact_count == source.tracks.size());
        assert(exact.boundaries.review_count == 0);

        // Opposite one-frame reference offsets must be visible, not "exact".
        if (disc == 1) ++markers[4].cue_frame;
        else --markers[4].cue_frame;
        const auto one_frame = review_precision_cue_edition_boundaries(
            deezer, {apple}, 0, disc, plan, markers);
        assert(one_frame.state == PrecisionCueEditionDiffState::FrameReviewRequired);
        assert(one_frame.boundaries.within_one_frame_count == 1);
        assert(one_frame.boundaries.differences[4].signed_delta_frames ==
               (disc == 1 ? -1 : 1));

        auto forged = plan;
        forged.boundaries[1].elapsed_ms += 1; // same 75fps frame, wrong edition ms
        assert(review_precision_cue_edition_boundaries(
            deezer, {apple}, 0, disc, forged, markers).state ==
            PrecisionCueEditionDiffState::Rejected);
        forged = plan;
        ++forged.total_duration_ms;
        assert(review_precision_cue_edition_boundaries(
            deezer, {apple}, 0, disc, forged, markers).state ==
            PrecisionCueEditionDiffState::Rejected);

        auto wrong_mix = apple;
        wrong_mix.tracks[4].title = "Synthetic Track 5 (Original)";
        assert(review_precision_cue_edition_boundaries(
            deezer, {wrong_mix}, 0, disc, plan, markers).state ==
            PrecisionCueEditionDiffState::EditionReviewRequired);
        auto alternate = apple;
        alternate.provider_release_id = "different-synthetic-edition";
        assert(review_precision_cue_edition_boundaries(
            deezer, {apple, alternate}, 0, disc, plan, markers).state ==
            PrecisionCueEditionDiffState::EditionReviewRequired);
        assert(review_precision_cue_edition_boundaries(
            deezer, {apple}, 1, disc, plan, markers).state ==
            PrecisionCueEditionDiffState::Rejected);
        assert(review_precision_cue_edition_boundaries(
            deezer, {apple}, 0, 3, plan, markers).state ==
            PrecisionCueEditionDiffState::Rejected);
    }
}

void test_cue_diff_rejects_incoherent_generated_plan() {
    using namespace djmeta;
    auto plan = make_precision_cue(synthetic_disc());
    std::vector<PrecisionCueReferenceBoundary> markers;
    for (const auto& b : plan.boundaries)
        markers.push_back({b.track_number, b.cue_frame});
    const auto reject = [&](const PrecisionCuePlan& input) {
        const auto audit = audit_precision_cue_boundaries(input, markers);
        assert(audit.state == PrecisionCueAuditState::Rejected);
        assert(audit.differences.empty()); // fail closed, never partial evidence
    };
    auto invalid = plan;
    invalid.boundaries[1].elapsed_ms = 20; // 20 ms is frame 2, not frame 1
    reject(invalid);
    invalid = plan;
    invalid.boundaries[1].elapsed_ms = -1;
    reject(invalid);
    invalid = plan;
    invalid.boundaries[2].elapsed_ms = plan.total_duration_ms;
    reject(invalid);
    invalid = plan;
    invalid.boundaries[2].elapsed_ms = invalid.boundaries[1].elapsed_ms;
    reject(invalid);
    invalid = plan;
    invalid.boundaries[2].cue_frame =
        milliseconds_to_cue_frame(plan.total_duration_ms);
    reject(invalid);
}


void test_physical_cue_index_bounds_and_create_gate() {
    using State = djmeta::PrecisionCuePhysicalBoundsState;
    auto disc = synthetic_disc();
    disc.tracks = {{"Opening","A",2000},{"Finale","B",1000}};
    const auto plan = djmeta::make_precision_cue(disc);
    const auto good = djmeta::audit_precision_cue_physical_bounds(plan,3000);
    assert(good.state == State::QualifiedForExplicitReview);
    assert(good.final_index_frame == 150);
    assert(good.final_track_frames_available == 75);
    assert(djmeta::audit_precision_cue_physical_bounds(plan,0).state == State::ReviewOnly);
    assert(djmeta::audit_precision_cue_physical_bounds(plan,1999).state == State::Rejected);
    assert(djmeta::audit_precision_cue_physical_bounds(plan,2020).state == State::ReviewOnly);
    assert(djmeta::audit_precision_cue_physical_bounds(plan,4000).state == State::QualifiedForExplicitReview);
    assert(djmeta::audit_precision_cue_physical_bounds(plan,4001).state == State::ReviewOnly);
    assert(djmeta::audit_precision_cue_physical_bounds(plan,6001).state == State::Rejected);
    auto corrupted = plan;
    corrupted.boundaries[1].cue_frame = 149;
    assert(djmeta::audit_precision_cue_physical_bounds(corrupted,3000).state == State::Rejected);
    corrupted = plan;
    corrupted.boundaries[1].track_number = 3;
    assert(djmeta::audit_precision_cue_physical_bounds(corrupted,3000).state == State::Rejected);
    corrupted = plan;
    corrupted.boundaries[1].elapsed_ms = 3000;
    assert(djmeta::audit_precision_cue_physical_bounds(corrupted,3000).state == State::Rejected);
    corrupted = plan;
    corrupted.boundaries[0].elapsed_ms = 1;
    assert(djmeta::audit_precision_cue_physical_bounds(corrupted,3000).state == State::Rejected);
    corrupted = plan;
    corrupted.boundaries.push_back({3,2999,225});
    assert(djmeta::audit_precision_cue_physical_bounds(corrupted,3000).state == State::Rejected);
    // Separate disc clocks, no cumulative multi-disc offsets.
    disc.disc_number = 1;
    disc.tracks = {{"First","A",60000},{"Second","B",60000}};
    const auto first_disc = djmeta::make_precision_cue(disc);
    disc.disc_number = 2;
    const auto second_disc = djmeta::make_precision_cue(disc);
    for (const auto& one_file : {first_disc, second_disc}) {
        assert(djmeta::audit_precision_cue_physical_bounds(one_file,120000).state ==
               State::QualifiedForExplicitReview);
        assert(djmeta::audit_precision_cue_physical_bounds(one_file,59999).state ==
               State::Rejected);
    }
}


void test_eight_disc_99_track_plan() {
    auto disc = synthetic_disc();
    disc.disc_number = 8;
    disc.total_discs = 8;
    disc.tracks = std::vector<djmeta::PrecisionCueTrack>(
        99, {"Synthetic", "Performer", 1000});
    const auto plan = djmeta::make_precision_cue(disc);
    assert(plan.boundaries.size() == 99);
    assert(plan.boundaries.front().cue_frame == 0);
    assert(plan.boundaries.back().cue_frame == 98 * 75);
    assert(plan.external_cue_utf8.find("REM TOTALDISCS 8") != std::string::npos);
    disc.total_discs = 9;
    disc.disc_number = 9;
    must_reject([&] { (void)djmeta::make_precision_cue(disc); });
}


void test_foobar_physical_mp3_path_admission() {
    using djmeta::precision_cue_foobar_local_windows_mp3_path;
    using djmeta::precision_cue_foobar_path_form;
    using djmeta::precision_cue_foobar_safe_scheme_token;
    const std::string local = R"(D:\Music\Synthetic.mp3)";
    assert(precision_cue_foobar_local_windows_mp3_path(local) == local);
    assert(precision_cue_foobar_local_windows_mp3_path(
        R"(file://D:\Music\Synthetic.mp3)") == local);
    assert(precision_cue_foobar_local_windows_mp3_path(
        R"(FILE://D:\Music\Synthetic.mp3)") == local);
    assert(precision_cue_foobar_local_windows_mp3_path(
        "file:///D:/Music/Synthetic.mp3") == local);
    // The standalone parser must NEVER guess a base directory for a
    // portable foobar file-relative:// URI. Host filesystem_v3 must first
    // resolve it to a real drive-absolute native path and verify identity.
    assert(!precision_cue_foobar_local_windows_mp3_path(
        "file-relative://Music/Synthetic.mp3"));
    assert(!precision_cue_foobar_local_windows_mp3_path(
        "file-relative://../Music/Synthetic.mp3"));
    // A virtual embedded-CUE subsong must never impersonate its physical MP3.
    // These are negative identity fixtures only; no actual files are opened.
    for (const auto* virtual_path : {
        "unpack://cue|D:/Music/Synthetic.mp3|2",
        "cue://D:/Music/Synthetic.mp3#track=2",
        "https://example.invalid/Synthetic.mp3",
        "file-relative://D:/Music/Synthetic.mp3"
    }) assert(!precision_cue_foobar_local_windows_mp3_path(virtual_path));
    assert(precision_cue_foobar_path_form("FILE://D:/Music/Synthetic.mp3") ==
           "file-uri");
    assert(precision_cue_foobar_safe_scheme_token("foobar+file://private/path")==
           "foobar+file");
    assert(precision_cue_foobar_safe_scheme_token(
        "https://username:password@host/private.mp3")=="https");
    assert(precision_cue_foobar_safe_scheme_token("unpack://private.wav")=="unpack");
    assert(precision_cue_foobar_safe_scheme_token("synthetic.mp3")=="unknown");
    assert(precision_cue_foobar_safe_scheme_token(
        "private_name@account://folder/song.mp3")=="unknown");
    assert(precision_cue_foobar_safe_scheme_token(
        "extraordinarilylongsecret://folder/song.mp3")=="unknown");
    assert(precision_cue_foobar_path_form("D:/Music/Synthetic.mp3") ==
           "drive-absolute");
    assert(precision_cue_foobar_path_form("synthetic.mp3") ==
           "relative-or-unknown");
    assert(precision_cue_foobar_path_form(R"(\\?\D:\Music\Synthetic.mp3)") ==
           "extended-device-path");
    for (const auto bad : {
        "Synthetic.mp3", "/Music/Synthetic.mp3",
        "file://server/share/Synthetic.mp3", "https://example/Synthetic.mp3",
        "file:/D:/Music/Synthetic.mp3",
        "D:/Music/../Synthetic.mp3", "D:/Music/./Synthetic.mp3",
        "D:/Music/CON.mp3", "D:/Music/bad?.mp3",
        "D:/Music/Synthetic.mp3/", "D:/Music//Synthetic.mp3",
        R"(\\server\share\Synthetic.mp3)"
    }) assert(!precision_cue_foobar_local_windows_mp3_path(bad));
}

} // namespace

void test_bounded_catalog_lookup_admission() {
    // All three entry points share this global UI-thread admission rule.
    for (std::size_t active = 0;
         active < djmeta::kPrecisionCueMaxOutstandingLookups; ++active)
        assert(djmeta::precision_cue_lookup_slot_available(active));
    assert(!djmeta::precision_cue_lookup_slot_available(4));
    assert(!djmeta::precision_cue_lookup_slot_available(5));
    assert(!djmeta::precision_cue_lookup_slot_available(
        std::numeric_limits<std::size_t>::max()));
    // Once an earlier job is reaped, a capacity slot becomes available.
    assert(djmeta::precision_cue_lookup_slot_available(3));
}

void test_catalog_http_fail_closed_diagnostics() {
    using djmeta::precision_cue_catalog_http_diagnostic;
    using namespace std::string_view_literals;
    assert(precision_cue_catalog_http_diagnostic(200).empty());
    assert(precision_cue_catalog_http_diagnostic(401) ==
           "access denied; verify authorized API access"sv);
    assert(precision_cue_catalog_http_diagnostic(403) ==
           "access denied; verify authorized API access"sv);
    assert(precision_cue_catalog_http_diagnostic(404) ==
           "album not available in this catalog"sv);
    assert(precision_cue_catalog_http_diagnostic(408) ==
           "request timed out; retry later"sv);
    assert(precision_cue_catalog_http_diagnostic(429) ==
           "rate limit reached; retry later"sv);
    assert(precision_cue_catalog_http_diagnostic(500) ==
           "catalog temporarily unavailable"sv);
    assert(precision_cue_catalog_http_diagnostic(503) ==
           "catalog temporarily unavailable"sv);
    assert(precision_cue_catalog_http_diagnostic(599) ==
           "catalog temporarily unavailable"sv);
    assert(precision_cue_catalog_http_diagnostic(301) ==
           "unexpected HTTP response"sv); // no redirect is followed
    assert(precision_cue_catalog_http_diagnostic(600) ==
           "unexpected HTTP response"sv);
}


void test_request_revision_cancel_and_stale_result() {
    djmeta::PrecisionCueRequestRevision revision;
    assert(!revision.request_is_current());
    revision.begin_request();
    assert(revision.request_is_current());
    revision.cancel_request();
    assert(!revision.request_is_current());
    revision.begin_request();
    assert(revision.request_is_current()); // only a NEW user request
    revision.input_changed(); // edit while a request is running
    assert(!revision.request_is_current());
    revision.cancel_request(); // repeated dismissal remains fail-closed
    assert(!revision.request_is_current());
    revision.begin_request();
    assert(revision.request_is_current());
}

void test_precision_cue_window_geometry_persistence() {
    using namespace djmeta;
    PrecisionCueWindowGeometry original{-1400, 75, 1100, 780, true};
    const auto text = encode_precision_cue_window_geometry(original);
    assert(text == "v1|-1400|75|1100|780|1");
    PrecisionCueWindowGeometry parsed;
    assert(parse_precision_cue_window_geometry(text, parsed));
    assert(parsed.x == original.x && parsed.y == original.y &&
           parsed.width == original.width && parsed.height == original.height &&
           parsed.maximized);
    for (const auto bad : {
        "", "v2|0|0|800|600|0", "v1|0|0|800|600",
        "v1|0|0|800|600|0|extra", "v1|0|0|800|600|2",
        "v1|0|0|800|600|a", "v1|0|0|999999|600|0",
        "v1|0|0|-1|600|0", "v1|0|0|800|0|0",
        "v1|0x4|0|800|600|0", "v1|1 |0|800|600|0",
        "v1|100001|0|800|600|0", "v1|0|0|800|600|1\n"
    }) {
        assert(!parse_precision_cue_window_geometry(bad, parsed));
    }
    // A former monitor was unplugged: recover to primary visible work area.
    original = {-1990, 80, 1100, 780, false};
    assert(fit_precision_cue_window_to_work_area(original,
        {0, 0, 1920, 1040}, 800, 660));
    assert(original.x == 0 && original.y == 80 &&
           original.width == 1100 && original.height == 780);
    // Still-present second monitor: preserve negative desktop coordinates.
    original = {-1500, 70, 1100, 780, true};
    assert(fit_precision_cue_window_to_work_area(original,
        {-1920, 0, 0, 1040}, 800, 660));
    assert(original.x == -1500 && original.maximized);
    // Clamp oversized windows and a hidden titlebar to visible desktop.
    original = {1000, -1000, 9000, 9000, false};
    assert(fit_precision_cue_window_to_work_area(original,
        {0, 30, 1280, 760}, 800, 660));
    assert(original.x == 0 && original.y == 30 &&
           original.width == 1280 && original.height == 730);
    // Empty/invalid work areas never install stale or corrupt geometry.
    assert(!fit_precision_cue_window_to_work_area(original,
        {0, 0, 0, 900}, 800, 660));
    assert(!fit_precision_cue_window_to_work_area(original,
        {0, 0, 1280, -2}, 800, 660));
}

int main() {
    test_foobar_physical_mp3_path_admission();
    test_foobar_scheme_diagnostics_are_bounded();
    test_integer_cumulative_rounding();
    test_precision_cue_window_geometry_persistence();
    test_request_revision_cancel_and_stale_result();
    test_physical_cue_index_bounds_and_create_gate();
    test_long_indices_and_unicode();
    test_frame_classification();
    test_quantization_audit_exact_integer_offsets();
    test_boundary_audit_multidisc_synthetic();
    test_read_only_edition_scoped_cue_diff();
    test_cue_diff_rejects_incoherent_generated_plan();
    test_invalid_input_fail_closed();
    test_calendar_date_cue_headers();
    test_generated_utf8_validation();
    test_unicode_control_and_bidi_safety();
    test_eight_disc_99_track_plan();
    test_bounded_catalog_lookup_admission();
    test_catalog_http_fail_closed_diagnostics();
    return 0;
}
