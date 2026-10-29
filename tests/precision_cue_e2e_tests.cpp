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
#include "djmeta/precision_cue_studio_handoff.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <stdexcept>
#include <utility>
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
    CHECK(diff.track_structure.size() == 15);
    CHECK(diff.track_structure[1].track_number == 2);
    CHECK(!diff.track_structure[1].index_00_frame);
    CHECK(!diff.track_structure[1].pregap_frames);
    CHECK(!diff.track_structure[1].postgap_frames);

    // A matching 75-fps timeline with a different track TITLE is not an
    // exact-edition claim. Keep 15/15 frame matches but localize text REVIEW.
    auto metadata = original;
    replace_once(metadata, "Mix Track 1 (Mixed)", "Mix Track 1 (Other Edition)");
    diff = compare_precision_cue_external_reference(plan, metadata, basename);
    CHECK(diff.state == State::ReviewRequired);
    CHECK(diff.metadata_conflict && !diff.album_metadata_conflict);
    CHECK(diff.track_metadata_conflicts.size() == 15);
    CHECK(diff.track_metadata_conflicts[0] && !diff.track_metadata_conflicts[1]);
    CHECK(diff.audit.exact_count == 15);
    CHECK(std::string(diff.diagnostic) ==
          "CUE_REFERENCE_TEXT_METADATA_CONFLICT_REQUIRES_REVIEW");

    // Both the album header and individual track performers are advisory
    // reference text, never proof of the selected Apple edition or master.
    auto album_title = original;
    replace_once(album_title, "TITLE \"Synthetic Continuous Mix (DJ Mix)\"",
                 "TITLE \"Different Album Edition\"");
    diff = compare_precision_cue_external_reference(plan, album_title, basename);
    CHECK(diff.state == State::ReviewRequired);
    CHECK(diff.album_metadata_conflict && diff.metadata_conflict);
    CHECK(diff.audit.exact_count == 15);
    CHECK(!diff.track_metadata_conflicts[0]);
    auto album_artist = original;
    replace_once(album_artist, "PERFORMER \"Various Artists\"",
                 "PERFORMER \"Other Artists\"");
    diff = compare_precision_cue_external_reference(plan, album_artist, basename);
    CHECK(diff.state == State::ReviewRequired && diff.album_metadata_conflict);
    auto track_artist = original;
    replace_once(track_artist, "    PERFORMER \"Synthetic DJ\"",
                 "    PERFORMER \"Different DJ\"");
    diff = compare_precision_cue_external_reference(plan, track_artist, basename);
    CHECK(diff.state == State::ReviewRequired && !diff.album_metadata_conflict);
    CHECK(diff.track_metadata_conflicts[0] && !diff.track_metadata_conflicts[1]);

    // Reference metadata is optional: omission must not fabricate a
    // mismatch, and matching explicit metadata still proves no audio.
    auto sparse_reference = original;
    replace_once(sparse_reference, "    TITLE \"Mix Track 1 (Mixed)\"\n", "");
    diff = compare_precision_cue_external_reference(
        plan, sparse_reference, basename);
    CHECK(diff.state == State::ExactFramesPreviewOnly);
    CHECK(!diff.metadata_conflict);
    CHECK(!diff.album_metadata_conflict);
    CHECK(diff.track_metadata_conflicts.size() == 15);

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

    // A reference INDEX 01 may be late and still physically in-bounds:
    // preserve REVIEW, not a fabricated exact match or write permission.
    auto near_end = original;
    replace_once(near_end, "INDEX 01 28:00:00", "INDEX 01 29:59:74");
    diff = compare_precision_cue_external_reference(plan, near_end, basename);
    CHECK(diff.state == State::ReviewRequired);
    CHECK(diff.audit.review_count == 1);
    // At or beyond the actual 30-minute disc end is unqualified, even
    // though the timestamp is valid CUE syntax and the order is monotonic.
    for (const auto* outside : {"INDEX 01 30:00:00", "INDEX 01 30:00:01"}) {
        auto out_of_disc = original;
        replace_once(out_of_disc, "INDEX 01 28:00:00", outside);
        const auto rejected = compare_precision_cue_external_reference(
            plan, out_of_disc, basename);
        CHECK(rejected.state == State::Unqualified);
        CHECK(rejected.audit.differences.empty());
        CHECK(rejected.track_structure.empty());
        CHECK(std::string(rejected.diagnostic) ==
              "CUE_REFERENCE_INDEX_OUTSIDE_DISC_CLOCK");
    }

    auto pregap = original;
    replace_once(pregap, "    INDEX 01 02:00:00",
                 "    INDEX 00 01:59:70\n    INDEX 01 02:00:00");
    diff = compare_precision_cue_external_reference(plan, pregap, basename);
    CHECK(diff.state == State::ReviewRequired);
    CHECK(diff.extra_index_or_pregap);
    CHECK(diff.audit.exact_count == 15);
    CHECK(diff.track_structure.size() == 15);
    CHECK(diff.track_structure[1].index_00_frame == 8995);
    CHECK(diff.track_structure[1].index_00_to_01_frames == 5);
    CHECK(!diff.track_structure[0].index_00_frame);
    CHECK(!diff.track_structure[1].pregap_frames);

    // Valid structural gaps remain visible as disc-wide REVIEW, even with
    // identical INDEX 01 positions. Impossible or duplicate gaps fail closed.
    auto valid_pregap = original;
    replace_once(valid_pregap, "    INDEX 01 02:00:00",
                 "    PREGAP 00:00:05\n    INDEX 01 02:00:00");
    diff = compare_precision_cue_external_reference(plan, valid_pregap, basename);
    CHECK(diff.state == State::ReviewRequired && diff.extra_index_or_pregap);
    CHECK(diff.track_structure[1].pregap_frames == 5);
    CHECK(!diff.track_structure[1].index_00_frame);
    auto valid_postgap = original;
    replace_once(valid_postgap, "    INDEX 01 02:00:00",
                 "    INDEX 01 02:00:00\n    POSTGAP 00:00:05");
    diff = compare_precision_cue_external_reference(plan, valid_postgap, basename);
    CHECK(diff.state == State::ReviewRequired && diff.extra_index_or_pregap);
    CHECK(diff.track_structure[1].postgap_frames == 5);
    CHECK(!diff.track_structure[1].pregap_frames);

    // A syntactically valid virtual gap longer than the physical disc
    // cannot be treated as bounded structural review evidence.
    for (const auto* op : {"PREGAP", "POSTGAP"}) {
        auto oversized_gap = original;
        const std::string replacement =
            std::string("    INDEX 01 02:00:00\n    ") + op + " 31:00:00";
        replace_once(oversized_gap, "    INDEX 01 02:00:00", replacement);
        const auto rejected = compare_precision_cue_external_reference(
            plan, oversized_gap, basename);
        CHECK(rejected.state == State::Unqualified);
        CHECK(rejected.track_structure.empty());
        CHECK(std::string(rejected.diagnostic) ==
              "CUE_REFERENCE_GAP_OUTSIDE_DISC_CLOCK");
    }

    auto backward_index00 = original;
    replace_once(backward_index00, "    INDEX 01 02:00:00",
                 "    INDEX 00 02:00:01\n    INDEX 01 02:00:00");
    CHECK(compare_precision_cue_external_reference(
        plan, backward_index00, basename).state == State::Unqualified);
    auto overlap_previous = original;
    replace_once(overlap_previous, "    INDEX 01 04:00:00",
                 "    INDEX 00 01:59:70\n    INDEX 01 04:00:00");
    CHECK(compare_precision_cue_external_reference(
        plan, overlap_previous, basename).state == State::Unqualified);
    auto duplicate_pregap = original;
    replace_once(duplicate_pregap, "    INDEX 01 02:00:00",
                 "    PREGAP 00:00:05\n    PREGAP 00:00:05\n    INDEX 01 02:00:00");
    const auto rejected_duplicate = compare_precision_cue_external_reference(
        plan, duplicate_pregap, basename);
    CHECK(rejected_duplicate.state == State::Unqualified);
    CHECK(rejected_duplicate.track_structure.empty());
    auto pregap_after_index = original;
    replace_once(pregap_after_index, "    INDEX 01 02:00:00",
                 "    INDEX 01 02:00:00\n    PREGAP 00:00:05");
    CHECK(compare_precision_cue_external_reference(
        plan, pregap_after_index, basename).state == State::Unqualified);
    auto postgap_before_index = original;
    replace_once(postgap_before_index, "    INDEX 01 02:00:00",
                 "    POSTGAP 00:00:05\n    INDEX 01 02:00:00");
    CHECK(compare_precision_cue_external_reference(
        plan, postgap_before_index, basename).state == State::Unqualified);
    auto duplicate_postgap = original;
    replace_once(duplicate_postgap, "    INDEX 01 02:00:00",
                 "    INDEX 01 02:00:00\n    POSTGAP 00:00:05\n    POSTGAP 00:00:05");
    CHECK(compare_precision_cue_external_reference(
        plan, duplicate_postgap, basename).state == State::Unqualified);
    auto competing_gap = original;
    replace_once(competing_gap, "    INDEX 01 02:00:00",
                 "    INDEX 00 01:59:70\n    PREGAP 00:00:05\n    INDEX 01 02:00:00");
    CHECK(compare_precision_cue_external_reference(
        plan, competing_gap, basename).state == State::Unqualified);

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

