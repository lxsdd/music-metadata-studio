# Explicit offline Beatport track JSON import

The Beatport catalog adapter in Music Metadata Studio does **not** have
authorized Beatport OAuth credentials or an enabled live network connector.
Do not scrape Beatport or use credentials from unrelated applications.

If you already lawfully possess a complete Beatport **track-detail JSON**
record, you may compare it locally in the existing foobar Prepare Tracks
**Find & compare** page:

1. Select one physical audio file. Its original tags and host playback duration
   are captured read-only. This action does not send anything to a provider.
2. Copy UTF-8 plain text with the line `@beatport-track-json`, followed by a
   complete JSON track object from an independently authorized source:
   ```text
   @beatport-track-json
   {"id":321,"name":"Home","mix_name":"Extended Mix",
    "artists":[{"name":"DJ Example"}],"length_ms":402500,
    "isrc":"GBXYZ2601234","genre":{"name":"Progressive House"}}
   ```
3. Click **Import clipboard**. The bounded JSON is parsed locally; Beatport
   never receives an HTTP request. The UI labels the values as **Pasted:
   beatport**, not as official API-fetched or independently verified evidence.
4. Physical TITLE/ARTIST/VERSION, optional ISRC and actual foobar duration
   are compared with the user-supplied identity. Mismatches, missing duration
   and ambiguous mix identity BLOCK provider proposals. Distinct recording
   and edition field scopes, original custom/multivalue and ReplayGain tags
   are preserved in a read-only comparison.
5. The raw JSON format cannot silently assign a track to any virtual CUE
   subsong. For a CUE source, use the existing explicit structured candidate
   syntax with `@cue_track_ordinal`, keeping unrepresentable tags blocked.

All comparison outcomes are review evidence only. No original tag, artwork,
ReplayGain, CUE carrier, media byte or file is written. This facility does
**not** certify the authenticity, licensing, API availability or correctness
of a user-provided JSON payload.

Automated scope: core positive/negative parsing and mix identity tests,
native production clipboard-handler static audit, exact-SHA Windows
Win32/x64 builds. Installed foobar and genuine provider integration remain
independent acceptance gates.
