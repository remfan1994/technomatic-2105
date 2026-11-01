TECHNOMATIC 2105 v29: native music validation

Scope

This report separates composition/scheduling measurements from listening.
No subjective listening evaluation is claimed. Counts of control weights do
not measure loudness. Host render speed does not measure Android device speed.

Reproducible sample

Each distribution uses 512 independent fresh engine instances at 48,000 Hz.
The seeds are uint32_t(i * 2654435761), i = 1..512. A fresh instance calls
prepare(48000), sets the channel selector, then reset(seed). This avoids
confounding the comparison with changes in recent-song selection history.

Channel configurations:
  No Channel: mask=0, blend=0, primary=0.
  All channels, Single: mask=4095, blend=0, primary=6.
  All channels, Neon Drift dominant: mask=4095, blend=1, primary=6.

Version 28 baseline

The original MusicEngine.cpp and MusicEngine.h were copied before v29 edits.
All 29 original tools/validate_v28.cpp checks passed on that original engine.

No Channel, 512 seeds:
  Tempo range: 82.39..169.39 BPM.
  Tempo median: 123.77 BPM.
  Tempo <= 90 BPM: 22/512 (4.3 percent).
  Tempo >= 135 BPM: 156/512 (30.5 percent).
  Winning style families: 14/32.
  Mean lead motif attacks: 12.50/16 steps.
  Mean hat attacks: 52.69/64 steps.
  Mean lead+bass+chord control-weight sum: 2.543.
  Mean auxiliary control-weight sum: 7.526.
  Mean texture/Fx/grain lane weights: 0.537/0.222/0.206.

All channels, Neon Drift dominant, same 512 seeds:
  Tempo range: 90.74..155.11 BPM.
  Tempo median: 121.19 BPM.
  Tempo <= 90 BPM: 0/512.
  Tempo >= 135 BPM: 98/512 (19.1 percent).
  Winning style families: 14/32.
  Mean lead motif attacks: 12.50/16 steps.
  Mean hat attacks: 53.62/64 steps.
  Mean lead+bass+chord control-weight sum: 2.495.
  Mean auxiliary control-weight sum: 7.443.

A separate all-channel Single baseline used primary=5 (Liquid Grid). That
configuration changed the winning candidate for 469/512 seeds and the style
for 450/512 seeds compared with No Channel. All-channel Hybrid with primary=5
preserved every winning candidate and style in that same sample. The original
v28 regression covered a one-bit Single mask, so it did not expose this issue.

The baseline source explains these measurements: candidate scoring rewarded
more note attacks; the instrument palette imposed a minimum active-layer
count; and a multi-channel Single selector consumed composition RNG draws.
The measurements do not establish that a given listener prefers one result.

Build and run current validation

Run these commands from the project root, with a C++17 host compiler:

  g++ -std=c++17 -O2 -pthread tools/validate_v29.cpp app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_validate_v29
  /tmp/technomatic_validate_v29

The v29 test includes the original v28 regression suite unchanged. Added
checks cover intent and tempo distribution, note/rest identity diversity,
channel identity isolation, sample-exact snapshot reloads, finite audio,
indefinite playback, and scheduled pitched-phrase diversity.

Produce exact CSV distributions:

  g++ -std=c++17 -O2 -pthread tools/audit_music.cpp app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_audit
  /tmp/technomatic_audit 512 0 0 0 > /tmp/v29_no_channel.csv
  /tmp/technomatic_audit 512 4095 0 6 > /tmp/v29_all_single.csv
  /tmp/technomatic_audit 512 4095 1 6 > /tmp/v29_neon_hybrid.csv

To compile the same audit against an extracted v28 source, use its header
and implementation together. Replace /path/to/v28 with that extracted root:

  g++ -std=c++17 -O2 -pthread '-DTECHNOMATIC_ENGINE_HEADER="/path/to/v28/app/src/main/cpp/MusicEngine.h"' tools/audit_music.cpp /path/to/v28/app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_audit_v28

Scheduled-phrase audit method

audit_music accepts optional arguments after primary:
  scheduled_phrases (default 0), single_seed (default 0).

For example, to inspect one seed for 256 phrases:
  /tmp/technomatic_audit 1 0 0 0 256 2654435761

