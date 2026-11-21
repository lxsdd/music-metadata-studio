#include "djmeta/cue_association.h"
#include "djmeta/cue_file_batch_preview.h"
#include "djmeta/cue_physical_selection_audit.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace djmeta;

void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void test_single() {
    const std::string cue = "REM GENRE House\r\nFILE \"old.mp3\" MP3\r\nTRACK 01 AUDIO\r\n";
    const auto plan = qualify_external_cue_association(cue,
        {{0,"old.mp3","id://old","Artist - Mix.mp3",true,true,"id://target"}},
        {{"phys-A","id://old","id://target"}});
    check(plan.qualified && plan.changes_references, "single association should qualify");
    check(plan.changes.size()==1 && plan.related_selected_audio_ids.size()==1 &&
          plan.related_selected_audio_ids[0]=="phys-A", "single association details");
    check(plan.proposed_cue_bytes == "REM GENRE House\r\nFILE \"Artist - Mix.mp3\" MP3\r\nTRACK 01 AUDIO\r\n", "preserved unrelated cue bytes");
    check(plan.original_cue_fingerprint != plan.proposed_cue_fingerprint, "cue postimage hash");
}
void test_multifile_unselected() {
    const std::string cue = "FILE \"a.flac\" WAVE\nFILE \"b.flac\" WAVE\n";
    const auto plan = qualify_external_cue_association(cue,
       {{0,"a.flac","src-A","a-new.flac",true,true,"dst-A"},
        {1,"b.flac","src-unselected","b.flac",true,true,"src-unselected"}},
       {{"A","src-A","dst-A"}});
    check(plan.qualified && plan.changes.size()==1, "multi-file mixed selection");
    check(plan.proposed_cue_bytes=="FILE \"a-new.flac\" WAVE\nFILE \"b.flac\" WAVE\n", "nonselected FILE unchanged");
    const auto rejected = qualify_external_cue_association(cue,
       {{0,"a.flac","src-A","a-new.flac",true,true,"dst-A"},
        {1,"b.flac","src-unselected","b.flac",true,false,"src-unselected"}},
       {{"A","src-A","dst-A"}});
    check(!rejected.qualified, "unselected reference must be verified after cue move");
}
void test_missing_and_ambiguous() {
    const std::string cue="FILE \"old.flac\" WAVE\n";
    check(!qualify_external_cue_association(cue, {}, {}).qualified, "missing host coverage");
    check(!qualify_external_cue_association(cue, {{0,"old.flac","src","next.flac",false,true,"to"}}, {}).qualified, "ambiguous host path");
    check(!qualify_external_cue_association(cue, {{0,"old.flac","src","next.flac",true,false,"to"}}, {}).qualified, "unchecked host target");
    check(!qualify_external_cue_association(cue, {{0,"wrong.flac","src","next.flac",true,true,"to"}}, {}).qualified, "stale expected file name");
    check(!qualify_external_cue_association(cue, {{1,"old.flac","src","next.flac",true,true,"to"}}, {}).qualified, "reference index out of range");
    check(!qualify_external_cue_association(cue, {{0,"old.flac","src","next.flac",true,true,"to"}}, {{"A","src","to"},{"B","src","to2"}}).qualified, "two selected physical source identities");
    check(!qualify_external_cue_association(cue, {{0,"old.flac","src","next.flac",true,true,"to"}}, {{"A","src","to"},{"A","other","to2"}}).qualified, "two selected physical IDs");
}
void test_exact_nochange_and_unsafe_postimage() {
    const std::string cue="FILE old.flac WAVE\n";
    const auto same=qualify_external_cue_association(cue, {{0,"old.flac","old","old.flac",true,true,"old"}}, {});
    check(same.qualified && !same.changes_references && same.proposed_cue_bytes == cue, "unchanged verified cue");
    const auto bad=qualify_external_cue_association(cue, {{0,"old.flac","old","evil\nFILE new.flac WAVE",true,true,"old"}}, {});
    check(!bad.qualified, "dangerous reference postimage");
}
void test_exact_target_identity_and_collisions() {
    const std::string cue="FILE \"old.flac\" WAVE\n";
    const auto good=qualify_external_cue_association(cue,
        {{0,"old.flac","src","new.flac",true,true,"to"}},
        {{"A","src","to"}});
    check(good.qualified && good.related_selected_audio_ids==
              std::vector<std::string>{"A"},
          "a real FILE destination identity matches the chosen audio target");
    const auto bare_bool=qualify_external_cue_association(cue,
        {{0,"old.flac","src","new.flac",true,true,""}},
        {{"A","src","to"}});
    check(!bare_bool.qualified && bare_bool.diagnostics==
              std::vector<std::string>{"CUE_DESTINATION_RELATION_UNVERIFIED"},
          "boolean verified with no host destination identity must fail");
    const auto mismatch=qualify_external_cue_association(cue,
        {{0,"old.flac","src","new.flac",true,true,"another-audio"}},
        {{"A","src","to"}});
    check(!mismatch.qualified && mismatch.diagnostics==
              std::vector<std::string>{"CUE_TARGET_DIFFERS_FROM_SELECTED_AUDIO_PLAN"},
          "a substituted FILE target cannot masquerade as chosen audio");
    const std::string multi=
        "FILE \"a.flac\" WAVE\nFILE \"b.flac\" WAVE\n";
    const auto collision=qualify_external_cue_association(multi,
        {{0,"a.flac","srcA","a-new.flac",true,true,"same-destination"},
         {1,"b.flac","srcB","b-new.flac",true,true,"same-destination"}},
        {{"A","srcA","same-destination"},{"B","srcB","same-destination"}});
    check(!collision.qualified && collision.diagnostics==
              std::vector<std::string>{"CUE_MULTIPLE_SOURCES_SHARE_DESTINATION"},
          "different audio sources cannot collide at one physical target");
    const auto unselected=qualify_external_cue_association(multi,
        {{0,"a.flac","srcA","a-new.flac",true,true,"dstA"},
         {1,"b.flac","srcB","b.flac",true,true,""}},
        {{"A","srcA","dstA"}});
    check(!unselected.qualified && unselected.diagnostics==
              std::vector<std::string>{"CUE_DESTINATION_RELATION_UNVERIFIED"},
          "even unselected FILE references require observed target identities");
}
void test_exact_joint_fileplan_cue_binding() {
    const std::string raw_cue=
        "FILE \"a.flac\" WAVE\nFILE \"b.flac\" WAVE\n";
    auto audio=[](const char* name,const char* src,const char* dst) {
        FilePlanItem row;
        row.physical_id=name;
        row.source_path=std::string("Z:/source/")+name+".flac";
        row.source_key=src;
        row.source_guard=std::string("guard:")+name;
        row.target_path=std::string("Z:/dest/")+name+".flac";
        row.target_key=dst;
        row.action=FileAction::Move;
        row.role=FileRole::Audio;
        row.target_presence=TargetPresence::Missing;
        row.cue_links=CueLinkState::Verified;
        row.ruleset_revision="rules:v1";
        row.metadata_fingerprint="tags:unchanged";
        row.planned_metadata_fingerprint="tags:unchanged";
        return row;
    };
    auto a=audio("A","srcA","dstA");
    auto b=audio("B","srcB","dstB");
    FilePlanItem cue;
    cue.physical_id="cue-physical";
    cue.source_path="Z:/source/mix.cue";
    cue.source_key="cue-source";
    cue.source_guard="cue-guard";
    cue.target_path="Z:/dest/mix.cue";
    cue.target_key="cue-target";
    cue.role=FileRole::ExternalCue;
    cue.action=FileAction::Move;
    cue.target_presence=TargetPresence::Missing;
    cue.cue_links=CueLinkState::Verified;
    cue.ruleset_revision="rules:v1";
    const std::vector<CueHostReference> host{
        {0,"a.flac","srcA","A.flac",true,true,"dstA"},
        {1,"b.flac","srcB","B.flac",true,true,"dstB"}
    };
    const std::vector<CueSelectedAudio> selected{
        {"A","srcA","dstA"},{"B","srcB","dstB"}
    };
    const std::vector<FilePlanItem> captured{a,b,cue};
    const auto joined=preview_cue_bound_file_batch(
        captured,2,raw_cue,host,selected);
    check(joined.eligible && joined.review.blocked==0 &&
          joined.review.ready_to_apply && joined.items.size()==3 &&
          joined.items[2].associated_audio_ids==
              std::vector<std::string>({"A","B"}) &&
          joined.items[2].cue_references_will_change &&
          joined.review.decisions[2].cue_bytes_write_needed &&
          captured[2].associated_audio_ids.empty() &&
          captured[2].cue_source_fingerprint.empty(),
          "source-verified multi-FILE and audio plans yield one immutable cue postimage");
    auto stale=captured;
    stale[1].target_key="unrelated-target";
    check(!preview_cue_bound_file_batch(stale,2,raw_cue,host,selected).eligible,
          "changed selected audio destination blocks joint frozen-intent candidate");
    stale=captured;
    stale[0].source_guard.clear();
    check(!preview_cue_bound_file_batch(stale,2,raw_cue,host,selected).eligible,
          "source without independent current physical guard fails closed");
    stale=captured;
    stale[2].cue_source_fingerprint="different old bytes";
    check(!preview_cue_bound_file_batch(stale,2,raw_cue,host,selected).eligible,
          "previously staged CUE bytes cannot be silently superseded");
    stale=captured;
    stale[2].associated_audio_ids={"A","foreign"};
    check(!preview_cue_bound_file_batch(stale,2,raw_cue,host,selected).eligible,
          "previous CUE participant set must never be silently replaced");
    stale=captured;
    stale[2].target_presence=TargetPresence::Unchecked;
    check(!preview_cue_bound_file_batch(stale,2,raw_cue,host,selected).eligible,
          "uninspected active CUE target fails full batch plan");
    stale=captured;
    stale[2].cue_values_will_change=true;
    check(!preview_cue_bound_file_batch(stale,2,raw_cue,host,selected).eligible,
          "a separate CUE values edit cannot bypass its own review authority");
    auto colliding=host;
    colliding[1].verified_destination_key="dstA";
    check(!preview_cue_bound_file_batch(captured,2,raw_cue,colliding,selected).eligible,
          "host-verified FILE destination collision blocks joint batch");
    const auto no_write_host=std::vector<CueHostReference>{
        {0,"a.flac","srcA","a.flac",true,true,"srcA"},
        {1,"b.flac","srcB","b.flac",true,true,"srcB"}
    };
    auto noop=captured;
    noop[0].action=FileAction::None;
    noop[1].action=FileAction::None;
    noop[2].action=FileAction::None;
    for(auto& row:noop) {
        row.target_path=row.source_path;
        row.target_key=row.source_key;
        row.target_presence=TargetPresence::Unchecked;
    }
    const std::vector<CueSelectedAudio> same{
        {"A","srcA","srcA"},{"B","srcB","srcB"}
    };
    const auto unchanged=preview_cue_bound_file_batch(
        noop,2,raw_cue,no_write_host,same);
    check(unchanged.eligible && unchanged.review.blocked==0 &&
          !unchanged.items[2].cue_references_will_change &&
          !unchanged.review.decisions[2].cue_bytes_write_needed &&
          unchanged.items[2].cue_source_fingerprint==
              unchanged.items[2].cue_postimage_fingerprint,
          "verified unchanged CUE and audio files imply exact zero CUE writes");
}
void test_physical_cue_selection_source_identity() {
    const std::vector<CueObservedFileSource> files{
        {0,"physical:A","guard:A",true},
        {1,"physical:B","guard:B",true},
        {2,"physical:unselected","guard:external",true}};
    const std::vector<CueSelectedPhysicalSource> selected{
        {5,"physical:A","guard:A",true},
        {9,"physical:B","guard:B",true}};
    const auto good=audit_cue_physical_selection(
        "physical:cue",files,selected);
    check(good.eligible && good.selected_matched==2 &&
          good.unselected_files==1 && good.selected_unreferenced==0 &&
          good.file_to_selected_row==std::vector<std::size_t>({
              5,9,cue_unselected_source_row}),
          "source-only native CUE FILEs match exact selected physical rows");
    auto altered=files;
    altered[1].source_guard="old-stale-guard";
    check(!audit_cue_physical_selection(
        "physical:cue",altered,selected).eligible,
        "same physical ID with changed source guard must not match");
    altered=files;altered[2].host_physical_key="physical:A";
    check(audit_cue_physical_selection(
        "physical:cue",altered,selected).rejection==
              "CUE_FILE_DUPLICATE_PHYSICAL_SOURCE",
          "CUE FILE alias to existing FILE cannot be counted twice");
    altered=files;altered[2].host_physical_key="physical:cue";
    check(audit_cue_physical_selection(
        "physical:cue",altered,selected).rejection=="CUE_FILE_ALIASES_CARRIER",
          "audio FILE may never point to CUE carrier itself");
    altered=files;altered[2].observed=false;
    check(!audit_cue_physical_selection(
        "physical:cue",altered,selected).eligible,
        "a missing CUE dependency blocks all-source mapping");
    altered=files;altered[1].file_index=8;
    check(!audit_cue_physical_selection(
        "physical:cue",altered,selected).eligible,
        "missing/out-of-order FILE ordinal blocks mapping");
    auto chosen=selected;chosen.push_back({12,"physical:outside","guard:x",true});
    check(audit_cue_physical_selection(
        "physical:cue",files,chosen).rejection==
              "CUE_SELECTED_AUDIO_NOT_REFERENCED",
          "selected but unreferenced audio cannot silently join CUE plan");
    chosen=selected;chosen.push_back({7,"physical:A","guard:alias",true});
    check(!audit_cue_physical_selection(
        "physical:cue",files,chosen).eligible,
        "duplicate selected physical source even at another row blocks");
    chosen=selected;chosen[0].qualified=false;
    check(!audit_cue_physical_selection(
        "physical:cue",files,chosen).eligible,
        "unqualified subsong or ambiguous selected audio blocks mapping");
    chosen=selected;chosen[1].source_row=5;
    check(!audit_cue_physical_selection(
        "physical:cue",files,chosen).eligible,
        "one selected row cannot masquerade as two physical sources");
    check(audit_cue_physical_selection(
        "physical:cue",files,{}).eligible,
        "CUE-only inspection may observe physical FILEs without selected audio");
    check(!audit_cue_physical_selection(
        "physical:cue",{},selected).eligible,
        "no FILE references never qualifies CUE");
    auto capped=files;
    for(std::size_t i=capped.size();i<129;++i)
        capped.push_back({i,"physical:other"+std::to_string(i),
                             "guard:other"+std::to_string(i),true});
    check(audit_cue_physical_selection(
        "physical:cue",capped,selected).rejection==
             "CUE_SOURCE_SELECTION_SHAPE_UNQUALIFIED",
          "129 FILE references must never appear fully source-qualified");
}

