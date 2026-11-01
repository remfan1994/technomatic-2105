# TECHNOMATIC 2105 v30: music-engine validation

Validated on 2026-10-18. This report covers native generation, musical development, instrument selection, deterministic replay and host audio rendering. Android build, metadata-container inspection and edited-recipe tests have separate reports.

## Result

The final native suite passed **63 checks**, both in the normal build and under **AddressSanitizer plus UndefinedBehaviorSanitizer**. The paired audit finds substantially more development in the actual primary note/rest grammar while preserving recognizable recall rhythms. Expanding the available synthesis catalogue did **not** expand the sampled ensemble: every matched seed retained its v29 supporting-role count, and the mean number of triggered pitched synthesis families fell from **5.9961 to 4.8359**, with the sampled maximum falling from **9 to 6**.

These are measured structural and software results. They are not a claim of subjective listening quality, proof that a melody can never repeat, or a reconstruction of the user's unspecified problematic recording.

Final native source SHA256:

| File | SHA256 |
|---|---|
| `app/src/main/cpp/MusicEngine.cpp` | `759885caecfaaa12dc07e83977e94948f17d853b1a69284ab1dd7adfcfe58496` |
| `app/src/main/cpp/MusicEngine.h` | `8acddec9294a13dc4a5854503b1f0c30e3f3c3e05ea731577d4e1170427ee18b` |

The immutable v29 baseline used `MusicEngine.cpp` SHA256 `5a3a3c9f58209f2fee8235fd8925093bb9689c196ec06f4c95b7d7b1eba86658` and header SHA256 `e6e6cec1f09110fd9501265764ca8d6b77ee8d15be02b882f30178562d8c15f6`. The old validators remain in the project as historical evidence. Their requirements for channels to alter playback are intentionally replaced by v30's neutral-channel tests.

## What the baseline showed

The new audit separates the principal musical line from everything surrounding it. Hashing the complete mixed arrangement can make a recurring lead appear new whenever a supporting note, drum, timing offset or timbre changes. That was insufficient for the reported monotony.

The baseline sweep used 256 deterministic seeds, with 512 four-beat phrases after a 16-phrase prefix for each seed: **131,072 phrases per version**. Seeds are `uint32_t(i * 2654435761)`, for `i = 1..256`. The observer calls the production grammar functions and represents each sixteenth-note position as a chord-relative integer pitch or a rest. Gain, timbre, pan, tiny timing differences and probabilistic performance omissions do not count as musical novelty.

V29's Hook sections occupied 13.7% to 34.4% of the sampled phrases, averaging 21.9%. This does **not** support a literal claim that most phrases were marked Hook. The stronger finding was the small rhythmic vocabulary within a song: only **4 to 9** primary note/rest patterns, with a median of **7**. A typical song repeated a previously heard primary phrase often even though the aggregate arrangement continued changing.

Two low-variety or high-recurrence examples were selected from the sweep. Neither was supplied as the user's failing seed.

| Seed | Intent / exact generated BPM | V29 distinct primary phrases | V29 four most common primary phrases |
|---|---|---:|---:|
| 3041712678 | Slow / 61.722839 | 87 of 512 | 169 of 512 |
| 2514750925 | Unhurried groove / 86.390976 | 99 of 512 | 187 of 512 |

Source inspection also identified regular bright accents: the old Spark path used positions 3, 7, 11 and 15, with an extra scale octave at position 15; Bell used positions 7 and 15, plus Hook positions 0 and 8, at a high register. Such fixed high accents could plausibly create a brief reward-like cadence. That is an inference from the implementation, not identification of an existing melody. V30 replaces those paths with generated supporting grammar and removes the universal rising chord cascade.

## Primary development and theme retention

V30's `PhrasePlan` and `foregroundNoteForStep` are used by the actual scheduler. The phrase can pass its foreground to an already selected bass, harmony or supporting instrument. The observer reports primary-lead turns separately from the continuing foreground across these handoffs.

An instrument handoff does not itself count as a new note sequence. The foreground fingerprint excludes instrument identity. Main-lead fingerprints include only turns owned by the main lead, so silence during another instrument's answer cannot manufacture apparent novelty. Comparisons of main-lead uniqueness account for the changed number of owned turns.

