#include "djmeta/precision_cue_write_preflight.h"
#include "djmeta/precision_cue_external_create.h"
#include "djmeta/precision_cue_foobar_path.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace djmeta;

#define VERIFY(condition) do {                                                  \
    if (!(condition)) {                                                        \
        std::fprintf(stderr, "PRECISION_CUE_TEST_FAILURE line %d: %s\n",       \
                     __LINE__, #condition);                                    \
        std::fflush(stderr);                                                   \
        std::exit(1);                                                          \
    }                                                                          \
} while (false)

namespace {
PrecisionCuePlan synthetic_plan(std::string_view basename = "Disc Über 東京.mp3") {
    PrecisionCueDisc disc;
    disc.album_title = "Synthetic Mix – 東京";
    disc.album_performer = "Various Artists";
    disc.audio_file_basename = std::string(basename);
    disc.tracks = {
        {"Start (Mixed)", "DJ A", 100001},
        {"End (Mixed)", "DJ B", 200002},
    };
    return make_precision_cue(disc);
}

void export_planning_positive_and_negative() {
    const auto cue = synthetic_plan();
    const auto good = plan_precision_cue_external_export(cue, "Disc Über 東京.mp3");
    VERIFY(good.eligible && good.target_cue_basename == "Disc Über 東京.cue");
    VERIFY(precision_cue_export_targets_unique({good,
        plan_precision_cue_external_export(synthetic_plan("Disc 2.MP3"), "Disc 2.MP3")}));
    VERIFY(!precision_cue_export_targets_unique({good, good}));

    auto wrong = plan_precision_cue_external_export(cue, "Other.mp3");
    VERIFY(!wrong.eligible);
    wrong = plan_precision_cue_external_export(cue, "../escape.mp3");
    VERIFY(!wrong.eligible);
    wrong = plan_precision_cue_external_export(cue, "CON.mp3");
    VERIFY(!wrong.eligible);

    auto altered = cue;
    altered.external_cue_utf8 += "FILE \"unexpected.mp3\" WAVE\n";
    VERIFY(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.external_cue_utf8 = "FILE \"Disc Über 東京.mp3\" WAVE\n";
    VERIFY(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.external_cue_utf8[0] = static_cast<char>(0xff);
    VERIFY(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.external_cue_utf8 += "  TRACK 03 AUDIO\n";
    VERIFY(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.external_cue_utf8 += "    INDEX 01 03:20:00\n";
    VERIFY(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.external_cue_utf8 += "    INDEX 00 00:00:00\n";
    VERIFY(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    // Valid FILE inventories alone cannot protect a generated CUE from
    // syntactically plausible injected/reordered directives.
    const auto reject_changed_cue = [&](std::string_view before, std::string_view after) {
        auto changed = cue;
        const auto at = changed.external_cue_utf8.find(before);
        VERIFY(at != std::string::npos);
        changed.external_cue_utf8.replace(at, before.size(), after);
        VERIFY(!plan_precision_cue_external_export(changed, "Disc Über 東京.mp3").eligible);
    };
    reject_changed_cue("REM DISCNUMBER 1\n",
                       "REM DISCNUMBER 1\nREM COMMENT \"unexpected\"\n");
    reject_changed_cue("REM TOTALDISCS 1\n",
                       "REM TOTALDISCS 1\nTITLE \"injected\"\n");
    reject_changed_cue("  TRACK 01 AUDIO\n", "  TRACK 02 AUDIO\n");
    reject_changed_cue("  TRACK 01 AUDIO\n", "  TRACK 1 AUDIO\n");
    reject_changed_cue("    INDEX 01 00:00:00\n",
                       "    INDEX 01 00:00:00\n    INDEX 01 00:00:00\n");
    reject_changed_cue("  TRACK 02 AUDIO\n",
                       "  TRACK 02 AUDIO\n    REM COMMENT \"injected\"\n");
    reject_changed_cue("    TITLE \"Start (Mixed)\"\n",
                       "    TITLE \"Start (Mixed)\"\n    TITLE \"injected\"\n");
    reject_changed_cue("    TITLE \"Start (Mixed)\"\n",
                       "    TITLE \"Start (Mixed)\" INJECT\n");
    reject_changed_cue("REM DISCNUMBER 1\n", "REM DISCNUMBER 0\n");
    reject_changed_cue("REM DISCNUMBER 1\n", "REM DISCNUMBER 2\n");
    reject_changed_cue("REM TOTALDISCS 1\n", "REM TOTALDISCS 01\n");
    reject_changed_cue("    INDEX 01 00:00:00\n",
                       "    INDEX 01 00:00:00 \n");
    reject_changed_cue("REM DISCNUMBER 1\n",
                       "REM DATE \"2026\"\nREM GENRE \"Electronic\"\nREM DISCNUMBER 1\n");
    altered = cue;
    altered.boundaries[1].cue_frame++;
    VERIFY(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.boundaries[0].elapsed_ms = 1;
    VERIFY(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.total_duration_ms = altered.boundaries.back().elapsed_ms;
    VERIFY(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.boundaries.clear();
    VERIFY(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    VERIFY(!precision_cue_export_targets_unique({good,
        plan_precision_cue_external_export(cue, "Other.mp3")}));
}

void reserved_unicode_device_alias_regressions() {
    // Windows recognizes superscript 1/2/3 in COM/LPT device aliases.
    // Keep these tests independent of filesystem locale and real audio.
    const std::vector<std::string> reserved = {
        "COM¹.mp3", "com².MP3", "CoM³.mix.mp3",
        "LPT¹.mp3", "lpt².MP3", "LpT³.mix.mp3",
        "COM1.mp3", "lpt9.mp3", "CON .mp3"
    };
    for (const auto& basename : reserved) {
        VERIFY(!precision_cue_safe_mp3_basename(basename));
        PrecisionCueDisc disc;
        disc.album_title = "Synthetic";
        disc.album_performer = "Test";
        disc.audio_file_basename = basename;
        disc.tracks = {{"Synthetic track", "Test", 120000}};
        bool rejected = false;
        try { (void)make_precision_cue(disc); }
        catch (const std::invalid_argument&) { rejected = true; }
        VERIFY(rejected);
        VERIFY(!plan_precision_cue_external_export(
            synthetic_plan(), basename).eligible);
    }
    // A similar non-device stem remains valid; do not ban Unicode generally.
    VERIFY(precision_cue_safe_mp3_basename("Album COM¹ mix.mp3"));
    VERIFY(precision_cue_safe_mp3_basename("COM¹ remix.mp3"));
    VERIFY(precision_cue_safe_mp3_basename("Disc Über 東京.mp3"));
}

void export_generated_layout_positive_edge_cases() {
    PrecisionCueDisc disc;
    disc.album_title = "Synthetic 99-track disc";
    disc.album_performer = "DJ Example";
    disc.genre = "Electronic";
    disc.release_date = "2026-10-19";
    disc.audio_file_basename = "disc-99.mp3";
    for (unsigned i = 1; i <= 99; ++i)
        disc.tracks.push_back({"Synthetic " + std::to_string(i), "DJ", 1000});
    const auto plan = make_precision_cue(disc);
    VERIFY(plan.boundaries.size() == 99);
    VERIFY(plan_precision_cue_external_export(plan, disc.audio_file_basename).eligible);

    disc.tracks.resize(1);
    const auto single = make_precision_cue(disc);
    VERIFY(plan_precision_cue_external_export(single, disc.audio_file_basename).eligible);
}

PrecisionCueWritePreflight approved() {
    PrecisionCueWritePreflight p;
    p.preview_available = true;
    p.preview_qualified = true;
    p.release_identity_confirmed = true;
    p.disc_identity_confirmed = true;
    p.explicit_user_approval = true;
    p.source_snapshot_current = true;
    p.target_snapshot_current = true;
    p.path_qualified = true;
    p.cue_content_qualified = true;
    return p;
}

void preflight_fail_closed_matrix() {
    using enum PrecisionCueWriteDecision;
    const PrecisionCueWritePreflight blank;
    VERIFY(preflight_precision_cue_external_write(blank) == Reject);
    auto p = approved();
    VERIFY(preflight_precision_cue_external_write(p) == ReadyForExplicitWrite);

    p = approved(); p.preview_available = false;
    VERIFY(preflight_precision_cue_external_write(p) == Reject);
    p = approved(); p.path_qualified = false;
    VERIFY(preflight_precision_cue_external_write(p) == Reject);
    p = approved(); p.cue_content_qualified = false;
    VERIFY(preflight_precision_cue_external_write(p) == Reject);

    p = approved(); p.preview_qualified = false;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);
    p.explicit_review_resolved = true;
    VERIFY(preflight_precision_cue_external_write(p) == ReadyForExplicitWrite);
    p.preview_available = false;
    VERIFY(preflight_precision_cue_external_write(p) == Reject);

    p = approved(); p.release_identity_confirmed = false;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);
    p = approved(); p.disc_identity_confirmed = false;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);
    p = approved(); p.source_snapshot_current = false;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);
    p = approved(); p.target_snapshot_current = false;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);
    p = approved(); p.explicit_user_approval = false;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);

    p = approved(); p.target_exists = true;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);
    p.existing_content_known = true;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);
    p.overwrite_approved = true;
    VERIFY(preflight_precision_cue_external_write(p) == ReadyForExplicitWrite);
    p.explicit_user_approval = false;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);
    p.existing_content_equal = true;
    VERIFY(preflight_precision_cue_external_write(p) == NoOp);
    p.source_snapshot_current = false;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);
    p.source_snapshot_current = true; p.target_snapshot_current = false;
    VERIFY(preflight_precision_cue_external_write(p) == ReviewRequired);
    p = approved(); p.existing_content_known = true;
    VERIFY(preflight_precision_cue_external_write(p) == Reject);
    p = approved(); p.existing_content_equal = true;
    VERIFY(preflight_precision_cue_external_write(p) == Reject);
    p = approved(); p.target_exists = true; p.existing_content_equal = true;
    VERIFY(preflight_precision_cue_external_write(p) == Reject);
}
}


