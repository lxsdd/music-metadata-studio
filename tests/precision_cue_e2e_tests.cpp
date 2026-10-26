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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    CHECK(create_confirmed_precision_cue_external(*review).status ==
          PrecisionCueExternalCreateStatus::Rejected);
    const auto before = inspect_precision_cue_existing_target(
        reviewed.discs[0], physical, "Disc 1.mp3");
    CHECK(before.state == PrecisionCueExistingTargetState::Absent);
    review->request.explicitly_approved_for_external_create = true;
    review->request.reviewed_preflight.explicit_user_approval = true;
    const auto created = create_confirmed_precision_cue_external(*review);
    CHECK(created.status == PrecisionCueExternalCreateStatus::Created);
    const auto after = inspect_precision_cue_existing_target(
        reviewed.discs[0], physical, "Disc 1.mp3");
    CHECK(after.state == PrecisionCueExistingTargetState::Identical);
    // An old reviewed snapshot must not overwrite an already published CUE.
    const auto repeated = create_confirmed_precision_cue_external(*review);
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
    CHECK(create_confirmed_precision_cue_external(*review).status !=
          PrecisionCueExternalCreateStatus::Created);
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