| Measure over 512 phrases per seed | V29 | V30 |
|---|---:|---:|
| Median distinct continuing foreground phrases | 147 | 459.5 |
| Median distinct main-lead phrases | 147 | 314 |
| Median main-lead turns available for that count | 512 | 363 |
| Median per-song fraction of main-lead turns with distinct phrases | 28.71% | 86.78% |
| Median main-lead note/rest rhythms | 7 | 167 |
| Range of main-lead note/rest rhythms | 4-9 | 54-223 |
| Worst concentration of four foreground phrases | 187 of 512 | 95 of 512 |
| Longest identical consecutive main-lead turn run | 4 | 2 |
| Median distinct bass foundation/foreground plans | 55 | 116 |
| Mean explicitly exploratory phrases | 227.6 | 282.0 |
| Mean phrases handed to another existing instrument | 0 | 149.1 |

The final observer includes the scheduler's chord-root bass arrival at a Confluence joint. Bass identities also include bass-owned foreground answers. These are planned note/rest identities, not a demand that a sparse bass must change every bar.

The improvements are visible in the same baseline examples:

| Seed | V29 main-lead identities | V30 main-lead identities / owned turns | V30 continuing foreground identities | Main-lead rhythms, v29 to v30 |
|---|---:|---:|---:|---:|
| 3041712678 | 87 / 512 | 318 / 352 | 471 / 512 | 7 to 108 |
| 2514750925 | 99 / 512 | 322 / 351 | 477 / 512 | 6 to 191 |

Recognizable recall remains deliberate. Hook sections occupy 28 to 104 phrases out of 512 in v30, and every sampled song retains both recall and substantial exploration. In a separate **64-seed, 256-phrase actual scheduler audit**, recall phrases used their characteristic **one or two note/rest rhythms**. Those recurring rhythms coexist with pitch development and the much wider rhythmic vocabulary outside recall.

The devices express musical relationships: Counterweight and PocketExchange change the conversation and rhythmic placement; HeldQuestion postpones resolution; Confluence coordinates an arrival; OpenRelease leaves space after it. They do not select prerecorded riffs. The tests require room for recall rather than maximizing the number of unique bars at any cost.

## Slow music

The four listening intents and their broad tempo coverage remain intact. Across 512 seeds, the counts were **136 slow, 122 unhurried groove, 129 driving and 125 intense**. There were **154 seeds at or below 90 BPM**, and **161 at or above 135 BPM**. All 32 original design families remained reachable.

The v29 slow tail had genuine register and cadence weaknesses. For example, seed 3816266512 had an average planned lead MIDI pitch of 92.75; seed 99665556 averaged 81.70. Both ended almost every sampled phrase on the local root. The final v30 figures for those same seeds are 67.11 and 69.64, respectively.

Across the 66 slow seeds in the paired 256-seed audit:

| Measure | V29 | V30 |
|---|---:|---:|
| Mean planned main-lead MIDI pitch | 68.73 | 64.05 |
| Foreground phrases ending on the local root pitch class | 80.30% | 20.67% |
| Mean planned main-lead articulation | 0.472 s | 0.450 s |
| Planned slow primary notes outside MIDI 52-78 | Present | 0 |

The articulation result is reported directly: v30 does not make every slow note longer than v29. Its slow changes also concern register, fewer automatic resolutions, longer harmonic spans, connected phrasing and actual instrument answers. In the separate scheduled audit, slow primary notes averaged **0.441 s**, compared with **0.058 s** for intense primary notes; slow foreground event rate averaged **1.808/s**, compared with **6.822/s** for intense pieces. Scheduled slow harmony gestures averaged **0.945 s**.

A concrete defect was caught during validation: cadence and arrival notes were initially assigned after the register clamp. The final engine folds the final assignments as well. The tests examined **20,346 final foreground notes** and **156 actually triggered harmony components** using public root edits at 24 and 60 and explicit private octave stress settings 1, 2 and 4. Foreground bounds held at MIDI 52-78 for slow melodic owners and 33-62 for bass owners; actual slow harmony components remained in 45-76. There is no public octave recipe key: that part is explicitly an internal stress test.

## More available instruments, no larger sampled ensemble

V29 stored one base model number per synthesis category but added color-dependent offsets when triggering a note. The heard synthesis family could therefore change as the tone control drifted. Counting only the stored base model would have missed this.

`audit_palette.cpp` observes the actual production trigger functions, forwards the event's selected instrument slot in v30, and counts distinct triggered model families by synthesis category. It measures total families used over the sampled song, not merely simultaneous voice count.

| Measure | V29 | V30 |
|---|---:|---:|
| Available lead families reached across 512 seeds | 16 | 32 |
| Available bass families | 12 | 20 |
| Available harmony families | 10 | 18 |
| Available drum kits | 8 | 12 |
| Mean triggered pitched families per song, matched 256-seed audit | 5.9961 | 4.8359 |
| Maximum triggered pitched families in that audit | 9 | 6 |

