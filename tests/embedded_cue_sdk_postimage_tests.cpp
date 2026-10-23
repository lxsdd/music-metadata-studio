#include "djmeta/embedded_cue_sdk_postimage.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace djmeta;
using namespace djmeta::online;
static void check(bool ok,const char* s) {
 if(!ok){std::cerr<<"FAIL: "<<s<<"\n";std::exit(1);}
}
struct FakeSdk {
 MetadataDocument tags;
 unsigned format=44100;
 void copy(const FakeSdk& b){*this=b;}
 std::size_t meta_set(const char* name,const char* value) {
   for(std::size_t i=0;i<tags.fields.size();++i)
      if(metadata_field_equals_ci(tags.fields[i].name,name)) {
         tags.fields[i].values={value};return i;
      }
   tags.fields.push_back({name,{value}});
   return tags.fields.size()-1;
 }
};
int main(){
 const std::string original="TITLE \"Old Album\"\r\n"
   "FILE \"x.flac\" WAVE\r\nTRACK 01 AUDIO\r\n"
   "TITLE \"Original Track\"\r\nINDEX 01 00:00:00\r\n";
 FakeSdk sdk;
 sdk.tags={{{"TITLE",{"Physical Title"}},{"CUESHEET",{original}},
            {"GENRE",{"House","Trance"}},{"MY CUSTOM",{"keep"}},
            {"REPLAYGAIN_TRACK_GAIN",{"-1.5 dB"}}}};
 const auto read=[](const auto& info){return info.tags;};
 PhysicalTagSourceSnapshot physical;
 physical.qualified_physical=true;
 physical.host_physical_key="os:physical";
 physical.source_guard="stat:v1";
 physical.original=sdk.tags;
 const CuePostimageEvidence selected{std::nullopt,
   {"ALBUM",{"New Album"},"musicbrainz",
     "00000000-0000-0000-0000-000000000001",EvidenceScope::Edition},
     false,true};
 const auto snapshot=capture_cue_postimage_review(
     original,CueCarrierKind::EmbeddedText,{selected});
 const auto post=preview_selected_cue_review_postimage(
     snapshot,original,{0});
 check(post.eligible && post.changed,"qualified embedded CUE edit");
 FakeSdk output;output.format=12;
 auto stage=[&](const auto& input,const auto& snap,const auto& proposal,
                 const auto& pick,FakeSdk& dst) {
    return project_embedded_cue_into_physical_sdk(
        input,dst,physical,physical.host_physical_key,
        physical.source_guard,snap,pick,proposal,read);
 };
 auto result=stage(sdk,snapshot,post,std::vector<std::size_t>{0},output);
 check(result.status==EmbeddedCueSdkStatus::Detached &&
       output.format==44100 &&
       output.tags.fields[1].values[0]==post.bytes &&
       output.tags.fields[0]==sdk.tags.fields[0] &&
       output.tags.fields[2]==sdk.tags.fields[2] &&
       output.tags.fields[3]==sdk.tags.fields[3] &&
       output.tags.fields[4]==sdk.tags.fields[4] &&
       sdk.tags.fields[1].values[0]==original,
       "only embedded CUE text edited, ReplayGain/custom/technical kept");
 auto noop=preview_selected_cue_review_postimage(snapshot,original,{});
 FakeSdk untouched;untouched.format=13;
 check(stage(sdk,snapshot,noop,std::vector<std::size_t>{},untouched).status==
       EmbeddedCueSdkStatus::ExactNoop && untouched.format==13,
       "embedded no-op avoids all output SDK changes");
 auto stale=sdk;stale.tags.fields[2].values[1]="externally changed";
 check(stage(stale,snapshot,post,std::vector<std::size_t>{0},untouched).status==
       EmbeddedCueSdkStatus::Blocked && untouched.format==13,
       "physical custom multivalue drift blocks operation");
 stale=sdk;stale.tags.fields[1].values[0]+=" ";
 check(stage(stale,snapshot,post,std::vector<std::size_t>{0},untouched).status==
       EmbeddedCueSdkStatus::Blocked,"changed source CUE bytes refused");
 auto virtual_snapshot=physical;virtual_snapshot.virtual_subsong=true;
 check(project_embedded_cue_into_physical_sdk(
       sdk,untouched,virtual_snapshot,physical.host_physical_key,
       physical.source_guard,snapshot,{0},post,read).status==
       EmbeddedCueSdkStatus::Blocked,"virtual subsong cannot authorize physical SDK projection");
 auto forged=post;forged.bytes+=" ";
 check(stage(sdk,snapshot,forged,std::vector<std::size_t>{0},untouched).status==
       EmbeddedCueSdkStatus::Blocked,
       "modified approved CUE bytes rejected");
 auto wrong=snapshot;wrong.carrier=CueCarrierKind::ExternalText;
 check(stage(sdk,wrong,post,std::vector<std::size_t>{0},untouched).status==
       EmbeddedCueSdkStatus::Blocked,
       "external FILE content cannot masquerade as embedded tag");
 stale=sdk;stale.tags.fields.push_back({"CUESHEET",{original}});
 check(stage(stale,snapshot,post,std::vector<std::size_t>{0},untouched).status==
       EmbeddedCueSdkStatus::Blocked,"ambiguous duplicate embedded CUE refused");
 std::cout<<"PASS: embedded CUE SDK projection touches exactly one physical field\n";
}
