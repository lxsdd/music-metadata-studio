#include "djmeta/precision_cue_apple.h"
#include "djmeta/precision_cue_assemble.h"
#include "djmeta/precision_cue_apple_link.h"
#include "djmeta/precision_cue_apple_preview.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const std::string valid = R"JSON({"resultCount":5,"results":[
 {"wrapperType":"collection","collectionId":12345,"collectionName":"Synthetic Mix","artistName":"Various Artists","trackCount":4},
 {"wrapperType":"track","kind":"song","collectionId":12345,"collectionName":"Synthetic Mix","collectionArtistName":"Various Artists","trackName":"First (Mixed)","artistName":"Artist One","discNumber":1,"discCount":2,"trackNumber":1,"trackCount":2,"trackTimeMillis":164307},
 {"wrapperType":"track","kind":"song","collectionId":12345,"collectionName":"Synthetic Mix","collectionArtistName":"Various Artists","trackName":"Second (Mixed)","artistName":"Artist Two","discNumber":1,"discCount":2,"trackNumber":2,"trackCount":2,"trackTimeMillis":280867},
 {"wrapperType":"track","kind":"song","collectionId":12345,"collectionName":"Synthetic Mix","collectionArtistName":"Various Artists","trackName":"Teil \u00dcber","artistName":"\u6771\u4eac","discNumber":2,"discCount":2,"trackNumber":1,"trackCount":2,"trackTimeMillis":200001},
 {"wrapperType":"track","kind":"song","collectionId":12345,"collectionName":"Synthetic Mix","collectionArtistName":"Various Artists","trackName":"Last","artistName":"Artist Four","discNumber":2,"discCount":2,"trackNumber":2,"trackCount":2,"trackTimeMillis":200002}
]})JSON";

template<class Func> void rejects(Func func) {
    bool caught = false;
    try { func(); } catch (const std::invalid_argument&) { caught = true; }
    assert(caught);
}

std::string replace_once(std::string input, const std::string& from, const std::string& to) {
    const auto at = input.find(from);
    assert(at != std::string::npos);
    input.replace(at, from.size(), to);
    return input;
}

void lookup_positive() {
    auto release = djmeta::parse_apple_precision_cue_lookup(valid, "00012345");
    assert(release.provider == "apple");
    assert(release.provider_release_id == "12345");
    assert(release.album_performer == "Various Artists");
    assert(release.tracks.size() == 4);
    assert(release.tracks[1].duration_ms == 280867);
    assert(release.tracks[2].disc_number == 2 && release.tracks[2].track_number == 1);
    assert(release.tracks[2].title == "Teil Über");
    assert(release.tracks[2].performer == "東京");
    assert(djmeta::apple_precision_cue_lookup_url("00012345") ==
           "https://itunes.apple.com/lookup?id=12345&entity=song&limit=200");
    assert(djmeta::apple_precision_cue_lookup_url("12345", "de") ==
           "https://itunes.apple.com/lookup?id=12345&entity=song&limit=200&country=DE");
    assert(djmeta::apple_precision_cue_album_search_url("Björk & Friends", "DE") ==
           "https://itunes.apple.com/search?media=music&entity=album&limit=25&country=DE&term=Bj%C3%B6rk%20%26%20Friends");

    auto reference = release;
    reference.provider = "deezer";
    reference.provider_release_id = "synthetic-deezer-id";
    djmeta::PrecisionCuePreviewRequest request;
    request.reference = reference;
    request.candidates = {release};
    request.physical_discs = {{"Disc 1.mp3", 445174}, {"Disc 2.mp3", 400003}};
    const auto result = djmeta::make_precision_cue_release_preview(request);
    assert(result.preview_qualified);
    assert(result.discs.size() == 2);
    assert(result.discs[1].boundaries[0].cue_frame == 0);
    assert(result.discs[1].external_cue_utf8.find("Teil Über") != std::string::npos);
    djmeta::ApplePrecisionCuePreviewInput integrated;
    integrated.album_url = "https://music.apple.com/de/album/synthetic-mix/12345";
    integrated.lookup_response_json = valid;
    integrated.reference_release = reference;
    integrated.local_discs = request.physical_discs;
    const auto combined = djmeta::preview_apple_precision_cue_album(integrated);
    assert(combined.state == djmeta::PrecisionCueMatchState::Selected);
    assert(combined.preview_qualified && combined.discs.size() == 2);
    assert(combined.discs[0].boundaries[0].cue_frame == 0);
    assert(combined.discs[1].boundaries[0].cue_frame == 0);

    integrated.album_url = "https://music.apple.com/de/album/synthetic-mix/54321";
    const auto wrong_id = djmeta::preview_apple_precision_cue_album(integrated);
    assert(wrong_id.state == djmeta::PrecisionCueMatchState::Rejected);
    assert(wrong_id.discs.empty() && !wrong_id.preview_qualified);

    integrated.album_url = "https://music.apple.com/de/album/synthetic-mix/12345";
    integrated.local_discs[0].measured_duration_ms += 3001;
    const auto wrong_local_duration = djmeta::preview_apple_precision_cue_album(integrated);
    assert(wrong_local_duration.state == djmeta::PrecisionCueMatchState::Rejected);
    assert(wrong_local_duration.discs.empty());
    integrated.local_discs[0].measured_duration_ms -= 3001;

    integrated.reference_release.tracks[0].duration_ms = 0;
    const auto uncertain = djmeta::preview_apple_precision_cue_album(integrated);
    assert(uncertain.state == djmeta::PrecisionCueMatchState::Review);
    assert(uncertain.discs.empty() && !uncertain.preview_qualified);
}

