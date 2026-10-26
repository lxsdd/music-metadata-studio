#include "djmeta/cue_reference_locator.h"
#include "djmeta/cue_raw_route_impact.h"

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
            "file:///Z:/Music/" + basename &&
            result.source_parent=="file:///Z:/Music/" && !result.nested,
            "safe UTF-8 sibling cue FILE");
    }
    for(const std::string& filename:{
            std::string("CD1/01 - Intro.wav"),
            std::string("CD2\\02 - Outro.flac"),
            std::string("CD3/Live/Äther.wav")}) {
        std::string normalized=filename;
        for(char& c:normalized) if(c=='\\') c='/';
        const auto candidate=djmeta::preview_cue_sibling_locator(cue,filename);
        require(candidate.eligible && candidate.nested &&
            candidate.source_parent=="file:///Z:/Music/" &&
            candidate.source_candidate=="file:///Z:/Music/"+normalized,
            "nested relative CUE FILE preserves the CUE parent and UTF-8");
    }
    const auto native_nested=djmeta::preview_cue_sibling_locator(
        "C:\\Music\\Album.cue","Disc 1/Track.flac");
    require(native_nested.eligible && native_nested.nested &&
            native_nested.source_parent=="C:\\Music\\" &&
            native_nested.source_candidate=="C:\\Music\\Disc 1\\Track.flac",
            "native Windows CUE nested path uses the original parent separator");
    for (const auto& [basename, expected] : {
             std::pair{"CON.wav", "CUE_FILE_RESERVED_DEVICE"},
             {"CON .wav", "CUE_FILE_RESERVED_DEVICE"},
             {"Aux  .flac", "CUE_FILE_RESERVED_DEVICE"},
             {"Lpt9 .mp3", "CUE_FILE_RESERVED_DEVICE"},
             {"COM¹.wav", "CUE_FILE_RESERVED_DEVICE"},
             {"com².flac", "CUE_FILE_RESERVED_DEVICE"},
             {"lpt³.wav", "CUE_FILE_RESERVED_DEVICE"},
             {"../escaped.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"sub/../escaped.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"sub/./escaped.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"sub//escaped.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"sub\\\\escaped.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"sub/CON.wav", "CUE_FILE_RESERVED_DEVICE"},
             {"sub/COM².wav", "CUE_FILE_RESERVED_DEVICE"},
             {"sub/name:ads.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"sub/track%2e.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
             {"sub/track\xE2\x80\xAE.wav", "CUE_FILE_UNSAFE_OR_NESTED_PATH"},
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

    // Full physical FILE->captured audio->RAW route inspection uses identity,
    // not a CUE basename or sorted UI row. Every output remains advisory.
    using namespace djmeta;
    const std::vector<CueObservedFileSource> files{
        {0,"phys-A","version-A",true,"host://a"},
        {1,"phys-B","version-B",true,"host://b"},
        {2,"phys-other","version-other",true,"host://other"}};
    const std::vector<CueSelectedPhysicalSource> selected{
        {4,"phys-A","version-A",true},
        {1,"phys-B","version-B",true}};
    const auto matched=audit_cue_physical_selection("cue-physical",files,selected);
    require(matched.eligible && matched.unselected_files==1,
            "host source graph with an unselected FILE");
    std::vector<CueRawRouteTrack> routes(5);
    routes[4]={"phys-A","version-A","Z:\\\\Library","CD1/01.wav",true,false,false};
    routes[1]={"phys-B","version-B","Z:\\\\Library","CD2/02.wav",true,false,false};
    auto impact=inspect_cue_raw_route_impact(files,matched,routes);
    require(impact.source_graph_qualified && impact.raw_candidates==2 &&
            impact.unselected==1 && impact.selected_blocked==0 &&
            impact.references[0].selected_source_row==4 &&
            impact.references[0].code=="CUE_ROUTE_RAW_CANDIDATE_ONLY" &&
            impact.references[0].raw_candidate=="Z:\\\\Library\\\\CD1/01.wav" &&
            impact.references[2].code=="CUE_FILE_AUDIO_NOT_SELECTED",
            "stable physical identities projected to RAW-only CUE routing impact");
    routes[4].pending_metadata_review=true;
    impact=inspect_cue_raw_route_impact(files,matched,routes);
    require(impact.raw_candidates==1 && impact.selected_blocked==1 &&
            impact.references[0].raw_candidate.empty() &&
            impact.references[0].code=="CUE_ROUTE_PENDING_METADATA_REVIEW",
            "unaccepted metadata cannot provide a candidate CUE relocation");
    routes[4].pending_metadata_review=false;
    routes[4].raw_batch_collision=true;
    impact=inspect_cue_raw_route_impact(files,matched,routes);
    require(impact.references[0].code=="CUE_ROUTE_RAW_BATCH_COLLISION",
            "whole-batch raw collision affects CUE dependency");
    routes[4].raw_batch_collision=false;
    routes[4].raw_relative_path="../outside.wav";
    impact=inspect_cue_raw_route_impact(files,matched,routes);
    require(impact.references[0].code=="CUE_ROUTE_RAW_PATH_UNSAFE_OR_EMPTY" &&
            impact.references[0].raw_candidate.empty(),
            "traversal never becomes a CUE relocation candidate");
    routes[4].raw_relative_path="CD1/01.wav";
    routes[4].source_guard="changed-version";
    impact=inspect_cue_raw_route_impact(files,matched,routes);
    require(impact.references[0].code=="CUE_SELECTED_SOURCE_STALE",
            "changing physical version blocks projected CUE destination");
    routes[4].source_guard="version-A";
    auto corrupt=matched;
    corrupt.file_to_selected_row[0]=999;
    impact=inspect_cue_raw_route_impact(files,corrupt,routes);
    require(impact.references[0].code=="CUE_SELECTED_ROW_STALE" &&
            impact.references[0].raw_candidate.empty(),
            "out of range selected row cannot be dereferenced");
    corrupt=matched;
    corrupt.eligible=false;
    impact=inspect_cue_raw_route_impact(files,corrupt,routes);
    require(!impact.source_graph_qualified && impact.raw_candidates==0 &&
            impact.selected_blocked==3,
            "failed graph forbids partial apparently qualified destinations");
    std::cout << "PASS: read-only external CUE FILE sibling and RAW-route impact qualification\n";
}