The supporting-role histogram was exactly equal for all 256 paired seeds: **56 with two supports, 125 with three, 71 with four and 4 with five**. The generator's allowed range remains one to six. It did not add layers to achieve variety. Supporting melodic roles share a bounded fixed family pool rather than taking a new instrument from the full catalogue whenever a phrase changes.

Every observed v30 role retained one fixed family through the scheduler/trigger audit. A separate complete three-minute PCM render also observed fixed role families and stayed within the six-family bound. Pitched family counts treat lead, bass and harmony synthesis categories separately; percussion is reported as its selected kit. The source still permits ordinary short voice tails, which are not additional instruments.

## Playback, export and control checks

The final `tools/validate_v30.cpp` suite passed 63 checks, including:

- 512-seed intent, tempo, original design and model-catalogue coverage; generated lead and bass grammar diversity; foreground/support balance and ensemble bounds.
- Paired primary development and palette comparisons against the immutable v29 baseline.
- 16,384 scheduled phrases with explicit ownership. There were no unselected foreground owners, wrong-owner events, out-of-scale pitched events, slow foreground range escapes, or scheduled Zap/Noise effects.
- 160 legacy channel-selector combinations producing identical neutral recipes. Selected legacy channels also produced sample-exact identical audio. Legacy rerender calls preserved elapsed position, history and continued audio.
- All four intent snapshots reconstructing 12 seconds sample exactly. Four further 30-second PCM renders were finite, bounded and non-silent.
- History retaining listened duration when a new tune begins and remaining capped at 20 tunes.
- A native export from seconds 10 to 40 writing the exact expected **5,760,000 PCM bytes**, and a 45-second finite ending remaining bounded.
- Foreground harmony taking priority over an active and a pending accompaniment pad, releasing the old voice before replacement within 10 ms. The targeted render kept one active pad and retained nonzero output.
- Muting the main lead preserving a bass-owned answer; muted primary bass/harmony tests; a completely muted 45-second finite export staying exactly silent through arrivals, silence recovery and its final cadence. Independently enabled historical support controls retain their own meaning.
- 800,000 accelerated drum mutations retaining sparse rests and bounded hat probability mass. This preserves the v29 fix against gradual density creep.
- A complete three-minute live render keeping the same seed, avoiding an automatic ending, and remaining finite and non-silent. Its peak was **0.641663**, RMS **0.085879**; the host took approximately **4.69 s** for that particular render. This is not a claim about Android CPU speed or battery use.

Separate edited-recipe coverage is provided by `tools/edited_snapshot_v30.cpp` and `tools/test_edited_snapshots.py`. It covers all 40 controls, presence and absence, zero/extreme inputs, successive canonical reloads and channel neutrality. Its final result was **48 cases, 144 reloads, 23,040,000 equal float samples and 12,582 assertions**. This covers the inherited edited-recipe drift that ordinary unedited replay tests would not detect. Metadata-container and audio-control findings are in the separate v30 reports; do not describe host helper tests as Android device export tests.

The complete 63-check native suite also passed AddressSanitizer and UndefinedBehaviorSanitizer without reported errors. Leak detection was disabled for the restricted host environment; this result makes no leak-detector claim. The normal build used `-O2 -Wall -Wextra -Werror`; the sanitizer build used `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`.

## Auditions

The companion `technomatic_2105_v30_auditions.zip` contains seven Opus files, recipes, a plain-text README and `probe.json`. Each is a 48-second stereo host render at 48 kHz. The actual live prefix is rendered before the excerpt is captured. The three intent examples use seconds 40-88. The paired v29/v30 examples use matching seconds 128-176.

| File | Seed | BPM | Interval | WAV peak | WAV RMS |
|---|---:|---:|---|---:|---:|
| `v30_slow_1013904226.opus` | 1013904226 | 77.018539 | 40-88 s | 0.641632 | 0.093286 |
| `v30_groove_2654435761.opus` | 2654435761 | 106.813927 | 40-88 s | 0.634674 | 0.085378 |
| `v30_intense_3668339987.opus` | 3668339987 | 168.518158 | 40-88 s | 0.529175 | 0.128606 |
| `v29_comparison_3041712678.opus` | 3041712678 | 61.722839 | 128-176 s | 0.457428 | 0.107437 |
| `v30_comparison_3041712678.opus` | 3041712678 | 61.722839 | 128-176 s | 0.626831 | 0.104319 |
| `v29_comparison_2514750925.opus` | 2514750925 | 86.390976 | 128-176 s | 0.501007 | 0.119397 |
| `v30_comparison_2514750925.opus` | 2514750925 | 86.390976 | 128-176 s | 0.546234 | 0.101254 |

