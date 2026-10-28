#include "djmeta/precision_cue_assemble.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>

using namespace djmeta;

namespace {
PrecisionCuePreviewRequest fixture() {
    PrecisionCuePreviewRequest r;
    r.reference.provider = "deezer";
    r.reference.provider_release_id = "synthetic-reference";
    r.reference.album_title = "Synthetic Mix (DJ Mix)";
    r.reference.album_performer = "Various Artists";
    r.reference.tracks = {
        {1, 1, "Opening (Mixed)", "DJ One", 100001},
        {1, 2, "Second (Mixed)", "DJ Two", 100002},
        {2, 1, "Opening Two (Mixed)", "DJ Three", 200001},
        {2, 2, "Finale (Mixed)", "DJ Four", 200002},
    };
    auto candidate = r.reference;
    candidate.provider = "apple";
    candidate.provider_release_id = "synthetic-apple";
    r.candidates.push_back(candidate);
    r.physical_discs = {{"Disc 1.mp3", 200003}, {"Disc 2.mp3", 400003}};
    r.genre = "Electronic";
    r.release_date = "2026";
    return r;
}

void calendar_date_studio_preview_gates() {
    auto f = fixture();
    f.reference.tracks.resize(2);
    f.candidates[0].tracks.resize(2);
    f.physical_discs.resize(1);
    f.genre.clear();
    f.release_date = "2025-02-29";
    auto preview = make_precision_cue_release_preview(f);
    assert(preview.state == PrecisionCueMatchState::Rejected && preview.discs.empty());
    assert(preview.diagnostics.back() == "CUE_DATE_INVALID");
    f.release_date = "2024-02-29";
    preview = make_precision_cue_release_preview(f);
    assert(preview.state == PrecisionCueMatchState::Review && preview.discs.size() == 1);
    assert(preview.header_provenance.release_date ==
           PrecisionCueHeaderFieldOrigin::ManualUnverified);
    f.release_date.clear();
    f.reference.release_date = "2024-02-29";
    f.candidates[0].release_date = "2024-02-29";
    preview = make_precision_cue_release_preview(f);
    assert(preview.state == PrecisionCueMatchState::Selected && preview.preview_qualified);
    assert(preview.header_provenance.release_date ==
           PrecisionCueHeaderFieldOrigin::SelectedCatalog);
    f.candidates[0].release_date = "2025-02-29";
    preview = make_precision_cue_release_preview(f);
    assert(preview.state == PrecisionCueMatchState::Rejected && preview.discs.empty());
}

void header_catalog_provenance_regressions() {
    auto f = fixture();
    // One unambiguous physical disc isolates the header review gate from
    // the independent multi-disc identity review.
    f.reference.tracks.resize(2);
    f.candidates[0].tracks.resize(2);
    f.physical_discs.resize(1);
    auto manual = make_precision_cue_release_preview(f);
    assert(manual.state == PrecisionCueMatchState::Review);
    assert(!manual.preview_qualified && manual.discs.size() == 1);
    assert(manual.header_provenance.genre == PrecisionCueHeaderFieldOrigin::ManualUnverified);
    assert(manual.header_provenance.release_date == PrecisionCueHeaderFieldOrigin::ManualUnverified);
    assert(manual.discs[0].external_cue_utf8.find("REM GENRE \"Electronic\"\n") != std::string::npos);
    assert(manual.discs[0].external_cue_utf8.find("REM DATE \"2026\"\n") != std::string::npos);

    f.candidates[0].genre = "Electronic";
    f.candidates[0].release_date = "2026";
    auto catalog = make_precision_cue_release_preview(f);
    assert(catalog.state == PrecisionCueMatchState::Selected && catalog.preview_qualified);
    assert(catalog.header_provenance.genre == PrecisionCueHeaderFieldOrigin::SelectedCatalog);
    assert(catalog.header_provenance.release_date == PrecisionCueHeaderFieldOrigin::SelectedCatalog);
    f.genre.clear();
    f.release_date.clear();
    auto inherited = make_precision_cue_release_preview(f);
    assert(inherited.state == PrecisionCueMatchState::Selected && inherited.preview_qualified);
    assert(inherited.discs[0].external_cue_utf8 == catalog.discs[0].external_cue_utf8);
    assert(inherited.header_provenance.genre == PrecisionCueHeaderFieldOrigin::SelectedCatalog);
    assert(inherited.header_provenance.release_date == PrecisionCueHeaderFieldOrigin::SelectedCatalog);

    f.genre = "Different Genre";
    f.release_date = "2025";
    auto conflict = make_precision_cue_release_preview(f);
    assert(conflict.state == PrecisionCueMatchState::Review && !conflict.preview_qualified);
    assert(conflict.header_provenance.genre ==
           PrecisionCueHeaderFieldOrigin::ConflictsWithSelectedCatalog);
    assert(conflict.header_provenance.release_date ==
           PrecisionCueHeaderFieldOrigin::ConflictsWithSelectedCatalog);
    assert(conflict.discs[0].external_cue_utf8.find("REM GENRE \"Different Genre\"\n") != std::string::npos);
    assert(conflict.discs[0].external_cue_utf8.find("REM DATE \"2025\"\n") != std::string::npos);
    f.genre.clear(); f.release_date.clear();
    f.candidates[0].genre.clear(); f.candidates[0].release_date.clear();
    auto absent = make_precision_cue_release_preview(f);
    assert(absent.state == PrecisionCueMatchState::Selected && absent.preview_qualified);
    assert(absent.header_provenance.genre == PrecisionCueHeaderFieldOrigin::Absent);
    assert(absent.header_provenance.release_date == PrecisionCueHeaderFieldOrigin::Absent);
    assert(absent.discs[0].external_cue_utf8.find("REM GENRE") == std::string::npos);
    assert(absent.discs[0].external_cue_utf8.find("REM DATE") == std::string::npos);
}

void selected_two_discs() {
    const auto r = make_precision_cue_release_preview(fixture());
    assert(r.state == PrecisionCueMatchState::Review);
    assert(!r.preview_qualified && r.discs.size() == 2);
    assert(r.provider == "apple" && r.provider_release_id == "synthetic-apple");
    assert(r.discs[0].total_duration_ms == 200003);
    assert(r.discs[1].total_duration_ms == 400003);
    assert(r.discs[0].boundaries[0].cue_frame == 0);
    assert(r.discs[1].boundaries[0].cue_frame == 0);
    assert(r.discs[0].external_cue_utf8.find("FILE \"Disc 1.mp3\" WAVE") != std::string::npos);
    assert(r.discs[1].external_cue_utf8.find("REM DISCNUMBER 2") != std::string::npos);
    assert(r.discs[0].external_cue_utf8.find("INDEX 01 01:40:00") != std::string::npos);
}

void fail_closed() {
    auto f = fixture();
    f.physical_discs.pop_back();
    auto r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());

