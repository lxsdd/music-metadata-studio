#include "djmeta/beatport_release_review.h"
#include "djmeta/beatport_public_page.h"
#include "djmeta/beatport_public_html.h"
#include "djmeta/native_candidate_labels.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool yes,const char* msg) {
    if(!yes) {std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}
}
template<typename F> void rejects(F run,const char* msg) {
    bool refused=false;
    try{run();}catch(const std::invalid_argument&){refused=true;}
    check(refused,msg);
}
const djmeta::online::FieldReviewRow* find(
    const std::vector<djmeta::online::FieldReviewRow>& rows,
    const std::string& field) {
    const auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& row){
        return row.field==field;
    });
    return it==rows.end()?nullptr:&*it;
}
}
int main() {
    using namespace djmeta::online;
    const std::string raw=R"JSON(@beatport-track-json
{"id":321,"name":"Home","mix_name":"Extended Mix",
 "artists":[{"name":"DJ Example"}],
 "remixers":[{"name":"Mixer Q"}],"length_ms":402500,
 "isrc":"GBXYZ2601234","genre":{"name":"Progressive House"},
 "new_release_date":"2026-10-16","release":{"id":987,"name":"Home EP"}}
)JSON";
    check(is_beatport_user_json_clipboard(raw) &&
          !is_beatport_user_json_clipboard(R"({"id":321})"),
          "only explicitly selected user JSON is imported");
    const std::string public_jsonld=
        "@beatport-public-jsonld\n"
        "@url=https://www.beatport.com/track/home/321\n"
        "{\"@context\":\"https://schema.org\",\"@type\":\"MusicRecording\","
        "\"url\":\"https://www.beatport.com/track/home/321\","
        "\"name\":\"Home (Extended Mix)\","
        "\"byArtist\":{\"name\":\"DJ Example\"},"
        "\"isrcCode\":\"GBXYZ2601234\",\"duration\":\"PT6M42S\","
        "\"datePublished\":\"2026-10-16\","
        "\"inAlbum\":{\"name\":\"Unverified EP\"}}";
    check(is_beatport_public_page_clipboard(public_jsonld) &&
          !is_beatport_public_page_clipboard(raw) &&
          qualify_beatport_public_track_url(
              "https://www.beatport.com/track/home/321").id==321 &&
          public_iso_duration_ms("PT6M42S")==402000 &&
          public_iso_duration_ms("PT1H3M2S")==3782000 &&
          !public_iso_duration_ms("PT25H"),
          "bounded user-selected public page identity and ISO duration");
    const auto public_track=parse_beatport_public_jsonld_clipboard(public_jsonld);
    check(public_track.id==321 && public_track.title=="Home" &&
          public_track.mix_name=="Extended Mix" &&
          public_track.artists==std::vector<std::string>{"DJ Example"} &&
          public_track.duration_ms==402000 &&
          public_track.release_title.empty() &&
          public_track.publish_date.empty(),
          "JSON-LD readable facts retain mix; unsupported album/date provenance is not invented");
    for(const char* url:{
        "http://www.beatport.com/track/home/321",
        "https://beatport.com/track/home/321",
        "https://www.beatport.com.evil.org/track/home/321",
        "https://www.beatport.com/track/home/321?token=x",
        "https://www.beatport.com/track/home/0321",
        "https://www.beatport.com/release/home/321",
        "https://www.beatport.com/track/home/321/related"}) {
        rejects([&]{(void)qualify_beatport_public_track_url(url);},
                "unqualified source URL, redirect, query or release route fails closed");
    }
    rejects([&] {
        std::string forged=public_jsonld;
        const auto key=forged.find("\"url\":\"https://www.beatport.com/track/home/321\"");
        check(key!=std::string::npos,"JSON-LD URL fixture exists");
        forged.replace(key,55,"\"url\":\"https://www.beatport.com/track/other/322\"");
        (void)parse_beatport_public_jsonld_clipboard(forged);
    }, "public page URL mismatch fails closed");
    check(public_jsonld_script_tag(
              R"(<script data-provenance='visible' TYPE="application/ld+json">)"),
          "real quoted JSON-LD type attribute is recognized");
    check(!public_jsonld_script_tag(
              R"(<script data-hint="type='application/ld+json'">)"),
          "quoted attribute CONTENT cannot spoof a script type");
    check(!public_jsonld_script_tag(
              R"(<script data-hint="type='application/ld+json'" type='text/javascript'>)"),
          "non-JSON-LD type overrides nothing inside another attribute value");
    check(!public_jsonld_script_tag(
              R"(<script type='application/ld+json' type='application/ld+json'>)"),
          "duplicate script type must fail closed");
    const std::string spoofed_jsonld=R"JSON({"@type":"MusicRecording","url":"https://www.beatport.com/track/home/321","name":"Home (Extended Mix)","byArtist":"DJ Example","duration":"PT6M42S"})JSON";
    rejects([&]{
        (void)parse_beatport_public_html(
            "https://www.beatport.com/track/home/321",
            std::string(R"(<script data-hint="type='application/ld+json'">)")+
            spoofed_jsonld+"</script>");
    }, "non-JSON-LD script cannot supply Beatport page identity");
    const std::string public_html=
        "<!doctype html><html><head><script data-provenance='visible' "
        "TYPE='application/ld+json'>"
        "{\"@context\":\"https://schema.org\",\"@graph\":["
        "{\"@type\":\"WebPage\",\"url\":\"https://www.beatport.com\"},"
        "{\"@type\":[\"Thing\",\"MusicRecording\"],"
        "\"@id\":\"https://www.beatport.com/track/home/321\","
        "\"name\":\"Home (Extended Mix)\","
        "\"byArtist\":{\"name\":\"DJ Example\"},"
        "\"duration\":\"PT6M42S\",\"isrcCode\":\"GBXYZ2601234\"}"
        "]}</script></head></html>";
    const auto page=parse_beatport_public_html(
        "https://www.beatport.com/track/home/321",public_html);
    check(page.id==321 && page.title=="Home" &&
          page.mix_name=="Extended Mix" &&
          page.artists==std::vector<std::string>{"DJ Example"} &&
          page.duration_ms==402000 && page.isrc=="GBXYZ2601234",
          "bounded public HTML JSON-LD @graph matches only the requested track");
    check(parse_beatport_public_html(
        "https://www.beatport.com/track/home/321",
        "<script type='application/ld+json'>"
        "{\"@type\":\"MusicRecording\","
        "\"url\":\"https://www.beatport.com/track/home/321\","
        "\"name\":\"Home (Extended Mix)\","
        "\"byArtist\":\"DJ Example\",\"duration\":\"PT6M42S\"}"
        "</script>").id==321,
        "public single-quoted script type and artist text both qualify");
    rejects([&]{(void)parse_beatport_public_html(
        "https://www.beatport.com/track/home/322",public_html);},
        "related track JSON-LD cannot be silently promoted to selected track");
    rejects([]{(void)parse_beatport_public_html(
        "https://www.beatport.com/track/home/321",
        "<script type='application/ld+json'>"
        "{\"@type\":\"MusicRecording\",\"name\":\"Home\","
        "\"byArtist\":\"DJ Example\",\"duration\":\"PT6M42S\"}"
        "</script>");},
        "native webpage requires its own embedded track identity");
    rejects([&]{(void)parse_beatport_public_html(
        "https://www.beatport.com/track/home/321",public_html+public_html);},
        "ambiguous duplicate public recording nodes are not accepted");
    rejects([]{(void)parse_beatport_public_html(
        "https://www.beatport.com/track/home/321",
        "<script type='text/html'>"
        "{\"@type\":\"MusicRecording\","
        "\"url\":\"https://www.beatport.com/track/home/321\"}"
        "</script>");},
        "HTML and unrelated script content are not JSON-LD evidence");
    rejects([]{(void)parse_beatport_public_html(
        "https://www.beatport.com/track/home/321",
        "<html><div>Consent or access challenge</div></html>");},
        "access challenges never trigger an anti-bot fallback");
    FieldReviewRow public_source{};
    public_source.candidate={"TITLE",{"Home"},"beatport","321",
        EvidenceScope::Recording,DateMeaning::NotDate};
    public_source.reason="beatport_public_webpage_readonly_exact_noop";
    FieldReviewRow pasted_source=public_source;
    pasted_source.reason="manual_beatport_readonly_exact_noop";
    check(online_evidence_origin_label(public_source)==
              "Beatport public page (unverified)" &&
          online_evidence_origin_label(pasted_source)=="Pasted: beatport",
          "page and pasted evidence are not mislabeled as authenticated API data");
    const auto track=parse_beatport_user_json_clipboard(raw);
    check(track.id==321 && track.release_id==987 &&
          track.mix_name=="Extended Mix" && track.duration_ms==402500,
          "manual structured DJ recording and edition identity retained");
    djmeta::MetadataDocument original;
    original.fields.push_back({"TITLE",{"Home (Extended Mix)"}});
    original.fields.push_back({"ARTIST",{"DJ Example"}});
    original.fields.push_back({"ISRC",{"GBXYZ2601234"}});
    original.fields.push_back({"GENRE",{"Trance","Progressive House"}});
    original.fields.push_back({"REPLAYGAIN_TRACK_GAIN",{"-5.4 dB"}});
    original.fields.push_back({"MYTAG",{"One","Two"}});
    const auto verified=review_beatport_user_track(original,402500,track);
    check(verified.identity==MatchDecision::Suggested,
          "complete mix, title, artist, ISRC and host duration allow manual review");
    const auto* genre=find(verified.fields,"GENRE");
    const auto* date=find(verified.fields,"DATE");
    const auto* remixer=find(verified.fields,"REMIXER");
    check(genre && genre->state==FieldReviewState::NeedsReview &&
          genre->original_values==std::vector<std::string>({"Trance","Progressive House"}) &&
          genre->candidate.values==std::vector<std::string>({"Progressive House"}) &&
          genre->reason.starts_with("manual_beatport_readonly_") &&
          date && date->candidate.date_meaning==DateMeaning::EditionRelease &&
          date->candidate.source_id=="987" &&
          remixer && remixer->candidate.values==std::vector<std::string>{"Mixer Q"} &&
          online_evidence_origin_label(*remixer)=="Pasted: beatport",
          "Beatport import is marked user-supplied, full-field and version-aware");
    const auto* gain=find(verified.fields,"REPLAYGAIN_TRACK_GAIN");
    const auto* custom=find(verified.fields,"MYTAG");
    check(gain && gain->state==FieldReviewState::Unchanged &&
          gain->candidate.values.empty() && custom &&
          custom->original_values==std::vector<std::string>({"One","Two"}),
          "unknown tags and ReplayGain preserved without proposed deletions");
    auto wrong=original;
    wrong.fields[0].values={"Home (Radio Edit)"};
    const auto blocked=review_beatport_user_track(wrong,402500,track);
    check(blocked.identity==MatchDecision::Rejected &&
          find(blocked.fields,"GENRE")->state==FieldReviewState::Blocked &&
          find(blocked.fields,"DATE")->state==FieldReviewState::Blocked,
          "wrong mix blocks all imported recording and edition proposals");
    auto wrong_artist=original;
    wrong_artist.fields[1].values={"Another DJ"};
    check(review_beatport_user_track(wrong_artist,402500,track).identity==
          MatchDecision::Rejected,"wrong artist cannot suggest Beatport metadata");
    auto missing=original;
    missing.fields[0].values={"Home"};
    check(review_beatport_user_track(missing,402500,track).identity!=
          MatchDecision::Suggested,
          "unverified explicit mix stays blocked despite same base title");
    auto dup=original;
    dup.fields.push_back({"title",{"Home (Extended Mix)"}});
    const auto duplicated=review_beatport_user_track(dup,402500,track);
    check(duplicated.identity==MatchDecision::Rejected &&
          find(duplicated.fields,"GENRE")->state==FieldReviewState::Blocked,
          "ambiguous physical identity does not silently select one TITLE");
    rejects([]{(void)parse_beatport_user_json_clipboard(R"({"id":3})");},
            "missing explicit manual import header");
    rejects([]{(void)parse_beatport_user_json_clipboard("@beatport-track-json\n{}");},
            "missing Beatport track ID and title");
    rejects([]{(void)parse_beatport_user_json_clipboard(
        "@beatport-track-json\n{\"id\":321,\"name\":\"Home\"}");},
        "incomplete artist/duration cannot masquerade as qualified Beatport track");
    rejects([]{(void)parse_beatport_user_json_clipboard(
        "@beatport-track-json\n{\"id\":321,\"name\":\"Bad\\nControl\","
        "\"artists\":[{\"name\":\"DJ\"}],\"length_ms\":402500}");},
        "control-bearing track title rejected");
    rejects([]{(void)parse_beatport_user_json_clipboard(
        std::string("@beatport-track-json\n")+std::string(65536,'x'));},
        "oversized user JSON rejected before parsing");
    // User can explicitly type facts visible on a lawful Beatport track
    // page. No developer key, JSON response, scraping or host file write.
    const std::string fact_text =
        "@beatport-track-facts\n"
        "@provider=beatport\n"
        "@id=track:321\n"
        "@scope=recording\n"
        "TITLE=Home\n"
        "ARTIST=DJ Example\n"
        "VERSION=Extended Mix\n"
        "DURATION=6:42\n"
        "REMIXER=Mixer Q\n"
        "ISRC=GBXYZ2601234\n"
        "GENRE=Progressive House\n"
        "BPM=128\n"
        "KEY=Gm\n"
        "RELEASE_ID=987\n"
        "ALBUM=Home EP\n"
        "LABEL=Demo Label\n"
        "DATE@edition=2026-10-16\n";
    check(is_beatport_user_facts_clipboard(fact_text) &&
          !is_beatport_user_facts_clipboard(raw),
          "factual Beatport header is explicit and separate from API JSON");
    const auto facts=parse_beatport_user_facts_clipboard(fact_text);
    check(facts.id==321 && facts.release_id==987 &&
          facts.title=="Home" && facts.mix_name=="Extended Mix" &&
          facts.duration_ms==402000 && facts.bpm==128 &&
          facts.musical_key=="Gm" &&
          facts.label_name=="Demo Label" &&
          facts.release_date=="2026-10-16",
          "typed Beatport facts retain exact track/mix/edition field identities");
    const auto fact_review=review_beatport_user_track(original,402500,facts);
    check(fact_review.identity==MatchDecision::Suggested &&
          find(fact_review.fields,"GENRE")->state==FieldReviewState::NeedsReview &&
          find(fact_review.fields,"DATE")->candidate.date_meaning==
              DateMeaning::EditionRelease &&
          find(fact_review.fields,"MYTAG")->state==FieldReviewState::Unchanged,
          "typed visible Beatport facts reach qualified, read-only complete physical review");
    const auto wrong_fact_review=review_beatport_user_track(
        wrong,402500,facts);
    check(wrong_fact_review.identity==MatchDecision::Rejected &&
          find(wrong_fact_review.fields,"GENRE")->state==FieldReviewState::Blocked,
          "same Beatport facts never override a conflicting mix");
    const auto fact_change=[&](std::string old, std::string replacement) {
        auto edited=fact_text;
        const auto pos=edited.find(old);
        check(pos!=std::string::npos,"fixture replacement exists");
        edited.replace(pos,old.size(),replacement);
        return edited;
    };
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("VERSION=Extended Mix\n",""));},
        "missing mix identity blocks plain Beatport facts");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("DURATION=6:42","DURATION=6:99"));},
        "incorrect seconds never produce synthetic duration");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("DURATION=6:42","DURATION=bad"));},
        "free-text guessed duration is rejected");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("@id=track:321","@id=track:00321"));},
        "noncanonical track ID cannot masquerade as exact Beatport identity");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("@provider=beatport","@provider=discogs"));},
        "Beatport fact importer cannot reuse foreign-provider evidence");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("@scope=recording","@scope=edition"));},
        "Beatport track facts must remain recording-scoped");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("BPM=128","BPM=900"));},
        "unsafe BPM is never silently normalized");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("DATE@edition=2026-10-16","DATE@edition=2026-02-30"));},
        "unqualified calendar dates do not become release dates");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("KEY=Gm","SURPRISE=arbitrary"));},
        "unrecognized Beatport fact fields fail closed");
    rejects([&]{(void)parse_beatport_user_facts_clipboard(
        fact_change("VERSION=Extended Mix\n",
                    "VERSION=Extended Mix\nVERSION=Radio Edit\n"));},
        "ambiguous supplied versions do not produce track suggestions");
    const std::string crlf_facts="@beatport-track-facts\r\n"+
        fact_text.substr(fact_text.find('\n')+1);
    check(parse_beatport_user_facts_clipboard(crlf_facts).id==321,
          "Windows CRLF Beatport fact marker accepted without changing evidence");

    const std::string windows="@beatport-track-json\r\n"+
        raw.substr(raw.find('\n')+1);
    check(parse_beatport_user_json_clipboard(windows).id==321,
          "Windows CRLF clipboard marker accepted");
    check(original.fields[4].values==std::vector<std::string>{"-5.4 dB"},
          "no media metadata writes or mutation");

    // Exact edition: two independently supplied JSON documents, no network.
    const std::string bundle =
        "@beatport-release-json\n"
        R"({"id":987,"name":"Home EP","track_count":2,"new_release_date":"2026-10-16"})" "\n"
        "@beatport-tracks-json\n"
        R"({"count":2,"results":[
          {"id":321,"name":"Home","mix_name":"Extended Mix","number":1,
           "artists":[{"name":"DJ Example"}],"length_ms":402500,
           "isrc":"GBXYZ2601234","release":{"id":987,"name":"Home EP"}},
          {"id":322,"name":"Home","mix_name":"Radio Edit","number":2,
           "artists":[{"name":"DJ Example"}],"length_ms":202500,
           "release":{"id":987,"name":"Home EP"}}]})";
    check(is_beatport_user_release_clipboard(bundle),
          "explicit Beatport release marker recognized");
    const auto edition=parse_beatport_user_release_clipboard(bundle);
    check(edition.provider=="beatport" && edition.edition_id=="987" &&
          edition.tracks.size()==2 &&
          edition.tracks[1].recording.mix==MixKind::Radio,
          "complete edition preserves two separate mix identities");
    const auto physical_review=review_beatport_user_edition_physical(
        original,402500,edition);
    const auto* first=find(physical_review,"Release Track 1 / TITLE");
    const auto* other=find(physical_review,"Release Track 2 / TITLE");
    const auto* album=find(physical_review,"Album / ALBUM");
    const auto* local_custom=find(physical_review,"Physical / MYTAG");
    check(first && first->state!=FieldReviewState::Blocked &&
          first->candidate.source_id=="321" &&
          first->candidate.scope==EvidenceScope::Recording &&
          other && other->state==FieldReviewState::Blocked &&
          album && album->candidate.source_id=="987" &&
          local_custom && local_custom->original_values==
              std::vector<std::string>({"One","Two"}) &&
          online_evidence_origin_label(*first)=="Pasted: beatport",
          "physical edition compares only exact mix and keeps local multivalues");
    const auto wrong_edition=review_beatport_user_edition_physical(
        wrong,402500,edition);
    check(find(wrong_edition,"Album / ALBUM")->state==
              FieldReviewState::Blocked,
          "conflicting physical mix cannot borrow edition facts");
    const auto cue=djmeta::inspect_cue_metadata(
        "PERFORMER \"DJ Example\"\n"
        "TITLE \"Home EP\"\n"
        "FILE \"sample.mp3\" MP3\n"
        "  TRACK 01 AUDIO\n"
        "    TITLE \"Home (Extended Mix)\"\n"
        "    PERFORMER \"DJ Example\"\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    TITLE \"Home (Radio Edit)\"\n"
        "    PERFORMER \"DJ Example\"\n"
        "    INDEX 01 06:42:00\n",
        djmeta::CueCarrierKind::ExternalText);
    check(cue.status==djmeta::CueSyntaxStatus::Parsed,
          "external CUE fixture parses");
    const auto cue_review=review_beatport_user_edition_cue(cue,edition);
    const auto* cue_album=find(cue_review,"Album / TITLE");
    bool match_seen=false,recording_seen=false;
    for(const auto& row:cue_review) {
        if(row.candidate.field=="MATCH_DIAGNOSTIC") match_seen=true;
        if(row.candidate.field=="TITLE" &&
           row.candidate.scope==EvidenceScope::Recording) {
            recording_seen=true;
            check(row.candidate.source_id=="321" ||
                  row.candidate.source_id=="322",
                  "CUE evidence preserves individual Beatport track ID");
        }
    }
    check(cue_album && cue_album->original_values==
              std::vector<std::string>{"Home EP"} &&
          match_seen && recording_seen,
          "CUE album and track evidence shown without fabricated virtual writes");
    // End-to-end source-scope safety: a parsed local CUE is not evidence
    // for an unrelated edition, even if the pasted release has a plausible
    // album name. All album changes require complete one-to-one tracks.
    auto unrelated=edition;
    for(auto& edition_track:unrelated.tracks) {
        edition_track.recording.title="Different recording";
        edition_track.recording.primary_artist="Another Artist";
    }
    const auto unrelated_review=review_beatport_user_edition_cue(cue,unrelated);
    const auto* unrelated_album=find(unrelated_review,"Album / TITLE");
    check(unrelated_album &&
          unrelated_album->state==FieldReviewState::Blocked &&
          unrelated_album->reason.find("unverified_edition")!=std::string::npos,
          "unrelated pasted edition cannot propose CUE global ALBUM edit");

    // Full-set duplicate detection: two remote fields that resolve to the
    // same CUE token must BOTH block. Never independently compare them.
    auto duplicated_album=edition;
    duplicated_album.fields.push_back(duplicated_album.fields.front());
    const auto album_collision=review_beatport_user_edition_cue(
        cue,duplicated_album);
    std::size_t blocked_titles=0;
    for(const auto& row:album_collision)
        if(row.field=="Album / TITLE" &&
           row.state==FieldReviewState::Blocked &&
           row.reason.find("cue_duplicate_provider_field_evidence")!=
                std::string::npos)
            ++blocked_titles;
    check(blocked_titles==2,
          "duplicate pasted album fields never compete for one CUE global");

    auto duplicated_track=edition;
    duplicated_track.tracks[0].fields.push_back(
        duplicated_track.tracks[0].fields.front());
    const auto track_collision=review_beatport_user_edition_cue(
        cue,duplicated_track);
    std::size_t track_fields=0,blocked_track_titles=0;
    for(const auto& row:track_collision)
        if(row.field=="CUE Track 1 / TITLE" ||
           row.field=="Edition Track 1 / TITLE") {
            ++track_fields;
            if(row.state==FieldReviewState::Blocked &&
               row.reason.find("cue_duplicate_provider_field_evidence")!=
                   std::string::npos)
                ++blocked_track_titles;
        }
    check(track_fields==2 && blocked_track_titles==2,
          "duplicate pasted recording fields cannot bypass CUE target collision gate");

    rejects([&]{(void)parse_beatport_user_release_clipboard(
        bundle.substr(0,bundle.find("\n@beatport-tracks-json")));},
        "release detail alone is not a full tracklist");
    rejects([&]{auto bad=bundle;auto p=bad.find("\"track_count\":2");
        bad.replace(p,15,"\"track_count\":3");
        (void)parse_beatport_user_release_clipboard(bad);},
        "incomplete pasted edition is rejected");
    // The same native labels now used by the production owner-data
    // ListView must expose distinct unverified release tracks, independent
    // of platform-specific inaccessible cross-process virtual list APIs.
    const djmeta::MetadataDocument empty_original{};
    const auto unmatched=review_beatport_user_edition_physical(
        empty_original,1000,edition);
    for (int track_number=1;track_number<=2;++track_number) {
        const std::string prefix="Release Track "+
            std::to_string(track_number);
        const auto* diagnostic=find(unmatched,prefix+" / MATCH");
        check(diagnostic && diagnostic->state==FieldReviewState::Blocked,
              "both user-pasted mixes lack physical recording identity");
        auto summary=*diagnostic;
        summary.field=prefix;
        summary.reason="musicbrainz_live_release_group";
        check(native_candidate_source_label(summary)=="Pasted: beatport" &&
              native_candidate_status_label(summary)=="Unmatched" &&
              summary.field==prefix,
              "native release row keeps mix identity, pasted origin and blocked state");
    }
    auto local_only=make_local_physical_original(
        original.fields.back(),"manual_beatport_local_only_no_provider_value");
    local_only.reason="local_physical_group";
    check(native_candidate_source_label(local_only)=="Local physical" &&
          native_candidate_status_label(local_only)=="Keep original",
          "native original-only lane cannot masquerade as a Beatport edit");
    std::cout<<"Beatport user clipboard read-only workflow PASS\n";
}
