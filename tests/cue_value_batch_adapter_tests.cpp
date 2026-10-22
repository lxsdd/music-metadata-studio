#include "djmeta/cue_value_batch_adapter.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using namespace djmeta;
using namespace djmeta::online;
static void check(bool ok,const char* why){
    if(!ok){std::cerr<<"FAIL: "<<why<<"\n";std::exit(1);}
}
static ReleaseEdition edition(){
    ReleaseEdition e;
    e.provider="musicbrainz";
    e.edition_id="00000000-0000-0000-0000-000000000001";
    e.fields.push_back({"ALBUM",{"Other album"},"musicbrainz",
                         e.edition_id,EvidenceScope::Edition});
    EditionTrack t;
    t.source_track_id="00000000-0000-0000-0000-000000000002";
    t.recording={"Sunrise (Extended Mix)","DJ","",
                  MixKind::Extended,"GBABC2600001",-1};
    t.track_number=1;t.disc_number=1;
    t.fields.push_back({"TITLE",{"Corrected Sunrise (Extended Mix)"},
                        "musicbrainz",t.source_track_id,EvidenceScope::Edition});
    e.tracks.push_back(t);
    return e;
}
static FilePlanItem carrier(){
    FilePlanItem item;
    item.physical_id="external-file-id:cue";
    item.source_path="Z:/Music/set.cue";
    item.source_key="z:/music/set.cue";
    item.source_guard="host-snapshot-v1";
    item.role=FileRole::ExternalCue;
    item.cue_links=CueLinkState::Verified;
    item.associated_audio_ids={"audio:one"};
    item.ruleset_revision="2026-10-22.1";
    return item;
}
int main(){
    const std::string raw=
        "TITLE \"Local album\"\n"
        "PERFORMER \"DJ\"\n"
        "FILE \"set.flac\" WAVE\n"
        "TRACK 01 AUDIO\n"
        "TITLE \"Sunrise (Extended Mix)\"\n"
        "PERFORMER \"DJ\"\n"
        "ISRC GBABC2600001\n"
        "INDEX 01 00:00:00\n";
    const auto mb=edition();
    auto base=carrier();
    const auto projected=stage_exact_external_cue_values_for_batch(
        base,raw,mb,{0,1});
    check(projected.eligible && projected.cue.changed &&
          projected.planned.cue_values_will_change &&
          !projected.planned.cue_references_will_change &&
          projected.cue.bytes.find("TITLE \"Other album\"")!=std::string::npos &&
          projected.cue.bytes.find("TITLE \"Corrected Sunrise (Extended Mix)\"")
            !=std::string::npos &&
          projected.cue.bytes.find("FILE \"set.flac\" WAVE")!=std::string::npos &&
          projected.planned.cue_source_fingerprint.size()==64 &&
          projected.planned.cue_postimage_fingerprint.size()==64,
          "exact MB edition maps selected CUE track + global values, no FILE rewrite");
    check(cue_carrier_content_fingerprint(raw)!=
          cue_carrier_content_fingerprint(projected.cue.bytes),
          "source and postimage are distinct SHA-256-covered byte streams");
    auto physical=FilePlanItem{};
    physical.physical_id="audio:one";
    physical.source_path="Z:/Music/set.flac";
    physical.source_key="z:/music/set.flac";
    physical.source_guard="audio-os-stat";
    physical.role=FileRole::Audio;
    physical.action=FileAction::None;
    physical.cue_links=CueLinkState::Verified;
    physical.ruleset_revision=projected.planned.ruleset_revision;
    physical.metadata_fingerprint="original tags sha";
    physical.planned_metadata_fingerprint=physical.metadata_fingerprint;
    const auto plan=review_batch_plan({physical,projected.planned});
    check(plan.ready_to_apply && plan.decisions.size()==2 &&
          plan.decisions[1].cue_bytes_write_needed &&
          !plan.decisions[1].file_operation_needed,
          "qualified CUE value bridge reaches the actual batch planner");

    const auto empty=stage_exact_external_cue_values_for_batch(base,raw,mb,{});
    check(empty.eligible && !empty.cue.changed &&
          !empty.planned.cue_values_will_change &&
          empty.planned.cue_source_fingerprint==
             empty.planned.cue_postimage_fingerprint,
          "no chosen edition fields implies byte-identical no-op");
    check(!stage_exact_external_cue_values_for_batch(
          projected.planned,raw,mb,{0,1}).eligible,
          "already selected staged carrier cannot be silently re-used");
    auto explicitly_reset=projected.planned;
    explicitly_reset.cue_values_will_change=false;
    const auto reproduced=stage_exact_external_cue_values_for_batch(
        explicitly_reset,raw,mb,{0,1});
    check(reproduced.eligible &&
          reproduced.planned.cue_postimage_fingerprint==
              projected.planned.cue_postimage_fingerprint,
          "explicit review reset can reproduce only the same qualified byte postimage");
    auto stale=base;
    stale.cue_source_fingerprint="stale data";
    check(!stage_exact_external_cue_values_for_batch(stale,raw,mb,{0,1}).eligible,
          "captured source bytes mismatch blocks new proposal");
    stale=base;stale.source_guard.clear();
    check(!stage_exact_external_cue_values_for_batch(stale,raw,mb,{0,1}).eligible,
          "missing host carrier source guard fails closed");
    stale=base;stale.role=FileRole::Audio;
    check(!stage_exact_external_cue_values_for_batch(stale,raw,mb,{0,1}).eligible,
          "virtual or embedded audio tag carrier cannot impersonate external CUE");
    stale=base;stale.cue_references_will_change=true;
    check(!stage_exact_external_cue_values_for_batch(stale,raw,mb,{0,1}).eligible,
          "FILE reference move has its own independent writer policy");
    stale=base;stale.cue_postimage_fingerprint="old selected value";
    check(!stage_exact_external_cue_values_for_batch(stale,raw,mb,{0,1}).eligible,
          "changed selection invalidates previously reviewed cue postimage");
    auto wrong=mb;
    wrong.tracks[0].recording.title="Sunrise (Radio Edit)";
    check(!stage_exact_external_cue_values_for_batch(base,raw,wrong,{0,1}).eligible,
          "wrong mix may not stage qualified CUE values");
    wrong=mb;
    wrong.tracks[0].fields[0].source_id="foreign-release-occurrence";
    check(!stage_exact_external_cue_values_for_batch(base,raw,wrong,{0,1}).eligible,
          "forged MB track occurrence refused");
    wrong=mb;wrong.tracks.push_back(wrong.tracks[0]);
    wrong.tracks[1].source_track_id="00000000-0000-0000-0000-000000000003";
    wrong.tracks[1].fields[0].source_id=wrong.tracks[1].source_track_id;
    const auto partial=stage_exact_external_cue_values_for_batch(base,raw,wrong,{0,1});
    check(!partial.eligible,
          "partial edition cannot stage global and track changes together");
    auto source_bound=base;
    source_bound.cue_source_fingerprint=projected.planned.cue_source_fingerprint;
    check(!stage_exact_external_cue_values_for_batch(source_bound,
            raw+" ",mb,{0,1}).eligible,
          "byte drift after capturing full release snapshot is refused");
    std::cout<<"PASS: edition-provenance CUE values bridge into exact byte batch plan\n";
}
