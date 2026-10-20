# CUE responsibility boundaries — Music Metadata Studio / Precision CUE

Status: coordination contract on PR #45 (Studio). Precision CUE PR #52 is an
**independent active worker** and must not be cherry-picked, overwritten,
merged or modified blindly.

## One eventual native foobar2000 component

**Studio owns:** Existing external/embedded CUE inspection as a read-only
input to Discogs/Beatport/Deezer/MusicBrainz metadata discovery, original
physical/CUE-global/CUE-track provenance, tag normalization, candidate
comparison, Accept/Reject/Reset review, source-aware file-organization
preview and user-facing modeless foobar UI.

**Precision CUE owns:** Generating a **new** CUE for a selected
single-file continuous mix: exact edition/tracklist and duration evidence,
integer timing and 75fps quantization, gap/transition uncertainty,
externally staged CUE creation and separately qualified embedded-write
research and preflight. Its timing/generation implementation is NOT
replicated in Studio.

**Shared once after explicit integration review:** Source version/identity
guards, original-CUE carrier readers, protected physical vs. virtual
semantics, external/embedded CUE encoding/structure protection, no-op
comparison, batch-approved writer/rollback policies, and compatible native
visual controls. Until that integration, each worker can maintain local
**read-only safety assertions** independently; that is temporary
qualification overlap, not permission to ship duplicate writers.

Integration cannot be assumed from green CI on either branch.
`component.rc`, `resource.h`, native workspace code, CMake, SDK
project and GitHub workflows are touched by both branches. Resolve
their conflicts deliberately with regression tests and one final real
Win32/x64 foobar acceptance, rather than a blind merge.

**Current hard boundary:** Neither a parser, a read-only native host
preview, a CUE postimage calculator nor a successful test fixture is
permission to write a physical/virtual audio track, an external/embedded
CUE, a FLAC Type-5 block or artwork. Zero unapproved media writes.
