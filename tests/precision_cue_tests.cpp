#include "djmeta/precision_cue.h"

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

} // namespace

int main() {
    test_integer_cumulative_rounding();
    test_long_indices_and_unicode();
    test_frame_classification();
    test_invalid_input_fail_closed();
    return 0;
}
