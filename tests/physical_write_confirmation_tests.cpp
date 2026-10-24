#include "djmeta/physical_write_confirmation.h"
#include <stdexcept>
#include <string>
#include <vector>

using djmeta::PhysicalTagValueApproval;
using djmeta::format_physical_write_confirmation;

namespace {
void check(bool condition,const char* detail) {
    if(!condition) throw std::runtime_error(detail);
}
}

int main() {
    const std::vector<PhysicalTagValueApproval> fields{
        {"TITLE",{"Old mix","Old alias"},{"New mix","New alias"}},
        {"RELEASEDATE",{},{"2026-10-23"}}
    };
    const auto message=format_physical_write_confirmation(
        "C:\\Music\\Example.flac",fields);
    check(message.find("File: C:\\Music\\Example.flac")!=std::string::npos,
          "source path missing");
    check(message.find("Changed fields: 2")!=std::string::npos,
          "change count missing");
    check(message.find("TITLE\n  BEFORE:\n    [1] Old mix\n    [2] Old alias")!=
          std::string::npos,"ordered original multivalues missing");
    check(message.find("AFTER:\n    [1] New mix\n    [2] New alias")!=
          std::string::npos,"ordered replacement multivalues missing");
    check(message.find("RELEASEDATE\n  BEFORE: [absent]")!=std::string::npos,
          "new-field identity missing");
    check(message.find("[1] 2026-10-23")!=std::string::npos,
          "new-field value missing");
    check(message.find("Submit this one-file write?")!=std::string::npos,
          "explicit final consent missing");
    check(djmeta::physical_write_display_text("a\tb\nc\\d")=="a\\tb\\nc\\\\d",
          "control character escaping failed");
    for(const auto& invalid:std::vector<std::vector<PhysicalTagValueApproval>>{
        {},{{"TITLE",{"same"},{"same"}}},{{"TITLE",{"old"},{}}}}) {
        bool refused=false;
        try { (void)format_physical_write_confirmation("C:\\x.flac",invalid); }
        catch(const std::invalid_argument&) { refused=true; }
        check(refused,"invalid change accepted");
    }
    bool oversized=false;
    try {
        (void)format_physical_write_confirmation("C:\\x.flac",
            {{"COMMENT",{"old"},{std::string(30000,'x')}}});
    } catch(const std::length_error&) { oversized=true; }
    check(oversized,"truncated confirmation accepted");
    bool invalid_utf8=false;
    try {
        (void)format_physical_write_confirmation("C:\\x.flac",
            {{"TITLE",{"old"},{std::string(1,static_cast<char>(0xff))}}});
    } catch(const std::invalid_argument&) { invalid_utf8=true; }
    check(invalid_utf8,"invalid UTF-8 accepted");
}
