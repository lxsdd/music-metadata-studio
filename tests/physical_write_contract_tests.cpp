#include "djmeta/physical_write_contract.h"
#include <cstdlib>
#include <iostream>
using namespace djmeta;
using namespace djmeta::online;
static void check(bool ok,const char* label) {
    if(!ok){std::cerr<<"FAIL: "<<label<<"\n";std::exit(1);}
}
int main(){
    MetadataDocument original{{{"TITLE",{"Old"}},{"GENRE",{"House","Trance"}},
                               {"CUSTOM",{"Keep"}},{"CUESHEET",{"exact bytes"}}}};
    PhysicalTagSourceSnapshot physical{true,false,"id:1","stamp:1",original};
    FieldEvidence edit{"TITLE",{"New"},"discogs","123",EvidenceScope::Edition};
    const auto evidence=capture_online_field_review(original,{edit},true,true);
    const auto proposed=stage_selected_online_fields(evidence,original,{0});
    FrozenPhysicalTagWrite frozen{"file:///C:/qa/source.mp3",physical,evidence,
                                  proposed,{0},{}};
    const auto validate=[&](const MetadataDocument& live,
                            const std::string& id="id:1",
                            const std::string& stamp="stamp:1") {
        return verify_frozen_physical_write(
            frozen,live,frozen.source_path,id,stamp).state;
    };
    check(validate(original)==PhysicalWriteGateState::Changed,
          "captured selected provider evidence qualifies changed values");
    check(validate(original,"replaced") == PhysicalWriteGateState::Blocked,
          "source identity changed");
    check(validate(original,"id:1","stale") == PhysicalWriteGateState::Blocked,
          "file stat changed");
    auto mutated=original;mutated.fields[1].values[1]="other";
    check(validate(mutated)==PhysicalWriteGateState::Blocked,
          "unselected multivalue changed");
    const auto planned=frozen.reviewed;
    frozen.reviewed.planned.fields[2].values={"tampered"};
    check(validate(original)==PhysicalWriteGateState::Blocked,
          "forged custom metadata difference");
    frozen.reviewed=planned;
    frozen.selected={0,0};
    check(validate(original)==PhysicalWriteGateState::Blocked,
          "duplicate selected provider row");
    frozen.selected.clear();
    check(validate(original)==PhysicalWriteGateState::Blocked,
          "no selection");
    frozen.selected={0};
    frozen.physical.virtual_subsong=true;
    check(validate(original)==PhysicalWriteGateState::Blocked,
          "virtual subsong denied");
    frozen.physical.virtual_subsong=false;
    frozen.source_path.clear();
    check(validate(original)==PhysicalWriteGateState::Blocked,
          "missing source path denied");
    // A second explicit request after foobar has refreshed its physical tags
    // must be recognized as an EXACT NO-OP, not write the file again.
    auto already_applied=original;
    already_applied.fields[0].values={"New"};
    const auto new_source=djmeta::PhysicalTagSourceSnapshot{
        true,false,"id:1","stamp:2",already_applied};
    const auto same_review=capture_online_field_review(
        already_applied,{edit},true,true);
    const auto same_staging=stage_selected_online_fields(
        same_review,already_applied,{0});
    const FrozenPhysicalTagWrite refreshed{
        "file:///C:/qa/source.mp3",new_source,same_review,same_staging,{0},{}};
    check(verify_frozen_physical_write(
              refreshed,already_applied,refreshed.source_path,
              "id:1","stamp:2").state==PhysicalWriteGateState::Noop,
          "equal provider field is a true SDK-free second-pass no-op");
    auto earlier_approval=frozen;
    earlier_approval.source_path="file:///C:/qa/source.mp3";
    check(verify_frozen_physical_write(
              earlier_approval,already_applied,earlier_approval.source_path,
              "id:1","stamp:1").state==PhysicalWriteGateState::Blocked,
          "old approval cannot be reused after a different physical postimage");
    auto changed_cue=original;
    changed_cue.fields[3].values={"different CUE bytes"};
    const FrozenPhysicalTagWrite source_old{
        "file:///C:/qa/source.mp3",physical,evidence,proposed,{0},{}};
    check(verify_frozen_physical_write(
              source_old,changed_cue,source_old.source_path,
              "id:1","stamp:1").state==PhysicalWriteGateState::Blocked,
          "protected embedded CUE drift blocks even TITLE-only write");
    std::cout<<"PASS: frozen native write contract rejects stale/provider/virtual inputs\n";
}
