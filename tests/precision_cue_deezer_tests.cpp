#include "djmeta/precision_cue_deezer.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace djmeta;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"Deezer fixture failed at %d: %s\n",__LINE__,#x); std::exit(1); } } while(false)
namespace {
std::string album(std::string_view tracks, std::string_view count="2",
                  std::string_view suffix="") {
    return std::string("{\"id\":1071045032,\"title\":\"Synthetic Édition\","
      "\"artist\":{\"name\":\"Example Artist\"},\"nb_tracks\":") +
      std::string(count) + ",\"tracks\":{\"data\":[" +
      std::string(tracks) + "],\"total\":" + std::string(count) +
      std::string(suffix) + "}}";
}
std::string row(int id,int disc,int index,std::string_view duration="123") {
    return std::string("{\"id\":") + std::to_string(id) +
      ",\"title\":\"Synthetic Track\",\"artist\":{\"name\":\"Example Artist\"},"
      "\"disk_number\":" + std::to_string(disc) +
      ",\"track_position\":" + std::to_string(index) +
      ",\"duration\":" + std::string(duration) + "}";
}
void rejects(std::string_view json) {
    try { (void)parse_deezer_precision_cue_album(json,"1071045032"); }
    catch (const std::invalid_argument&) { return; }
    CHECK(false);
}
}
int main() {
    CHECK(deezer_precision_cue_album_api_url("001071045032") ==
          "https://api.deezer.com/album/1071045032");
    for (const auto invalid : {"0","-1","1?x=2","1/../2","18446744073709551615"})
        try { (void)deezer_precision_cue_album_api_url(invalid); CHECK(false); }
        catch (const std::invalid_argument&) {}
    const auto one=row(11,1,1);
    const auto two=row(12,1,2);
    const auto good=album(one+","+two);
    const auto parsed=parse_deezer_precision_cue_album(good,"1071045032");
    CHECK(parsed.provider=="deezer" && parsed.provider_release_id=="1071045032");
    CHECK(parsed.tracks.size()==2 && parsed.tracks[0].duration_ms==123000);
    CHECK(parsed.tracks[1].track_number==2);
    const auto multidisc=parse_deezer_precision_cue_album(
        album(row(11,1,1)+","+row(12,2,1)),"1071045032");
    CHECK(multidisc.tracks[1].disc_number==2 && multidisc.tracks[1].track_number==1);
    rejects(album(one,"2")); // truncated
    rejects(album(one+","+two,"3")); // declared count mismatch
    rejects(album(one+","+two,"2",",\"next\":\"https://api.deezer.com/album/1/tracks?index=2\""));
    rejects(album(one+","+two,"2",",\"next\":false"));
    rejects(album(one+","+one)); // duplicate provider track ID and position
    rejects(album(row(11,1,1)+","+row(12,1,3))); // gap
    rejects(album(row(11,1,1)+","+row(12,3,1))); // disc gap
    rejects(album(row(11,9,1)+","+row(12,9,2))); // disc beyond 8
    rejects(album(row(11,1,1,"0")+","+two));
    rejects(album(row(11,1,1,"1.25")+","+two));
    rejects(album(row(11,1,1,"86401")+","+two));
    rejects("{\"error\":{\"type\":\"DataException\"}}");
    rejects(album(one+","+two)+"{}"); // trailing JSON
    auto wrong_id=good;
    const auto id_at=wrong_id.find("1071045032");
    CHECK(id_at!=std::string::npos);
    wrong_id.replace(id_at,10,"1071045033");
    rejects(wrong_id);
    // Whole-second Deezer metadata must respect each 24-hour disc clock.
    const auto exact_day = album(row(11,1,1,"43200")+","+row(12,1,2,"43200"));
    CHECK(parse_deezer_precision_cue_album(exact_day,"1071045032").tracks.size()==2);
    rejects(album(row(11,1,1,"43200")+","+row(12,1,2,"43201")));
    // Two valid 24-hour discs must not be rejected as a 48-hour album.
    const auto two_days = album(row(11,1,1,"86400")+","+row(12,2,1,"86400"));
    CHECK(parse_deezer_precision_cue_album(two_days,"1071045032").tracks.size()==2);
    CHECK(parsed.tracks[0].duration_ms % 1000 == 0);
    std::puts("PASS: strict Deezer read-only complete album/edition/seconds fixtures");
}
