#include "djmeta/online_fields.h"
#include "djmeta/online_field_staging.h"
#include "djmeta/online_field_ui_selection.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void check(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace djmeta;
    using namespace djmeta::online;
    const MetadataDocument original{{
        {"ARTIST", {"A", "B"}},
        {"DATE", {"1998"}},
        {"GENRE", {"House"}},
        {"COMMENT", {"keep this"}},
        {"CUESHEET", {"FILE a.mp3 MP3\nTRACK 01 AUDIO"}},
        {"X_CUSTOM", {"leave alone"}}
    }};
    const auto saved = original;
    const std::vector<FieldEvidence> evidence{
        {"artist", {"A", "B"}, "discogs", "release:100"},
        {"GENRE", {"Tech House"}, "beatport", "track:500"},
        {"DATE", {"2005-04-01"}, "discogs", "release:100",
          EvidenceScope::Edition, DateMeaning::EditionRelease},
        {"DATE_RAW", {"1998-06-15"}, "musicbrainz", "recording:1",
          EvidenceScope::Recording, DateMeaning::OriginalRelease},
        {"COMMENT", {}, "discogs", "release:100"},
        {"CUESHEET", {"edited"}, "discogs", "release:100"},
        {"REPLAYGAIN_TRACK_GAIN", {"+0.3 dB"}, "discogs", "release:100"},
        {"X_CUSTOM", {"should remain"}, "", ""},
        {"DATE", {"1970"}, "discogs", "release:100"},
        {"ALBUM", {"Collection", "Special Edition"}, "discogs", "release:100",
          EvidenceScope::Edition, DateMeaning::NotDate}
    };
    const auto rows = review_online_fields(original, evidence);
    check(original == saved && rows.size() == evidence.size(),
          "immutable input and exactly one review row per source evidence");
    check(rows[0].state == FieldReviewState::Unchanged &&
          rows[0].reason == "exact_noop" &&
          rows[0].original_values == std::vector<std::string>({"A", "B"}) &&
          rows[0].local_field_index == 0,
          "multivalue artist unchanged without writer authorization");
    check(rows[1].state == FieldReviewState::NeedsReview &&
          rows[1].reason == "existing_values_differ",
          "changed genre is a REVIEW, not an automated tag write");
    check(rows[2].state == FieldReviewState::NeedsReview &&
          rows[2].candidate.date_meaning == DateMeaning::EditionRelease &&
          rows[2].original_values == std::vector<std::string>({"1998"}),
          "edition date must retain DATE provenance and original");
    check(rows[3].state == FieldReviewState::NeedsReview &&
          rows[3].local_field_index == no_local_field &&
          rows[3].candidate.date_meaning == DateMeaning::OriginalRelease,
          "DATE_RAW can be proposed but never silently derived from DATE");
    check(rows[4].state == FieldReviewState::Blocked &&
          rows[4].reason == "empty_evidence_must_not_delete_tag",
          "empty provider field cannot delete an existing tag");
    check(rows[5].state == FieldReviewState::Blocked &&
          rows[5].reason == "protected_metadata_field" &&
          rows[6].state == FieldReviewState::Blocked,
          "CUE and ReplayGain remain under dedicated writer boundaries");
    check(rows[7].state == FieldReviewState::Blocked &&
          rows[7].reason == "invalid_evidence_identity",
          "missing provider identity is not an authoritative source");
    check(rows[8].state == FieldReviewState::Blocked &&
          rows[8].reason == "unknown_date_semantics",
          "date of unknown meaning cannot silently replace DATE");
    check(rows[9].state == FieldReviewState::NeedsReview &&
          rows[9].candidate.values.size() == 2,
          "unknown future custom fields and multivalue inputs remain visible");

    const MetadataDocument duplicate{{{"TITLE", {"a"}}, {"title", {"b"}}}};
    const auto duplicates = review_online_fields(
        duplicate, {{"title", {"c"}, "deezer", "track:3"}});
    check(duplicates.size() == 1 &&
          duplicates[0].state == FieldReviewState::Blocked &&
          duplicates[0].local_field_index == no_local_field &&
          duplicates[0].reason == "ambiguous_duplicate_local_field",
          "duplicate names cannot be flattened or arbitrarily selected");
    const auto malformed = review_online_fields(
        original, {{std::string("ARTIST\0", 7), {"X"}, "deezer", "track:1"}});
    check(malformed[0].state == FieldReviewState::Blocked &&
          malformed[0].reason == "invalid_evidence_identity",
          "invalid control byte in tag name cannot enter draft");

    const auto extra_safety = review_online_fields(original, {
        {"USLT", {"words"}, "discogs", "r1"},
        {"SYLT", {"words"}, "discogs", "r1"},
        {"COMMENT", {std::string("A\0B", 3)}, "discogs", "r1"},
        {"TITLE", {std::string("\xC0\xAF", 2)}, "discogs", "r1"},
        {"GENRE", {""}, "discogs", "r1"}
    });
    check(extra_safety.size() == 5 &&
          extra_safety[0].reason == "protected_metadata_field" &&
          extra_safety[1].reason == "protected_metadata_field" &&
          extra_safety[2].reason == "invalid_or_empty_evidence_value" &&
          extra_safety[3].reason == "invalid_or_empty_evidence_value" &&
          extra_safety[4].reason == "invalid_or_empty_evidence_value",
          "shared lyrics safety and UTF-8/NUL/empty provider-value guards");

    const auto repeated = review_online_fields(original, {
        {"genre", {"House"}, "discogs", "release:100", EvidenceScope::Edition},
        {"GENRE", {"Tech House"}, "discogs", "release:100", EvidenceScope::Edition},
        {"GENRE", {"Dance"}, "musicbrainz", "release:100", EvidenceScope::Edition},
        {"GENRE", {"Electronic"}, "discogs", "release:100", EvidenceScope::Recording}
    });
    check(repeated.size() == 4 &&
          repeated[0].state == FieldReviewState::Blocked &&
          repeated[1].state == FieldReviewState::Blocked &&
          repeated[0].reason == "duplicate_provider_field_evidence" &&
          repeated[1].reason == "duplicate_provider_field_evidence" &&
          repeated[2].state == FieldReviewState::NeedsReview &&
          repeated[3].state == FieldReviewState::NeedsReview,
          "same-origin duplicates blocked, independent provider/scope preserved");
    const auto identical = review_online_fields(original, {
        {"ARTIST", {"A", "B"}, "discogs", "release:100"},
        {"artist", {"A", "B"}, "discogs", "release:100"}
    });
    check(identical[0].state == FieldReviewState::Blocked &&
          identical[1].state == FieldReviewState::Blocked,
          "identical duplicate evidence must not create duplicate approvals");
    const auto malformed_origin = review_online_fields(original, {
        {"TITLE", {"X"}, "discogs\nother", "release:1"},
        {"TITLE", {"X"}, "discogs", std::string("id\0hidden", 9)},
        {"TITLE", {"X"}, "discogs", std::string(513, 'x')},
        {"TITLE", {"X"}, "discogs", "release:2"}
    });
    check(malformed_origin[0].reason == "invalid_evidence_identity" &&
          malformed_origin[1].reason == "invalid_evidence_identity" &&
          malformed_origin[2].reason == "invalid_evidence_identity" &&
          malformed_origin[3].state == FieldReviewState::NeedsReview,
          "provider/source identity controls, NUL and bounds fail closed");
    const auto bounded = review_online_fields(original, {
        {"GENRE", std::vector<std::string>(129, "x"), "discogs", "release:3"},
        {"COMMENT", {std::string(8193, 'x')}, "discogs", "release:3"}
    });
    check(bounded[0].reason == "unbounded_evidence_values" &&
          bounded[1].reason == "invalid_or_empty_evidence_value",
          "provider field-vector and value bounds prevent unreviewable proposals");
    FieldReviewRow discogs_live;
    discogs_live.candidate.provider="discogs";
    discogs_live.candidate.source_id="4243/track/1";
    discogs_live.candidate.scope=EvidenceScope::Edition;
    discogs_live.candidate.date_meaning=DateMeaning::EditionRelease;
    discogs_live.reason="discogs_live_release_group";
    check(is_structural_or_non_tag_field("MATCH_DIAGNOSTIC"),
          "match diagnostic field cannot become a physical tag proposal");
    check(online_evidence_origin_label(discogs_live)=="Discogs API" &&
          online_evidence_scope_label(discogs_live.candidate.scope)=="exact edition" &&
          online_evidence_date_label(discogs_live.candidate.date_meaning)=="edition release",
          "verified Discogs track provenance and edition date are visible");
    discogs_live.reason="manual_candidate_comparison";
    check(online_evidence_origin_label(discogs_live)=="Pasted: discogs",
          "pasted Discogs evidence is never relabeled official API");
    discogs_live.reason="discogs_live_release_group";
    discogs_live.candidate.provider="musicbrainz";
    check(online_evidence_origin_label(discogs_live)=="Pasted: musicbrainz",
          "mismatched provider and origin cannot claim official Discogs");
    discogs_live.reason="musicbrainz_live_release_group";
    check(online_evidence_origin_label(discogs_live)=="MusicBrainz API",
          "verified MusicBrainz source remains distinct");
    discogs_live.reason="cue_inventory_read_only";
    check(online_evidence_origin_label(discogs_live)=="Local CUE",
          "CUE carrier provenance stays separate from online providers");
    // Source-only physical fields do not inherit remote provider IDs.
    const MetadataField physical_tag{"MY CUSTOM",{"A","B"}};
    for(const auto& reason:{"discogs_local_only_no_provider_value",
                            "musicbrainz_local_only_no_provider_value",
                            "manual_beatport_local_only_no_provider_value"}) {
        const auto original_only=make_local_physical_original(physical_tag,reason);
        check(is_local_physical_original(original_only) &&
              online_evidence_origin_label(original_only)=="Local physical" &&
              original_only.candidate.provider=="local-physical" &&
              original_only.candidate.source_id=="physical-original" &&
              original_only.candidate.values.empty() &&
              original_only.original_values==physical_tag.values &&
              original_only.state==FieldReviewState::Unchanged,
              "original-only multivalues cannot borrow official/typed source identity");
        auto forged_provider=original_only;
        forged_provider.candidate.provider="discogs";
        check(!is_local_physical_original(forged_provider) &&
              online_evidence_origin_label(forged_provider)=="Pasted: discogs",
              "provider-only relabeling never claims local physical origin");
    }
    const auto physical_group=make_local_physical_original(
        physical_tag,"local_physical_group");
    check(online_evidence_origin_label(physical_group)=="Local physical",
          "group header retains local carrier provenance");
    // Both Discogs and MusicBrainz native search details use this shared
    // bounded source inventory, retaining all 70 original fields when able.
    MetadataDocument seventy;
    for(int i=0;i<70;++i)
        seventy.fields.push_back({"CUSTOM_"+std::to_string(i),
            {std::to_string(i),"Second value"}});
    const auto original_seventy=seventy;
    std::vector<FieldReviewRow> full_detail;
    append_local_physical_inventory(full_detail,seventy);
    check(full_detail.size()==70 && seventy==original_seventy &&
          full_detail[69].original_values==seventy.fields[69].values &&
          online_evidence_origin_label(full_detail[69])=="Local physical",
          "online search must not stop silently at 50 original physical tags");
    std::vector<FieldReviewRow> bounded_inventory{
        make_local_physical_original(physical_tag,"local_physical_inventory_readonly")};
    append_local_physical_inventory(bounded_inventory,seventy,12);
    check(bounded_inventory.size()==12 &&
          bounded_inventory[10].original_values==seventy.fields[9].values &&
          bounded_inventory[11].reason=="local_physical_inventory_overflow" &&
          bounded_inventory[11].original_values.front().find("60 original field")!=std::string::npos &&
          bounded_inventory[11].state==FieldReviewState::Blocked &&
          online_evidence_origin_label(bounded_inventory[11])=="Local physical",
          "large catalog previews expose hidden-original count and preserve source");
    bool rejected_unrenderable=false;
    try {
        std::vector<FieldReviewRow> no_room;
        append_local_physical_inventory(no_room,seventy,1);
    } catch(const std::invalid_argument&) {rejected_unrenderable=true;}
    check(rejected_unrenderable,"cannot silently discard a local inventory");

    // Field-selective online intake is still a pure preview. Nothing is
    // accepted without a selection, and unknown physical tags survive.
    const MetadataDocument stage_original{{
        {"TITLE",{"Song (Original Mix)"}},
        {"ARTIST",{"Primary","Collaborator"}},
        {"GENRE",{"Trance","Progressive"}},
        {"DATE",{"1998"}},
        {"CUSTOM",{"A","B"}},
        {"COMMENT",{"Leave intact"}}
    }};
    const std::vector<FieldEvidence> stage_evidence{
        {"ARTIST",{"Wrong"}, "discogs","release:100",EvidenceScope::Edition},
        {"GENRE",{"House","Progressive House"}, "discogs","release:100",
         EvidenceScope::Edition},
        {"DATE",{"2026-10-21"}, "discogs","release:100",
         EvidenceScope::Edition,DateMeaning::EditionRelease},
        {"LABEL",{"Synthetic Label"}, "discogs","release:100",
         EvidenceScope::Edition},
        {"CUESHEET",{"Unsafe"}, "discogs","release:100",
         EvidenceScope::Edition},
        {"GENRE",{"Tech House"}, "musicbrainz","release:200",
         EvidenceScope::Edition},
        {"COMMENT",{"Leave intact"}, "musicbrainz","release:200",
         EvidenceScope::Edition}
    };
    // Only physical release-level rows from the verified exact Discogs
    // edition may be projected. The visible list-row index is not evidence.
    const ReleaseEdition approved_edition{
        "discogs","release:100",
        {stage_evidence[1],stage_evidence[3]},{}
    };
    const auto album_review=review_online_fields(stage_original,approved_edition.fields);
    auto first_album=album_review[0];
    first_album.field="Album / "+first_album.field;
    first_album.reason="discogs_live_release_edition_"+first_album.reason;
    auto second_album=album_review[1];
    second_album.field="Album / "+second_album.field;
    second_album.reason="discogs_live_release_edition_"+second_album.reason;
    check(physical_discogs_edition_field_index(approved_edition,first_album)==0 &&
          physical_discogs_edition_field_index(approved_edition,second_album)==1,
          "exact edition album evidence resolves by source and field, not sorted view index");
    auto excluded=first_album;
    excluded.reason="discogs_live_release_cue_track_match_review";
    check(!physical_discogs_edition_field_index(approved_edition,excluded),
          "CUE track evidence must not join a physical-field preview");
    excluded=first_album;
    excluded.field="Physical / GENRE";
    check(!physical_discogs_edition_field_index(approved_edition,excluded),
          "raw original physical tag is not selectable online evidence");
    excluded=first_album;
    excluded.state=FieldReviewState::Blocked;
    check(!physical_discogs_edition_field_index(approved_edition,excluded),
          "blocked or ambiguous edition match cannot be staged");
    excluded=first_album;
    excluded.candidate.source_id="release:other";
    check(!physical_discogs_edition_field_index(approved_edition,excluded),
          "another release identity cannot select this edition's fields");
    excluded=first_album;
    excluded.candidate.values={"Forged"};
    check(!physical_discogs_edition_field_index(approved_edition,excluded),
          "displayed evidence must byte-match a verified provider field");
    excluded=first_album;
    excluded.reason="discogs_live_search_unverified_field";
    check(!physical_discogs_edition_field_index(approved_edition,excluded),
          "search snippets remain evidence-only without staging rights");
    const auto exact_staged=capture_online_field_review(
        stage_original,approved_edition.fields,true,true);
    const auto exact_projected=stage_selected_online_fields(
        exact_staged,stage_original,{*physical_discogs_edition_field_index(
            approved_edition,second_album)});
    check(exact_projected.planned.fields.back()==
          MetadataField{"LABEL",{"Synthetic Label"}} &&
          exact_projected.explicitly_changed==1,
          "visible exact-album selection reaches detached stage with no writer");

    const auto session=capture_online_field_review(stage_original,stage_evidence,true,true);
    const auto untouched=stage_selected_online_fields(session,stage_original,{});
    check(untouched.planned==stage_original &&
          untouched.explicitly_changed==0 && untouched.exact_noops==0,
          "no online selection means no tag modification in preview");
    const auto selected=stage_selected_online_fields(session,stage_original,{1,3});
    check(selected.explicitly_changed==2 &&
          selected.planned.fields.size()==stage_original.fields.size()+1 &&
          selected.planned.fields[2].values==
              std::vector<std::string>({"House","Progressive House"}) &&
          selected.planned.fields[3]==stage_original.fields[3] &&
          selected.planned.fields[4]==stage_original.fields[4] &&
          selected.planned.fields.back()==MetadataField{"LABEL",{"Synthetic Label"}} &&
          stage_original.fields[2].values==
              std::vector<std::string>({"Trance","Progressive"}),
          "explicitly staged fields do not flatten or delete original multivalues");
    const auto same=stage_selected_online_fields(session,stage_original,{6});
    check(same.planned==stage_original &&
          same.explicitly_changed==0 && same.exact_noops==1,
          "selection of an identical remote value yields a zero-change preview");
    const auto refuses=[&](const auto& captured,
                           const MetadataDocument& live,
                           std::vector<std::size_t> choice) {
        try {(void)stage_selected_online_fields(captured,live,choice);}
        catch(const std::invalid_argument&) {return true;}
        return false;
    };
    check(refuses(session,stage_original,{4}) &&
          refuses(session,stage_original,{2}) &&
          refuses(session,stage_original,{1,5}) &&
          refuses(session,stage_original,{1,1}) &&
          refuses(session,stage_original,{999}),
          "protected CUE, unmapped edition DATE, conflicting providers and duplicate or unknown choices are blocked");
    const auto unchanged_date_evidence=std::vector<FieldEvidence>{
        {"DATE",{"1998"},"discogs","release:100",EvidenceScope::Edition,
         DateMeaning::EditionRelease}
    };
    const auto unchanged_date_session=capture_online_field_review(
        stage_original,unchanged_date_evidence,true,true);
    const auto unchanged_date_projection=stage_selected_online_fields(
        unchanged_date_session,stage_original,{0});
    check(unchanged_date_projection.exact_noops==1 &&
          unchanged_date_projection.explicitly_changed==0 &&
          unchanged_date_projection.planned==stage_original,
          "unchanged typed edition date remains a genuine zero-change preview");
    check(online_date_requires_explicit_target(FieldEvidence{
              "RELEASEDATE",{"2024"},"discogs","release:100",
              EvidenceScope::Edition,DateMeaning::EditionRelease}) &&
          online_date_requires_explicit_target(FieldEvidence{
              "DATE",{"1998"},"discogs","master:4343",
              EvidenceScope::Release,DateMeaning::OriginalRelease}),
          "all qualified master and exact-edition dates require explicit target mapping");
    // A deliberate target policy can stage the exact verified edition date
    // to a separate field without changing the user's existing DATE.
    const auto chosen_edition_date=stage_selected_online_fields(
        session,stage_original,{2},{{2,"RELEASEDATE"}});
    check(chosen_edition_date.explicitly_changed==1 &&
          chosen_edition_date.planned.fields[3]==stage_original.fields[3] &&
          chosen_edition_date.planned.fields.back()==
              MetadataField{"RELEASEDATE",{"2026-10-21"}},
          "explicit edition date target keeps original DATE intact");
    const auto chosen_date_override=stage_selected_online_fields(
        session,stage_original,{2},{{2,"DATE"}});
    check(chosen_date_override.explicitly_changed==1 &&
          chosen_date_override.planned.fields[3].values==
              std::vector<std::string>{"2026-10-21"} &&
          stage_original.fields[3].values==std::vector<std::string>{"1998"},
          "only explicit named DATE choice may replace physical DATE in memory");
    const auto refuses_date_target=[&](
        const std::vector<std::size_t>& indices,
        const std::map<std::size_t,std::string>& target) {
        try {(void)stage_selected_online_fields(
            session,stage_original,indices,target);}
        catch(const std::invalid_argument&) {return true;}
        return false;
    };
    check(refuses_date_target({2},{{2,"GENRE"}}) &&
          refuses_date_target({2},{{2,"CUESHEET"}}) &&
          refuses_date_target({1},{{2,"DATE"}}) &&
          refuses_date_target({1},{{1,"RELEASEDATE"}}) &&
          refuses_date_target({2,1},{{2,"GENRE"}}),
          "invalid, unselected, nondate and conflicting target mappings are blocked");
    // Two independent official date meanings can be staged to two explicit
    // targets without changing DATE or flattening any original values.
    auto master_linked=approved_edition;
    master_linked.master_id="4343";
    const FieldEvidence genuine_master{"DATE",{"1998"},"discogs",
        "master:4343",EvidenceScope::Release,DateMeaning::OriginalRelease};
    const FieldEvidence newer_edition{"RELEASEDATE",{"2026-10-21"},"discogs",
        master_linked.edition_id,EvidenceScope::Edition,
        DateMeaning::EditionRelease};
    master_linked.fields.push_back(newer_edition);
    check(verified_discogs_master_year(master_linked,genuine_master) &&
          !verified_discogs_master_year(approved_edition,genuine_master),
          "master date requires exact edition-linked master provenance");
    const auto pair_snapshot=capture_online_field_review(
        stage_original,{genuine_master,newer_edition},true,true);
    const auto pair=discogs_dual_date_indices(master_linked,pair_snapshot.evidence);
    check(pair && pair->master_index==0 && pair->edition_index==1,
          "master date A and exact edition date B remain independent");
    auto master_row=pair_snapshot.comparisons[0];
    master_row.field="Discogs Master / DATE";
    master_row.reason="discogs_live_master_year_verified_"+master_row.reason;
    check(physical_discogs_master_field_index(
              master_linked,pair_snapshot.evidence,master_row) == 0,
          "verified no-op master field stays eligible for an explicit new target");
    const auto both=stage_selected_online_fields(
        pair_snapshot,stage_original,
        {pair->master_index,pair->edition_index},
        {{pair->master_index,"ORIGINALDATE"},
         {pair->edition_index,"RELEASEDATE"}});
    check(both.explicitly_changed==2 &&
          both.planned.fields[3]==stage_original.fields[3] &&
          both.planned.fields[both.planned.fields.size()-2]==
              MetadataField{"ORIGINALDATE",{"1998"}} &&
          both.planned.fields.back()==
              MetadataField{"RELEASEDATE",{"2026-10-21"}},
          "dual date preview maps master and edition to distinct tags without touching DATE");
    check(refuses_date_target({2},{{2,"GENRE"}}),
          "invalid date targets remain blocked after paired preview");
    auto forged_edition=newer_edition;
    forged_edition.values={"2025-12-31"};
    check(!discogs_dual_date_indices(
              master_linked,{genuine_master,forged_edition}),
          "a forged edition date absent from the exact release cannot be paired");
    auto forged_master=genuine_master;
    forged_master.source_id="master:9999";
    check(!verified_discogs_master_year(master_linked,forged_master) &&
          !discogs_dual_date_indices(
              master_linked,{forged_master,newer_edition}),
          "wrong Discogs master ID cannot enter date pairing");
    check(!discogs_dual_date_indices(master_linked,{newer_edition}) &&
          !discogs_dual_date_indices(master_linked,{genuine_master}),
          "missing master or edition date blocks atomic pairing");
    check(!discogs_dual_date_indices(master_linked,
              {genuine_master,genuine_master,newer_edition}),
          "conflicting duplicate master dates block atomic pairing");
    auto altered_original=stage_original;
    altered_original.fields[4].values.push_back("External edit");
    check(refuses(session,altered_original,{1}),
          "modifying even a non-selected custom tag invalidates staged review");
    auto forged=session;
    forged.comparisons[1].candidate.values={"Forged"};
    check(refuses(forged,stage_original,{1}),
          "modified review cannot launder a different proposed value");
    bool invalid_source=false;
    try {(void)capture_online_field_review(stage_original,stage_evidence,false,true);}
    catch(const std::invalid_argument&) {invalid_source=true;}
    check(invalid_source,"virtual CUE and unqualified physical sources are ineligible");
    invalid_source=false;
    try {(void)capture_online_field_review(stage_original,stage_evidence,true,false);}
    catch(const std::invalid_argument&) {invalid_source=true;}
    check(invalid_source,"unverified search snippet is ineligible for staging");
    std::cout << "PASS: online field provenance, duplicate-origin safety, bounds, no-op and review-only\\n";
}