    f = fixture(); f.physical_discs[1].mp3_basename = "../escape.mp3";
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());

    f = fixture(); f.physical_discs[1].mp3_basename = "CON.mp3";
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());

    f = fixture(); f.physical_discs[1].measured_duration_ms += 3001;
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());

    f = fixture(); f.candidates[0].tracks[2].title = "Opening Two (Original Mix)";
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());

    f = fixture(); auto second = f.candidates[0];
    second.provider_release_id = "second-edition"; f.candidates.push_back(second);
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Ambiguous && r.discs.empty());

    f = fixture(); f.reference.tracks[0].duration_ms = 0;
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Review && r.discs.empty());

    f = fixture(); f.physical_discs[1].measured_duration_ms = -1;
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());
}

void duration_review() {
    auto f = fixture(); f.physical_discs[0].measured_duration_ms = 0;
    auto r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Review && !r.preview_qualified);
    assert(r.discs.size() == 2);

    f = fixture(); f.physical_discs[1].measured_duration_ms += 1001;
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Review && r.discs.size() == 2);

    f = fixture(); f.physical_discs[1].measured_duration_ms += 1000;
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Review && !r.preview_qualified);
}

void disc_mapping_regressions() {
    auto f = fixture();
    // Sorted basenames deliberately disagree with the provider's disc order.
    f.physical_discs = {{"A - Disc 2.mp3", 400003}, {"Z - Disc 1.mp3", 200003}};
    auto r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Review && !r.preview_qualified);
    assert(r.discs.size() == 2);
    assert(r.discs[0].external_cue_utf8.find("FILE \"Z - Disc 1.mp3\" WAVE") != std::string::npos);
    assert(r.discs[1].external_cue_utf8.find("FILE \"A - Disc 2.mp3\" WAVE") != std::string::npos);

    f = fixture();
    f.physical_discs[0].measured_duration_ms = 0;
    f.physical_discs[1].measured_duration_ms = 0;
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Ambiguous && r.discs.empty());

    // Identical disc lengths admit two permutations: fail closed.
    const auto ambiguous = resolve_precision_cue_disc_mapping(
        {200003, 200003}, {{"A.mp3", 200003}, {"B.mp3", 200003}});
    assert(ambiguous.state == PrecisionCueMatchState::Ambiguous);
    assert(ambiguous.physical_index_by_disc.empty());

    const auto single = resolve_precision_cue_disc_mapping(
        {200003}, {{"Single.mp3", 200003}});
    assert(single.state == PrecisionCueMatchState::Selected);
    assert(single.physical_index_by_disc.size() == 1 &&
           single.physical_index_by_disc[0] == 0);
    const auto unknown = resolve_precision_cue_disc_mapping(
        {200003}, {{"Single.mp3", 0}});
    assert(unknown.state == PrecisionCueMatchState::Review);

    f = fixture();
    f.physical_discs[1].measured_duration_ms = 24LL * 60 * 60 * 1000 + 1;
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());

    f = fixture();
    f.physical_discs[0].mp3_basename = "disc 2.MP3";
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());
}

