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

int main() {
    selected_two_discs();
    fail_closed();
    duration_review();
    disc_mapping_regressions();
    explicitly_reviewed_disc_mapping();
    partial_disc_mapping_validation();
    return 0;
}
