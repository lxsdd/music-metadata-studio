"""Offline guard: canonical Studio PR alone can run source-pinned
protected live native foobar QA; no Secret escapes into ordinary jobs.
"""
from pathlib import Path
SOURCE=(Path(__file__).resolve().parents[1]/".github/workflows/foobar.yml").read_text(encoding="utf-8")
OLD="31930d4fca571f449f2c57044bea5ba2e01f0ca7"
NEW="a71edfa479ba478611e32086e41665e80eb305a2"
PREFIX="  discogs-authorized-native:\n"
END="  # One-shot source-to-component package qualification"

def problems(full:str)->list[str]:
    if full.count(PREFIX)!=1 or full.count(END)!=1:
        return ["missing unique protected job"]
    head,tail=full.split(PREFIX,1)
    protected=tail.split(END,1)[0]
    bad=[]
    for c in (
        "github.event_name == 'pull_request'",
        "github.event.pull_request.number == 45",
        "github.event.pull_request.head.repo.full_name == github.repository",
        "github.event.pull_request.head.ref == 'feature/online-metadata-discovery-foundation-20261009'",
        "github.event.pull_request.base.ref == 'main'",
        "github.event_name == 'workflow_dispatch'",
        "inputs.discogs_live == true",
        "environment: discogs-qa",
        "needs: [static-audit, host-gate-script, windows-build]",
        "ref: ${{ github.event.pull_request.head.sha || github.sha }}",
        "if: matrix.platform == 'Win32'",
        "secrets.MMS_DISCOGS_QA_TOKEN",
        "authorized-discogs-probe: 'true'",
        "authorized-discogs-full-artist-probe:",
        "MMS_DISCOGS_QA_RELEASE_ID:",
        "lxsdd/dev-infrastructure-public/.github/actions/foobar-host-smoke@"+NEW,
    ):
        if c not in protected: bad.append("missing trusted native condition "+c)
    if "MMS_PROTECTED_NATIVE_HOST=SKIPPED_PINNED_ACTION_MANUAL_ONLY" in protected:
        bad.append("native QA still marked manual-only")
    if "if: github.event_name == 'workflow_dispatch'\n        uses: lxsdd/dev-infrastructure-public/.github/actions/foobar-host-smoke@" in protected:
        bad.append("native action still manually gated")
    if protected.count("secrets.MMS_DISCOGS_QA_TOKEN") != 2 or "secrets.MMS_DISCOGS_QA_TOKEN" in head:
        bad.append("protected token scope changed or escaped")
    if full.count("lxsdd/dev-infrastructure-public/.github/actions/foobar-host-smoke@"+NEW)!=1:
        bad.append("new privileged ref reused by unprotected job")
    if full.count("lxsdd/dev-infrastructure-public/.github/actions/foobar-host-smoke@"+OLD)!=1:
        bad.append("original unprivileged action pin drift")
    if "pull_request_target:" in full or "uses: actions/upload-artifact@" not in full:
        bad.append("unexpected protected event or publication contract changed")
    return bad

def main()->None:
    assert not problems(SOURCE),problems(SOURCE)
    fixtures=(
        SOURCE.replace("github.event.pull_request.number == 45",
                       "github.event.pull_request.number == 46",1),
        SOURCE.replace("github.event.pull_request.head.repo.full_name == github.repository",
                       "github.event.pull_request.head.repo.full_name != github.repository",1),
        SOURCE.replace("environment: discogs-qa", "environment: unprotected",1),
        SOURCE.replace("github.event.pull_request.base.ref == 'main'",
                       "github.event.pull_request.base.ref == 'foreign'",1),
        SOURCE.replace("lxsdd/dev-infrastructure-public/.github/actions/foobar-host-smoke@"+NEW,
                       "lxsdd/dev-infrastructure-public/.github/actions/foobar-host-smoke@"+OLD,1),
        SOURCE.replace("authorized-discogs-probe: 'true'",
                       "authorized-discogs-probe: 'false'",1),
    )
    for mutated in fixtures:
        assert problems(mutated),"unsafe protected-host workflow mutation escaped"
    print("PASS: real authenticated native protected PR-only source-pinned Win32/x64 host policy and negative mutations")

if __name__=="__main__": main()
