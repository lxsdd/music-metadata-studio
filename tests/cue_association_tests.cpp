#include "djmeta/cue_association.h"

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
    test_exact_nochange_and_unsafe_postimage(); test_unsupported_source_encoding();
    std::cout << "PASS: 7 cue association test suites\n";
}