void lookup_synthetic_thirty() {
    std::string json = R"JSON({"resultCount":31,"results":[{"wrapperType":"collection","collectionId":12345,"collectionName":"Synthetic Mix","artistName":"Various Artists","trackCount":30})JSON";
    for (unsigned disc = 1; disc <= 2; ++disc) {
        const auto count = disc == 1 ? 16u : 14u;
        for (unsigned track = 1; track <= count; ++track) {
            json += ",{\"wrapperType\":\"track\",\"kind\":\"song\",\"collectionId\":12345,";
            json += "\"collectionName\":\"Synthetic Mix\",\"collectionArtistName\":\"Various Artists\",";
            json += "\"trackName\":\"Synthetic Track " + std::to_string(track) +
                    "\",\"artistName\":\"Synthetic Performer\",";
            json += "\"discNumber\":" + std::to_string(disc) +
                    ",\"discCount\":2,\"trackNumber\":" + std::to_string(track) +
                    ",\"trackCount\":" + std::to_string(count) +
                    ",\"trackTimeMillis\":" + std::to_string(120000 + track * 17) + "}";
        }
    }
    json += "]}";
    auto release = djmeta::parse_apple_precision_cue_lookup(json, "12345");
    assert(release.tracks.size() == 30);
    assert(release.tracks[15].disc_number == 1 && release.tracks[15].track_number == 16);
    assert(release.tracks[16].disc_number == 2 && release.tracks[16].track_number == 1);
    assert(release.tracks[29].disc_number == 2 && release.tracks[29].track_number == 14);
    rejects([&] {
        auto incomplete = replace_once(json, "\"resultCount\":31", "\"resultCount\":30");
        (void)djmeta::parse_apple_precision_cue_lookup(incomplete, "12345");
    });
    rejects([&] {
        auto incomplete = replace_once(json, "\"trackCount\":30", "\"trackCount\":31");
        (void)djmeta::parse_apple_precision_cue_lookup(incomplete, "12345");
    });
}

void album_search() {
    const std::string sample = R"JSON({"resultCount":2,"results":[{"wrapperType":"collection","collectionId":12345,"collectionName":"Mix (DJ Mix)","artistName":"Various Artists","trackCount":30},{"wrapperType":"collection","collectionId":67890,"collectionName":"Mix (Original)","artistName":"Various Artists","trackCount":15}]})JSON";
    const auto albums = djmeta::parse_apple_precision_cue_album_search(sample);
    assert(albums.size() == 2);
    assert(albums[0].collection_id == "12345" && albums[0].track_count == 30);
    assert(albums[1].title == "Mix (Original)");
    assert(djmeta::parse_apple_precision_cue_album_search(
        R"JSON({"resultCount":0,"results":[]})JSON").empty());
    rejects([&] {
        (void)djmeta::parse_apple_precision_cue_album_search(
            replace_once(sample, "\"resultCount\":2", "\"resultCount\":3"));
    });
    rejects([&] {
        (void)djmeta::parse_apple_precision_cue_album_search(
            replace_once(sample, "\"collectionId\":67890", "\"collectionId\":12345"));
    });
}

void lookup_negative() {
    rejects([] { (void)djmeta::apple_precision_cue_lookup_url("123&entity=podcast"); });
    rejects([] { (void)djmeta::apple_precision_cue_lookup_url("0"); });
    rejects([] { (void)djmeta::apple_precision_cue_lookup_url("123", "D&"); });
    rejects([] { (void)djmeta::apple_precision_cue_album_search_url("bad\nline", "DE"); });
    rejects([] { (void)djmeta::apple_precision_cue_album_search_url("  ", "DE"); });
    rejects([&] { (void)djmeta::parse_apple_precision_cue_lookup(valid, "98765"); });
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
        {"\"resultCount\":5", "\"resultCount\":4"},
        {"\"trackTimeMillis\":164307", "\"trackTimeMillis\":164.307"},
        {"\"trackTimeMillis\":164307", "\"trackTimeMillis\":0"},
        {"\"trackCount\":4", "\"trackCount\":5"},
        {"\"discCount\":2", "\"discCount\":3"},
        {"\"discNumber\":2", "\"discNumber\":3"},
        {"\"trackNumber\":2", "\"trackNumber\":1"},
        {"\"kind\":\"song\"", "\"kind\":\"music-video\""},
        {"\"trackName\":\"Last\"", "\"trackName\":\"Bad\\uD800\""},
        {"\"trackName\":\"Last\"", "\"trackName\":\"Last\",\"trackName\":\"Other\""},
        {"\"trackName\":\"Last\"", "\"trackName\":\"Bad\\nNewline\""},
        {"\"collectionId\":12345", "\"collectionId\":99999"},
        {"\"wrapperType\":\"collection\"", "\"wrapperType\":\"track\""},
    }) {
        const auto malformed = replace_once(valid, from, to);
        rejects([&] { (void)djmeta::parse_apple_precision_cue_lookup(malformed, "12345"); });
    }
    rejects([] { (void)djmeta::parse_apple_precision_cue_lookup("{}", "12345"); });
    rejects([] { (void)djmeta::parse_apple_precision_cue_lookup("{\"resultCount\":1,\"results\":[]}", "12345"); });
    rejects([&] {
        const auto duplicate = replace_once(valid, "\"trackName\":\"Last\"", "\"trackName\":\"Last\",\"trackName\":\"Other\"");
        (void)djmeta::parse_apple_precision_cue_lookup(duplicate, "12345");
    });
}
} // namespace

int main() {
    lookup_positive();
    lookup_synthetic_thirty();
    album_search();
    lookup_negative();
}
