#include "djmeta/cue_value_postimage.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
namespace {
void check(bool yes,const char* msg){
    if(!yes){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}
}}
int main(){
    using namespace djmeta;
    const std::string cue=std::string("\xef\xbb\xbf")+
        "REM UNKNOWN \"unknown remains\"\r\n"
        "TITLE \"Ä Album\"\r\n"
        "PERFORMER \"Album Performer\"\r\n"
        "FILE \"disc 1.flac\" WAVE\r\n"
        " TRACK 01 AUDIO\r\n"
        "  TITLE \"Old Track\"\r\n"
        "  INDEX 00 00:00:00\r\n"
        "  INDEX 01 00:02:00\r\n"
        "FILE \"disc 2.flac\" WAVE\r\n"
        " TRACK 01 AUDIO\r\n"
        "  TITLE OldSecond\r\n"
        "  INDEX 01 00:00:00\r\n";
    const auto changed=preview_cue_value_postimage(cue,
        CueCarrierKind::ExternalText,{
          {std::nullopt,"TITLE","Ä Album","New Album"},
          {0,"TITLE","Old Track","Extended Mix"},
          {1,"TITLE","OldSecond","Next Mix"}});
    auto expected=cue;
    auto patch=[](std::string& value,const std::string& a,const std::string& b){
        const auto i=value.find(a);
        if(i==std::string::npos){std::exit(2);}
        value.replace(i,a.size(),b);
    };
    patch(expected,"Ä Album","New Album");
    patch(expected,"Old Track","Extended Mix");
    patch(expected,"OldSecond","Next Mix");
    check(changed.eligible && changed.changed && changed.bytes==expected,
          "only exact value spans differ; BOM, EOL, FILE, INDEX, REM intact");
    const auto no_op=preview_cue_value_postimage(cue,CueCarrierKind::EmbeddedText,
         {{0,"TITLE","Old Track","Old Track"}});
    check(no_op.eligible && !no_op.changed && no_op.bytes==cue,
          "embedded Cue byte-identical no-op");
    const auto embedded=preview_cue_value_postimage(cue,CueCarrierKind::EmbeddedText,
         {{0,"TITLE","Old Track","Neu Ä"}});
    check(embedded.eligible && embedded.changed,
          "embedded Cue can preview UTF-8 metadata, without writer");
    const auto reject=[&](const std::string& raw,std::vector<CueValueChange> change){
        const auto out=preview_cue_value_postimage(
            raw,CueCarrierKind::ExternalText,change);
        return !out.eligible && !out.changed && out.bytes.empty() &&
               !out.rejection.empty();
    };
    check(reject(cue,{{0,"TITLE","not original","New"}}),"stale original");
    check(reject(cue,{{2,"TITLE","Old Track","New"}}),"unknown track ordinal");
    check(reject(cue,{{std::nullopt,"ISRC","X","Y"}}),"global ISRC unsupported");
    check(reject(cue,{{0,"REM UNKNOWN","X","Y"}}),"unknown REM protected");
    check(reject(cue,{{0,"TITLE","Old Track","A\nFILE bad.mp3 MP3"}}),"line injection");
    check(reject(cue,{{0,"TITLE","Old Track","A\" TITLE \"B"}}),"quote injection");
    check(reject(cue,{{1,"TITLE","OldSecond"," Bad"}}),"unquoted whitespace");
    check(reject(cue,{{0,"TITLE","Old Track","A"},
                      {0,"TITLE","Old Track","B"}}),"duplicate edits");
    check(reject(cue,{{0,"TITLE","Old Track",std::string("\xff")}}),"invalid UTF-8");
    check(reject("FILE a.flac WAVE\nTRACK 01 AUDIO\nTITLE A\nTITLE B\n",
          {{0,"TITLE","A","X"}}),"ambiguous original duplicated");
    check(reject("FILE a.flac WAVE\nTRACK 01 AUDIO\nTITLE \"unterminated\n",
          {{0,"TITLE","bad","X"}}),"invalid source blocked");
    check(reject(cue,{{1,"REM GENRE","X","Y"}}),"unsupported track scope");
    const auto empty=preview_cue_value_postimage(cue,CueCarrierKind::ExternalText,{});
    check(empty.eligible && !empty.changed && empty.bytes==cue,
          "no selected changes is pure no-op");
    std::cout<<"PASS: exact-span CUE value postimage/no-op/UTF-8/structural safety\n";
}