void test_unsupported_source_encoding() {
    const std::string bad = std::string("\xff\xfe",2) + "F\0I\0";
    check(!qualify_external_cue_association(bad, {}, {}).qualified, "UTF16 cannot be rewritten as UTF8");
}

void test_complete_selected_audio_and_alias_destinations() {
    const std::string original =
        "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\n"
        "FILE \"b.flac\" WAVE\nTRACK 02 AUDIO\n";
    const auto verified=qualify_external_cue_association(original,
        {{0,"a.flac","srcA","a-new.flac",true,true,"dstA"},
         {1,"b.flac","srcB","b-new.flac",true,true,"dstB"}},
        {{"A","srcA","dstA"},{"B","srcB","dstB"}});
    check(verified.qualified &&
          verified.related_selected_audio_ids==std::vector<std::string>({"A","B"}) &&
          verified.changes.size()==2,
          "two-source CUE reference plan preserves every selected physical ID");
    const auto orphan=qualify_external_cue_association(original,
        {{0,"a.flac","srcA","a-new.flac",true,true,"dstA"},
         {1,"b.flac","srcB","b-new.flac",true,true,"dstB"}},
        {{"A","srcA","dstA"},{"NOT_IN_CUE","some-other-physical-source","dstX"}});
    check(!orphan.qualified && !orphan.diagnostics.empty() &&
          orphan.diagnostics[0]=="SELECTED_AUDIO_NOT_REFERENCED_BY_CUE",
          "cannot silently skip a selected audio absent from CUE FILE list");
    const auto aliasConflict=qualify_external_cue_association(original,
        {{0,"a.flac","same-physical","a-new.flac",true,true,"dstA"},
         {1,"b.flac","same-physical","b-new.flac",true,true,"dstA"}},
        {{"A","same-physical","dstA"}});
    check(!aliasConflict.qualified && !aliasConflict.diagnostics.empty() &&
          aliasConflict.diagnostics[0]=="CUE_SAME_SOURCE_CONFLICTING_DESTINATION_REFERENCES",
          "one physical source cannot produce two conflicting FILE names");
    const auto aliasSame=qualify_external_cue_association(original,
        {{0,"a.flac","same-physical","same.flac",true,true,"dstA"},
         {1,"b.flac","same-physical","same.flac",true,true,"dstA"}},
        {{"A","same-physical","dstA"}});
    check(aliasSame.qualified && aliasSame.related_selected_audio_ids.size()==1,
          "same physical source with consistent destination can be qualified");
}

int main() {
    test_single(); test_multifile_unselected(); test_missing_and_ambiguous();
    test_complete_selected_audio_and_alias_destinations();
    test_exact_target_identity_and_collisions();
    test_exact_joint_fileplan_cue_binding();
    test_physical_cue_selection_source_identity();
    test_exact_nochange_and_unsafe_postimage(); test_unsupported_source_encoding();
    std::cout << "PASS: 9 cue association test suites\n";
}
