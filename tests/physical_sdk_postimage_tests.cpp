#include "djmeta/physical_sdk_postimage.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using namespace djmeta;
static void check(bool yes,const char* label) {
 if(!yes){std::cerr<<"FAIL: "<<label<<"\n";std::exit(1);}
}
struct FakeSdkInfo {
 MetadataDocument tags;
 int sample_rate=44100;
 bool artwork=true;
 bool lossy_multivalue=false;
 void copy(const FakeSdkInfo& x) {*this=x;}
 std::size_t meta_set(const char* name,const char* value) {
    const auto folded=physical_fold_name(name);
    for(std::size_t i=0;i<tags.fields.size();++i)
        if(physical_fold_name(tags.fields[i].name)==folded) {
            tags.fields[i].values={value};
            return i;
        }
    tags.fields.push_back({name,{value}});
    return tags.fields.size()-1;
 }
 void meta_add_value(std::size_t i,const char* value) {
    if(!lossy_multivalue)tags.fields[i].values.emplace_back(value);
 }
};
int main() {
 FakeSdkInfo src;
 src.tags={{{"TITLE",{"Old"}},{"GENRE",{"Trance","Progressive"}},
            {"MY CUSTOM",{"exact","unknown"}},{"CUESHEET",{"FILE x.flac WAVE\r\n"}},
            {"REPLAYGAIN_TRACK_GAIN",{"-1.50 dB"}}}};
 const auto read=[](const auto& sdk){return sdk.tags;};
 PhysicalTagSourceSnapshot captured;
 captured.qualified_physical=true;
 captured.host_physical_key="volume:123";
 captured.source_guard="stat:456";
 captured.original=src.tags;
 std::vector<PhysicalTagValueApproval> approvals{
    {"TITLE",{"Old"},{"Updated"}},
    {"GENRE",{"Trance","Progressive"},{"House","Progressive","Techno"}}
 };
 auto qualified=preview_explicit_physical_tag_values(
    captured,src.tags,captured.host_physical_key,captured.source_guard,approvals);
 check(qualified.eligible && qualified.changed,"explicit postimage qualified");
 FakeSdkInfo output;
 output.sample_rate=1;output.artwork=false;
 auto result=project_explicit_physical_sdk_postimage(
    src,output,captured,captured.host_physical_key,captured.source_guard,
    approvals,qualified,read);
 check(result.status==PhysicalSdkPostimageStatus::Detached &&
       compare_physical_text_tags(output.tags,qualified.planned)==
          TagWriteDecision::Unchanged &&
       output.sample_rate==44100 && output.artwork &&
       src.tags.fields[0].values[0]=="Old" &&
       output.tags.fields[1].values.size()==3 &&
       output.tags.fields[2]==src.tags.fields[2] &&
       output.tags.fields[3]==src.tags.fields[3] &&
       output.tags.fields[4]==src.tags.fields[4],
       "SDK detached copy only edits explicit fields, preserves format/artwork/CUE/RG");
 FakeSdkInfo untouched;untouched.sample_rate=123;
 auto stale=src;stale.tags.fields[2].values[1]="concurrent";
 check(project_explicit_physical_sdk_postimage(
    stale,untouched,captured,captured.host_physical_key,captured.source_guard,
    approvals,qualified,read).status==PhysicalSdkPostimageStatus::Blocked &&
    untouched.sample_rate==123 && untouched.tags.fields.empty(),
    "stale custom-field source rejects without mutating output");
 check(project_explicit_physical_sdk_postimage(
    src,untouched,captured,"replacement-identity",captured.source_guard,
    approvals,qualified,read).status==PhysicalSdkPostimageStatus::Blocked,
    "physical host identity changed");
 auto forged=qualified;forged.planned.fields[2].values={"discarded"};
 check(project_explicit_physical_sdk_postimage(
    src,untouched,captured,captured.host_physical_key,captured.source_guard,
    approvals,forged,read).status==PhysicalSdkPostimageStatus::Blocked,
    "extra unapproved custom mutation rejected");
 auto invalid=approvals;invalid.push_back(
    {"CUESHEET",{"FILE x.flac WAVE\r\n"},{"FILE y.flac WAVE\r\n"}});
 check(project_explicit_physical_sdk_postimage(
    src,untouched,captured,captured.host_physical_key,captured.source_guard,
    invalid,qualified,read).status==PhysicalSdkPostimageStatus::Blocked,
    "embedded CUE cannot piggyback");
 FakeSdkInfo lossy=src;lossy.lossy_multivalue=true;
 check(project_explicit_physical_sdk_postimage(
    lossy,untouched,captured,captured.host_physical_key,captured.source_guard,
    approvals,qualified,read).status==PhysicalSdkPostimageStatus::Blocked,
    "SDK implementation truncating multivalues is detected before publish");
 auto same=preview_explicit_physical_tag_values(
    captured,src.tags,captured.host_physical_key,captured.source_guard,{});
 check(same.eligible && !same.changed,"no-op valid");
 check(project_explicit_physical_sdk_postimage(
    src,untouched,captured,captured.host_physical_key,captured.source_guard,
    {},same,read).status==PhysicalSdkPostimageStatus::ExactNoop &&
    untouched.sample_rate==123,
    "zero edits never copies or writes SDK object");
 const auto derived=physical_sdk_changed_field_approvals(src.tags,qualified.planned);
 check(derived.size()==2 &&
       project_explicit_physical_sdk_postimage(
           src,untouched,captured,captured.host_physical_key,captured.source_guard,
           derived,qualified,read).status==PhysicalSdkPostimageStatus::Detached,
       "native verified postimage diff reconstructs explicit selections");
 std::cout<<"PASS: SDK detached physical postimage preserves all unselected metadata\n";
}
