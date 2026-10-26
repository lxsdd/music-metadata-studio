// Fully synthetic, network-free Deezer -> Apple -> 75fps -> review ->
// physical-source snapshot -> separately approved external create-only path.
// The fake .mp3 bytes are not audio and never enter a production foobar host.
#include "djmeta/precision_cue_deezer.h"
#include "djmeta/precision_cue_apple.h"
#include "djmeta/precision_cue_assemble.h"
#include "djmeta/precision_cue_source_consensus.h"
#include "djmeta/precision_cue_boundary_audit.h"
#include "djmeta/precision_cue_reference_diff.h"
#include "djmeta/precision_cue_external_host_review.h"
#include "djmeta/precision_cue_external_inspect.h"
#include "djmeta/precision_cue_external_batch.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "PRECISION_CUE_E2E_FAIL line %d: %s\n", __LINE__, #x); \
    std::exit(1); } } while (false)

namespace {
namespace fs = std::filesystem;
using namespace djmeta;

std::string deezer_fixture() {
    std::string json =
        R"JSON({"id":123456,"title":"Synthetic Continuous Mix (DJ Mix)","artist":{"name":"Various Artists"},"nb_tracks":30,"tracks":{"data":[)JSON";
    for (unsigned i = 0; i < 30; ++i) {
        if (i) json += ",";
        json += "{\"id\":" + std::to_string(1000 + i) +
            ",\"title\":\"Mix Track " + std::to_string(i + 1) +
            " (Mixed)\",\"artist\":{\"name\":\"Synthetic DJ\"},\"disk_number\":" +
            std::to_string(i / 15 + 1) + ",\"track_position\":" +
            std::to_string(i % 15 + 1) + ",\"duration\":120}";
    }
    return json + R"(],"total":30}})";
}

std::string apple_fixture() {
    std::string json = R"JSON({"resultCount":31,"results":[{"wrapperType":"collection","collectionId":456789,"collectionName":"Synthetic Continuous Mix (DJ Mix)","artistName":"Various Artists","trackCount":30})JSON";
    for (unsigned i = 0; i < 30; ++i) {
        json += ",{\"wrapperType\":\"track\",\"kind\":\"song\",\"collectionId\":456789,"
            "\"collectionName\":\"Synthetic Continuous Mix (DJ Mix)\","
            "\"collectionArtistName\":\"Various Artists\",\"trackName\":\"Mix Track " +
            std::to_string(i + 1) +
            " (Mixed)\",\"artistName\":\"Synthetic DJ\",\"discNumber\":" +
            std::to_string(i / 15 + 1) +
            ",\"discCount\":2,\"trackNumber\":" + std::to_string(i % 15 + 1) +
            ",\"trackCount\":15,\"trackTimeMillis\":120000}";
    }
    return json + "]}";
}

struct SyntheticDirectory {
    fs::path path;
    SyntheticDirectory() {
        const auto base = fs::canonical(fs::temp_directory_path());
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            const auto candidate = base / ("precision-cue-synthetic-" +
                precision_cue_external_detail::random_suffix());
            std::error_code ec;
            if (fs::create_directory(candidate, ec)) {
                path = candidate;
                return;
            }
        }
        CHECK(false);
    }
    ~SyntheticDirectory() {
        if (!path.empty()) {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    }
};

void synthetic_file(const fs::path& path, char fill) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    CHECK(out.good());
    out << std::string(4096, fill);
    out.close();
    CHECK(out.good());
}

void replace_once(std::string& bytes, std::string_view before,
                  std::string_view after) {
    const auto at = bytes.find(before);
    CHECK(at != std::string::npos);
    bytes.replace(at, before.size(), after);
}

