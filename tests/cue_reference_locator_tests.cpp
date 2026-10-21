#include "djmeta/cue_reference_locator.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

static void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main() {
    const std::string cue = "file:///Z:/Music/Album.cue";
    for (const std::string& basename : {
             std::string("Disc 1.wav"), std::string("Mix (Extended).flac"),
             std::string("Björk - Track.wav"), std::string("COM10.wav"),
             std::string("Äther.wav")}) {
        const auto result = djmeta::preview_cue_sibling_locator(cue, basename);
        require(result.eligible && result.source_candidate ==
            "file:///Z:/Music/" + basename, "safe UTF-8 sibling cue FILE");
    }
    for (const auto& [basename, expected] : {
             std::pair{"CON.wav", "CUE_FILE_RESERVED_DEVICE"},
             {"CON .wav", "CUE_FILE_RESERVED_DEVICE"},
             {"Aux  .flac", "CUE_FILE_RESERVED_DEVICE"},
             {"Lpt9 .mp3", "CUE_FILE_RESERVED_DEVICE"},
             {"COM¹.wav", "CUE_FILE_RESERVED_DEVICE"},
             {"com².flac", "CUE_FILE_RESERVED_DEVICE"},
             {"lpt³.wav", "CUE_FILE_RESERVED_DEVICE"},
             {"../escaped.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"sub/part.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"C:evil.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"name:ads.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"track?.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"lastspace.wav ", "CUE_FILE_AMBIGUOUS_WINDOWS_NAME"},
             {"lastdot.wav.", "CUE_FILE_AMBIGUOUS_WINDOWS_NAME"}}) {
        const auto result = djmeta::preview_cue_sibling_locator(cue, basename);
        require(!result.eligible && result.reason == expected,
                "ambiguous CUE FILE name must fail closed");
    }
    auto invalid = djmeta::preview_cue_sibling_locator(
        cue, std::string("\xC3\x28", 2));
    require(!invalid.eligible && invalid.reason == "CUE_FILE_ENCODING_UNQUALIFIED",
            "invalid UTF-8 in CUE FILE name must not be resolved");
    invalid = djmeta::preview_cue_sibling_locator(
        std::string("file:///Z:/bad\xC3\x28.cue"), "safe.wav");
    require(!invalid.eligible && invalid.reason == "CUE_FILE_ENCODING_UNQUALIFIED",
            "invalid UTF-8 in carrier must not be resolved");
    invalid = djmeta::preview_cue_sibling_locator("Album.cue", "safe.wav");
    require(!invalid.eligible && invalid.reason == "CUE_PARENT_LOCATOR_UNQUALIFIED",
            "missing CUE parent path must fail closed");
    std::cout << "PASS: read-only external CUE FILE sibling lexical qualification\n";
}
