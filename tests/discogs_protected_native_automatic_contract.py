#!/usr/bin/env python3
"""Mutation-based static guards for PR-triggered protected Discogs native QA."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
TEXT=(ROOT / ".github/workflows/foobar.yml").read_text(encoding="utf-8")
START="  discogs-authorized-native:\n"
END="\n  # One-shot source-to-component package qualification"

def problems(value: str) -> list[str]:
    if value.count(START)!=1 or value.count(END)!=1:
        return ["missing single protected native job"]
    regular, rest=value.split(START,1)
    secured, following=rest.split(END,1)
    expected=[
      "github.event_name == 'workflow_dispatch'",
      "inputs.discogs_live == true",
      "github.ref == 'refs/heads/feature/online-metadata-discovery-foundation-20261009'",
      "github.event_name == 'pull_request'",
      "github.event.pull_request.number == 45",
      "github.event.pull_request.head.repo.full_name == github.repository",
      "github.event.pull_request.head.ref == 'feature/online-metadata-discovery-foundation-20261009'",
      "github.event.pull_request.base.ref == 'main'",
      "needs: [static-audit, host-gate-script, windows-build]",
      "environment: discogs-qa",
      "runs-on: windows-2022",
      "platform: [Win32, x64]",
      "ref: ${{ github.event.pull_request.head.sha || github.sha }}",
      "persist-credentials: false",
      "MMS_DISCOGS_QA_TOKEN: ${{ secrets.MMS_DISCOGS_QA_TOKEN }}",
      "FB2K_DISCOGS_QA_TOKEN: ${{ secrets.MMS_DISCOGS_QA_TOKEN }}",
      "MMS_DISCOGS_QA_RELEASE_ID: ${{ github.event_name == 'pull_request' && '37332711' || inputs.discogs_release_id }}",
      "run: ./scripts/qa-discogs-discovery.ps1 -Mode Live -ReleaseId $env:MMS_DISCOGS_QA_RELEASE_ID",
      "authorized-discogs-probe: 'true'",
      "authorized-discogs-full-artist-probe: ${{ (github.event_name == 'pull_request' || inputs.discogs_release_id == '37332711') && 'true' || 'false' }}",
      "discogs-release-id: ${{ github.event_name == 'pull_request' && '37332711' || inputs.discogs_release_id }}"
    ]
    issues=["lost authorized gate: "+item for item in expected if item not in secured]
    # The new separately reviewed, immutable shared host action from
    # infrastructure Draft #56 validates the original GitHub event JSON,
    # PR #45 exact head SHA and workspace Git HEAD itself. Neither spoof
    # the runner event nor retain the superseded manual-only gate.
    native="      - name: Authorized official Discogs native exact-edition gate\n"
    expected_native=(
        native+
        "        uses: lxsdd/dev-infrastructure-public/.github/actions/"
        "foobar-host-smoke@5c8c71aa1b24e4c6cb84af06cd4608fafb0f7c56\n"
    )
    if secured.count(native)!=1 or expected_native not in secured:
        issues.append("protected native E2E action missing its reviewed exact source pin")
    if ("MMS_PROTECTED_NATIVE_HOST=SKIPPED_PINNED_ACTION_MANUAL_ONLY" in secured or
        native+"        if: github.event_name == 'workflow_dispatch'\n" in secured):
        issues.append("native host still manual-only, despite separately approved safe PR admission")
    if secured.count("secrets.MMS_DISCOGS_QA_TOKEN")!=2:
        issues.append("token must only be in two protected steps")
    if "secrets.MMS_DISCOGS_QA_TOKEN" in regular+following:
        issues.append("secret present in unprotected jobs")
    if "pull_request_target:" in value:
        issues.append("privileged untrusted pull_request_target")
    for bad in ("uses: actions/upload-artifact@", "uses: actions/create-release@"):
        if bad in secured: issues.append("protected job publishing binaries")
    return issues

def mutation(before: str, after: str) -> str:
    head, rest=TEXT.split(START,1)
    job, tail=rest.split(END,1)
    assert job.count(before)==1, "mutate one protected job location"
    return head+START+job.replace(before,after,1)+END+tail

def main() -> None:
    assert not problems(TEXT), problems(TEXT)
    pairs=[
      ("github.event.pull_request.head.repo.full_name == github.repository",
       "github.event.pull_request.head.repo.full_name != github.repository"),
      ("github.event.pull_request.number == 45",
       "github.event.pull_request.number == 46"),
      ("github.event.pull_request.base.ref == 'main'",
       "github.event.pull_request.base.ref == 'other'"),
      ("github.event.pull_request.head.ref == 'feature/online-metadata-discovery-foundation-20261009'",
       "github.event.pull_request.head.ref == 'untrusted'"),
      ("environment: discogs-qa", "environment: public"),
      ("needs: [static-audit, host-gate-script, windows-build]",
       "needs: static-audit"),
      ("ref: ${{ github.event.pull_request.head.sha || github.sha }}",
       "ref: ${{ github.sha }}"),
      ("authorized-discogs-probe: 'true'",
       "authorized-discogs-probe: 'false'"),
      ("      - name: Authorized official Discogs native exact-edition gate\n"
       "        uses: lxsdd/dev-infrastructure-public/.github/actions/"
       "foobar-host-smoke@5c8c71aa1b24e4c6cb84af06cd4608fafb0f7c56\n",
       "      - name: Authorized official Discogs native exact-edition gate\n"
       "        if: github.event_name == 'workflow_dispatch'\n"
       "        uses: lxsdd/dev-infrastructure-public/.github/actions/"
       "foobar-host-smoke@5c8c71aa1b24e4c6cb84af06cd4608fafb0f7c56\n")
    ]
    for prior, next_value in pairs:
        assert problems(mutation(prior,next_value)), "unsafe mutation escaped"
    assert problems(TEXT+"\npull_request_target:\n"), "target event escaped"
    assert problems(TEXT.replace("  package:\n", "  package:\n"
        "    env:\n      TOKEN: ${{ secrets.MMS_DISCOGS_QA_TOKEN }}\n",1)), "public token leak escaped"
    print("PASS: protected PR authentic native E2E with reviewed source-pinned shared action and negative mutations")

if __name__=="__main__":
    main()