void reference_cue_timeline_regressions(const PrecisionCuePlan& plan) {
    using State = PrecisionCueReferenceDiffState;
    const auto& original = plan.external_cue_utf8;
    const std::string basename = "Disc 1.mp3";
    auto diff = compare_precision_cue_external_reference(plan, original, basename);
    CHECK(diff.state == State::ExactFramesPreviewOnly);
    CHECK(diff.audit.exact_count == 15);
    CHECK(diff.audit.within_one_frame_count == 0);
    CHECK(diff.audit.review_count == 0);

    // Different descriptive metadata is not a different INDEX timeline,
    // but it is NOT proof that two editions or audio masters coincide.
    auto metadata = original;
    replace_once(metadata, "Mix Track 1 (Mixed)", "Mix Track 1 (Other Edition)");
    diff = compare_precision_cue_external_reference(plan, metadata, basename);
    CHECK(diff.state == State::ExactFramesPreviewOnly);

    auto one_frame = original;
    replace_once(one_frame, "INDEX 01 02:00:00", "INDEX 01 02:00:01");
    diff = compare_precision_cue_external_reference(plan, one_frame, basename);
    CHECK(diff.state == State::WithinOneFramePreviewOnly);
    CHECK(diff.audit.exact_count == 14);
    CHECK(diff.audit.within_one_frame_count == 1);
    CHECK(diff.audit.differences[1].signed_delta_frames == -1);

    auto two_frames = original;
    replace_once(two_frames, "INDEX 01 02:00:00", "INDEX 01 02:00:02");
    diff = compare_precision_cue_external_reference(plan, two_frames, basename);
    CHECK(diff.state == State::ReviewRequired);
    CHECK(diff.audit.review_count == 1);

    auto pregap = original;
    replace_once(pregap, "    INDEX 01 02:00:00",
                 "    INDEX 00 01:59:70\n    INDEX 01 02:00:00");
    diff = compare_precision_cue_external_reference(plan, pregap, basename);
    CHECK(diff.state == State::ReviewRequired);
    CHECK(diff.extra_index_or_pregap);
    CHECK(diff.audit.exact_count == 15);

    auto wrong_name = original;
    replace_once(wrong_name, "Disc 1.mp3", "Disc 2.mp3");
    CHECK(compare_precision_cue_external_reference(plan, wrong_name, basename).state ==
          State::Unqualified);
    auto wrong_type = original;
    replace_once(wrong_type, "\"Disc 1.mp3\" WAVE", "\"Disc 1.mp3\" BINARY");
    CHECK(compare_precision_cue_external_reference(plan, wrong_type, basename).state ==
          State::Unqualified);

    auto duplicate_index = original;
    replace_once(duplicate_index, "    INDEX 01 02:00:00",
                 "    INDEX 01 02:00:00\n    INDEX 01 02:00:00");
    CHECK(compare_precision_cue_external_reference(plan, duplicate_index, basename).state ==
          State::Unqualified);
    auto bad_seconds = original;
    replace_once(bad_seconds, "INDEX 01 02:00:00", "INDEX 01 02:60:00");
    CHECK(compare_precision_cue_external_reference(plan, bad_seconds, basename).state ==
          State::Unqualified);
    auto bad_frames = original;
    replace_once(bad_frames, "INDEX 01 02:00:00", "INDEX 01 02:00:75");
    CHECK(compare_precision_cue_external_reference(plan, bad_frames, basename).state ==
          State::Unqualified);
    auto missing_index = original;
    replace_once(missing_index, "    INDEX 01 02:00:00\n", "");
    CHECK(compare_precision_cue_external_reference(plan, missing_index, basename).state ==
          State::Unqualified);
    auto extra_track = original + "  TRACK 16 AUDIO\n    INDEX 01 30:00:00\n";
    CHECK(compare_precision_cue_external_reference(plan, extra_track, basename).state ==
          State::Unqualified);
    auto second_file = original + "FILE \"other.mp3\" MP3\n";
    CHECK(compare_precision_cue_external_reference(plan, second_file, basename).state ==
          State::Unqualified);
    auto unknown_command = original + "  POSTPROCESS true\n";
    CHECK(compare_precision_cue_external_reference(plan, unknown_command, basename).state ==
          State::Unqualified);
    auto crlf = original;
    for (std::size_t offset = 0; offset < crlf.size(); ++offset)
        if (crlf[offset] == '\n') { crlf.insert(offset, 1, '\r'); ++offset; }
    CHECK(compare_precision_cue_external_reference(plan, crlf, basename).state ==
          State::ExactFramesPreviewOnly);
    const std::string bom = "\xef\xbb\xbf" + original;
    CHECK(compare_precision_cue_external_reference(plan, bom, basename).state ==
          State::ExactFramesPreviewOnly);
    CHECK(compare_precision_cue_external_reference(
        plan, original + std::string("\xff", 1), basename).state == State::Unqualified);
    CHECK(compare_precision_cue_external_reference(
        plan, std::string(1024 * 1024 + 1, 'X'), basename).state == State::Unqualified);
}