// These tests use the actual create-only backend, not a mocked filesystem.
// The internal deterministic callback runs AFTER successful all-disc review
// and immediately BEFORE an individual writer, forcing the otherwise rare
// windows where external activity could make one disc stale.
void batch_midflight_external_race_regressions(
    const std::vector<PrecisionCuePlan>& plans,
    const PrecisionCueRelease& edition) {
    using State = PrecisionCueExternalBatchState;
    using Existing = PrecisionCueExistingTargetState;
    CHECK(plans.size() == 2);

    const auto scenario = [&](int mode) {
        SyntheticDirectory temporary;
        const auto a = temporary.path / "Disc 1.mp3";
        const auto b = temporary.path / "Disc 2.mp3";
        const auto cue_a = temporary.path / "Disc 1.cue";
        const auto cue_b = temporary.path / "Disc 2.cue";
        synthetic_file(a, 'A');
        synthetic_file(b, 'B');
        const auto ia = precision_cue_external_detail::
            physical_source_snapshot(a, temporary.path, "Disc 1.mp3");
        const auto ib = precision_cue_external_detail::
            physical_source_snapshot(b, temporary.path, "Disc 2.mp3");
        CHECK(ia && ib);
        const auto ra = capture_precision_cue_external_review(
            plans[0], a, "Disc 1.mp3", *ia);
        const auto rb = capture_precision_cue_external_review(
            plans[1], b, "Disc 2.mp3", *ib);
        CHECK(ra && rb);
        const std::vector<PrecisionCueReviewedExternalCreate> reviews{*ra, *rb};
        CHECK(preflight_precision_cue_external_batch(reviews, edition).state ==
              State::ReadyForConfirmation);

        const auto raced = precision_cue_batch_detail::
            create_confirmed_batch_with_probe(
                reviews, edition, true, [&](std::size_t index) {
                    if (mode == 1 && index == 1) {
                        const auto timestamp = fs::last_write_time(b);
                        synthetic_file(b, 'X'); // identical size, inode, restored time
                        std::error_code ec;
                        fs::last_write_time(b, timestamp, ec);
                        CHECK(!ec);
                    }
                    if (mode == 2 && index == 1) {
                        std::ofstream external(cue_b, std::ios::binary);
                        CHECK(external.good());
                        external << "another process created this .cue";
                    }
                    if (mode == 3 && index == 0) {
                        std::ofstream external(cue_a, std::ios::binary);
                        CHECK(external.good());
                        external << "other process created disc 1";
                    }
                    if (mode == 4 && index == 0)
                        throw std::runtime_error("injected before first writer");
                    if (mode == 5 && index == 1)
                        throw std::runtime_error("injected after first writer");
                });
        CHECK(raced.state == (mode == 3 || mode == 4
            ? State::RejectedBeforeCreate : State::PartialNeedsReview));
        CHECK(raced.created_count == (mode == 3 || mode == 4 ? 0u : 1u));
        CHECK(raced.failed_disc_index == (mode == 3 || mode == 4 ? 0u : 1u));
        CHECK(raced.target_paths.size() == 2);
        CHECK(raced.target_paths[0] == cue_a &&
              raced.target_paths[1] == cue_b);
        if (mode == 3 || mode == 4) {
            CHECK(inspect_precision_cue_existing_target(
                plans[1], b, "Disc 2.mp3").state == Existing::Absent);
            if (mode == 4) CHECK(precision_cue_target_is_absent(cue_a));
        } else {
            CHECK(inspect_precision_cue_existing_target(
                plans[0], a, "Disc 1.mp3").state == Existing::Identical);
        }
        if (mode == 1 || mode == 5)
            CHECK(precision_cue_target_is_absent(cue_b));
        if (mode == 2 || mode == 3) {
            const auto target = mode == 2 ? cue_b : cue_a;
            std::ifstream file(target, std::ios::binary);
            const std::string actual((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
            CHECK(actual == (mode == 2
                ? "another process created this .cue"
                : "other process created disc 1"));
        }
        // Stage must never be present after a rejected or failed publish.
        for (const auto& entry : fs::directory_iterator(temporary.path))
            CHECK(entry.path().filename().string().find(".precision-cue-") != 0);
    };
    for (int mode = 1; mode <= 5; ++mode) scenario(mode);
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
    request.physical_source_identity_by_selection =
        {"synthetic-physical-1", "synthetic-physical-2"};
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
    // The canonical Studio workspace can consume rows without acquiring a
    // second window/writer engine or treating a 75-fps match as audio proof.
    const std::vector<PrecisionCueStudioSourceView> studio_sources{
        {0, true, "synthetic-physical-1", 1800000},
        {0, true, "synthetic-physical-2", 1800000}};
    const std::vector<std::optional<std::string>> no_references(2);
    const auto studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references);
    CHECK(studio.state == PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(studio.rows.size() == 30);
    CHECK(studio.rows[0].measured_physical_disc_duration_ms == 1800000);
    CHECK(studio.rows[0].signed_physical_disc_duration_delta_ms == 0);
    CHECK(studio.rows[15].signed_physical_disc_duration_delta_ms == 0);
    // Even a +1 ms re-probe invalidates a cached physical snapshot despite
    // unchanged CUE text, 75-fps boundaries and canonical source identity.
    auto stale_duration_sources = studio_sources;
    stale_duration_sources[0].measured_duration_ms += 1;
    const auto stale_duration_studio = prepare_precision_cue_studio_review(
        request, reviewed, stale_duration_sources, no_references);
    CHECK(stale_duration_studio.state == PrecisionCueStudioHandoffState::Blocked);
    CHECK(stale_duration_studio.rows.empty());
    CHECK(std::string(stale_duration_studio.diagnostic) ==
          "STUDIO_PHYSICAL_DURATION_SNAPSHOT_CHANGED");
    stale_duration_sources[0].measured_duration_ms = 0;
    CHECK(prepare_precision_cue_studio_review(
        request, reviewed, stale_duration_sources, no_references).rows.empty());
    stale_duration_sources[0].measured_duration_ms = -1;
    CHECK(prepare_precision_cue_studio_review(
        request, reviewed, stale_duration_sources, no_references).rows.empty());
    // Unknown local duration stays explicitly unknown and review-only;
    // never fill it from Apple/Deezer catalog totals.
    auto unknown_duration_request = request;
    unknown_duration_request.physical_discs[0].measured_duration_ms = 0;
    const auto unknown_duration_preview =
        make_precision_cue_release_preview(unknown_duration_request);
    auto unknown_duration_sources = studio_sources;
    unknown_duration_sources[0].measured_duration_ms = 0;
    const auto unknown_duration_studio = prepare_precision_cue_studio_review(
        unknown_duration_request, unknown_duration_preview,
        unknown_duration_sources, no_references);
    CHECK(unknown_duration_studio.state ==
          PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(unknown_duration_studio.rows.size() == 30);
    CHECK(unknown_duration_studio.rows[0].measured_physical_disc_duration_ms == 0);
    CHECK(!unknown_duration_studio.rows[0].signed_physical_disc_duration_delta_ms);
    CHECK(unknown_duration_studio.rows[15].signed_physical_disc_duration_delta_ms == 0);
    // A reviewed permutation must attach each measured duration to the
    // actual physical selection, not to the displayed release disc order.
    auto reordered_request = request;
    reordered_request.reviewed_physical_index_by_disc = {1, 0};
    reordered_request.physical_discs[1].measured_duration_ms += 12;
    const auto reordered_preview =
        make_precision_cue_release_preview(reordered_request);
    auto reordered_sources = studio_sources;
    reordered_sources[1].measured_duration_ms += 12;
    const auto reordered_studio = prepare_precision_cue_studio_review(
        reordered_request, reordered_preview, reordered_sources, no_references);
    CHECK(reordered_studio.rows.size() == 30);
    CHECK(reordered_studio.rows[0].physical_selection_index == 1);
    CHECK(reordered_studio.rows[0].measured_physical_disc_duration_ms == 1800012);
    CHECK(reordered_studio.rows[0].signed_physical_disc_duration_delta_ms == 12);
    CHECK(reordered_studio.rows[15].physical_selection_index == 0);
    CHECK(reordered_studio.rows[15].signed_physical_disc_duration_delta_ms == 0);
    CHECK(studio.header_provenance.genre ==
          PrecisionCueHeaderFieldOrigin::SelectedCatalog ||
          studio.header_provenance.genre == PrecisionCueHeaderFieldOrigin::Absent);
    // Conflicting sourced release genres must stop at the strict matcher
    // and independently at the Studio handoff, even if the full 30-track
    // two-disc CUE timeline is otherwise unchanged.
    auto conflicting_genre_request = request;
    conflicting_genre_request.reference.genre = "Synthetic Reference Genre";
    conflicting_genre_request.candidates[0].genre = "Synthetic Catalog Genre";
    CHECK(match_precision_cue_releases(
        conflicting_genre_request.reference,
        conflicting_genre_request.candidates).state == PrecisionCueMatchState::Review);
    const auto conflicting_genre_preview =
        make_precision_cue_release_preview(conflicting_genre_request);
    CHECK(!conflicting_genre_preview.preview_qualified);
    const auto conflicting_genre_studio = prepare_precision_cue_studio_review(
        conflicting_genre_request, conflicting_genre_preview,
        studio_sources, no_references);
    CHECK(conflicting_genre_studio.state == PrecisionCueStudioHandoffState::Blocked);
    CHECK(conflicting_genre_studio.rows.empty());
    // An unqualified preview has no physical-source snapshot: the Studio
    // handoff fails at that earlier independent identity gate, before rows.
    CHECK(conflicting_genre_preview.discs.empty());
    CHECK(std::string(conflicting_genre_studio.diagnostic) ==
          "STUDIO_PHYSICAL_SOURCE_SNAPSHOT_CHANGED");
    // The same known genre, normalized for ASCII case and whitespace, must
    // preserve the existing 30-track read-only Studio review path.
    auto matching_genre_request = request;
    matching_genre_request.reference.genre = "  ELECTRONIC  ";
    matching_genre_request.candidates[0].genre = "Electronic";
    const auto matching_genre_preview =
        make_precision_cue_release_preview(matching_genre_request);
    CHECK(matching_genre_preview.discs.size() == 2);
    const auto matching_genre_studio = prepare_precision_cue_studio_review(
        matching_genre_request, matching_genre_preview,
        studio_sources, no_references);
    CHECK(matching_genre_studio.rows.size() == 30);
    CHECK(matching_genre_studio.state ==
          PrecisionCueStudioHandoffState::RowsRequireReview);
    // A year-only Deezer date can match a full Apple date without
    // inventing a day. The 30-track/two-disc handoff remains REVIEW.
    auto partial_catalog_date = request;
    partial_catalog_date.reference.release_date = "2026";
    partial_catalog_date.candidates[0].release_date = "2026-10-16";
    CHECK(match_precision_cue_releases(
        partial_catalog_date.reference, partial_catalog_date.candidates).state ==
        PrecisionCueMatchState::Selected);
    const auto partial_catalog_preview =
        make_precision_cue_release_preview(partial_catalog_date);
    CHECK(partial_catalog_preview.discs.size() == 2);
    CHECK(partial_catalog_preview.state == PrecisionCueMatchState::Review);
    CHECK(!partial_catalog_preview.preview_qualified);
    CHECK(std::find(partial_catalog_preview.diagnostics.begin(),
                    partial_catalog_preview.diagnostics.end(),
                    "CUE_RELEASE_DATE_PRECISION_REVIEW") !=
          partial_catalog_preview.diagnostics.end());
    const auto partial_catalog_studio = prepare_precision_cue_studio_review(
        partial_catalog_date, partial_catalog_preview, studio_sources, no_references);
    CHECK(partial_catalog_studio.rows.size() == 30);
    CHECK(partial_catalog_studio.state == PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(partial_catalog_studio.catalog_consensus_state ==
          PrecisionCueConsensusState::Review);
    CHECK(std::string(partial_catalog_studio.catalog_diagnostic) ==
          "STUDIO_RELEASE_DATE_PRECISION_REQUIRES_REVIEW");
    CHECK(partial_catalog_studio.catalog_release_date_precision_review);
    // A cached read-only preview with altered warning evidence is stale even
    // if its 30 track starts, two CUE texts and physical IDs are unchanged.
    auto missing_warning_preview = partial_catalog_preview;
    missing_warning_preview.diagnostics.clear();
    const auto missing_warning_review = prepare_precision_cue_studio_review(
        partial_catalog_date, missing_warning_preview, studio_sources, no_references);
    CHECK(missing_warning_review.state == PrecisionCueStudioHandoffState::Blocked);
    CHECK(missing_warning_review.rows.empty());
    CHECK(std::string(missing_warning_review.diagnostic) ==
          "STUDIO_PREVIEW_CHANGED_SINCE_REVIEW");
    auto injected_warning_preview = partial_catalog_preview;
    injected_warning_preview.diagnostics.push_back("UNATTESTED_UI_WARNING");
    CHECK(prepare_precision_cue_studio_review(
        partial_catalog_date, injected_warning_preview,
        studio_sources, no_references).rows.empty());
    auto altered_normal_preview = reviewed;
    altered_normal_preview.diagnostics.push_back("UNATTESTED_UI_WARNING");
    CHECK(prepare_precision_cue_studio_review(
        request, altered_normal_preview, studio_sources, no_references).rows.empty());
    // The native review gate accepts only date-precision uncertainty; it
    // refuses any concurrent Deezer duration contradiction, even when the
    // source calendar fields remain compatible.
    const auto date_catalog = audit_precision_cue_source_consensus({
        {partial_catalog_date.candidates[0], 1},
        {partial_catalog_date.reference, 1000}
    });
    CHECK(classify_precision_cue_deezer_apple_preview(date_catalog) ==
          PrecisionCueDeezerApplePreviewGate::ReadOnlyDatePrecisionReview);
    auto bad_deezer_date = partial_catalog_date.reference;
    bad_deezer_date.tracks[0].duration_ms += 4000;
    const auto contradictory_catalog = audit_precision_cue_source_consensus({
        {partial_catalog_date.candidates[0], 1}, {bad_deezer_date, 1000}
    });
    CHECK(contradictory_catalog.first_coarse_conflict.has_value());
    CHECK(contradictory_catalog.diagnostics.size() == 2);
    CHECK(classify_precision_cue_deezer_apple_preview(contradictory_catalog) ==
          PrecisionCueDeezerApplePreviewGate::Blocked);
    partial_catalog_date.reference.release_date = "2026-11";
    const auto conflicting_month_preview =
        make_precision_cue_release_preview(partial_catalog_date);
    CHECK(conflicting_month_preview.discs.empty());
    CHECK(prepare_precision_cue_studio_review(
        partial_catalog_date, conflicting_month_preview,
        studio_sources, no_references).rows.empty());
    // 30-track / two-disc date provenance: neither a forged provider date
    // nor an impossible manual REM DATE may reach native Studio review rows.
    auto impossible_catalog_date = request;
    impossible_catalog_date.reference.release_date = "2025-02-29";
    impossible_catalog_date.candidates[0].release_date = "2025-02-29";
    const auto impossible_catalog_preview =
        make_precision_cue_release_preview(impossible_catalog_date);
    CHECK(impossible_catalog_preview.state == PrecisionCueMatchState::Rejected);
    CHECK(impossible_catalog_preview.discs.empty());
    CHECK(prepare_precision_cue_studio_review(
        impossible_catalog_date, impossible_catalog_preview,
        studio_sources, no_references).rows.empty());
    auto impossible_manual_date = request;
    impossible_manual_date.release_date = "2026-04-31";
    const auto impossible_manual_preview =
        make_precision_cue_release_preview(impossible_manual_date);
    CHECK(impossible_manual_preview.state == PrecisionCueMatchState::Rejected);
    CHECK(impossible_manual_preview.discs.empty());
    CHECK(prepare_precision_cue_studio_review(
        impossible_manual_date, impossible_manual_preview,
        studio_sources, no_references).rows.empty());
    // The read-only Studio adapter must surface manually supplied header
    // metadata without relabelling it as an Apple catalog fact.
    auto manual_header_request = request;
    manual_header_request.genre = "Synthetic user-supplied genre";
    manual_header_request.release_date = "2025";
    const auto manual_header_preview =
        make_precision_cue_release_preview(manual_header_request);
    const auto manual_header_studio = prepare_precision_cue_studio_review(
        manual_header_request, manual_header_preview, studio_sources, no_references);
    CHECK(manual_header_studio.rows.size() == 30);
    CHECK(manual_header_studio.state == PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(manual_header_studio.header_provenance.genre ==
          (apple.genre.empty() ? PrecisionCueHeaderFieldOrigin::ManualUnverified
                               : PrecisionCueHeaderFieldOrigin::ConflictsWithSelectedCatalog));
    CHECK(manual_header_studio.header_provenance.release_date ==
          (apple.release_date.empty() ? PrecisionCueHeaderFieldOrigin::ManualUnverified
                                      : PrecisionCueHeaderFieldOrigin::ConflictsWithSelectedCatalog));
    auto forged_header_preview = manual_header_preview;
    forged_header_preview.header_provenance.genre = PrecisionCueHeaderFieldOrigin::SelectedCatalog;
    CHECK(prepare_precision_cue_studio_review(
        manual_header_request, forged_header_preview, studio_sources, no_references).rows.empty());
    CHECK(!studio.rows[1].reference_track_structure);
    // An INDEX 00 belongs to track 2, not all 15 physical rows. The
    // complete disc still requires review; disc 2 is unaffected.
    auto structured_reference = reviewed.discs[0].external_cue_utf8;
    replace_once(structured_reference, "    INDEX 01 02:00:00",
                 "    INDEX 00 01:59:70\n    INDEX 01 02:00:00");
    auto reference_selection = no_references;
    reference_selection[0] = structured_reference;
    const auto structured_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, reference_selection);
    CHECK(structured_studio.rows.size() == 30);
    CHECK(structured_studio.rows[1].reference_track_structure.has_value());
    CHECK(structured_studio.rows[1].reference_track_structure->track_number == 2);
    CHECK(structured_studio.rows[1].reference_track_structure->index_00_to_01_frames == 5);
    CHECK(!structured_studio.rows[0].reference_track_structure->index_00_frame);
    CHECK(structured_studio.rows[0].reference_has_extra_index_or_pregap);
    CHECK(structured_studio.rows[0].reference_state ==
          PrecisionCueStudioReferenceState::ReviewRequired);
    CHECK(!structured_studio.rows[15].reference_track_structure);
    CHECK(!structured_studio.rows[15].reference_has_extra_index_or_pregap);

    // A second-edition track TITLE in an otherwise frame-identical external
    // CUE is localized to its physical Studio row. No other track/disc
    // receives a fabricated discrepancy or a writer authorization.
    auto text_reference = reviewed.discs[0].external_cue_utf8;
    replace_once(text_reference, "Mix Track 2 (Mixed)",
                 "Mix Track 2 (Different Edition)");
    auto text_selection = no_references;
    text_selection[0] = text_reference;
    const auto text_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, text_selection);
    CHECK(text_studio.state == PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(text_studio.rows.size() == 30);
    CHECK(!text_studio.rows[0].reference_track_metadata_conflict);
    CHECK(text_studio.rows[0].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);
    CHECK(text_studio.rows[1].reference_track_metadata_conflict);
    CHECK(!text_studio.rows[1].reference_album_metadata_conflict);
    CHECK(text_studio.rows[1].reference_state ==
          PrecisionCueStudioReferenceState::ReviewRequired);
    CHECK(text_studio.rows[1].signed_reference_delta_frames == 0);
    CHECK(!text_studio.rows[2].reference_track_metadata_conflict);
    CHECK(text_studio.rows[2].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);
    CHECK(text_studio.rows[15].reference_state ==
          PrecisionCueStudioReferenceState::NotProvided);

    // A conflicting album TITLE applies to the entire referenced disc,
    // while the other selected physical disc remains untouched.
    auto album_text_reference = reviewed.discs[0].external_cue_utf8;
    replace_once(album_text_reference, "TITLE \"Synthetic Continuous Mix (DJ Mix)\"",
                 "TITLE \"Different Catalog Edition\"");
    text_selection[0] = album_text_reference;
    const auto album_text_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, text_selection);
    CHECK(album_text_studio.rows.size() == 30);
    CHECK(album_text_studio.rows[0].reference_album_metadata_conflict);
    CHECK(album_text_studio.rows[14].reference_album_metadata_conflict);
    CHECK(album_text_studio.rows[14].reference_state ==
          PrecisionCueStudioReferenceState::ReviewRequired);
    CHECK(!album_text_studio.rows[15].reference_album_metadata_conflict);
    CHECK(album_text_studio.rows[15].reference_state ==
          PrecisionCueStudioReferenceState::NotProvided);

    // Optional Spotify/Amazon evidence must appear in the existing Studio
    // handoff, not another foobar workspace or a new write authorization.
    auto spotify_release = apple;
    spotify_release.provider = "spotify";
    spotify_release.provider_release_id = "offline-synthetic-spotify";
    const PrecisionCueOptionalSource spotify_optional{
        spotify_release, PrecisionCueOptionalFormat::SpotifyWebMilliseconds, true};
    auto optional_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references, {spotify_optional});
    CHECK(optional_studio.state == PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(optional_studio.rows.size() == 30);
    CHECK(optional_studio.optional_provider_evidence.size() == 1);
    CHECK(optional_studio.optional_provider_evidence[0].comparison.differences.size() == 30);
    CHECK(optional_studio.optional_provider_evidence[0].comparison_resolution_ms == 1);
    CHECK(optional_studio.rows[0].optional_track_evidence.size() == 1);
    CHECK(optional_studio.rows[0].optional_track_evidence[0].provider == "spotify");
    CHECK(optional_studio.rows[0].optional_track_evidence[0].provider_release_id ==
          spotify_release.provider_release_id);
    CHECK(optional_studio.rows[0].optional_track_evidence[0].reported_field_unit_ms == 1);
    CHECK(optional_studio.rows[15].optional_track_evidence[0].signed_start_cumulative_delta_ms == 0);
    CHECK(optional_studio.rows[0].optional_track_evidence[0].signed_start_frame_delta == 0);
    CHECK(optional_studio.rows[15].optional_track_evidence[0].signed_start_frame_delta == 0);
    using OptionalStart = PrecisionCueStudioOptionalStartState;
    CHECK(optional_studio.rows[0].optional_track_evidence[0].start_state ==
          OptionalStart::ExactFrameAdvisoryNotAudioProof);
    CHECK(optional_studio.rows[15].optional_track_evidence[0].start_state ==
          OptionalStart::ExactFrameAdvisoryNotAudioProof);
    auto shifted_spotify = spotify_optional;
    shifted_spotify.release.tracks[0].duration_ms += 14;
    shifted_spotify.release.tracks[1].duration_ms -= 14;
    const auto shifted_optional_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references, {shifted_spotify});
    CHECK(shifted_optional_studio.state == PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(shifted_optional_studio.rows.size() == 30);
    CHECK(shifted_optional_studio.rows[1].optional_track_evidence[0].
          signed_start_cumulative_delta_ms == 14);
    CHECK(shifted_optional_studio.rows[2].optional_track_evidence[0].
          signed_start_cumulative_delta_ms == 0);
    CHECK(shifted_optional_studio.rows[15].optional_track_evidence[0].
          signed_start_cumulative_delta_ms == 0);
    CHECK(shifted_optional_studio.rows[0].optional_track_evidence[0].
          signed_start_frame_delta == 0);
    CHECK(shifted_optional_studio.rows[1].optional_track_evidence[0].
          signed_start_frame_delta == 1);
    CHECK(shifted_optional_studio.rows[2].optional_track_evidence[0].
          signed_start_frame_delta == 0);
    CHECK(shifted_optional_studio.rows[15].optional_track_evidence[0].
          signed_start_frame_delta == 0);
    CHECK(shifted_optional_studio.rows[1].optional_track_evidence[0].start_state ==
          OptionalStart::WithinOneFrameReview);
    CHECK(shifted_optional_studio.rows[2].optional_track_evidence[0].start_state ==
          OptionalStart::ExactFrameAdvisoryNotAudioProof);
    auto changed_last_track = spotify_optional;
    changed_last_track.release.tracks[14].duration_ms += 14;
    const auto disc_tail_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references, {changed_last_track});
    CHECK(disc_tail_studio.state == PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(disc_tail_studio.rows.size() == 30);
    CHECK(disc_tail_studio.optional_provider_evidence[0].comparison.diagnostics[0] ==
          "DISC_TAIL_DURATION_DISAGREEMENT_REVIEW");
    CHECK(disc_tail_studio.rows[14].optional_track_evidence[0].
          disc_tail_duration_disagreement);
    CHECK(disc_tail_studio.rows[14].optional_track_evidence[0].start_state ==
          OptionalStart::ExactFrameAdvisoryNotAudioProof);
    CHECK(disc_tail_studio.rows[15].optional_track_evidence[0].start_state ==
          OptionalStart::ExactFrameAdvisoryNotAudioProof);
    CHECK(!disc_tail_studio.rows[15].optional_track_evidence[0].
          disc_tail_duration_disagreement);
    // A +1-ms final-track change does not move the CUE frame, but must
    // remain a separately visible disc-tail duration REVIEW.
    changed_last_track.release.tracks[14].duration_ms -= 13;
    const auto subframe_tail_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references, {changed_last_track});
    CHECK(subframe_tail_studio.rows.size() == 30);
    CHECK(subframe_tail_studio.optional_provider_evidence[0].comparison.state ==
          PrecisionCueConsensusState::Review);
    CHECK(subframe_tail_studio.optional_provider_evidence[0].comparison.diagnostics[0] ==
          "DISC_TAIL_DURATION_DISAGREEMENT_REVIEW");
    CHECK(subframe_tail_studio.rows[14].optional_track_evidence[0].
          disc_tail_duration_disagreement);
    CHECK(subframe_tail_studio.rows[14].optional_track_evidence[0].
          same_frame_duration_disagreement);
    CHECK(subframe_tail_studio.rows[14].optional_track_evidence[0].
          signed_end_frame_delta == 0);
    CHECK(subframe_tail_studio.rows[14].optional_track_evidence[0].start_state ==
          OptionalStart::ExactFrameAdvisoryNotAudioProof);
    CHECK(!subframe_tail_studio.rows[15].optional_track_evidence[0].
          disc_tail_duration_disagreement);
    // +1/-1 ms can cancel within a disc while both INDEX 01 and end
    // frame deltas stay zero; expose both affected physical track rows.
    auto compensated_spotify = spotify_optional;
    compensated_spotify.release.tracks[0].duration_ms += 1;
    compensated_spotify.release.tracks[1].duration_ms -= 1;
    const auto compensated_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references, {compensated_spotify});
    CHECK(compensated_studio.rows.size() == 30);
    CHECK(compensated_studio.optional_provider_evidence[0].comparison.state ==
          PrecisionCueConsensusState::Review);
    CHECK(compensated_studio.optional_provider_evidence[0].comparison.diagnostics[0] ==
          "SUBFRAME_TRACK_DURATION_DISAGREEMENT_REVIEW");
    CHECK(compensated_studio.rows[0].optional_track_evidence[0].
          same_frame_duration_disagreement);
    CHECK(compensated_studio.rows[1].optional_track_evidence[0].
          same_frame_duration_disagreement);
    CHECK(!compensated_studio.rows[15].optional_track_evidence[0].
          same_frame_duration_disagreement);
    CHECK(compensated_studio.rows[1].optional_track_evidence[0].
          signed_start_frame_delta == 0);
    auto amazon_release = apple;
    amazon_release.provider = "amazon_music";
    amazon_release.provider_release_id = "offline-synthetic-amazon";
    for (auto& track : amazon_release.tracks)
        track.duration_ms = (track.duration_ms / 1000) * 1000;
    const PrecisionCueOptionalSource amazon_optional{
        amazon_release, PrecisionCueOptionalFormat::AmazonWebSeconds, true};
    optional_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references,
        {spotify_optional, amazon_optional});
    CHECK(optional_studio.state == PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(optional_studio.rows.size() == 30);
    CHECK(optional_studio.optional_provider_evidence.size() == 2);
    CHECK(optional_studio.optional_provider_evidence[1].comparison_resolution_ms == 1000);
    CHECK(optional_studio.rows[0].optional_track_evidence.size() == 2);
    CHECK(optional_studio.rows[0].optional_track_evidence[1].provider == "amazon_music");
    CHECK(optional_studio.rows[0].optional_track_evidence[1].
          conservative_comparison_resolution_ms == 1000);
    CHECK(optional_studio.rows[15].optional_track_evidence[1].
          signed_start_cumulative_delta_ms == 0);
    CHECK(optional_studio.rows[0].optional_track_evidence[1].start_state ==
          OptionalStart::CoarseCatalogOnly);
    CHECK(optional_studio.rows[1].optional_track_evidence[1].start_state ==
          OptionalStart::CoarseCatalogOnly);
    auto amazon_device = amazon_optional;
    amazon_device.format = PrecisionCueOptionalFormat::AmazonDeviceApproximateMilliseconds;
    amazon_device.release = apple;
    amazon_device.release.provider = "amazon_music";
    amazon_device.release.provider_release_id = "offline-synthetic-amazon-device";
    const auto device_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references, {amazon_device});
    CHECK(device_studio.rows.size() == 30);
    CHECK(device_studio.rows[0].optional_track_evidence[0].reported_field_unit_ms == 1);
    CHECK(device_studio.rows[0].optional_track_evidence[0].signed_start_frame_delta == 0);
    CHECK(device_studio.rows[0].optional_track_evidence[0].start_state ==
          OptionalStart::CoarseCatalogOnly);
    CHECK(device_studio.optional_provider_evidence[0].comparison_resolution_ms == 1000);
    CHECK(device_studio.rows[0].optional_track_evidence[0].
          conservative_comparison_resolution_ms == 1000);
    auto spoofed = amazon_optional;
    spoofed.release.tracks[3].duration_ms += 7;
    CHECK(prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references,
        {spotify_optional, spoofed}).state == PrecisionCueStudioHandoffState::Blocked);
    spoofed = spotify_optional;
    spoofed.release.tracks[4].title = "Different Original Mix";
    CHECK(prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references,
        {spoofed}).state == PrecisionCueStudioHandoffState::Blocked);
    CHECK(prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, no_references,
        {spotify_optional, spotify_optional}).state == PrecisionCueStudioHandoffState::Blocked);

    CHECK(studio.catalog_consensus_state ==
          PrecisionCueConsensusState::SingleHighResolutionSource);
    CHECK(std::string(studio.catalog_diagnostic) ==
          "STUDIO_DEEZER_SECONDS_IDENTITY_ONLY_NOT_FRAME_PROOF");
    CHECK(!studio.first_coarse_conflict);
    CHECK(studio.rows[0].corroborating_provider == "deezer");
    CHECK(studio.rows[0].corroborating_release_id == deezer.provider_release_id);
    CHECK(studio.rows[0].corroborating_resolution_ms == 1000);
    CHECK(studio.rows[0].signed_corroborating_track_delta_ms ==
          consensus.differences[0].signed_delta_ms);
    CHECK(studio.rows[15].signed_corroborating_cumulative_delta_ms ==
          consensus.differences[15].signed_cumulative_delta_ms);
    CHECK(studio.rows[0].signed_corroborating_start_delta_ms == 0);
    CHECK(studio.rows[1].signed_corroborating_start_delta_ms ==
          consensus.differences[1].signed_start_cumulative_delta_ms);
    CHECK(studio.rows[15].signed_corroborating_start_delta_ms == 0);
    CHECK(studio.rows[16].signed_corroborating_start_delta_ms ==
          consensus.differences[16].signed_start_cumulative_delta_ms);
    CHECK(studio.rows[0].disc_number == 1 &&
          studio.rows[0].track_number == 1 &&
          studio.rows[0].cue_frame == 0 &&
          studio.rows[0].provider == "apple");
    CHECK(studio.rows[15].disc_number == 2 &&
          studio.rows[15].track_number == 1 &&
          studio.rows[15].cue_frame == 0 &&
          studio.rows[15].physical_selection_index == 1);
    CHECK(studio.rows[29].track_number == 15);
    CHECK(studio.rows[0].reference_state ==
          PrecisionCueStudioReferenceState::NotProvided);
    CHECK(!studio.rows[0].signed_reference_delta_frames);
    CHECK(studio.rows[0].signed_quantization_offset_us == 0);
    CHECK(studio.rows[15].signed_quantization_offset_us == 0);
    CHECK(!studio.rows[0].reference_has_extra_index_or_pregap);
    CHECK(studio.rows[0].optional_track_evidence.empty());

    // One Apple track starts 7 ms after a whole-second reference while the
    // physical disc total stays unchanged. Integer 75-fps rounding yields
    // 120013.333 ms, i.e. +6333 us: never describe this as audio proof.
    auto quantized_request = request;
    quantized_request.candidates[0].tracks[0].duration_ms += 7;
    quantized_request.candidates[0].tracks[1].duration_ms -= 7;
    const auto quantized_preview =
        make_precision_cue_release_preview(quantized_request);
    const auto quantized_studio = prepare_precision_cue_studio_review(
        quantized_request, quantized_preview, studio_sources, no_references);
    CHECK(quantized_studio.rows.size() == 30);
    CHECK(quantized_studio.rows[1].elapsed_ms == 120007);
    CHECK(quantized_studio.rows[1].cue_frame == 9001);
    CHECK(quantized_studio.rows[1].signed_quantization_offset_us == 6333);
    CHECK(quantized_studio.rows[2].signed_quantization_offset_us == 0);

    // A claimed Deezer catalog containing fractional milliseconds is not
    // legitimate whole-second evidence, even when the matcher still finds
    // the same release and the Apple CUE timeline is unchanged.
    auto fractional_deezer = request;
    fractional_deezer.reference.tracks[0].duration_ms += 1;
    const auto fractional_preview =
        make_precision_cue_release_preview(fractional_deezer);
    CHECK(fractional_preview.discs.size() == 2);
    const auto fractional_handoff = prepare_precision_cue_studio_review(
        fractional_deezer, fractional_preview, studio_sources, no_references);
    CHECK(fractional_handoff.state == PrecisionCueStudioHandoffState::Blocked);
    CHECK(fractional_handoff.rows.empty());
    CHECK(std::string(fractional_handoff.diagnostic) ==
          "STUDIO_DEEZER_CATALOG_EVIDENCE_UNQUALIFIED");

    // An unknown catalog provider is not silently presented as corroborated
    // Deezer evidence. The same Apple CUE may be reviewed but not approved.
    auto unknown_catalog = request;
    unknown_catalog.reference.provider = "unverified";
    const auto unknown_preview = make_precision_cue_release_preview(unknown_catalog);
    CHECK(unknown_preview.discs.size() == 2);
    const auto unknown_handoff = prepare_precision_cue_studio_review(
        unknown_catalog, unknown_preview, studio_sources, no_references);
    CHECK(unknown_handoff.state ==
          PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(unknown_handoff.rows.size() == 30);
    CHECK(unknown_handoff.catalog_consensus_state ==
          PrecisionCueConsensusState::Rejected);
    CHECK(std::string(unknown_handoff.catalog_diagnostic) ==
          "STUDIO_NO_QUALIFIED_DEEZER_CORROBORATION");
    CHECK(unknown_handoff.rows[0].corroborating_provider.empty());
    CHECK(!unknown_handoff.rows[0].signed_corroborating_track_delta_ms);

    // Independent reference text stays in memory. Exact-frame and one-frame
    // matches are explicitly labelled NOT AUDIO PROOF, never write consent.
    auto references_for_studio = no_references;
    references_for_studio[0] = reviewed.discs[0].external_cue_utf8;
    const auto exact_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, references_for_studio);
    CHECK(exact_studio.rows.size() == 30);
    CHECK(exact_studio.rows[1].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);
    CHECK(exact_studio.rows[1].signed_reference_delta_frames == 0);
    replace_once(*references_for_studio[0],
                 "INDEX 01 02:00:00", "INDEX 01 02:00:01");
    const auto frame_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, references_for_studio);
    CHECK(frame_studio.rows[1].reference_state ==
          PrecisionCueStudioReferenceState::WithinOneFrameNotAudioProof);
    CHECK(frame_studio.rows[1].signed_reference_delta_frames == -1);
    CHECK(frame_studio.rows[0].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);
    CHECK(frame_studio.rows[2].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);
    CHECK(!frame_studio.rows[1].reference_has_extra_index_or_pregap);

    // A two-frame error requires review only for the affected track; exact
    // neighbors stay exact, but the complete handoff remains review-only.
    replace_once(*references_for_studio[0],
                 "INDEX 01 02:00:01", "INDEX 01 02:00:03");
    const auto two_frame_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, references_for_studio);
    CHECK(two_frame_studio.state ==
          PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(two_frame_studio.rows[0].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);
    CHECK(two_frame_studio.rows[1].reference_state ==
          PrecisionCueStudioReferenceState::ReviewRequired);
    CHECK(two_frame_studio.rows[1].signed_reference_delta_frames == -3);
    CHECK(two_frame_studio.rows[2].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);

    // An INDEX 00 is a structural disc-level warning. All rows must stay
    // REVIEW, even though every INDEX 01 still agrees exactly.
    references_for_studio[0] = reviewed.discs[0].external_cue_utf8;
    replace_once(*references_for_studio[0], "    INDEX 01 02:00:00",
                 "    INDEX 00 01:59:70\n    INDEX 01 02:00:00");
    const auto gap_studio = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, references_for_studio);
    CHECK(gap_studio.rows.size() == 30);
    CHECK(gap_studio.state == PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(gap_studio.rows[0].reference_state ==
          PrecisionCueStudioReferenceState::ReviewRequired);
    CHECK(gap_studio.rows[1].reference_state ==
          PrecisionCueStudioReferenceState::ReviewRequired);
    CHECK(gap_studio.rows[1].reference_has_extra_index_or_pregap);
    CHECK(gap_studio.rows[1].signed_reference_delta_frames == 0);

    // A release-disc number is NOT a physical selection index. The CUE
    // reference travels with its selected MP3 through a reviewed permutation.
    auto swapped_request = request;
    swapped_request.physical_discs = {
        {"Disc 2.mp3", 1800000}, {"Disc 1.mp3", 1800000}};
    swapped_request.reviewed_physical_index_by_disc = {1, 0};
    swapped_request.physical_source_identity_by_selection =
        {"synthetic-physical-2", "synthetic-physical-1"};
    const std::vector<PrecisionCueStudioSourceView> swapped_sources{
        {0, true, "synthetic-physical-2", 1800000},
        {0, true, "synthetic-physical-1", 1800000}};
    const auto swapped_preview = make_precision_cue_release_preview(swapped_request);
    CHECK(swapped_preview.discs.size() == 2);
    CHECK(swapped_preview.physical_index_by_disc ==
          swapped_request.reviewed_physical_index_by_disc);
    std::vector<std::optional<std::string>> physical_references{
        swapped_preview.discs[1].external_cue_utf8,
        swapped_preview.discs[0].external_cue_utf8};
    const auto swapped_exact = prepare_precision_cue_studio_review(
        swapped_request, swapped_preview, swapped_sources, physical_references);
    CHECK(swapped_exact.rows.size() == 30);
    CHECK(swapped_exact.rows[0].physical_selection_index == 1);
    CHECK(swapped_exact.rows[15].physical_selection_index == 0);
    CHECK(swapped_exact.rows[1].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);
    CHECK(swapped_exact.rows[16].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);
    auto only_physical_disc_two = physical_references;
    only_physical_disc_two[1].reset();
    const auto one_sided = prepare_precision_cue_studio_review(
        swapped_request, swapped_preview, swapped_sources, only_physical_disc_two);
    CHECK(one_sided.rows.size() == 30);
    CHECK(one_sided.rows[1].reference_state ==
          PrecisionCueStudioReferenceState::NotProvided);
    CHECK(!one_sided.rows[1].signed_reference_delta_frames);
    CHECK(one_sided.rows[16].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);
    replace_once(*physical_references[0],
                 "INDEX 01 02:00:00", "INDEX 01 02:00:01");
    const auto swapped_one_frame = prepare_precision_cue_studio_review(
        swapped_request, swapped_preview, swapped_sources, physical_references);
    CHECK(swapped_one_frame.rows.size() == 30);
    CHECK(swapped_one_frame.rows[1].signed_reference_delta_frames == 0);
    CHECK(swapped_one_frame.rows[16].physical_selection_index == 0);
    CHECK(swapped_one_frame.rows[16].reference_state ==
          PrecisionCueStudioReferenceState::WithinOneFrameNotAudioProof);
    CHECK(swapped_one_frame.rows[16].signed_reference_delta_frames == -1);
    auto misordered_references = physical_references;
    std::swap(misordered_references[0], misordered_references[1]);
    const auto mismatched = prepare_precision_cue_studio_review(
        swapped_request, swapped_preview, swapped_sources, misordered_references);
    CHECK(mismatched.state == PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(mismatched.rows[0].reference_state ==
          PrecisionCueStudioReferenceState::Unqualified);
    CHECK(mismatched.rows[15].reference_state ==
          PrecisionCueStudioReferenceState::Unqualified);
    auto swapped_virtual = swapped_sources;
    swapped_virtual[0].subsong = 1;
    CHECK(prepare_precision_cue_studio_review(
        swapped_request, swapped_preview, swapped_virtual, physical_references).rows.empty());
    swapped_virtual = studio_sources;
    swapped_virtual[1].host_readonly_physical_probe_passed = false;
    CHECK(prepare_precision_cue_studio_review(
        swapped_request, swapped_preview, swapped_virtual, physical_references).rows.empty());
    CHECK(prepare_precision_cue_studio_review(
        swapped_request, reviewed, swapped_sources, physical_references).rows.empty());

    // Non-involutive three-disc cycle detects accidental inverse mapping.
    auto cycle_request = request;
    for (std::size_t i = 0; i < 30; ++i) {
        const auto disc = static_cast<unsigned>(i / 10 + 1);
        const auto track = static_cast<unsigned>(i % 10 + 1);
        cycle_request.reference.tracks[i].disc_number = disc;
        cycle_request.reference.tracks[i].track_number = track;
        cycle_request.candidates[0].tracks[i].disc_number = disc;
        cycle_request.candidates[0].tracks[i].track_number = track;
    }
    cycle_request.physical_discs = {
        {"Disc 2.mp3", 1200000}, {"Disc 3.mp3", 1200000},
        {"Disc 1.mp3", 1200000}};
    cycle_request.reviewed_physical_index_by_disc = {2, 0, 1};
    cycle_request.physical_source_identity_by_selection =
        {"synthetic-physical-2", "synthetic-physical-3", "synthetic-physical-1"};
    const auto cycle_preview = make_precision_cue_release_preview(cycle_request);
    CHECK(cycle_preview.discs.size() == 3);
    CHECK(cycle_preview.physical_index_by_disc ==
          cycle_request.reviewed_physical_index_by_disc);
    const std::vector<PrecisionCueStudioSourceView> cycle_sources{
        {0, true, "synthetic-physical-2", 1200000},
        {0, true, "synthetic-physical-3", 1200000},
        {0, true, "synthetic-physical-1", 1200000}};
    std::vector<std::optional<std::string>> cycle_references{
        cycle_preview.discs[1].external_cue_utf8,
        cycle_preview.discs[2].external_cue_utf8,
        cycle_preview.discs[0].external_cue_utf8};
    const auto cycle_exact = prepare_precision_cue_studio_review(
        cycle_request, cycle_preview, cycle_sources, cycle_references);
    CHECK(cycle_exact.rows.size() == 30);
    CHECK(cycle_exact.rows[0].physical_selection_index == 2);
    CHECK(cycle_exact.rows[10].physical_selection_index == 0);
    CHECK(cycle_exact.rows[20].physical_selection_index == 1);
    for (const auto& row : cycle_exact.rows)
        CHECK(row.reference_state ==
              PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);
    replace_once(*cycle_references[1],
                 "INDEX 01 02:00:00", "INDEX 01 02:00:01");
    const auto cycle_one_frame = prepare_precision_cue_studio_review(
        cycle_request, cycle_preview, cycle_sources, cycle_references);
    CHECK(cycle_one_frame.rows.size() == 30);
    CHECK(cycle_one_frame.rows[1].signed_reference_delta_frames == 0);
    CHECK(cycle_one_frame.rows[11].signed_reference_delta_frames == 0);
    CHECK(cycle_one_frame.rows[21].physical_selection_index == 1);
    CHECK(cycle_one_frame.rows[21].reference_state ==
          PrecisionCueStudioReferenceState::WithinOneFrameNotAudioProof);
    CHECK(cycle_one_frame.rows[21].signed_reference_delta_frames == -1);
    auto cycle_missing = cycle_references;
    cycle_missing[0].reset();
    const auto cycle_partial = prepare_precision_cue_studio_review(
        cycle_request, cycle_preview, cycle_sources, cycle_missing);
    CHECK(cycle_partial.rows.size() == 30);
    CHECK(cycle_partial.rows[11].reference_state ==
          PrecisionCueStudioReferenceState::NotProvided);
    CHECK(!cycle_partial.rows[11].signed_reference_delta_frames);
    CHECK(cycle_partial.rows[1].reference_state ==
          PrecisionCueStudioReferenceState::ExactFrameNotAudioProof);

    references_for_studio[0] = "not a CUE";
    const auto invalid_reference = prepare_precision_cue_studio_review(
        request, reviewed, studio_sources, references_for_studio);
    CHECK(invalid_reference.state ==
          PrecisionCueStudioHandoffState::RowsRequireReview);
    CHECK(invalid_reference.rows[0].reference_state ==
          PrecisionCueStudioReferenceState::Unqualified);
    CHECK(!invalid_reference.rows[0].signed_reference_delta_frames);

    // Stale, forged, virtual and incomplete selections must fail CLOSED,
    // with no partial row handoff and no hidden disc permutation.
    // Same names, durations, CUE bytes and disc permutation must not mask
    // a different physical file, an aliased selection or a stale snapshot.
    auto changed_host = studio_sources;
    changed_host[0].physical_source_identity = "different-physical-file";
    CHECK(prepare_precision_cue_studio_review(
        request, reviewed, changed_host, no_references).rows.empty());
    auto duplicate_host = studio_sources;
    duplicate_host[1].physical_source_identity =
        duplicate_host[0].physical_source_identity;
    CHECK(prepare_precision_cue_studio_review(
        request, reviewed, duplicate_host, no_references).rows.empty());
    auto missing_host = studio_sources;
    missing_host[0].physical_source_identity.clear();
    CHECK(prepare_precision_cue_studio_review(
        request, reviewed, missing_host, no_references).rows.empty());
    auto changed_request_identity = request;
    changed_request_identity.physical_source_identity_by_selection[0] =
        "different-physical-file";
    CHECK(prepare_precision_cue_studio_review(
        changed_request_identity, reviewed, studio_sources, no_references).rows.empty());
    auto changed_displayed_identity = reviewed;
    changed_displayed_identity.physical_source_identity_by_selection[0] =
        "different-physical-file";
    CHECK(prepare_precision_cue_studio_review(
        request, changed_displayed_identity, studio_sources, no_references).rows.empty());
    auto missing_request_identity = request;
    missing_request_identity.physical_source_identity_by_selection.clear();
    CHECK(prepare_precision_cue_studio_review(
        missing_request_identity, reviewed, studio_sources, no_references).rows.empty());
    auto virtual_sources = studio_sources;
    virtual_sources[0].subsong = 1;
    CHECK(prepare_precision_cue_studio_review(
        request, reviewed, virtual_sources, no_references).rows.empty());
    virtual_sources = studio_sources;
    virtual_sources[1].host_readonly_physical_probe_passed = false;
    CHECK(prepare_precision_cue_studio_review(
        request, reviewed, virtual_sources, no_references).rows.empty());
    auto stale_preview = reviewed;
    ++stale_preview.discs[0].boundaries[1].cue_frame;
    CHECK(prepare_precision_cue_studio_review(
        request, stale_preview, studio_sources, no_references).rows.empty());
    stale_preview = reviewed;
    stale_preview.physical_index_by_disc = {1, 0};
    CHECK(prepare_precision_cue_studio_review(
        request, stale_preview, studio_sources, no_references).rows.empty());
    stale_preview.physical_index_by_disc.clear();
    CHECK(prepare_precision_cue_studio_review(
        request, stale_preview, studio_sources, no_references).rows.empty());
    stale_preview = reviewed;
    stale_preview.discs[0].external_cue_utf8 += "REM UNKNOWN 1\n";
    CHECK(prepare_precision_cue_studio_review(
        request, stale_preview, studio_sources, no_references).rows.empty());
    auto remapped_request = request;
    remapped_request.reviewed_physical_index_by_disc = {1, 0};
    CHECK(prepare_precision_cue_studio_review(
        remapped_request, reviewed, studio_sources, no_references).rows.empty());
    remapped_request = request;
    remapped_request.reviewed_physical_index_by_disc = {0, 0};
    CHECK(prepare_precision_cue_studio_review(
        remapped_request, reviewed, studio_sources, no_references).rows.empty());
    remapped_request = request;
    remapped_request.reviewed_physical_index_by_disc.clear();
    CHECK(prepare_precision_cue_studio_review(
        remapped_request, reviewed, studio_sources, no_references).rows.empty());
    remapped_request = request;
    remapped_request.candidates[0].tracks[1].title =
        "Mix Track 2 (Original Mix)";
    CHECK(prepare_precision_cue_studio_review(
        remapped_request, reviewed, studio_sources, no_references).rows.empty());
    remapped_request = request;
    remapped_request.candidates[0].tracks[1].duration_ms += 1;
    CHECK(prepare_precision_cue_studio_review(
        remapped_request, reviewed, studio_sources, no_references).rows.empty());
    remapped_request = request;
    remapped_request.physical_discs[0].mp3_basename = "Wrong.mp3";
    CHECK(prepare_precision_cue_studio_review(
        remapped_request, reviewed, studio_sources, no_references).rows.empty());
    reference_cue_timeline_regressions(reviewed.discs[0]);
    batch_reviewed_external_cue_regressions(reviewed.discs, apple);
    batch_midflight_external_race_regressions(reviewed.discs, apple);

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