void explicitly_reviewed_disc_mapping() {
    auto f = fixture();
    // Two same-length discs: duration-only matching must remain ambiguous.
    f.reference.tracks[2].duration_ms = 100001;
    f.reference.tracks[3].duration_ms = 100002;
    f.candidates[0].tracks[2].duration_ms = 100001;
    f.candidates[0].tracks[3].duration_ms = 100002;
    f.physical_discs = {{"B.mp3", 200003}, {"A.mp3", 200003}};
    auto r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Ambiguous && r.discs.empty());

    // User explicitly chooses the reversed order: proposal only, never auto-approved.
    f.reviewed_physical_index_by_disc = {1, 0};
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Review && !r.preview_qualified);
    assert(r.discs.size() == 2);
    assert(r.discs[0].external_cue_utf8.find("FILE \"A.mp3\" WAVE") != std::string::npos);
    assert(r.discs[1].external_cue_utf8.find("FILE \"B.mp3\" WAVE") != std::string::npos);

    // Explicit duplicate assignment is never accepted, even with equal durations.
    f.reviewed_physical_index_by_disc = {0, 0};
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());
    f.reviewed_physical_index_by_disc = {0, 2};
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());
    f.reviewed_physical_index_by_disc = {0};
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());

    f.reviewed_physical_index_by_disc = {1, 0};
    f.physical_discs[0].measured_duration_ms += 4000;
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Rejected && r.discs.empty());

    f.physical_discs[0].measured_duration_ms = 0;
    f.physical_discs[1].measured_duration_ms = 0;
    r = make_precision_cue_release_preview(f);
    assert(r.state == PrecisionCueMatchState::Review && !r.preview_qualified);
    assert(r.discs.size() == 2);
}
}