The audit invokes the real onStep scheduler and drains its event queue after
each step. It skips 16 opening phrases and records complete 16-step phrases.
Each pitched event contributes voice kind, integer MIDI pitch, and onset
rounded to 1/8 of a sixteenth-note step. Pads contribute their chord pitches.
Drums, amplitude, pan, timbre, and random decimal timing are excluded from
the fingerprint. The audit counts exact phrase strings, not evolving seeds
or parameter hashes. A separate count checks for Zap/Noise special effects.

This is a fast structural scheduling simulation. It does not render every
voice between steps, so it does not reproduce the exact performance RNG
consumption or sustained-pad occupancy of a full audio render. Full PCM
snapshot and continuity tests cover the actual render path separately.
Finite measurements can establish observed diversity; they cannot prove
that every complete musical phrase stays unique for infinite playback.

Preview utility

  g++ -std=c++17 -O2 -pthread tools/render_preview.cpp app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_render_preview

Arguments are output WAV path, excerpt seconds, seed, channel mask, blend,
primary, and starting seconds. The prefix is rendered before the excerpt,
so development starts from the original live song state:

  /tmp/technomatic_render_preview /tmp/preview.wav 24 2654435761 0 0 0 40

Version 29 observed results

All assertions in tools/validate_v29.cpp passed on the final native source.
The full console output is retained in tools/validation/v29_native.log. The final
per-seed CSV distributions are retained in tools/validation/ alongside the relevant
v28 baseline distributions.

Distribution, same 512 independent seeds:

  No Channel:
    Tempo range 57.05..178.98 BPM, median 110.22 BPM.
    Tempo <= 90: 154/512 (30.1 percent).
    Tempo >= 135: 161/512 (31.4 percent).
    Winning style families: 32/32, versus 14/32 in v28.

  All channels, Single, primary=6:
    Tempo range 56.58..179.67 BPM, median 111.41 BPM.
    Tempo <= 90: 153/512 (29.9 percent).
    Tempo >= 135: 153/512 (29.9 percent).
    Candidate/style/intent differences from No Channel: 0/0/0.

  All channels, Neon Drift dominant:
    Tempo range 58.15..179.84 BPM, median 110.77 BPM.
    Tempo <= 90: 151/512 (29.5 percent), versus 0/512 in v28.
    Tempo >= 135: 167/512 (32.6 percent).
    Candidate/style/intent differences from No Channel: 0/0/0.

No Channel intent distribution and symbolic diversity:

  Intent 0, slow: 136 seeds, 31 winning style families.
    Mean BPM 69.37, lead attacks 5.39/16, hat attacks 22.29/64.
    Distinct lead note/rest phrases: 133/136.
    Distinct base bass note/rest phrases: 87/136.
    Distinct bass base+verse+answer note/rest grammars: 135/136.

  Intent 1, groove: 122 seeds, 31 winning style families.
    Mean BPM 98.90, lead attacks 6.95/16, hat attacks 29.31/64.
    Distinct lead note/rest phrases: 122/122.
    Distinct base bass note/rest phrases: 106/122.
    Distinct bass base+verse+answer note/rest grammars: 122/122.

  Intent 2, driving: 129 seeds, 32 winning style families.
    Mean BPM 128.98, lead attacks 8.63/16, hat attacks 37.52/64.
    Distinct lead note/rest phrases: 129/129.
    Distinct base bass note/rest phrases: 125/129.
    Distinct bass base+verse+answer note/rest grammars: 129/129.

  Intent 3, intense: 125 seeds, 30 winning style families.
    Mean BPM 162.69, lead attacks 10.38/16, hat attacks 46.40/64.
    Distinct lead note/rest phrases: 125/125.
    Distinct base bass note/rest phrases: 123/125.
    Distinct bass base+verse+answer note/rest grammars: 125/125.

The first draft of the test required 90 percent unique base bass phrases.
That assertion failed for slow and groove. Reviewing the actual generator
showed sparse root-focused cells, with separately generated verse and answer
development. The final test therefore requires 95 percent unique complete
base+verse+answer note/rest grammars and still reports raw base-phrase counts.
No gain, timbre, seed number, root transposition or harmony hash is included
in that identity measurement. The music was not made busier to satisfy an
arbitrary uniqueness threshold on a short bass anchoring cell.

