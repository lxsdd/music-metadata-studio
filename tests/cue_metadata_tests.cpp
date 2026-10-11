#include "djmeta/cue_metadata.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char* why) {
    if (!condition) { std::cerr << "FAIL: " << why << "\n"; std::exit(1); }
}
bool has(const std::vector<std::string>& reasons, const char* expected) {
    return std::find(reasons.begin(), reasons.end(), expected) != reasons.end();
}
}

int main() {
    using namespace djmeta;

    const std::string text = std::string("\xef\xbb\xbf") +
        "REM COMMENT \"FILE not-a-real-reference.mp3 MP3\"\r\n"
        "REM GENRE \"Melodic Trance\"\r\n"
        "REM DATE 2002\r\n"
        "TITLE \"Album - Äußere\"\r\n"
        "PERFORMER \"Album Artist\"\r\n"
        "FILE \"disc one.flac\" WAVE\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    TITLE \"Sunrise (Original Mix)\"\r\n"
        "    PERFORMER \"Track One Artist\"\r\n"
        "    ISRC GB1234567890\r\n"
        "    REM COMMENT \"track one note\"\r\n"
        "    REM UNKNOWN \"preserve me\"\r\n"
        "    INDEX 00 00:00:00\r\n"
        "    INDEX 01 00:02:00\r\n"
        "FILE \"disc two.flac\" WAVE\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    TITLE \"Sunset\"\r\n"
        "    INDEX 01 00:00:00\r\n";
    const auto original = text;
    const auto external = inspect_cue_metadata(text, CueCarrierKind::ExternalText);
    check(original == text && external.status == CueSyntaxStatus::Parsed &&
          external.encoding == CueTextEncoding::Utf8Bom &&
          external.carrier == CueCarrierKind::ExternalText,
          "original and encoded carrier remain byte-identical");
    check(external.files.size() == 2 && external.tracks.size() == 2 &&
          external.tracks[0].file_reference_index == 0 &&
          external.tracks[1].file_reference_index == 1 &&
          external.tracks[0].declared_track_number == 1 &&
          external.tracks[1].declared_track_number == 1,
          "multi FILE supports repeated track 01 on different discs");
    check(external.globals.size() == 5 && external.tracks[0].local_fields.size() == 4 &&
          external.tracks[1].local_fields.size() == 1,
          "unknown REM, INDEX, FILE and pseudo-FILE in comments never become metadata");
    const auto& title = external.tracks[0].local_fields[0];
    check(title.name == "TITLE" && title.value == "Sunrise (Original Mix)" &&
          text.substr(title.value_begin, title.value_end - title.value_begin) == title.value &&
          title.quoted && title.scope == CueFieldScope::TrackLocal,
          "exact original value spans maintained for a later separate patcher");
    const auto one_artist = effective_cue_field(external, 0, "PERFORMER");
    const auto two_artist = effective_cue_field(external, 1, "PERFORMER");
    check(one_artist.present && !one_artist.inherited &&
          one_artist.value == "Track One Artist" &&
          two_artist.present && two_artist.inherited &&
          two_artist.value == "Album Artist",
          "track override vs global artist inheritance are independent");
    const auto album_genre = effective_cue_field(external, 1, "REM GENRE");
    check(album_genre.present && album_genre.inherited &&
          album_genre.value == "Melodic Trance",
          "genre has explicit inherited provenance");
    check(effective_cue_field(external, 1, "TITLE").value == "Sunset" &&
          effective_cue_field(external, 0, "TITLE").value == "Sunrise (Original Mix)",
          "individual track TITLEs are independent of album TITLE");
    const auto without_track_title = inspect_cue_metadata(
        "TITLE Album\nFILE x.flac WAVE\nTRACK 01 AUDIO\n",
        CueCarrierKind::ExternalText);
    check(!effective_cue_field(without_track_title, 0, "TITLE").present,
          "missing track TITLE never inherits the album TITLE");
    check(effective_cue_field(external, 0, "ISRC").present &&
          !effective_cue_field(external, 1, "ISRC").present,
          "track ISRC never inherits from global metadata");

    const auto embedded = inspect_cue_metadata(text, CueCarrierKind::EmbeddedText);
    check(embedded.carrier == CueCarrierKind::EmbeddedText &&
          embedded.tracks[0].local_fields[0].value == title.value,
          "same raw cue text has same semantic parser for either carrier");

    const auto duplicate = inspect_cue_metadata(
        "PERFORMER \"P\"\n"
        "PERFORMER \"Q\"\n"
        "FILE \"x.mp3\" MP3\n"
        "TRACK 01 AUDIO\n"
        "TITLE \"A\"\n",
        CueCarrierKind::ExternalText);
    const auto inherited = effective_cue_field(duplicate, 0, "PERFORMER");
    check(inherited.ambiguous && !inherited.present,
          "duplicate global inheritance is never resolved arbitrarily");

    const auto double_local = inspect_cue_metadata(
        "FILE x.mp3 MP3\nTRACK 01 AUDIO\nTITLE A\nTITLE B\n",
        CueCarrierKind::EmbeddedText);
    check(effective_cue_field(double_local, 0, "TITLE").ambiguous,
          "duplicated local TITLE requires review");

    const auto repeated = inspect_cue_metadata(
        "FILE x.mp3 MP3\nTRACK 01 AUDIO\nTITLE One\n"
        "TRACK 01 AUDIO\nTITLE Two\n",
        CueCarrierKind::ExternalText);
    check(repeated.status == CueSyntaxStatus::NeedsReview &&
          has(repeated.diagnostics, "CUE_DUPLICATE_TRACK_NUMBER_WITHIN_FILE"),
          "duplicated number within same file is unqualified");

    const auto no_track = inspect_cue_metadata(
        "TITLE Album\nFILE x.mp3 MP3\n", CueCarrierKind::ExternalText);
    check(no_track.status == CueSyntaxStatus::Invalid &&
          has(no_track.diagnostics, "CUE_NO_TRACK_DECLARATIONS"),
          "CUE without tracks cannot be treated as an album");

    const auto malformed = inspect_cue_metadata(
        "FILE x.mp3 MP3\nTRACK 01 AUDIO\nTITLE \"unterminated\n",
        CueCarrierKind::ExternalText);
    check(malformed.status == CueSyntaxStatus::NeedsReview &&
          has(malformed.diagnostics, "CUE_METADATA_VALUE_UNQUALIFIED") &&
          !effective_cue_field(malformed, 0, "TITLE").present,
          "unterminated quotes must not create invented tag values");

    const auto binary = inspect_cue_metadata(
        std::string("\xff\xfe", 2) + std::string("F\0I\0", 4),
        CueCarrierKind::EmbeddedText);
    check(binary.status == CueSyntaxStatus::NeedsReview &&
          binary.encoding == CueTextEncoding::UnsupportedUtf16 &&
          binary.tracks.empty(),
          "unknown embedded byte encoding is blocked, not transcoded");

    const auto non_audio = inspect_cue_metadata(
        "FILE x.bin BINARY\nTRACK 01 MODE1/2352\nTITLE DATA\n",
        CueCarrierKind::ExternalText);
    check(non_audio.status == CueSyntaxStatus::NeedsReview &&
          has(non_audio.diagnostics, "CUE_NON_AUDIO_TRACK"),
          "data tracks cannot be silently matched as audio recordings");

    std::string long_tracks = "FILE x.flac WAVE\n";
    for (int n = 1; n <= 1025; ++n)
        long_tracks += "TRACK " + std::to_string(n) + " AUDIO\nTITLE Song\n";
    const auto too_many = inspect_cue_metadata(long_tracks, CueCarrierKind::ExternalText);
    check(too_many.status == CueSyntaxStatus::NeedsReview &&
          has(too_many.diagnostics, "CUE_TRACK_DECLARATION_UNQUALIFIED"),
          "large/malformed CUE refuses >1024 track inventory without a write");

    std::cout << "PASS: external/embedded CUE album, track, inheritance, spans, encoding and bounds\n";
}
