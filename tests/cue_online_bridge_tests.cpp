#include "djmeta/cue_online_bridge.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << "\n"; std::exit(1); }
}
}

int main() {
    using namespace djmeta;
    using namespace djmeta::online;

    const std::string cue = "TITLE \"Compilation Album\"\n"
        "PERFORMER \"Various Artists\"\n"
        "FILE \"A.flac\" WAVE\n"
        "  TRACK 01 AUDIO\n"
        "    TITLE \"Solar (Extended Mix)\"\n"
        "    PERFORMER \"Artist A\"\n"
        "    ISRC GBABC2600001\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"B.flac\" WAVE\n"
        "  TRACK 01 AUDIO\n"
        "    TITLE \"Lunar\"\n"
        "    PERFORMER \"Artist B\"\n"
        "    INDEX 01 00:00:00\n";
    const auto snapshot = cue;
    const auto inventory = inspect_cue_metadata(cue, CueCarrierKind::ExternalText);
    const auto local = prepare_cue_release_lookup(inventory);
    check(cue == snapshot && inventory.status == CueSyntaxStatus::Parsed &&
          local.eligible && local.tracks.size() == 2 &&
          local.tracks[0].source_index == 0 && local.tracks[1].source_index == 1,
          "immutable CUE album to stable local comparison records");
    check(local.tracks[0].recording.title == "Solar (Extended Mix)" &&
          local.tracks[1].recording.title == "Lunar" &&
          local.tracks[0].recording.primary_artist == "Artist A" &&
          local.tracks[1].recording.primary_artist == "Artist B" &&
          local.tracks[0].recording.isrc == "GBABC2600001",
          "track title, performer and ISRC do not leak album TITLE/artist");
    check(local.tracks[0].disc_number == -1 &&
          local.tracks[1].disc_number == -1 &&
          local.tracks[0].track_number == 1 &&
          local.tracks[1].track_number == 1 &&
          local.tracks[0].recording.duration_ms == -1,
          "multiple FILEs never imply disc positions or fabricated durations");

    ReleaseEdition edition{"musicbrainz", "release-100", {}, {
        {"r2", {"Lunar", "Artist B", "", MixKind::Unknown, "", -1}, -1, 1, {}},
        {"r1", {"Solar (Extended Mix)", "Artist A", "",
                MixKind::Unknown, "GBABC2600001", -1}, -1, 1, {}}
    }};
    const auto association = align_release(local.tracks, edition);
    check(association.matched == 2 && association.unmatched == 0 &&
          association.tracks[0].edition_track_index == 1 &&
          association.tracks[1].edition_track_index == 0,
          "tracklist assignment by recording identity handles duplicate positions");

    const auto embedded = inspect_cue_metadata(cue, CueCarrierKind::EmbeddedText);
    const auto embedded_lookup = prepare_cue_release_lookup(embedded);
    check(embedded_lookup.eligible &&
          embedded_lookup.carrier == CueCarrierKind::EmbeddedText &&
          embedded_lookup.tracks[0].recording.title == local.tracks[0].recording.title,
          "external and embedded text share exact semantic lookup engine");

    const auto orphan = inspect_cue_metadata(
        "TITLE \"Only Album Name\"\nPERFORMER \"Album Artist\"\n"
        "FILE x.mp3 MP3\nTRACK 01 AUDIO\n",
        CueCarrierKind::EmbeddedText);
    const auto missing_title = prepare_cue_release_lookup(orphan);
    check(!missing_title.eligible && missing_title.tracks.empty() &&
          !missing_title.reasons.empty(),
          "missing track title cannot inherit album title");

    const auto duplicated = inspect_cue_metadata(
        "PERFORMER X\nPERFORMER Y\nFILE x.mp3 MP3\n"
        "TRACK 01 AUDIO\nTITLE One\n",
        CueCarrierKind::ExternalText);
    check(!prepare_cue_release_lookup(duplicated).eligible,
          "ambiguous global inheritance blocks automatic track identity");

    const auto uncertain = inspect_cue_metadata(
        "FILE x.mp3 MP3\nTRACK 01 AUDIO\nTITLE X\nPERFORMER Y\n"
        "TRACK 01 AUDIO\nTITLE Z\nPERFORMER Y\n",
        CueCarrierKind::ExternalText);
    check(!prepare_cue_release_lookup(uncertain).eligible,
          "unqualified duplicate CUE structure never enters confident matching");

    std::string large = "PERFORMER P\nFILE x.flac WAVE\n";
    for (int n = 1; n <= 129; ++n)
        large += "TRACK " + std::to_string(n) +
            " AUDIO\nTITLE Track" + std::to_string(n) + "\n";
    const auto parsed_large = inspect_cue_metadata(large, CueCarrierKind::ExternalText);
    const auto too_large = prepare_cue_release_lookup(parsed_large);
    check(parsed_large.status == CueSyntaxStatus::Parsed &&
          !too_large.eligible && too_large.tracks.empty(),
          "partial release match bound refuses 129 rather than dropping tracks");

    std::cout << "PASS: external/embedded CUE identity, release matching, abstention, bounds\n";
}