Layer balance, No Channel:
  Mean foreground control weight: 2.807, versus 2.543 in v28.
  Mean auxiliary control weight: 1.596, versus 7.526 in v28.
  Maximum auxiliary/foreground control-weight ratio: 1.1315.
  Texture noise, Fx and grain lane weights: zero in all 512 cases.
These are arrangement controls, not a measurement of acoustic masking.

Development audit:
  Four representative seeds, three channel configurations, 256 phrases each.
  Seed 1013904226: slow; seed 2654435761: groove.
  Seed 2027808452: driving; seed 3668339987: intense.
  Total scheduled phrases inspected: 3,072, after opening prefixes.
  No Zap or Noise special-effect events were scheduled.
  No adjacent repeated complete pitched phrases were observed.
  No complete pitched phrase repeated within the previous 32 phrases.
  Nine cases had 256/256 distinct complete pitched phrases.
  Three cases had 255/256: slow Neon Hybrid, groove No Channel, groove Single.
The isolated distant repeats are retained in the report; this is not a claim
of a mathematical guarantee that no phrase ever repeats.

For the No Channel representatives, pitched upper-voice events per second
were 3.944 slow, 7.274 groove, 10.163 driving and 17.019 intense. These include
harmonic and supporting upper voices, not only the primary melody. Mean
upper-voice note durations were 0.325, 0.166, 0.109 and 0.063 seconds. This
confirms a change in actual articulation/density as well as the BPM range.

Indefinite drum-development regression:
  Each representative was subjected to 200,000 mutateDrumsOnly calls.
  Probability-mass and active-position bounds were checked every 1,000 calls.
  Total accelerated mutations: 800,000.
  Slow: initial hat mass 6.4637; observed maximum 7.5949; active hits 16 -> 16.
  Groove: initial 15.7448; maximum 17.3441; active hits 32 -> 32.
  Driving: initial 20.3141; maximum 22.6938; active hits 40 -> 40.
  Intense: initial 29.4048; maximum 32.0077; active hits 48 -> 48.
The original reflected random walk could gradually fill initially empty
drum positions. The final mutation stays near the generated reference rhythm.
This is an accelerated state-mutation test, not 800,000 audio phrases rendered.

Actual PCM and regression checks:
  The original v28 regression suite passed unchanged.
  128 additional channel comparisons retained the complete tested core.
  12 snapshots reconstructed six seconds of stereo audio sample exactly,
    including loading while a different channel selector was active.
  Export-range duration and finite-ending checks passed.
  A real 180-second stereo render retained the same seed, kept advancing its
    elapsed time, and entered no automatic live ending.
  That render was non-silent, all samples finite, peak 0.641663, RMS 0.090526.
  Host wall time for that 180-second render was 8.157 seconds; this is not an
    Android performance benchmark.

AddressSanitizer and UndefinedBehaviorSanitizer smoke checks passed using the
existing tools/smoke_v28.cpp against the final v29 engine. LeakSanitizer cannot
inspect the execution host's /proc/ptrace environment, so leak checking was
disabled for that run; no leak-free claim is made. Reproduction:

  g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread tools/smoke_v28.cpp app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_v29_sanitizer
  ASAN_OPTIONS=detect_leaks=0 /tmp/technomatic_v29_sanitizer

Final native source SHA-256:
  MusicEngine.cpp: 5a3a3c9f58209f2fee8235fd8925093bb9689c196ec06f4c95b7d7b1eba86658
  MusicEngine.h: e6e6cec1f09110fd9501265764ca8d6b77ee8d15be02b882f30178562d8c15f6

Listening previews

Three 48-second excerpts were rendered from live seconds 40..88 using No
Channel, so their arrangements had passed their generated opening:
  slow_1013904226: 77.0185 BPM.
  groove_2654435761: 106.8139 BPM.
  hyper_3668339987: 168.5182 BPM.
WAV masters and 160 kbit/s variable-rate Opus encodings were generated.
The Opus files carry seed, channel, engine and excerpt information. These
previews were encoded by the host utility; they are not evidence about the
Android metadata writer. Metadata-writer validation is documented separately.
