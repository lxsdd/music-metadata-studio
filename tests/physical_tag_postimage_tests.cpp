#include "djmeta/physical_tag_postimage.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace djmeta;
static void check(bool test,const char* reason){
    if(!test){std::cerr<<"FAIL: "<<reason<<"\n";std::exit(1);}
}
int main(){
    MetadataDocument original{{{"TITLE",{"Old Title"}},
        {"GENRE",{"House","Progressive House"}},
        {"CUSTOM DJ NOTES",{"Original DJ note"}},
        {"CUESHEET",{"FILE \"long mix.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00"}},
        {"REPLAYGAIN_TRACK_GAIN",{"-3.00 dB"}}}};
    PhysicalTagSourceSnapshot snap{true,false,"volume:physical-id","source:v1",original};
    const auto ok=preview_explicit_physical_tag_values(snap,original,
        "volume:physical-id","source:v1",{
            {"title",{"Old Title"},{"New Title"}},
            {"GENRE",{"House","Progressive House"},{"House","Melodic House"}},
            {"DATE",{},{"2026-10-22"}}
        });
    check(ok.eligible && ok.changed && ok.explicitly_changed==3 &&
          ok.planned.fields.size()==original.fields.size()+1,
          "three user-selected changes are planned, without implicit deletes");
    check(ok.planned.fields[1].values.size()==2 &&
          ok.planned.fields[1].values[0]=="House" &&
          ok.planned.fields[1].values[1]=="Melodic House" &&
          ok.planned.fields[2]==original.fields[2] &&
          ok.planned.fields[3]==original.fields[3] &&
          ok.planned.fields[4]==original.fields[4],
          "custom, replaygain, entire CUE and ordered multivalues untouched");
    const auto none=preview_explicit_physical_tag_values(snap,original,
        "volume:physical-id","source:v1",{});
    check(none.eligible && !none.changed && none.planned==original,
          "no selected changes means no writer/timestamp modification");
    const auto noop=preview_explicit_physical_tag_values(snap,original,
        "volume:physical-id","source:v1",{
            {"TITLE",{"Old Title"},{"Old Title"}}
        });
    check(noop.eligible && !noop.changed && noop.exact_noops==1 &&
          noop.planned==original,"identical values are an exact no-op");
    auto reordered=original;
    std::swap(reordered.fields[0],reordered.fields[1]);
    reordered.fields[0].name="genre";
    check(preview_explicit_physical_tag_values(snap,reordered,
        "volume:physical-id","source:v1",{
            {"TITLE",{"Old Title"},{"Retitled"}}
        }).eligible,"field enumeration order/key casing are immaterial");
    const auto fail=[&](const PhysicalTagSourceSnapshot& snapshot,
                        const MetadataDocument& tags,
                        const std::string& id,const std::string& version,
                        const std::vector<PhysicalTagValueApproval>& approvals){
        const auto result=preview_explicit_physical_tag_values(
            snapshot,tags,id,version,approvals);
        return !result.eligible && !result.changed && !result.rejection.empty();
    };
    check(fail(snap,original,"volume:replacement","source:v1",{}),
          "replacement physical identity blocked");
    check(fail(snap,original,"volume:physical-id","source:v2",{}),
          "stale physical source version blocked");
    auto virtual_source=snap;
    virtual_source.virtual_subsong=true;
    check(fail(virtual_source,original,"volume:physical-id","source:v1",{}),
          "virtual CUE subsongs cannot impersonate physical source");
    auto stale=original;
    stale.fields[2].values={"Updated externally"};
    check(fail(snap,stale,"volume:physical-id","source:v1",{}),
          "modified original custom tag invalidates all approvals");
    check(fail(snap,original,"volume:physical-id","source:v1",{
        {"TITLE",{"not the original"},{"Altered"}}
    }),"stale selected tag refuses modification");
    check(fail(snap,original,"volume:physical-id","source:v1",{
        {"TITLE",{"Old Title"},{"A"}},
        {"title",{"Old Title"},{"B"}}
    }),"case-insensitive repeated field approvals rejected");
    check(fail(snap,original,"volume:physical-id","source:v1",{
        {"CUESHEET",original.fields[3].values,{"garbage"}}
    }),"embedded CUE cannot be passed as a normal physical tag change");
    check(fail(snap,original,"volume:physical-id","source:v1",{
        {"replaygain_track_gain",original.fields[4].values,{"-4.0 dB"}}
    }),"replaygain requires dedicated host guard");
    check(fail(snap,original,"volume:physical-id","source:v1",{
        {"CUSTOM DJ NOTES",{"Original DJ note"},{}}
    }),"field deletion requires separate qualification");
    check(fail(snap,original,"volume:physical-id","source:v1",{
        {"CUSTOM DJ NOTES",{"Original DJ note"},{std::string("\xff")}}
    }),"malformed UTF-8 replacement blocked");
    check(fail(snap,original,"volume:physical-id","source:v1",{
        {"DATE",{"Wrong"},{ "2026" }}
    }),"source-absent field cannot be forged as existing");
    auto ambiguous=original;
    ambiguous.fields.push_back({"title",{"Other"}});
    check(fail(snap,ambiguous,"volume:physical-id","source:v1",{}),
          "duplicate case-insensitive original blocked");
    check(!preview_explicit_physical_tag_values(snap,original,
          "volume:physical-id","source:v1",{
          {"GENRE",{"House","Progressive House"},{"House","Melodic House"}}
    }).planned.fields.empty(),
          "planner emits a complete detached document");
    std::cout<<"PASS: selected physical multivalue/tag postimage and protected CUE guards\n";
}