void batch_reviewed_external_cue_regressions(
    const std::vector<PrecisionCuePlan>& plans,
    const PrecisionCueRelease& edition) {
    using State = PrecisionCueExternalBatchState;
    using TargetState = PrecisionCueExistingTargetState;
    CHECK(plans.size() == 2);
    SyntheticDirectory temporary;
    const auto source_a = temporary.path / "Disc 1.mp3";
    const auto source_b = temporary.path / "Disc 2.mp3";
    synthetic_file(source_a, 'a');
    synthetic_file(source_b, 'b');
    const auto identity_a =
        precision_cue_external_detail::physical_source_snapshot(
            source_a, temporary.path, "Disc 1.mp3");
    const auto identity_b =
        precision_cue_external_detail::physical_source_snapshot(
            source_b, temporary.path, "Disc 2.mp3");
    CHECK(identity_a && identity_b);
    const auto review_a = capture_precision_cue_external_review(
        plans[0], source_a, "Disc 1.mp3", *identity_a);
    const auto review_b = capture_precision_cue_external_review(
        plans[1], source_b, "Disc 2.mp3", *identity_b);
    CHECK(review_a && review_b);
    const std::vector<PrecisionCueReviewedExternalCreate> reviewed{
        *review_a, *review_b
    };
    const auto cue_a = temporary.path / "Disc 1.cue";
    const auto cue_b = temporary.path / "Disc 2.cue";
    auto qualified = preflight_precision_cue_external_batch(reviewed, edition);
    CHECK(qualified.state == State::ReadyForConfirmation);
    CHECK(qualified.target_paths.size() == 2);
    CHECK(qualified.created_count == 0);
    CHECK(precision_cue_target_is_absent(cue_a) &&
          precision_cue_target_is_absent(cue_b));

    // Two independent filenames may be hardlinks of one physical MP3.
    // Never treat duplicate underlying file IDs as a valid two-disc source.
    const auto alias = temporary.path / "Disc 2 alias.mp3";
    std::error_code hardlink_ec;
    fs::create_hard_link(source_a, alias, hardlink_ec);
    if (!hardlink_ec) {
        const auto alias_identity =
            precision_cue_external_detail::physical_source_snapshot(
                alias, temporary.path, "Disc 2 alias.mp3");
        CHECK(alias_identity);
        CHECK(alias_identity->volume_id == identity_a->volume_id);
        CHECK(alias_identity->file_id == identity_a->file_id);
        auto alias_plan = plans[1];
        replace_once(alias_plan.external_cue_utf8,
            "FILE \"Disc 2.mp3\" WAVE", "FILE \"Disc 2 alias.mp3\" WAVE");
        const auto alias_review = capture_precision_cue_external_review(
            alias_plan, alias, "Disc 2 alias.mp3", *alias_identity);
        CHECK(alias_review);
        const auto duplicate = preflight_precision_cue_external_batch(
            {*review_a, *alias_review}, edition);
        CHECK(duplicate.state == State::RejectedBeforeCreate);
        CHECK(duplicate.diagnostic == "CUE_BATCH_DUPLICATE_PHYSICAL_SOURCE");
        CHECK(precision_cue_target_is_absent(cue_a));
        CHECK(precision_cue_target_is_absent(
            temporary.path / "Disc 2 alias.cue"));
    }

    // A valid preflight is never implicit batch authorization.
    auto forbidden = create_confirmed_precision_cue_external_batch(
        reviewed, edition, false);
    CHECK(forbidden.state == State::RejectedBeforeCreate);
    CHECK(forbidden.created_count == 0);
    CHECK(precision_cue_target_is_absent(cue_a) &&
          precision_cue_target_is_absent(cue_b));

    // No filename order or arbitrary vector permutation may silently
    // relabel a disc; duplicate disc references also fail closed.
    CHECK(preflight_precision_cue_external_batch(
        {*review_b, *review_a}, edition).state == State::RejectedBeforeCreate);
    CHECK(preflight_precision_cue_external_batch(
        {*review_a, *review_a}, edition).state == State::RejectedBeforeCreate);
    CHECK(preflight_precision_cue_external_batch(
        {*review_a}, edition).state == State::RejectedBeforeCreate);

    // Even if both plans have a valid physical source and matching disc
    // count, two album editions must never be grouped as one release.
    auto mixed_editions = reviewed;
    replace_once(mixed_editions[1].request.plan.external_cue_utf8,
        "Synthetic Continuous Mix (DJ Mix)",
        "Synthetic Continuous Mix (Another Edition)");
    mixed_editions[1].request.reviewed_cue_sha256 =
        precision_cue_sha256(mixed_editions[1].request.plan.external_cue_utf8);
    const auto edition_mismatch =
        preflight_precision_cue_external_batch(mixed_editions, edition);
    CHECK(edition_mismatch.state == State::RejectedBeforeCreate);
    CHECK(edition_mismatch.diagnostic == "CUE_BATCH_ALBUM_EDITION_CONFLICT");

    // Same album title and performer, same track count and timing, but a
    // different mixed track version on only disc two: all release positions
    // must be re-derived from ONE selected Apple album at the write gate.
    auto mismatched_tracks = reviewed;
    replace_once(mismatched_tracks[1].request.plan.external_cue_utf8,
        "Mix Track 16 (Mixed)", "Mix Track 16 (Original Mix)");
    mismatched_tracks[1].request.reviewed_cue_sha256 =
        precision_cue_sha256(
            mismatched_tracks[1].request.plan.external_cue_utf8);
    const auto track_conflict =
        preflight_precision_cue_external_batch(mismatched_tracks, edition);
    CHECK(track_conflict.state == State::RejectedBeforeCreate);
    CHECK(track_conflict.diagnostic == "CUE_BATCH_EDITION_TRACKLIST_CONFLICT");
    auto wrong_catalog = edition;
    wrong_catalog.tracks[16].duration_ms += 1;
    CHECK(preflight_precision_cue_external_batch(reviewed, wrong_catalog).state ==
          State::RejectedBeforeCreate);
    wrong_catalog = edition;
    wrong_catalog.provider = "deezer"; // Whole-second catalogs lack timing precision.
    CHECK(preflight_precision_cue_external_batch(reviewed, wrong_catalog).state ==
          State::RejectedBeforeCreate);
    CHECK(precision_cue_target_is_absent(cue_a) &&
          precision_cue_target_is_absent(cue_b));

    // A pre-existing DIFFERENT sibling on the second disc prevents
    // publication of the first disc: whole-batch preflight is mandatory.
    {
        std::ofstream existing(cue_b, std::ios::binary);
        CHECK(existing.good());
        existing << "existing second-disc CUE";
    }
    const auto blocked = create_confirmed_precision_cue_external_batch(
        reviewed, edition, true);
    CHECK(blocked.state == State::RejectedBeforeCreate);
    CHECK(blocked.created_count == 0);
    CHECK(precision_cue_target_is_absent(cue_a));
    CHECK(fs::exists(cue_b));
    fs::remove(cue_b);

    // Content-change with equal size/inode/mtime cannot be hidden by a
    // metadata-only source snapshot; revalidate full source digest.
    const auto old_mtime = fs::last_write_time(source_b);
    synthetic_file(source_b, 'c');
    std::error_code ec;
    fs::last_write_time(source_b, old_mtime, ec);
    CHECK(!ec);
    auto stale = create_confirmed_precision_cue_external_batch(reviewed, edition, true);
    CHECK(stale.state == State::RejectedBeforeCreate);
    CHECK(stale.created_count == 0);
    CHECK(precision_cue_target_is_absent(cue_a));
    CHECK(precision_cue_target_is_absent(cue_b));

    // Restore a fresh, separately reviewed source snapshot, then create
    // both external targets through their actual create-only backend.
    const auto fresh_b =
        precision_cue_external_detail::physical_source_snapshot(
            source_b, temporary.path, "Disc 2.mp3");
    CHECK(fresh_b);
    const auto rereview_b = capture_precision_cue_external_review(
        plans[1], source_b, "Disc 2.mp3", *fresh_b);
    CHECK(rereview_b);
    std::vector<PrecisionCueReviewedExternalCreate> renewed{
        *review_a, *rereview_b
    };
    CHECK(preflight_precision_cue_external_batch(renewed, edition).state ==
          State::ReadyForConfirmation);
    const auto complete = create_confirmed_precision_cue_external_batch(
        renewed, edition, true);
    CHECK(complete.state == State::CreatedAll);
    CHECK(complete.created_count == 2);
    CHECK(inspect_precision_cue_existing_target(
        plans[0], source_a, "Disc 1.mp3").state == TargetState::Identical);
    CHECK(inspect_precision_cue_existing_target(
        plans[1], source_b, "Disc 2.mp3").state == TargetState::Identical);

    // Second invocation must not overwrite either CUE, even if consent
    // is supplied again; exact target bytes remain unchanged.
    const auto again = create_confirmed_precision_cue_external_batch(
        renewed, edition, true);
    CHECK(again.state == State::RejectedBeforeCreate);
    CHECK(again.created_count == 0);
    {
        std::ifstream a(cue_a, std::ios::binary);
        std::ifstream b(cue_b, std::ios::binary);
        const std::string bytes_a(
            (std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
        const std::string bytes_b(
            (std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
        CHECK(bytes_a == plans[0].external_cue_utf8);
        CHECK(bytes_b == plans[1].external_cue_utf8);
    }
}

void cross_provider_release_and_cue() {
    const auto deezer = parse_deezer_precision_cue_album(
        deezer_fixture(), "123456");
    const auto apple = parse_apple_precision_cue_lookup(
        apple_fixture(), "456789");
    CHECK(deezer.tracks.size() == 30 && apple.tracks.size() == 30);
    CHECK(deezer.tracks[15].disc_number == 2 &&
          deezer.tracks[15].track_number == 1);
    CHECK(match_precision_cue_releases(deezer, {apple}).state ==
          PrecisionCueMatchState::Selected);
    const auto consensus = audit_precision_cue_source_consensus({
        {apple, 1}, {deezer, 1000}});
    CHECK(consensus.state == PrecisionCueConsensusState::SingleHighResolutionSource);
    CHECK(consensus.differences.size() == 30);

    PrecisionCuePreviewRequest request;
    request.reference = deezer;
    request.candidates = {apple};
    request.physical_discs = {{"Disc 1.mp3", 1800000}, {"Disc 2.mp3", 1800000}};
    // Equal-length discs cannot be mapped from filenames or durations.
    const auto ambiguous = make_precision_cue_release_preview(request);
    CHECK(ambiguous.state == PrecisionCueMatchState::Ambiguous);
    CHECK(ambiguous.discs.empty());
    request.reviewed_physical_index_by_disc = {0, 1};
    const auto reviewed = make_precision_cue_release_preview(request);
    CHECK(reviewed.state == PrecisionCueMatchState::Review);
    CHECK(!reviewed.preview_qualified && reviewed.discs.size() == 2);
    CHECK(reviewed.discs[0].boundaries.size() == 15);
    CHECK(reviewed.discs[1].boundaries.size() == 15);
    CHECK(reviewed.discs[0].boundaries.front().cue_frame == 0);
    CHECK(reviewed.discs[1].boundaries.front().cue_frame == 0);
    CHECK(reviewed.discs[1].external_cue_utf8.find(
        "FILE \"Disc 2.mp3\" WAVE") != std::string::npos);
    reference_cue_timeline_regressions(reviewed.discs[0]);
    batch_reviewed_external_cue_regressions(reviewed.discs, apple);

    std::vector<PrecisionCueReferenceBoundary> references;
    for (const auto& b : reviewed.discs[0].boundaries)
        references.push_back({b.track_number, b.cue_frame});
    const auto exact = review_precision_cue_edition_boundaries(
        deezer, {apple}, 0, 1, reviewed.discs[0], references);
    CHECK(exact.state == PrecisionCueEditionDiffState::ExactFramePreviewOnly);
    references[1].cue_frame++;
    const auto one = audit_precision_cue_boundaries(reviewed.discs[0], references);
    CHECK(one.state == PrecisionCueAuditState::WithinOneFrame);
    CHECK(one.within_one_frame_count == 1 && one.exact_count == 14);
    references[1].cue_frame++;
    CHECK(audit_precision_cue_boundaries(reviewed.discs[0], references).state ==
          PrecisionCueAuditState::Review);
    references[1].track_number = 99;
    CHECK(audit_precision_cue_boundaries(reviewed.discs[0], references).state ==
          PrecisionCueAuditState::Rejected);

    auto wrong_edition = apple;
    wrong_edition.tracks[14].title = "Mix Track 15 (Original Mix)";
    CHECK(match_precision_cue_releases(deezer, {wrong_edition}).state !=
          PrecisionCueMatchState::Selected);
    auto drift = apple;
    for (auto& track : drift.tracks) track.duration_ms += 999;
    const auto incompatible = audit_precision_cue_source_consensus({
        {drift, 1}, {deezer, 1000}});
    CHECK(incompatible.state == PrecisionCueConsensusState::Review);
    CHECK(incompatible.first_coarse_conflict.has_value());
    CHECK(incompatible.first_coarse_conflict->disc_number == 1);

    // Use only synthetic temporary bytes to exercise the real create-only
    // backend. No production audio, tags, embedded CUE or private files.
    SyntheticDirectory temporary;
    const auto physical = temporary.path / "Disc 1.mp3";
    synthetic_file(physical, 'A');
    const auto selected = precision_cue_external_detail::physical_source_snapshot(
        physical, temporary.path, "Disc 1.mp3");
    CHECK(selected.has_value());
    auto review = capture_precision_cue_external_review(
        reviewed.discs[0], physical, "Disc 1.mp3", *selected);
    CHECK(review.has_value());
    // Single-disc Create must now enforce the SAME full Apple edition,
    // exact canonical CUE and integer-ms / 75fps model as Create All.
    CHECK(validate_precision_cue_selected_disc(*review, apple, 0) == nullptr);
    CHECK(validate_precision_cue_selected_disc(*review, apple, 1) != nullptr);
    CHECK(validate_precision_cue_selected_disc(*review, deezer, 0) != nullptr);
    auto unrelated = apple;
    unrelated.tracks[1].title = "Mix Track 2 (Original Mix)";
    CHECK(validate_precision_cue_selected_disc(*review, unrelated, 0) != nullptr);
    unrelated = apple;
    unrelated.tracks[1].duration_ms += 1;
    CHECK(validate_precision_cue_selected_disc(*review, unrelated, 0) != nullptr);
    auto mutated_plan = *review;
    mutated_plan.request.plan.boundaries[1].cue_frame++;
    CHECK(validate_precision_cue_selected_disc(
        mutated_plan, apple, 0) != nullptr);
    CHECK(create_confirmed_precision_cue_selected_disc(
        *review, apple, 0).status == PrecisionCueExternalCreateStatus::Rejected);
    CHECK(create_confirmed_precision_cue_external(*review).status ==
          PrecisionCueExternalCreateStatus::Rejected);
    const auto before = inspect_precision_cue_existing_target(
        reviewed.discs[0], physical, "Disc 1.mp3");
    CHECK(before.state == PrecisionCueExistingTargetState::Absent);
    review->request.explicitly_approved_for_external_create = true;
    review->request.reviewed_preflight.explicit_user_approval = true;
    CHECK(create_confirmed_precision_cue_selected_disc(
        *review, unrelated, 0).status == PrecisionCueExternalCreateStatus::Rejected);
    CHECK(precision_cue_target_is_absent(temporary.path / "Disc 1.cue"));
    const auto created = create_confirmed_precision_cue_selected_disc(
        *review, apple, 0);
    CHECK(created.status == PrecisionCueExternalCreateStatus::Created);
    const auto after = inspect_precision_cue_existing_target(
        reviewed.discs[0], physical, "Disc 1.mp3");
    CHECK(after.state == PrecisionCueExistingTargetState::Identical);
    // An old reviewed snapshot must not overwrite an already published CUE.
    const auto repeated = create_confirmed_precision_cue_selected_disc(
        *review, apple, 0);
    CHECK(repeated.status != PrecisionCueExternalCreateStatus::Created);
    CHECK(inspect_precision_cue_existing_target(
        reviewed.discs[0], physical, "Disc 1.mp3").state ==
          PrecisionCueExistingTargetState::Identical);

    // A conflicting sibling reference is strictly read-only evidence. Even
    // frame-near equivalence must NEVER permit an overwrite or second create.
    auto different = reviewed.discs[0].external_cue_utf8;
    replace_once(different, "INDEX 01 02:00:00", "INDEX 01 02:00:01");
    const auto cue_path = temporary.path / "Disc 1.cue";
    {
        std::ofstream out(cue_path, std::ios::binary | std::ios::trunc);
        CHECK(out.good());
        out << different;
        out.close();
        CHECK(out.good());
    }
    const auto conflict = inspect_precision_cue_existing_target(
        reviewed.discs[0], physical, "Disc 1.mp3");
    CHECK(conflict.state == PrecisionCueExistingTargetState::Different);
    CHECK(conflict.observed_different_cue.has_value());
    CHECK(*conflict.observed_different_cue == different);
    const auto conflicting_timeline = compare_precision_cue_external_reference(
        reviewed.discs[0], *conflict.observed_different_cue, "Disc 1.mp3");
    CHECK(conflicting_timeline.state ==
          PrecisionCueReferenceDiffState::WithinOneFramePreviewOnly);
    CHECK(create_confirmed_precision_cue_selected_disc(
        *review, apple, 0).status != PrecisionCueExternalCreateStatus::Created);
    {
        std::ifstream in(cue_path, std::ios::binary);
        const std::string persisted((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
        CHECK(persisted == different);
    }

    const auto physical2 = temporary.path / "Disc 2.mp3";
    synthetic_file(physical2, 'B');
    const auto selected2 = precision_cue_external_detail::physical_source_snapshot(
        physical2, temporary.path, "Disc 2.mp3");
    CHECK(selected2.has_value());
    auto stale = capture_precision_cue_external_review(
        reviewed.discs[1], physical2, "Disc 2.mp3", *selected2);
    CHECK(stale.has_value());
    const auto previous_time = fs::last_write_time(physical2);
    synthetic_file(physical2, 'C'); // same length, same inode
    std::error_code ec;
    fs::last_write_time(physical2, previous_time, ec); // restore mtime
    CHECK(!ec);
    CHECK(!observe_precision_cue_external_review(*stale)
               .preflight.source_snapshot_current);
    stale->request.explicitly_approved_for_external_create = true;
    stale->request.reviewed_preflight.explicit_user_approval = true;
    CHECK(create_confirmed_precision_cue_external(*stale).status !=
          PrecisionCueExternalCreateStatus::Created);
    CHECK(precision_cue_target_is_absent(temporary.path / "Disc 2.cue"));
}
} // namespace

int main() {
    cross_provider_release_and_cue();
    return 0;
}