void sha256_exact_byte_regressions() {
    VERIFY(precision_cue_sha256_hex("") ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    VERIFY(precision_cue_sha256_hex("abc") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    VERIFY(precision_cue_sha256_hex(std::string(64, 'a')) ==
           "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    VERIFY(precision_cue_sha256_hex(
           "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const auto cue = synthetic_plan().external_cue_utf8;
    VERIFY(precision_cue_sha256(cue) != precision_cue_sha256(cue + " "));
    auto nul_suffixed = cue;
    nul_suffixed.push_back('\0');
    VERIFY(precision_cue_sha256(cue) != precision_cue_sha256(nul_suffixed));
}

void commit_revalidation_fail_closed_matrix() {
    using enum PrecisionCueWriteDecision;
    PrecisionCueWriteCommitEvidence e;
    e.preview = approved();
    e.at_commit = approved();
    e.source_snapshot_token_preview = 101;
    e.source_snapshot_token_commit = 101;
    e.target_snapshot_token_preview = 202;
    e.target_snapshot_token_commit = 202;
    const auto reviewed = synthetic_plan().external_cue_utf8;
    e.reviewed_cue_sha256 = precision_cue_sha256(reviewed);
    e.at_commit_cue_sha256 = precision_cue_sha256(reviewed);
    VERIFY(revalidate_precision_cue_external_commit(e) == ReadyForExplicitWrite);
    e.at_commit_cue_sha256 = precision_cue_sha256(reviewed + " ");
    VERIFY(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.at_commit_cue_sha256 = e.reviewed_cue_sha256;
    e.reviewed_cue_sha256 = {};
    VERIFY(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.reviewed_cue_sha256 = precision_cue_sha256(reviewed);
    e.source_snapshot_token_commit = 103;
    VERIFY(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.source_snapshot_token_commit = 101;
    e.target_snapshot_token_commit = 203;
    VERIFY(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.target_snapshot_token_commit = 202;
    e.target_snapshot_token_preview = 0;
    VERIFY(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.target_snapshot_token_preview = 202;
    e.at_commit.target_exists = true;
    VERIFY(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.at_commit = approved();
    e.at_commit.explicit_user_approval = false;
    VERIFY(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.at_commit = approved();
    e.preview.source_snapshot_current = false;
    VERIFY(revalidate_precision_cue_external_commit(e) == ReviewRequired);
}


void foobar_local_windows_path_regressions() {
    const auto verify_path = [](std::string_view uri, std::string_view expected) {
        const auto result = precision_cue_foobar_local_windows_mp3_path(uri);
        VERIFY(result && *result == expected);
    };
    verify_path("file://C:\\Music\\Mix.mp3", "C:\\Music\\Mix.mp3");
    verify_path("file:///Z:/Music/Disc Über 東京.mp3",
                "Z:\\Music\\Disc Über 東京.mp3");
    verify_path("file://d:/Album/CD2.MP3", "d:\\Album\\CD2.MP3");
    for (const auto invalid : {
        "https://example.com/Mix.mp3", "file://server/share/Mix.mp3",
        "file:////server/share/Mix.mp3", "file://C:relative.mp3",
        "file:///C:/../Mix.mp3", "file://C:\\Folder\\.\\Mix.mp3",
        "file://C:\\Folder\\\\Mix.mp3", "file://C:\\Folder\\Mix.mp3:extra",
        "file://C:\\Folder\\COM1.mp3", "file://C:\\Folder\\Mix.cue",
        "file://C:\\Folder\\Mix.mp3?query",
        "file://C:\\Folder\\Mix.mp3\\"
    }) VERIFY(!precision_cue_foobar_local_windows_mp3_path(invalid));
}

void create_only_external_writer_regressions() {
    namespace fs = std::filesystem;
    using Status = PrecisionCueExternalCreateStatus;

    // Only synthetic CUE bytes are written, inside a disposable test-owned
    // directory. No host, physical music or production tag file is accessed.
    const auto sandbox = fs::canonical(fs::temp_directory_path()) /
        ("precision-cue-synthetic-" +
         precision_cue_external_detail::random_suffix());
    fs::create_directory(sandbox);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{sandbox};

    PrecisionCueExternalCreateRequest req;
    req.plan = synthetic_plan();
    req.mp3_basename = "Disc Über 東京.mp3";
    req.qualified_sibling_directory = fs::canonical(sandbox);
    const auto physical_source = req.qualified_sibling_directory /
        precision_cue_external_detail::utf8_path(req.mp3_basename);
    {
        // Disposable dummy file, never actual user audio.
        std::ofstream mock_mp3(physical_source, std::ios::binary);
        mock_mp3 << "synthetic MP3 fixture -- no audio";
    }
    req.physical_mp3_absolute_path = fs::canonical(physical_source);
    req.reviewed_preflight = approved();
    req.reviewed_source_snapshot_token = 101;
    req.reviewed_target_snapshot_token = 202;
    req.reviewed_cue_sha256 = precision_cue_sha256(req.plan.external_cue_utf8);
    req.explicitly_approved_for_external_create = true;
    const auto target = req.qualified_sibling_directory /
        precision_cue_external_detail::utf8_path("Disc Über 東京.cue");

    std::size_t probes = 0;
    std::uint64_t current_source = 101;
    std::uint64_t current_target = 202;
    auto observe = [&] {
        ++probes;
        return PrecisionCueExternalFreshSnapshot{
            req.reviewed_preflight, current_source, current_target};
    };

    req.explicitly_approved_for_external_create = false;
    VERIFY(create_new_precision_cue_external(req, observe).status == Status::Rejected);
    VERIFY(probes == 0 && !fs::exists(target));
    req.explicitly_approved_for_external_create = true;

    req.reviewed_cue_sha256 = precision_cue_sha256("incorrect");
    VERIFY(create_new_precision_cue_external(req, observe).status ==
           Status::ReviewRequired);
    VERIFY(probes == 0 && !fs::exists(target));
    req.reviewed_cue_sha256 = precision_cue_sha256(req.plan.external_cue_utf8);

    req.reviewed_preflight.explicit_user_approval = false;
    VERIFY(create_new_precision_cue_external(req, observe).status ==
           Status::ReviewRequired);
    VERIFY(!fs::exists(target));
    req.reviewed_preflight = approved();

    req.qualified_sibling_directory = fs::path("relative-unqualified");
    VERIFY(create_new_precision_cue_external(req, observe).status ==
           Status::Rejected);
    req.qualified_sibling_directory = fs::canonical(sandbox);

    const auto original_source = req.physical_mp3_absolute_path;
    req.physical_mp3_absolute_path = fs::path("relative/incorrect.mp3");
    VERIFY(create_new_precision_cue_external(req, observe).status ==
           Status::Rejected);
    req.physical_mp3_absolute_path = req.qualified_sibling_directory /
        precision_cue_external_detail::utf8_path("Unrelated.mp3");
    VERIFY(create_new_precision_cue_external(req, observe).status ==
           Status::Rejected);
    req.physical_mp3_absolute_path = original_source;

    current_source = 500;
    VERIFY(create_new_precision_cue_external(req, observe).status ==
           Status::ReviewRequired);
    current_source = 101;
    current_target = 909;
    VERIFY(create_new_precision_cue_external(req, observe).status ==
           Status::ReviewRequired);
    current_target = 202;

    // A changing source during stage creation must prevent publication and
    // must clean up the temporary file.
    probes = 0;
    auto stale_after_staging = [&] {
        ++probes;
        return PrecisionCueExternalFreshSnapshot{
            approved(), probes == 1 ? 101u : 777u, 202};
    };
    VERIFY(create_new_precision_cue_external(req, stale_after_staging).status ==
           Status::ReviewRequired);
    VERIFY(!fs::exists(target));
    VERIFY(std::distance(fs::directory_iterator(req.qualified_sibling_directory),
                         fs::directory_iterator{}) == 1);

    // Even a callback that falsely reports unchanged identity must not hide
    // a physical-source size change between staging and publication.
    probes = 0;
    auto physical_changes_during_stage = [&] {
        ++probes;
        if (probes == 2) {
            std::ofstream mutate(physical_source, std::ios::binary | std::ios::app);
            mutate << "simulated stale source";
        }
        return PrecisionCueExternalFreshSnapshot{approved(), 101, 202};
    };
    VERIFY(create_new_precision_cue_external(
               req, physical_changes_during_stage).status == Status::ReviewRequired);
    VERIFY(!fs::exists(target));
    VERIFY(std::distance(fs::directory_iterator(req.qualified_sibling_directory),
                         fs::directory_iterator{}) == 1);

    probes = 0;
    const auto created = create_new_precision_cue_external(req, observe);
    VERIFY(created.status == Status::Created);
    VERIFY(probes == 2 && fs::exists(target));
    {
        std::ifstream file(target, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
        VERIFY(bytes == req.plan.external_cue_utf8);
    }
    const auto previous_timestamp = fs::last_write_time(target);

    // A repeated "create" request must not truncate or overwrite the target.
    VERIFY(create_new_precision_cue_external(req, observe).status ==
           Status::Collision);
    VERIFY(fs::last_write_time(target) == previous_timestamp);

    // No-op is a verified byte-identical result and never opens a writer.
    req.reviewed_preflight.target_exists = true;
    req.reviewed_preflight.existing_content_known = true;
    req.reviewed_preflight.existing_content_equal = true;
    VERIFY(create_new_precision_cue_external(req, observe).status == Status::NoOp);
    VERIFY(fs::last_write_time(target) == previous_timestamp);

    // Even a falsely asserted equality must not produce a NoOp.
    {
        std::ofstream tamper(target, std::ios::binary | std::ios::trunc);
        tamper << "existing CUE must survive";
    }
    VERIFY(create_new_precision_cue_external(req, observe).status ==
           Status::Collision);
    {
        std::ifstream file(target, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        VERIFY(content == "existing CUE must survive");
    }

    // A same-size/same-time replacement has a distinct physical file ID.
    const auto before = precision_cue_external_detail::physical_source_snapshot(
        physical_source, req.qualified_sibling_directory, req.mp3_basename);
    VERIFY(before.has_value());
    const auto alternate = sandbox / "replacement-source.tmp";
    {
        std::ofstream stream(alternate, std::ios::binary);
        stream << std::string(static_cast<std::size_t>(before->length), 'x');
    }
    fs::last_write_time(alternate, before->last_write);
    const auto preserved = sandbox / "original-source.keep";
    fs::rename(physical_source, preserved);
    fs::rename(alternate, physical_source);
    const auto after = precision_cue_external_detail::physical_source_snapshot(
        physical_source, req.qualified_sibling_directory, req.mp3_basename);
    VERIFY(after.has_value());
    VERIFY(after->length == before->length);
    VERIFY(after->last_write == before->last_write);
    VERIFY(after->file_id != before->file_id ||
           after->volume_id != before->volume_id);
    VERIFY(!precision_cue_external_detail::physical_source_unchanged(
        *before, physical_source, req.qualified_sibling_directory, req.mp3_basename));

    // Host-parent path resolution must fail closed on a symlink alias.
#ifndef _WIN32
    const auto alias = sandbox.parent_path() /
        ("precision-cue-alias-" + precision_cue_external_detail::random_suffix());
    fs::create_directory_symlink(sandbox, alias);
    struct AliasCleanup {
        fs::path path;
        ~AliasCleanup() { std::error_code ec; fs::remove(path, ec); }
    } alias_cleanup{alias};
    req.qualified_sibling_directory = alias;
    VERIFY(create_new_precision_cue_external(req, observe).status ==
           Status::Rejected);
#endif
}

int main() {
    // MSVC aborts unhandled exceptions without producing usable CTest output.
    // Surface unexpected runtime validation errors across all CI platforms.
    try {
        export_planning_positive_and_negative();
        reserved_unicode_device_alias_regressions();
        export_generated_layout_positive_edge_cases();
        preflight_fail_closed_matrix();
        sha256_exact_byte_regressions();
        commit_revalidation_fail_closed_matrix();
        foobar_local_windows_path_regressions();
        create_only_external_writer_regressions();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "PRECISION_CUE_TEST_EXCEPTION: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "PRECISION_CUE_TEST_EXCEPTION: unknown\n");
        return 1;
    }
}