All seven WAV sources had zero clipped samples. Every encoded Opus decoded to exactly **2,304,000 stereo frames**. Opus container duration can include preroll; decoded sample length was checked directly. Encoding used ffmpeg/libopus with a requested 160 kbit/s VBR. Tags include seed, version, intent, BPM, channel status and excerpt bounds. No loudness normalization was applied.

These are live native-engine excerpts, not the Android MediaCodec path and not finite exports with a generated ending. The three intent seeds are the first matching seeds in the deterministic sweep. The comparison seeds were selected by the baseline audit, not by subjective listening or an assertion that these short intervals reproduce the user's rare report.

## Reproduction and evidence

Run commands from the project root. A host C++17 compiler, Python 3 and ffmpeg/ffprobe are sufficient for the native tools and audition packaging.

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread tools/validate_v30.cpp app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_validate_v30
/tmp/technomatic_validate_v30

g++ -std=c++17 -O2 -pthread tools/audit_development.cpp app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_development_v30
/tmp/technomatic_development_v30 256 512

g++ -std=c++17 -O2 -pthread tools/audit_palette.cpp app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_palette_v30
/tmp/technomatic_palette_v30 256 128

g++ -std=c++17 -O2 -pthread tools/audit_foreground.cpp app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_foreground_v30
/tmp/technomatic_foreground_v30 64 256

g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread tools/validate_v30.cpp app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_validate_v30_sanitized
ASAN_OPTIONS=detect_leaks=0 /tmp/technomatic_validate_v30_sanitized
```

For the same observer against v29, use the immutable baseline header and implementation. Replace `/path/to/v29` with its extracted project directory:

```sh
g++ -std=c++17 -O2 -pthread -DTECHNOMATIC_AUDIT_V29 '-DTECHNOMATIC_ENGINE_HEADER="/path/to/v29/app/src/main/cpp/MusicEngine.h"' tools/audit_development.cpp /path/to/v29/app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_development_v29
/tmp/technomatic_development_v29 256 512

g++ -std=c++17 -O2 -pthread -DTECHNOMATIC_AUDIT_V29 '-DTECHNOMATIC_ENGINE_HEADER="/path/to/v29/app/src/main/cpp/MusicEngine.h"' tools/audit_palette.cpp /path/to/v29/app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_palette_v29
/tmp/technomatic_palette_v29 256 128
```

For an audition, compile `tools/render_preview.cpp` with the matching engine implementation. Its arguments are output WAV, duration, seed, legacy mask, legacy blend, legacy primary and live start time. For example:

```sh
g++ -std=c++17 -O2 -pthread tools/render_preview.cpp app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_render_v30
mkdir -p /tmp/technomatic_v30_previews
/tmp/technomatic_render_v30 /tmp/technomatic_v30_previews/v30_slow_1013904226.wav 48 1013904226 0 0 0 40 > /tmp/technomatic_v30_previews/v30_slow_1013904226.txt
```

Repeat the documented filenames, seeds and ranges from the audition table; compile the v29 renderer from the immutable v29 project for its two rows. Then run `python3 tools/package_v30_auditions.py` to encode, inspect decoded frame counts and tags, and create the seven-file audition ZIP.

Evidence lives in `tools/validation/`: `v30_native.log`, `v30_sanitizer.log`, `v30_engine_sha256.txt`, `v29_primary_development.csv`, `v30_primary_development.csv`, `v29_triggered_palette.csv`, `v30_triggered_palette.csv`, `v30_scheduled_foreground.csv` and `v30_comparison_summary.json`. `tools/package_v30_auditions.py` verifies and packages the documented host excerpts.

The paired audits are deliberately fast structural simulations. The planned audit queries the same grammar functions used by playback. The scheduled audit calls `onStep` and drains events per sixteenth; the palette audit also calls the real trigger functions. They do not reproduce every sustained-voice occupancy decision, exact performance-RNG ordering or PCM interaction. Separate full renders, sample-exact replay and the explicit pad-priority test cover those concrete execution risks. A finite seed/phrase sweep cannot establish absence of every future repetition or subjective resemblance. Recurring thematic material is intentional.
