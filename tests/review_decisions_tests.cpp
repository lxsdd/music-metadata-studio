#include "djmeta/review_decisions.h"
#include "djmeta/review_display.h"

#include <array>
#include <cstdlib>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* msg) {
    if (!value) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}
template<typename F> void should_reject(F action, const char* msg) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    check(false, msg);
}
}

int main() {
    using namespace djmeta;
    // Collapsed headers are not proposal rows. Expansion preserves the
    // original sorted identities within each safety class.
    std::vector<MetadataDiffRow> grouped_rows(4);
    grouped_rows[0].safety=SafetyClass::Safe;
    grouped_rows[1].safety=SafetyClass::Confident;
    grouped_rows[2].safety=SafetyClass::Review;
    grouped_rows[3].safety=SafetyClass::Safe;
    const std::vector<std::size_t> sorted_rows{3,0,2,1};
    const auto compact=group_review_rows(
        grouped_rows,sorted_rows,{false,false,false});
    const auto header=(std::numeric_limits<std::size_t>::max)();
    check(compact.counts==std::array<std::size_t,3>{1,1,2} &&
          compact.row_order==std::vector<std::size_t>{header,header,header} &&
          compact.group_at_row==std::vector<int>{0,1,2},
          "collapsed review has three non-actionable summary rows");
    const auto expanded=group_review_rows(
        grouped_rows,sorted_rows,{true,false,true});
    check(expanded.row_order==
              std::vector<std::size_t>{header,2,header,header,3,0} &&
          expanded.group_at_row==std::vector<int>{0,-1,1,2,-1,-1},
          "expanded review retains exact proposal indices and sorted ties");
    should_reject([&]{ (void)group_review_rows(
        grouped_rows,{3,0,2,2},{true,true,true}); },
        "duplicate proposal index must not enter native review");
    should_reject([&]{ (void)group_review_rows(
        grouped_rows,{3,0,2,4},{true,true,true}); },
        "out-of-range proposal index must not enter native review");
    should_reject([&]{ (void)group_review_rows(
        grouped_rows,{3,0,2},{true,true,true}); },
        "stale proposal count must not enter native review");

    const MetadataDocument original {{
        {"TITLE", {" Old Title "}},
        {"ARTIST", {"Original Artist"}},
        {"VERSION", {"Mix A"}},
        {"CUSTOM", {"Uno", " Dos "}},
    }};
    AnalysisResult analysis;
    analysis.input_fingerprint = fingerprint(original);
    analysis.ruleset_revision = "test-v1";
    auto proposal = [&](std::size_t field, std::size_t value, const char* target,
                        SafetyClass safety) {
        Proposal p;
        p.field_index = field;
        p.field = original.fields[field].name;
        p.value_index = value;
        p.original_value = original.fields[field].values[value];
        p.proposed_value = target;
        p.safety = safety;
        p.rule_ids.push_back("stable.test.rule");
        return p;
    };
    analysis.proposals = {
        proposal(0, 0, "Old Title", SafetyClass::Safe),
        proposal(1, 0, "Canonical Artist", SafetyClass::Confident),
        proposal(2, 0, "Mix B", SafetyClass::Review),
        proposal(3, 1, "Dos", SafetyClass::Safe),
    };
    const auto automatic = project_review_decisions(original, analysis);
    check(automatic.document.fields[0].values[0] == "Old Title" &&
          automatic.document.fields[1].values[0] == "Original Artist" &&
          automatic.document.fields[2].values[0] == "Mix A" &&
          automatic.document.fields[3].values[0] == "Uno" &&
          automatic.document.fields[3].values[1] == "Dos" &&
          automatic.automatic_safe == 2 &&
          automatic.unresolved_semantic == 2,
          "only SAFE default preview, unrelated fields preserved");

    // The ordinary Review pane may show SAFE as an unapproved suggestion,
    // but physical tags and Title Formatting input use explicit choices ONLY.
    const auto initial_route=project_explicit_review_decisions(original,analysis);
    check(initial_route.document==original &&
          initial_route.automatic_safe==0 &&
          initial_route.explicitly_rejected==0 &&
          initial_route.unresolved_semantic==2,
          "unaccepted SAFE may not silently change initial FileOps filenames");
    auto only_safe=std::vector<ReviewDecision>(analysis.proposals.size());
    only_safe[0].action=ReviewAction::Accept;
    const auto accepted_route=project_explicit_review_decisions(
        original,analysis,only_safe);
    check(accepted_route.document.fields[0].values[0]=="Old Title" &&
          accepted_route.document.fields[3]==original.fields[3] &&
          accepted_route.unresolved_semantic==2 &&
          accepted_route.explicitly_accepted==1 &&
          accepted_route.automatic_safe==0,
          "a single accepted SAFE must not approve any other pending SAFE");
    auto safe_noop=only_safe;
    safe_noop[0].action=ReviewAction::ManualValue;
    safe_noop[0].manual_value=" Old Title ";
    check(project_explicit_review_decisions(original,analysis,safe_noop).document==
              original,
          "manual original value produces identical routing metadata");
    should_reject([&]{(void)project_explicit_review_decisions(
        original,analysis,{ReviewDecision{}});},
        "partial explicit decision snapshot cannot enter file routing");
    auto forged_route=analysis;
    forged_route.proposals[0].field_index=20;
    should_reject([&]{(void)project_explicit_review_decisions(
        original,forged_route);},
        "unmatched proposal index cannot pass explicit routing guard");

    forged_route=analysis;
    forged_route.proposals[0].proposed_value.clear();
    should_reject([&]{(void)project_explicit_review_decisions(
        original,forged_route);},
        "pending forged SAFE deletion remains blocked even without routing changes");
    forged_route=analysis;
    forged_route.proposals[0].proposed_value=std::string("Bad \\xC0\\xAF",6);
    should_reject([&]{(void)project_explicit_review_decisions(
        original,forged_route);},
        "pending malformed SAFE remains blocked by original preview validation");

    std::vector<ReviewDecision> decisions(analysis.proposals.size());
    decisions[0].action = ReviewAction::Reject;
    decisions[1].action = ReviewAction::Accept;
    decisions[2].action = ReviewAction::ManualValue;
    decisions[2].manual_value = "Custom Edit";
    const auto edited = project_review_decisions(original, analysis, decisions);
    check(edited.document.fields[0].values[0] == " Old Title " &&
          edited.document.fields[1].values[0] == "Canonical Artist" &&
          edited.document.fields[2].values[0] == "Custom Edit" &&
          edited.document.fields[3].values[1] == "Dos" &&
          edited.automatic_safe == 1 &&
          edited.explicitly_accepted == 1 &&
          edited.explicitly_rejected == 1 &&
          edited.manually_replaced == 1 &&
          edited.unresolved_semantic == 0,
          "Accept/Reject/Manual and unchanged SAFE are deterministic");

    // The native "Proposed" column must display the decision's actual
    // staged value, not a discarded change after Reject. Reset is a
    // *preview* and must never be mislabeled as an applied tag.
    check(review_visible_value(" Old Title ", "Old Title", &decisions[0])==
              " Old Title " &&
          review_visible_value("Original Artist","Canonical Artist",
                               &decisions[1])=="Canonical Artist" &&
          review_visible_value("Mix A","Mix B",&decisions[2])=="Custom Edit",
          "native Accept/Reject/Manual readback matches staged semantic choice");
    const ReviewDecision reset_view{ReviewAction::Pending, ""};
    check(review_visible_value("Mix A","Mix B",&reset_view)=="Mix B" &&
          review_visible_value("Mix A","Mix B",nullptr)=="Mix A",
          "Reset displays a pending proposal but missing decision keeps original");
    const ReviewDecision invalid_view{
        static_cast<ReviewAction>(100), "forbidden"};
    check(review_visible_value("Mix A","Mix B",&invalid_view)=="Mix A",
          "unknown review decision must never display a proposed write");
    decisions[2] = ReviewDecision{ReviewAction::Pending, ""};
    const auto pending = project_review_decisions(original, analysis, decisions);
    check(pending.unresolved_semantic == 1 &&
          pending.document.fields[2].values[0] == "Mix A",
          "unresolved REVIEW never leaks into projection");
    check(original.fields[0].values[0] == " Old Title " &&
          original.fields[1].values[0] == "Original Artist",
          "projection never mutates user originals");

    auto stale = original;
    stale.fields[0].values[0] = "Changed elsewhere";
    should_reject([&]{ (void)project_review_decisions(stale, analysis); },
                  "reject stale fingerprints");
    should_reject([&]{ (void)project_review_decisions(original, analysis, {ReviewDecision{}}); },
                  "reject decision cardinality mismatch");
    auto invalid = analysis;
    invalid.proposals[0].original_value = "not the original";
    should_reject([&]{ (void)project_review_decisions(original, invalid); },
                  "reject proposal source mismatch");
    invalid = analysis;
    invalid.proposals.push_back(invalid.proposals[0]);
    should_reject([&]{ (void)project_review_decisions(original, invalid); },
                  "reject colliding field/value proposals");
    decisions[2].action = ReviewAction::ManualValue;
    decisions[2].manual_value.clear();
    should_reject([&]{ (void)project_review_decisions(original, analysis, decisions); },
                  "manual empty value is not an implicit tag deletion");
    decisions[2].manual_value = "Bad\nValue";
    should_reject([&]{ (void)project_review_decisions(original, analysis, decisions); },
                  "unsafe control characters rejected in manual input");

    // The same structural guard is enforced at review-projection time.
    const MetadataDocument structured {{
        {"CUESHEET", {"FILE \"song.mp3\" MP3\r\n  TRACK 01 AUDIO"}},
        {"LYRICS", {"First line\nSecond line"}},
        {"TITLE", {"  Song  "}}
    }};
    AnalysisResult unsafe;
    unsafe.input_fingerprint = fingerprint(structured);
    Proposal forgedCue;
    forgedCue.field_index = 0;
    forgedCue.field = "CUESHEET";
    forgedCue.value_index = 0;
    forgedCue.original_value = structured.fields[0].values[0];
    forgedCue.proposed_value = "broken FILE reference";
    for (auto safety : {SafetyClass::Safe, SafetyClass::Review}) {
        forgedCue.safety = safety;
        unsafe.proposals = {forgedCue};
        should_reject([&]{ (void)project_review_decisions(structured, unsafe); },
                      "generic projection cannot silently replace embedded cuesheet");
        should_reject([&]{ (void)project_review_decisions(structured, unsafe,
            {ReviewDecision{ReviewAction::Accept, ""}}); },
            "manual Accept in generic review does not bypass protected CUE");
    }
    Proposal multiline;
    multiline.field_index = 1;
    multiline.field = "LYRICS";
    multiline.original_value = structured.fields[1].values[0];
    multiline.proposed_value = "one line";
    multiline.safety = SafetyClass::Safe;
    unsafe.proposals = {multiline};
    should_reject([&]{ (void)project_review_decisions(structured, unsafe); },
                  "forged SAFE lyrics flattening cannot enter review projection");
    multiline.safety = SafetyClass::Review;
    unsafe.proposals = {multiline};
    const auto explicitReview = project_review_decisions(structured, unsafe,
        {ReviewDecision{ReviewAction::Accept, ""}});
    check(explicitReview.document.fields[1].values[0] == "one line" &&
          explicitReview.explicitly_accepted == 1,
          "explicit REVIEW lyric replacement remains supported");

    // Malformed legacy source bytes stay immutable, but malformed new text
    // cannot cross accepted or manually entered review boundaries.
    const MetadataDocument malformedOriginal{{
        {"TITLE", {" Original "}},
        {"COMMENT", {std::string("Old \xC2", 5)}}
    }};
    AnalysisResult malformedAnalysis;
    malformedAnalysis.input_fingerprint = fingerprint(malformedOriginal);
    Proposal bad;
    bad.field_index = 0;
    bad.value_index = 0;
    bad.field = "TITLE";
    bad.original_value = " Original ";
    bad.proposed_value = std::string("Bad \xED\xA0\x80", 7);
    bad.safety = SafetyClass::Safe;
    malformedAnalysis.proposals = {bad};
    should_reject([&]{ (void)project_review_decisions(malformedOriginal, malformedAnalysis); },
                  "invalid SAFE postimage must be rejected");
    for (const auto& bad_text : {
        std::string("New\nline"),
        std::string("New\0hidden", 10)}) {
        bad.proposed_value = bad_text;
        malformedAnalysis.proposals = {bad};
        should_reject([&]{
            (void)project_review_decisions(malformedOriginal, malformedAnalysis);
        }, "forged SAFE multiline/NUL postimage entered review");
    }
    bad.proposed_value = std::string("Bad \xED\xA0\x80", 7);
    bad.safety = SafetyClass::Review;
    malformedAnalysis.proposals = {bad};
    should_reject([&]{
        (void)project_review_decisions(malformedOriginal, malformedAnalysis,
             {ReviewDecision{ReviewAction::Accept, ""}});
    }, "explicit Accept cannot authorize invalid UTF-8 postimage");
    const auto ignored = project_review_decisions(malformedOriginal, malformedAnalysis,
         {ReviewDecision{ReviewAction::Reject, ""}});
    check(ignored.document == malformedOriginal &&
          ignored.explicitly_rejected == 1,
          "reject malformed proposal preserves byte-exact legacy source");
    bad.proposed_value = "Repaired";
    malformedAnalysis.proposals = {bad};
    should_reject([&]{
        (void)project_review_decisions(malformedOriginal, malformedAnalysis,
             {ReviewDecision{ReviewAction::ManualValue, std::string("Bad \x80", 5)}});
    }, "invalid UTF-8 manual replacement accepted");
    const auto international = project_review_decisions(malformedOriginal, malformedAnalysis,
         {ReviewDecision{ReviewAction::ManualValue, "Tiësto"}});
    check(international.document.fields[0].values[0] == "Tiësto" &&
          international.document.fields[1] == malformedOriginal.fields[1],
          "valid Unicode names accepted without changing unrelated malformed values");

    // Even a forged SAFE proposal is not a generic tag-delete command.
    const MetadataDocument blankSource{{{"TITLE", {"    "}}}};
    AnalysisResult blankAnalysis;
    blankAnalysis.input_fingerprint = fingerprint(blankSource);
    Proposal blank;
    blank.field_index = 0;
    blank.field = "TITLE";
    blank.value_index = 0;
    blank.original_value = "    ";
    blank.proposed_value = "";
    blank.safety = SafetyClass::Safe;
    blankAnalysis.proposals = {blank};
    should_reject([&]{ (void)project_review_decisions(blankSource, blankAnalysis); },
                  "automatic SAFE empty tag must not pass review");
    blank.safety = SafetyClass::Review;
    blankAnalysis.proposals = {blank};
    should_reject([&]{
        (void)project_review_decisions(blankSource, blankAnalysis,
            {ReviewDecision{ReviewAction::Accept, ""}});
    }, "Accept is not an implicit tag delete action");
    const auto kept = project_review_decisions(blankSource, blankAnalysis,
        {ReviewDecision{ReviewAction::Reject, ""}});
    check(kept.document == blankSource && kept.explicitly_rejected == 1,
          "Reject should preserve a blank source tag exactly");

    AnalysisResult none;
    none.input_fingerprint = fingerprint(original);
    const auto zero = project_review_decisions(original, none);
    check(zero.document == original && zero.automatic_safe == 0 &&
          zero.unresolved_semantic == 0,
          "no-op selection and empty proposals retain metadata");

    std::cout << "PASS: non-writing proposal decisions and atomic stale/conflict/unsafe guards\n";
}