void partial_disc_mapping_validation() {
    const std::vector<std::int64_t> durations = {200003, 400003};
    const std::vector<PrecisionCuePhysicalDisc> files = {
        {"Disc 1.mp3", 200003}, {"Disc 2.mp3", 400003}
    };
    auto r = validate_precision_cue_partial_disc_mapping(durations, files, {0, 2});
    assert(r.state == PrecisionCueMatchState::Review);
    assert(r.physical_index_by_disc.empty());
    r = validate_precision_cue_partial_disc_mapping(durations, files, {0, 1});
    assert(r.state == PrecisionCueMatchState::Review);
    assert(r.physical_index_by_disc.size() == 2);
    r = validate_precision_cue_partial_disc_mapping(durations, files, {0, 0});
    assert(r.state == PrecisionCueMatchState::Rejected);
    r = validate_precision_cue_partial_disc_mapping(durations, files, {1, 2});
    assert(r.state == PrecisionCueMatchState::Rejected);
    r = validate_precision_cue_partial_disc_mapping(durations, files, {0, 3});
    assert(r.state == PrecisionCueMatchState::Rejected);
    r = validate_precision_cue_partial_disc_mapping(durations, files, {0});
    assert(r.state == PrecisionCueMatchState::Rejected);
    r = validate_precision_cue_partial_disc_mapping(
        {200003, 200003}, {{"A.mp3", 200003}, {"B.mp3", 200003}}, {1, 0});
    assert(r.state == PrecisionCueMatchState::Review);
    assert(r.physical_index_by_disc.size() == 2);
}


void impossible_partial_completion_regression() {
    const std::vector<std::int64_t> durations = {100000, 104000, 110000};
    const std::vector<PrecisionCuePhysicalDisc> files = {
        {"A.mp3", 100000}, {"B.mp3", 102000}, {"C.mp3", 110000}
    };
    const auto valid = validate_precision_cue_partial_disc_mapping(
        durations, files, {0, 3, 3});
    assert(valid.state == PrecisionCueMatchState::Review);
    assert(valid.physical_index_by_disc.empty());
    const auto impossible = validate_precision_cue_partial_disc_mapping(
        durations, files, {1, 3, 3});
    assert(impossible.state == PrecisionCueMatchState::Rejected);
    assert(impossible.physical_index_by_disc.empty());
    assert(!impossible.diagnostics.empty());
    assert(impossible.diagnostics.back() == "PARTIAL_DISC_MAPPING_NO_COMPLETION");
    const auto complete = validate_precision_cue_partial_disc_mapping(
        durations, files, {0, 1, 2});
    assert(complete.state == PrecisionCueMatchState::Review);
    assert(complete.physical_index_by_disc.size() == 3);
}


void eight_disc_end_to_end_read_only_preview() {
    auto request = fixture();
    request.reference.tracks.clear();
    request.candidates[0].tracks.clear();
    request.physical_discs.clear();
    for (unsigned disc = 1; disc <= 8; ++disc) {
        const std::int64_t track_ms = 1000 + 137 * disc;
        for (unsigned track = 1; track <= 99; ++track) {
            const PrecisionCueReleaseTrack entry{
                disc, track, "Synthetic Mixed Track", "Synthetic DJ", track_ms};
            request.reference.tracks.push_back(entry);
            request.candidates[0].tracks.push_back(entry);
        }
        request.physical_discs.push_back({
            "Synthetic Disc " + std::to_string(disc) + ".mp3", 99 * track_ms});
    }
    const auto preview = make_precision_cue_release_preview(request);
    assert(preview.state == PrecisionCueMatchState::Review);
    assert(!preview.preview_qualified && preview.discs.size() == 8);
    for (unsigned disc = 1; disc <= 8; ++disc) {
        const auto& plan = preview.discs[disc - 1];
        assert(plan.boundaries.size() == 99);
        assert(plan.boundaries.front().cue_frame == 0);
        assert(plan.total_duration_ms == 99 * (1000 + 137 * disc));
        assert(plan.external_cue_utf8.find(
            "REM DISCNUMBER " + std::to_string(disc) + "\n") != std::string::npos);
        assert(plan.external_cue_utf8.find(
            "FILE \"Synthetic Disc " + std::to_string(disc) + ".mp3\" WAVE") !=
            std::string::npos);
    }

    request.candidates[0].tracks[99].disc_number = 1;
    const auto invalid = make_precision_cue_release_preview(request);
    assert(invalid.state == PrecisionCueMatchState::Rejected && invalid.discs.empty());
}

int main() {
    header_catalog_provenance_regressions();
    calendar_date_studio_preview_gates();
    selected_two_discs();
    fail_closed();
    duration_review();
    disc_mapping_regressions();
    explicitly_reviewed_disc_mapping();
    partial_disc_mapping_validation();
    impossible_partial_completion_regression();
    eight_disc_end_to_end_read_only_preview();
    return 0;
}
