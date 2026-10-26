#include "djmeta/cue_reference_locator.h"
#include "djmeta/cue_raw_route_impact.h"
#include "djmeta/cue_file_navigation.h"
#include "djmeta/cue_inspection_queue.h"

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
    routes[4]={"phys-A","version-A","Z:\\Library","CD1/01.wav",true,false,false,FileAction::Copy};
    routes[1]={"phys-B","version-B","Z:\\Library","CD2/02.wav",true,false,false,FileAction::Copy};
    auto impact=inspect_cue_raw_route_impact(files,matched,routes);
    require(impact.source_graph_qualified && impact.raw_candidates==2 &&
            impact.copy_raw_candidates==2 && impact.move_raw_candidates==0 &&
            impact.unselected==1 && impact.selected_blocked==0 &&
            impact.references[0].selected_source_row==4 &&
            impact.references[0].code=="CUE_AUDIO_COPY_RAW_ONLY_SOURCE_CUE_UNCHANGED" &&
            impact.references[0].requested_action==FileAction::Copy &&
            impact.references[0].raw_candidate=="Z:\\Library\\CD1/01.wav" &&
            impact.references[2].code=="CUE_FILE_AUDIO_NOT_SELECTED",
            "stable physical identities projected to RAW-only CUE routing impact");
    // Different explicit operations MUST give different original-CUE
    // consequences, not an invented automatic FILE-reference relocation.
    routes[4].file_action=FileAction::None;
    routes[4].pending_metadata_review=true;
    impact=inspect_cue_raw_route_impact(files,matched,routes);
    require(impact.kept_in_place==1 && impact.copy_raw_candidates==1 &&
            impact.selected_blocked==0 &&
            impact.references[0].code=="CUE_FILE_AUDIO_LEFT_IN_PLACE" &&
            impact.references[0].raw_candidate.empty(),
            "No file action leaves original FILE untouched despite tag review");
    routes[4].pending_metadata_review=false;
    routes[4].file_action=FileAction::Rename;
    impact=inspect_cue_raw_route_impact(files,matched,routes);
    require(impact.rename_blocked==1 &&
            impact.references[0].code=="CUE_RENAME_PARENT_UNVERIFIED" &&
            impact.references[0].raw_candidate.empty(),
            "Rename requires real final foobar same-directory target");
    routes[4].file_action=FileAction::Move;
    impact=inspect_cue_raw_route_impact(files,matched,routes);
    require(impact.partial_move_blocked==1 && impact.raw_candidates==1 &&
            impact.references[0].code=="CUE_MOVE_PARTIAL_DEPENDENCIES_UNQUALIFIED" &&
            impact.references[0].raw_candidate.empty(),
            "Move with unselected CUE FILE dependencies cannot claim safe rewrite");
    // All FILE sources are now physically covered by the captured audio.
    auto complete_files=files;
    complete_files.pop_back();
    const auto complete=audit_cue_physical_selection(
        "cue-physical",complete_files,selected);
    require(complete.eligible && complete.unselected_files==0,
            "complete physical CUE FILE selection for Move staging");
    impact=inspect_cue_raw_route_impact(complete_files,complete,routes);
    require(impact.move_raw_candidates==1 && impact.copy_raw_candidates==1 &&
            impact.unselected==0 && impact.selected_blocked==0 &&
            impact.references[0].code=="CUE_AUDIO_MOVE_RAW_ONLY_REWRITE_UNQUALIFIED" &&
            impact.references[0].raw_candidate=="Z:\\Library\\CD1/01.wav" &&
            impact.references[1].code=="CUE_AUDIO_COPY_RAW_ONLY_SOURCE_CUE_UNCHANGED",
            "complete selection distinguishes CUE rewrite from copy sidecar");
    routes[4].file_action=static_cast<FileAction>(99);
    impact=inspect_cue_raw_route_impact(complete_files,complete,routes);
    require(impact.references[0].code=="CUE_UNKNOWN_FILE_ACTION" &&
            impact.references[0].raw_candidate.empty(),
            "invalid operation never masquerades as CUE relocation");
    routes[4].file_action=FileAction::Copy;
    const auto source_graph_invalid=[&] {
        auto stale=matched;
        stale.unselected_files=99;
        return inspect_cue_raw_route_impact(files,stale,routes);
    }();
    require(!source_graph_invalid.source_graph_qualified &&
            source_graph_invalid.raw_candidates==0 &&
            source_graph_invalid.selected_blocked==files.size(),
            "untrusted FILE graph cardinality inconsistency fails closed");
    require(describe_cue_raw_route_code(
        "CUE_FILE_AUDIO_LEFT_IN_PLACE").find("No file action")!=
            std::string_view::npos &&
        describe_cue_raw_route_code(
        "CUE_MOVE_PARTIAL_DEPENDENCIES_UNQUALIFIED").find("Move blocked")!=
            std::string_view::npos,
            "clear native per-FILE action risk descriptions");
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

    // Distinct external-CUE carrier staging: a default native Move is not
    // approval to move the CUE sidecar with its referenced audio.
    std::vector<CueRawRouteTrack> all_moves(5);
    all_moves[4]={"phys-A","version-A","Z:\\Library","CD1/01.wav",true,false,false,FileAction::Move};
    all_moves[1]={"phys-B","version-B","Z:\\Library","CD2/02.wav",true,false,false,FileAction::Move};
    const auto all_move_impact=inspect_cue_raw_route_impact(
        complete_files,complete,all_moves);
    require(all_move_impact.move_raw_candidates==2 &&
            all_move_impact.selected_blocked==0,
            "complete two-FILE staged move candidates remain raw-only");
    auto carrier=inspect_cue_sidecar_raw_action(
        all_move_impact,FileAction::Move,false,true,false,false,
        "Z:\\Library","Album.cue");
    require(!carrier.raw_only_candidate &&
            carrier.code=="CUE_SIDECAR_ACTION_NOT_EXPLICITLY_STAGED",
            "default Move on CUE carrier is never implicit user consent");
    carrier=inspect_cue_sidecar_raw_action(
        all_move_impact,FileAction::None,true,true,false,false,
        "Z:\\Library","Album.cue");
    require(carrier.code=="CUE_SIDECAR_LEFT_WITH_MOVED_AUDIO_RISK" &&
            carrier.raw_candidate.empty(),
            "leaving CUE behind while audio moves exposes orphan risk");
    carrier=inspect_cue_sidecar_raw_action(
        all_move_impact,FileAction::Move,true,true,false,false,
        "Z:\\Library","Album.cue");
    require(carrier.raw_only_candidate &&
            carrier.code=="CUE_SIDECAR_MOVE_RAW_ONLY_REWRITE_AND_FINAL_CUE_LINKS_UNQUALIFIED" &&
            carrier.raw_candidate=="Z:\\Library\\Album.cue",
            "explicit CUE move is still only RAW pending final host FILE postimage");
    auto mixed_moves=all_moves;
    mixed_moves[1].file_action=FileAction::Copy;
    const auto mixed_impact=inspect_cue_raw_route_impact(
        complete_files,complete,mixed_moves);
    carrier=inspect_cue_sidecar_raw_action(
        mixed_impact,FileAction::Move,true,true,false,false,
        "Z:\\Library","Album.cue");
    require(carrier.code=="CUE_SIDECAR_AUDIO_ACTIONS_MIXED_OR_UNQUALIFIED",
            "mixed audio Move and Copy cannot be papered over by CUE move");
    auto all_copies=all_moves;
    all_copies[4].file_action=FileAction::Copy;
    all_copies[1].file_action=FileAction::Copy;
    const auto all_copy_impact=inspect_cue_raw_route_impact(
        complete_files,complete,all_copies);
    carrier=inspect_cue_sidecar_raw_action(
        all_copy_impact,FileAction::Copy,true,true,false,false,
        "Z:\\Library","Album.cue");
    require(carrier.raw_only_candidate &&
            carrier.code=="CUE_SIDECAR_COPY_RAW_ONLY_COPIED_LINKS_UNQUALIFIED",
            "copied CUE requires independent links to copied audio");
    carrier=inspect_cue_sidecar_raw_action(
        all_copy_impact,FileAction::None,true,true,false,false,
        "Z:\\Library","Album.cue");
    require(carrier.code=="CUE_SIDECAR_AND_ORIGINAL_FILE_LINKS_UNCHANGED",
            "audio copies with untouched CUE preserve original FILE reference");
    carrier=inspect_cue_sidecar_raw_action(
        all_copy_impact,FileAction::Rename,true,true,false,false,
        "Z:\\Library","Album.cue");
    require(carrier.code=="CUE_SIDECAR_RENAME_FINAL_PARENT_UNQUALIFIED",
            "CUE rename cannot guess host-final parent equivalence");
    carrier=inspect_cue_sidecar_raw_action(
        all_copy_impact,FileAction::Copy,true,false,false,false,
        "Z:\\Library","Album.cue");
    require(carrier.code=="CUE_SIDECAR_PHYSICAL_CARRIER_UNQUALIFIED",
            "unqualified physical CUE carrier cannot be staged");
    carrier=inspect_cue_sidecar_raw_action(
        all_copy_impact,FileAction::Copy,true,true,true,false,
        "Z:\\Library","Album.cue");
    require(carrier.code=="CUE_SIDECAR_CUE_VALUES_PENDING",
            "pending CUE metadata blocks sidecar move or copy");
    carrier=inspect_cue_sidecar_raw_action(
        all_copy_impact,FileAction::Copy,true,true,false,true,
        "Z:\\Library","Album.cue");
    require(carrier.code=="CUE_SIDECAR_RAW_BATCH_COLLISION",
            "full-batch CUE carrier collision blocks sidecar copy");
    carrier=inspect_cue_sidecar_raw_action(
        all_copy_impact,FileAction::Copy,true,true,false,false,
        "Z:\\Library","../escape.cue");
    require(carrier.code=="CUE_SIDECAR_RAW_CARRIER_PATH_UNQUALIFIED",
            "unsafe sidecar target path cannot become a candidate");
    carrier=inspect_cue_sidecar_raw_action(
        inspect_cue_raw_route_impact(files,matched,all_copies),
        FileAction::Copy,true,true,false,false,
        "Z:\\Library","Album.cue");
    require(carrier.code=="CUE_SIDECAR_INCOMPLETE_OR_BLOCKED_AUDIO_GRAPH",
            "copied CUE cannot omit unselected FILE sources");
    carrier=inspect_cue_sidecar_raw_action(
        all_copy_impact,static_cast<FileAction>(99),true,true,false,false,
        "Z:\\Library","Album.cue");
    require(carrier.code=="CUE_SIDECAR_ACTION_INVALID",
            "invalid sidecar action is explicitly blocked");

    // Native candidate source_id encodes stable captured physical row IDs
    // only. File locations sorting/filtering cannot rebind an activated row.
    const auto nav_id=cue_file_navigation_id(0,4);
    require(nav_id=="cue-raw-route:1/source/5",
            "CUE routing IDs retain original 1-based source row identity");
    auto nav=resolve_cue_file_navigation(nav_id,2,5);
    require(nav && nav->file_index==0 && nav->source_index==4 &&
            !nav->carrier, "linked CUE FILE resolves exact captured physical source");
    nav=resolve_cue_file_navigation("cue-sidecar-action",2,5);
    require(nav && nav->carrier && nav->source_index==2,
            "carrier action links to exact CUE physical source row");
    for(const std::string_view bad:{
        "cue-raw-route:1", "cue-raw-route:1/source/0",
        "cue-raw-route:1/source/6", "cue-raw-route:129/source/1",
        "cue-raw-route:01/source/1", "cue-raw-route:1/source/01",
        "cue-raw-route:1/source/99999999999999999999999999",
        "cue-raw-route:1/source/-1", "cue-raw-route:1/source/1/extra",
        "cue-raw-route:1/source/1\\tWRITER", "cue-file:1", "cue-raw-route:unavailable"}) {
        require(!resolve_cue_file_navigation(bad,2,5),
                "malformed/unselected/external CUE FILE link cannot navigate");
    }
    require(!resolve_cue_file_navigation("cue-sidecar-action",5,5) &&
            !resolve_cue_file_navigation("cue-sidecar-action",0,0),
            "stale or empty captured CUE carrier selection fails closed");
    require(cue_file_navigation_id(128,1)=="cue-raw-route:unavailable" &&
            cue_file_navigation_id(1,100000)=="cue-raw-route:unavailable",
            "out-of-bounds CUE reference cannot invent a target source");
    CueInspectionQueue queue;
    std::vector<CueQueuePlanRow> snapshot(54129);
    snapshot[8]={"private-id","private-guard","R:\\PRIVATE","Track.flac",
                 FileAction::Move,true,true,false,false};
    queue.observe(snapshot);
    const std::vector<std::size_t> cues{8,32};
    require(queue.counts(cues).missing==2 &&
            queue.next_attention(cues,8)==32,
            "54k source-order CUE inspection navigates missing source");
    auto moved=inspect_cue_sidecar_raw_action(
        all_move_impact,FileAction::Move,true,true,false,false,
        "Z:\\Library","Album.cue");
    queue.record(8,all_move_impact,moved);
    require(queue.stage(8)==CueQueueStage::RawOnly,
            "CUE FILE move is only RAW, not final FileOps");
    auto kept=inspect_cue_sidecar_raw_action(
        all_copy_impact,FileAction::None,true,true,false,false,
        "Z:\\Library","Album.cue");
    queue.record(32,all_copy_impact,kept);
    require(queue.stage(32)==CueQueueStage::OriginalKept,
            "CUE kept original on audio Copy remains advisory");
    require(queue.next_attention(cues,32)==queue.plan.size(),
            "Next CUE must not endlessly revisit previously inspected RAW-only CUEs");
    auto tsv=queue.export_tsv(cues);
    require(tsv.find("#9\tRAW_ONLY_FINAL_CUE_UNQUALIFIED")!=std::string::npos &&
            tsv.find("#33\tORIGINAL_CUE_KEPT")!=std::string::npos &&
            tsv.find("PRIVATE")==std::string::npos &&
            tsv.find("private-id")==std::string::npos,
            "whole-batch CUE audit exports no source keys or paths");
    require(tsv.find("#9/FILE#1\t#5\tMove\t"
                "CUE_AUDIO_MOVE_RAW_ONLY_REWRITE_UNQUALIFIED\tNOT_RESOLVED")!=
                std::string::npos &&
            tsv.find("#33/FILE#1\t#5\tCopy\t"
                "CUE_AUDIO_COPY_RAW_ONLY_SOURCE_CUE_UNCHANGED\tNOT_RESOLVED")!=
                std::string::npos &&
            tsv.find("Sidecar finding\tCUE_SIDECAR_")!=std::string::npos,
            "native CUE clipboard audit shows exact FILE ordinal, captured source and risk");
    auto forged=all_move_impact;
    forged.references[0].code="CUE\\tINJECTED";
    try { queue.record(8,forged,moved); require(false,"unsafe FILE code"); }
    catch(const std::invalid_argument&) {}
    forged=all_move_impact;
    forged.references[0].reference_index=1;
    try { queue.record(8,forged,moved); require(false,"reordered FILE"); }
    catch(const std::invalid_argument&) {}
    forged=all_move_impact;
    forged.references[0].selected_source_row=queue.plan.size();
    try { queue.record(8,forged,moved); require(false,"forged source row"); }
    catch(const std::invalid_argument&) {}
    require(queue.stage(8)==CueQueueStage::RawOnly &&
            queue.export_tsv(cues).find("#9/FILE#1\t#5\tMove")!=
                std::string::npos,
            "rejected FILE evidence preserves last qualified receipt");
    tsv=queue.export_tsv(cues,true);
    require(tsv.find("#33\tORIGINAL_CUE_KEPT")==std::string::npos &&
            tsv.find("#33/FILE#")==std::string::npos,
            "Copy issues excludes unchanged original CUE FILEs");
    const auto epoch=queue.epoch;
    queue.observe(snapshot);
    require(queue.epoch==epoch && queue.stage(8)==CueQueueStage::RawOnly,
            "sort and filter preserve exact current inspection");
    snapshot[11].raw_filename="different-hidden.flac";
    queue.observe(snapshot);
    require(queue.epoch!=epoch && queue.counts(cues).stale==2 &&
            queue.stage(8)==CueQueueStage::Stale,
            "changing hidden source invalidates ALL prior CUE inspections");
    tsv=queue.export_tsv(cues,true);
    require(tsv.find("#9\tSTALE_REINSPECT")!=std::string::npos &&
            tsv.find("#33\tSTALE_REINSPECT")!=std::string::npos &&
            tsv.find("/FILE#")==std::string::npos,
            "stale CUEs included in privacy-safe Copy issues");
    require(queue.next_attention(cues,32)==8,
            "stale earlier CUE carrier is found after queue wrap");
    // A reversed 54,129-source ListView must retain original source IDs.
    std::vector<std::size_t> all_sorted(54129);
    for(std::size_t i=0;i<all_sorted.size();++i)
        all_sorted[i]=all_sorted.size()-i-1;
    auto visible=filter_cue_queue_view(
        queue,cues,all_sorted,BatchLocationQueue::CueAll);
    require(visible==std::vector<std::size_t>({32,8}),
            "all CUE carriers preserve sorted view but physical source IDs");
    visible=filter_cue_queue_view(
        queue,cues,all_sorted,BatchLocationQueue::CueAttention);
    require(visible==std::vector<std::size_t>({32,8}),
            "changed hidden source exposes every stale CUE");
    visible=filter_cue_queue_view(
        queue,cues,all_sorted,BatchLocationQueue::CueRawOnly);
    require(visible.empty(),
            "stale CUE cannot appear as a currently inspected RAW-only source");
    queue.record(8,all_move_impact,moved);
    queue.record(32,all_copy_impact,kept);
    visible=filter_cue_queue_view(
        queue,cues,all_sorted,BatchLocationQueue::CueRawOnly);
    require(visible==std::vector<std::size_t>({8}),
            "RAW-only CUE filter preserves source identity");
    visible=filter_cue_queue_view(
        queue,cues,all_sorted,BatchLocationQueue::CueOriginalKept);
    require(visible==std::vector<std::size_t>({32}),
            "original-kept CUE filter uses same inspected source graph");
    visible=filter_cue_queue_view(
        queue,cues,all_sorted,BatchLocationQueue::CueAttention);
    require(visible.empty(),
            "completed CUE inspection is not repeatedly queued as missing");
    auto duplicate=all_sorted;
    duplicate[0]=duplicate[1];
    try {
        (void)filter_cue_queue_view(queue,cues,duplicate,
                                    BatchLocationQueue::CueAll);
        require(false,"duplicate visible ID");
    } catch(const std::invalid_argument&) {}
    try {
        (void)filter_cue_queue_view(queue,cues,{8,32},
                                    BatchLocationQueue::CueRawOnly);
        require(false,"truncated physical source permutation");
    } catch(const std::invalid_argument&) {}
    try {
        (void)filter_cue_queue_view(queue,{32,8},all_sorted,
                                    BatchLocationQueue::CueAll);
        require(false,"out of order carrier list");
    } catch(const std::invalid_argument&) {}
    try {
        (void)filter_cue_queue_view(queue,cues,all_sorted,
                                    BatchLocationQueue::FileActionBlocked);
        require(false,"non-CUE filter");
    } catch(const std::invalid_argument&) {}
    try { (void)queue.counts({32,8}); require(false,"invalid order"); }
    catch(const std::invalid_argument&) {}
    std::cout << "PASS: action-aware CUE FILE and source-identity navigation dry-run qualification\n";
}
