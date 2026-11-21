#include "djmeta/online_physical_postimage.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace djmeta;
using namespace djmeta::online;
static void check(bool ok,const char* reason){
    if(!ok){std::cerr<<"FAIL: "<<reason<<"\n";std::exit(1);}
}
int main(){
    const MetadataDocument original{{{"TITLE",{"Old"}},
        {"ALBUM ARTIST",{"Original Artist"}},
        {"GENRE",{"House","Progressive House"}},
        {"CUSTOM DJ DATA",{"preserve"}},
        {"CUESHEET",{"FILE mix.flac WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00"}}}};
    PhysicalTagSourceSnapshot source{true,false,"physical:one","guard:v1",original};
    const std::vector<FieldEvidence> evidence{
        {"TITLE",{"New"},"discogs","release:1"},
        {"ALBUM ARTIST",{"New Artist"},"discogs","release:1"},
        {"GENRE",{"House","Deep House"},"discogs","release:1"},
        {"YEAR",{"2026"},"discogs","release:1"},
        {"CUESHEET",{"forged"},"discogs","release:1"}
    };
    const auto captured=capture_online_field_review(original,evidence,true,true);
    const auto projected=stage_selected_online_fields(captured,original,{0,1,2,3});
    const auto valid=verify_online_physical_tag_postimage(
        source,original,"physical:one","guard:v1",captured,{0,1,2,3},{},projected);
    check(valid.eligible && valid.changed && valid.explicitly_changed==4 &&
          valid.planned.fields.size()==6,"four explicit changes retain unselected source fields");
    check(valid.planned.fields[1].values==std::vector<std::string>{"New Artist"} &&
          valid.planned.fields[2].values==std::vector<std::string>({"House","Deep House"}) &&
          valid.planned.fields[3]==original.fields[3] &&
          valid.planned.fields[4]==original.fields[4],
          "ALBUM ARTIST, exact ordered genre values, custom tag and CUE are preserved");
    const auto empty=stage_selected_online_fields(captured,original,{});
    const auto no_op=verify_online_physical_tag_postimage(
        source,original,"physical:one","guard:v1",captured,{},{},empty);
    check(no_op.eligible && !no_op.changed && no_op.planned==original,
          "empty user selection is exact no-op");
    const auto malformed=[&](const OnlineFieldStagedPreview& candidate,
                              std::string identity="physical:one",
                              std::string guard="guard:v1"){
        const auto output=verify_online_physical_tag_postimage(
            source,original,identity,guard,captured,{0,1,2,3},{},candidate);
        return !output.eligible && !output.changed && !output.rejection.empty();
    };
    check(malformed(projected,"physical:replaced"),"other physical identity rejected");
    check(malformed(projected,"physical:one","guard:stale"),"stale source rejected");
    auto forged=projected;
    forged.planned.fields.erase(forged.planned.fields.begin()+3);
    check(malformed(forged),"provider absence cannot delete unselected custom field");
    forged=projected;
    forged.planned.fields[4].values={"forged CUE"};
    check(malformed(forged),"CUE cannot change through normal physical staging");
    forged=projected;
    forged.explicitly_changed=1;
    check(malformed(forged),"miscounted approvals refused");
    forged=projected;
    forged.planned.fields.push_back({"title",{"duplicated"}});
    check(malformed(forged),"ambiguous physical key casing refused");
    forged=projected;
    forged.planned.fields[3].values={"mutated without approval"};
    check(malformed(forged),"unselected custom mutation cannot sneak past staged field count");
    // An attacker can preserve the four-difference count by replacing
    // an approved TITLE edit with an unselected CUSTOM field edit.
    forged=projected;
    forged.planned.fields[0].values={"Old"};
    forged.planned.fields[3].values={"Forged custom data"};
    check(malformed(forged),
          "same changed-count cannot substitute an unselected custom field");
    forged=projected;
    forged.planned.fields[2].values={"House","Forged provider value"};
    check(malformed(forged),
          "selected field value must match authoritative provider evidence");
    check(!verify_online_physical_tag_postimage(
          source,original,"physical:one","guard:v1",
          captured,{0,1,3},{},projected).eligible,
          "postimage cannot be reused after selecting different provider rows");
    check(!verify_online_physical_tag_postimage(
          source,original,"physical:one","guard:v1",
          captured,{0,1,1,3},{},projected).eligible,
          "duplicate selected evidence IDs fail closed");
    auto tampered_capture=captured;
    tampered_capture.evidence[0].values={"Unverified"};
    check(!verify_online_physical_tag_postimage(
          source,original,"physical:one","guard:v1",
          tampered_capture,{0,1,2,3},{},projected).eligible,
          "changed provider evidence invalidates a previously accepted postimage");
    auto forged_noop=empty;
    forged_noop.exact_noops=1;
    check(!verify_online_physical_tag_postimage(
          source,original,"physical:one","guard:v1",
          captured,{},{},forged_noop).eligible,
          "fabricated no-op count does not authenticate selections");
    auto virtual_source=source;
    virtual_source.virtual_subsong=true;
    check(!verify_online_physical_tag_postimage(virtual_source,original,
          "physical:one","guard:v1",captured,{0,1,2,3},{},projected).eligible,
          "virtual subsongs cannot qualify physical tag postimage");
    bool cuesheet_blocked=false;
    try {
        (void)stage_selected_online_fields(captured,original,{4});
    } catch(const std::invalid_argument&) {
        cuesheet_blocked=true;
    }
    check(cuesheet_blocked,
          "protected CUESHEET cannot pass the selected online field gate");
    std::cout<<"PASS: UI-selected online tag preview cross-check and source safety\n";
}
