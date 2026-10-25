#include "djmeta/review_physical_postimage.h"
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
    for(const auto* reserved : {"REPLAYGAIN_REFERENCE_LOUDNESS",
                                "replaygain_track_range",
                                "METADATA_BLOCK_PICTURE",
                                "coverart","COVERARTMIME"}) {
        check(physical_tag_immutable(reserved),
              "specialized audio/tag carriers are not generic physical metadata");
        check(fail(snap,original,"volume:physical-id","source:v1",{
            {reserved,{},{"forged rewrite"}}
        }),"ReplayGain and artwork cannot enter physical field insertions");
    }
    auto rich=original;
    rich.fields.push_back({"REPLAYGAIN_REFERENCE_LOUDNESS",{"89 dB"}});
    rich.fields.push_back({"METADATA_BLOCK_PICTURE",{"binary payload left opaque"}});
    rich.fields.push_back({"COVERART",{"opaque base64 art"}}); 
    auto rich_source=snap;
    rich_source.original=rich;
    const auto rich_preview=preview_explicit_physical_tag_values(
        rich_source,rich,"volume:physical-id","source:v1",{
            {"TITLE",{"Old Title"},{"Retitled"}}
        });
    check(rich_preview.eligible && rich_preview.changed &&
          rich_preview.planned.fields.size()==rich.fields.size() &&
          rich_preview.planned.fields[5]==rich.fields[5] &&
          rich_preview.planned.fields[6]==rich.fields[6] &&
          rich_preview.planned.fields[7]==rich.fields[7],
          "unrelated normalizer title edit preserves all ReplayGain/art payloads");

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

    // Normalizer decisions and physical SDK plans are deliberately separate.
    // Pending SAFE is visible in preview but NEVER implicit write consent.
    AnalysisResult analysis;
    analysis.input_fingerprint=fingerprint(original);
    analysis.ruleset_revision="rules:v1";
    const auto proposed=[&](std::size_t field,std::size_t value,
                            const std::string& replacement,SafetyClass safety){
        Proposal p;
        p.field_index=field;
        p.field=original.fields[field].name;
        p.value_index=value;
        p.original_value=original.fields[field].values[value];
        p.proposed_value=replacement;
        p.safety=safety;
        p.rule_ids={"test.rule"};
        return p;
    };
    analysis.proposals={
        proposed(0,0,"Old Title",SafetyClass::Safe),
        proposed(1,1,"Melodic House",SafetyClass::Review),
        proposed(2,0,"Suggested note",SafetyClass::Confident)
    };
    const std::vector<ReviewDecision> pending(analysis.proposals.size());
    const auto pending_tags=preview_explicit_normalizer_physical_tags(
        snap,original,"volume:physical-id","source:v1",
        analysis,"rules:v1",pending);
    check(pending_tags.eligible && !pending_tags.changed &&
          pending_tags.planned==original && pending_tags.explicitly_changed==0,
          "pending SAFE and semantic normalizer proposals do not permit a tag write");
    auto explicit_decisions=pending;
    explicit_decisions[1].action=ReviewAction::Accept;
    explicit_decisions[2]={ReviewAction::ManualValue,"Reviewed DJ note"};
    const auto staged_normalizer=preview_explicit_normalizer_physical_tags(
        snap,original,"volume:physical-id","source:v1",
        analysis,"rules:v1",explicit_decisions);
    check(staged_normalizer.eligible && staged_normalizer.changed &&
          staged_normalizer.explicitly_changed==2 &&
          staged_normalizer.planned.fields[0]==original.fields[0] &&
          staged_normalizer.planned.fields[1].values==
              std::vector<std::string>({"House","Melodic House"}) &&
          staged_normalizer.planned.fields[2].values==
              std::vector<std::string>{"Reviewed DJ note"} &&
          staged_normalizer.planned.fields[3]==original.fields[3] &&
          staged_normalizer.planned.fields[4]==original.fields[4],
          "explicit semantic Accept and manual override create exact physical postimage");
    explicit_decisions[0].action=ReviewAction::Accept;
    const auto with_equal_safe_accept=preview_explicit_normalizer_physical_tags(
        snap,original,"volume:physical-id","source:v1",
        analysis,"rules:v1",explicit_decisions);
    check(with_equal_safe_accept.eligible && with_equal_safe_accept.changed &&
          with_equal_safe_accept.explicitly_changed==2 &&
          with_equal_safe_accept.planned.fields[0]==original.fields[0],
          "equal SAFE acceptance does not create a phantom physical tag write");
    auto whitespace_source=original;
    whitespace_source.fields[0].values={" Old Title "};
    auto whitespace_snap=snap;
    whitespace_snap.original=whitespace_source;
    auto whitespace_analysis=analysis;
    whitespace_analysis.input_fingerprint=fingerprint(whitespace_source);
    whitespace_analysis.proposals[0].original_value=" Old Title ";
    const auto unapproved_safe=preview_explicit_normalizer_physical_tags(
        whitespace_snap,whitespace_source,
        "volume:physical-id","source:v1",
        whitespace_analysis,"rules:v1",pending);
    check(unapproved_safe.eligible && !unapproved_safe.changed &&
          unapproved_safe.planned==whitespace_source,
          "real whitespace SAFE still does not become a physical write without Accept");
    const auto explicitly_safe=preview_explicit_normalizer_physical_tags(
        whitespace_snap,whitespace_source,
        "volume:physical-id","source:v1",
        whitespace_analysis,"rules:v1",
        {ReviewDecision{ReviewAction::Accept,""},
         ReviewDecision{},ReviewDecision{}});
    check(explicitly_safe.eligible && explicitly_safe.changed &&
          explicitly_safe.explicitly_changed==1 &&
          explicitly_safe.planned.fields[0].values==
              std::vector<std::string>{"Old Title"} &&
          explicitly_safe.planned.fields[1]==whitespace_source.fields[1],
          "explicit SAFE Accept alone stages the exact changed TITLE");
    const auto normalizer_rejected=[&](
        const AnalysisResult& captured,
        const std::vector<ReviewDecision>& selection,
        const std::string& key="volume:physical-id",
        const std::string& guard="source:v1",
        const std::string& rules="rules:v1"){
        const auto result=preview_explicit_normalizer_physical_tags(
            snap,original,key,guard,captured,rules,selection);
        return !result.eligible && !result.changed && !result.rejection.empty();
    };
    check(normalizer_rejected(analysis,explicit_decisions,
                              "volume:physical-id","source:v1","rules:v2"),
          "changed normalization ruleset invalidates every approval");
    check(normalizer_rejected(analysis,explicit_decisions,
                              "different:physical-id"),
          "normalizer cannot cross a changed physical file identity");
    check(normalizer_rejected(analysis,explicit_decisions,
                              "volume:physical-id","source:v2"),
          "normalizer cannot cross a changed source guard");
    check(normalizer_rejected(analysis,{explicit_decisions[0]}),
          "partial/stale explicit decision vector cannot cross gate");
    auto wrong_analysis=analysis;
    wrong_analysis.proposals[1].original_value="forged original";
    check(normalizer_rejected(wrong_analysis,explicit_decisions),
          "forged rule-proposal original cannot authorize physical write");
    auto invalid_decisions=explicit_decisions;
    invalid_decisions[2].manual_value="unsafe\nCUE line";
    check(normalizer_rejected(analysis,invalid_decisions),
          "unsafe manual normalizer value cannot cross physical guard");
    auto protected_analysis=analysis;
    protected_analysis.proposals={
        proposed(3,0,"forged cue text",SafetyClass::Review)
    };
    check(normalizer_rejected(protected_analysis,
          {ReviewDecision{ReviewAction::Accept,""}}),
          "normalizer cannot bypass dedicated CUESHEET writer");
    auto equal_analysis=analysis;
    equal_analysis.proposals={
        proposed(0,0,"Old Title",SafetyClass::Safe)
    };
    const auto equal_noop=preview_explicit_normalizer_physical_tags(
        snap,original,"volume:physical-id","source:v1",
        equal_analysis,"rules:v1",
        {ReviewDecision{ReviewAction::Accept,""}});
    check(equal_noop.eligible && !equal_noop.changed &&
          equal_noop.planned==original,
          "explicit acceptance of an already equal normalizer value is zero-write");
    std::cout<<"PASS: selected physical multivalue/tag postimage and protected CUE guards\n";
}
