#include "djmeta/precision_cue.h"
#include "djmeta/precision_cue_foobar_path.h"
#include "djmeta/precision_cue_boundary_audit.h"
#include "djmeta/precision_cue_physical_bounds.h"
#include "djmeta/precision_cue_lookup_budget.h"
#include "djmeta/precision_cue_http_status.h"
#include "djmeta/precision_cue_window_geometry.h"

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
    const std::string local = "D:\\\\Music\\\\Synthetic.mp3";
    assert(precision_cue_foobar_local_windows_mp3_path(local) == local);
    assert(precision_cue_foobar_local_windows_mp3_path(
        "file://D:\\\\Music\\\\Synthetic.mp3") == local);
    assert(precision_cue_foobar_local_windows_mp3_path(
        "FILE://D:\\\\Music\\\\Synthetic.mp3") == local);
    assert(precision_cue_foobar_local_windows_mp3_path(
        "file:///D:/Music/Synthetic.mp3") == local);
    assert(precision_cue_foobar_path_form("FILE://D:/Music/Synthetic.mp3") ==
           "file-uri");
    assert(precision_cue_foobar_path_form("D:/Music/Synthetic.mp3") ==
           "drive-absolute");
    assert(precision_cue_foobar_path_form("synthetic.mp3") ==
           "relative-or-unknown");
    assert(precision_cue_foobar_path_form("\\\\\\\\?\\\\D:\\\\Music\\\\Synthetic.mp3") ==
           "extended-device-path");
    for (const auto bad : {
        "Synthetic.mp3", "/Music/Synthetic.mp3",
        "file://server/share/Synthetic.mp3", "https://example/Synthetic.mp3",
        "file:/D:/Music/Synthetic.mp3",
        "D:/Music/../Synthetic.mp3", "D:/Music/./Synthetic.mp3",
        "D:/Music/CON.mp3", "D:/Music/bad?.mp3",
        "D:/Music/Synthetic.mp3/", "D:/Music//Synthetic.mp3",
        "\\\\\\\\server\\\\share\\\\Synthetic.mp3"
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
    test_integer_cumulative_rounding();
    test_precision_cue_window_geometry_persistence();
    test_physical_cue_index_bounds_and_create_gate();
    test_long_indices_and_unicode();
    test_frame_classification();
    test_quantization_audit_exact_integer_offsets();
    test_boundary_audit_multidisc_synthetic();
    test_invalid_input_fail_closed();
    test_generated_utf8_validation();
    test_unicode_control_and_bidi_safety();
    test_eight_disc_99_track_plan();
    test_bounded_catalog_lookup_admission();
    test_catalog_http_fail_closed_diagnostics();
    return 0;
}
