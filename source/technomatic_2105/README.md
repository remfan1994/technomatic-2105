# Technomatic 2105

Technomatic 2105 is an Android procedural electronic music player. It synthesizes music locally on the phone without samples, network access, accounts, advertising, analytics, trackers, or external audio assets.

Version 0.30.2 further increases average output loudness with a stronger fixed calibration. It preserves v30.1's removal of automatic whole-mix and coordinated part-volume changes. The v30 instrument vocabulary, per-song ensemble size, themes, complementary phrases, improvisation and slow-music grammar remain. Channels are discontinued. Musical percussion and pitched instruments are synthesized locally; there are no imported songs, samples, background hiss beds, noise/zap effects or delay effects.

One generated composition continues indefinitely within its own identity until the listener explicitly changes it. Next creates a new composition at the forward end of history; ordinary playback never automatically replaces its seed.

## Fixed playback level in v30.2

The output calibration increases from 1.0 to 1.75, giving +4.86 dB below the peak curve. This is one constant for all tracks and all times, with the same full-scale ceiling of 1.0. Strong peaks are smoothly rounded, so their increase is smaller than the linear gain. This is not distortion-free amplification: stronger calibration makes the peak curve act on more samples, especially in dense tracks. The measured comparison and its limits are in `docs/LOUDNESS_V30_2_VALIDATION.txt`.

Android's system media volume is the listener's volume control. The app uses a fixed output treatment and does not change the system setting. Kick-driven whole-mix ducking, automatic RMS gain riding, the old attenuating master control, transition fades and the export's whole-file tail fade are removed. The correction applies to both live playback and offline OGG/FLAC rendering.

Phrase and section controls also stop scaling the instruments' volumes together. Intro/outro ramps, Hook/Surge boosts, Shadow/OpenRelease dips and evolving section gain are removed from positive note amplitudes. The same phrase factors still decide note activity and structural entrances/exits. Fixed instrument balance, note accents, rolls, flams and individual attack/release envelopes remain, so musical development comes from the notes and their relationships.

The output uses the full digital range up to 100 percent, replacing the old ceiling of about 64 percent. Its fixed calibration and memoryless peak curve have no detector that turns the master up or down, no gain-recovery envelope and no lookahead delay. The curve rounds peaks within [-1, 1] without a moving volume control or a lower digital ceiling. Individual instruments retain their note envelopes and articulation; rests and changes in instrumentation can still change the amount of sound present.

This is an output revision of the v30 generator. Current recipes retain `engine=30` and record `outputrev=2`; OGG and FLAC expose that revision as `OUTPUT_MASTER_VERSION=2`. Old saved v30 and v30.1 compositions receive the current output when loaded. Their notes and seed identity remain, but the rendered waveforms differ because the output treatment changed. The controls, latest-20 history and memory model remain those of v30.1; cancelled scanner/rewind and proposed persistent naming features are not included.

## Live playback model

Fresh installs begin with:

```text
Playback: one indefinitely evolving sound
Selection: the full music generator
```

The current seed remains fixed during ordinary playback. The engine continues producing related statements, answers, returns, variations, layer changes and gradual bounded development without automatically replacing the sound with another seed.

A new composition seed is created only by an explicit action such as Next or loading another seed/history entry. There is no channel selector or named-channel coloration in v30. Old channel preferences and legacy channel controls cannot restrict the new generator.

Restart returns the current rendition to its beginning. Previous and Next navigate the latest-20 history when corresponding entries exist; Next creates a new seed only at the forward end of history.

## Main screen

```text
Start / Stop
Elapsed: <time in current sound>
Previous / Restart / Next
Advanced
Track Listing (latest 20)
Clear History
```

Elapsed time is informational and has no live playback limit. History rows show the seed and actual listened duration. Tap a row to load it. Long-press a row to copy its seed.

## Creative purpose

The owner publishes TECHNOMATIC 2105 Bismillah and associates its complementary musical relationships with remembrance of Allah. The engineering expression of that purpose is a coherent musical identity that remains open to continuing development:

```text
theme = stable rhythmic and tonal identity
reply = related transformation of the theme in its current context
development = exchanges that preserve relationships while changing phrases
```

The music itself carries this idea. There is no separate demonstration mode, imported commercial cue or fixed radio-song template. General scales, harmony, oscillator mathematics and synthesis techniques supply building blocks for generated compositions.

## Channel retirement

Channels were removed in v30. Main controls, history and the Metadata Editor no longer present channel choices. Legacy native setters remain inert compatibility entry points; old recipe channel fields are neutralized when loaded. A legacy named channel therefore cannot reactivate its former sound profile.

## Musical range and scoring

The seed chooses its listening intent before any of the 48 candidates are scored:

```text
Intent 0: Slow            56-84 BPM
Intent 1: Relaxed groove  84-112 BPM
Intent 2: Driving         112-144 BPM
Intent 3: Intense         144-180 BPM
```

Each intent has equal probability in the seed mapping. Short runs can naturally contain an uneven mix. Within these ranges, the generator independently varies pace, movement, syncopation, tonal color, harmony, bass weight, legato and ensemble size. The 32 existing sound-profile families remain starting points for synthesis and articulation; they are not stored songs or fixed riffs.

The old score rewarded additional notes and layers. The new score compares note counts, interval span, bass movement and accompaniment with the selected intent. A slow candidate is judged as a slow composition. Its longer articulation, rests and smaller ensemble are deliberate musical choices, not defects for the search to repair.

Drum sentences are generated from weighted rhythmic positions, pulse relationships and answers. Fixed drum-pattern and drum-DNA tables have been removed. A clear lead/bass/harmony foundation receives 1-6 supporting pitched instruments, with phrase-level accompaniment changes and call/response. The former minimum of 12 or 15 active lanes is gone.

Long-term drum changes are bounded around the original rhythm. Repeated mutation cannot gradually fill every rest and turn a sparse tune into a busy one. Pitched motifs retain their established bounded development and periodic theme returns.

Tonal color spans natural minor, Dorian, Phrygian, harmonic minor, minor pentatonic, major, Lydian, Mixolydian and major pentatonic. These are general musical building blocks; no human song or performer is used as a template.

## Foreground development and musical devices

v30 gives the foreground melody its own coordinated phrase plan. Each generated cycle contains 10-18 phrases, including one or two phrases of theme recall, related developments and exchanges, then a question, arrival and release. The cycle length and relationships belong to the composition; continuing cycles vary their note placement, contour turns, exchange points and participating voices. This is a generated form, not a stored song or a promise of a unique melody at every instant.

Five devices connect melodic development with the existing ensemble:

| Device | Musical behavior |
| --- | --- |
| Counterweight | A related answer changes contour around a generated exchange point and can pass to an existing bass or supporting voice. |
| Pocket Exchange | Interior notes move within bounded rhythmic windows while important phrase anchors remain. Another existing voice can take the foreground. |
| Held Question | The line leaves a suspended ending while percussion and harmony make room for it. |
| Confluence | Melody, bass and harmony meet at a shared generated arrival point, using the instruments already selected for the song. |
| Open Release | Activity falls after the arrival, leaving space around the phrase; an existing harmony voice can carry the foreground. |

When another instrument answers, it takes over the foreground role. Its ordinary accompaniment part yields, and other supporting parts leave room for the line. A sustained accompaniment pad cannot suppress a newly scheduled foreground harmony answer: the previous pad receives a short release before replacement, with no second active pad during that foreground handoff. Ordinary accompaniment can retain brief release-tail overlaps; sustained pad stacking remains restricted.

Slow compositions receive longer harmonic spans, a middle-register foreground, bounded voice leading and articulation that follows the actual spaces between developed notes. Intermediate phrases can end on a third, fifth or suspension instead of returning to the root every bar. These restrictions protect continuity while preserving a generated melody and changing phrases.

## Generated tension grammar

Tension is generated as part of each sound's identity. The old universal fixed pad-stab rhythm remains removed.

The engine chooses among several original devices:

```text
Vacuum
Convergence
Hinge
Shadow
Afterimage
Pad Breath (rare)
```

Their timing, spacing, pitch relationship, duration, accent, and recurrence cycle are derived from the seed. Most pressure events use short asymmetric electronic gestures. Sustained pad breath is uncommon and cannot stack into a repeating pad wall.

## Generated timbre grammar

v30 broadens the electronic sound vocabulary while retaining the existing ensemble roles and support-count rule. Each composition receives a stable instrument palette. Its instrument families remain fixed while articulation and timbral detail can develop within those families.

The grammar independently selects and shapes:

```text
20 bass synthesis models
32 lead synthesis models
18 pad synthesis models
12 drum-kit families
generated pad voicings
bass attack, release, glide, pulse width, and motion
lead attack, release, glide, vibrato, modulation, and air
pad attack, release, detune, motion, width, and voice count
drum body, metallic content, and noise balance
```

The larger catalogue is used across compositions. It does not add ensemble lanes or permit accumulating more instruments during a song. New families include reed-like odd-harmonic bodies, rounded struck tones with decaying upper partials, voiced tones with controlled saturation, and sustained tones whose harmonic balance opens gradually. Their oscillator, harmonic, envelope and nonlinear shaping equations supply families of related sounds rather than fixed recordings. There are no sample files, soundfonts or imported commercial patches.

The supporting lanes share at most three additional pitched families, giving a ceiling of six pitched synthesis families per composition across lead, bass, harmony and support. The v29 support-count selection rule is retained. Tone changes shape the chosen family rather than accidentally switching to a different family.

The retired Velvet Circuit profile cannot be selected or restored. Regular high-register sparkle and bell lattices were replaced with generated, phrase-related pitched responses, and legacy bell-like synthesis was revised toward harmonic instrument tones. These changes address the reported resemblance structurally. No source inspection can establish that every generated output will be unlike every existing musical phrase; there was no recording or seed for the reported example.

## Music engine

The engine separates three musical layers:

```text
Identity:
  seed, tonal center, generated harmony, generated lead grammar,
  generated bass grammar, fixed instrument palette, pulse character

Development:
  statements, answers, hook returns, verse variation, fragments,
  theme recall, counter-lines, arrangement devices, bounded evolution

Performance:
  velocity, timing, ornaments, omissions, fills, stereo motion,
  small timbral and rhythmic changes
```

A sound remains recognizable because its identity stays bounded. It avoids becoming a static loop because development and performance continue changing inside that identity.

The current composition path includes:

```text
48 symbolic composition candidates evaluated for each new sound
procedurally generated harmony paths
procedurally generated lead statement, hook, verse, answer, and recall phrases
procedurally generated bass statement, verse, and answer grammar
randomly generated counter, arpeggio, pulse, and ornament phrases
explicit phrase and section memory
recognition-oriented hook passages and freer exploratory passages
bounded multi-layer evolution with periodic returns toward the initial identity
long-term theme recall
session anti-repetition memory between manually generated sounds
```

The audio path uses fixed-size voices, no sample decoding, no neural model, no convolution and no file IO in the audio callback. Continuous texture and delay processing remain removed. Percussion is shaped as musical instrument attacks.

Repeated motifs are intentional: they make a tune recognizable. Generated development avoids a fixed repeating audio loop. A finite deterministic generator cannot promise that every conceivable note sequence is unique over mathematically infinite playback.

## Generated openings and endings

Openings are generated as part of each composition rather than imposed as a fixed four-bar buildup. Each seed receives a 3-8 phrase reveal grammar, one of six opening geometries, and independent entrance timing for drums, bass, lead, and harmony.

Live playback remains indefinite and therefore has no forced ending. Finite OGG/FLAC exports receive a 3-8 phrase ending grammar. Some resolve through a generated tonal cadence; others dissolve by allowing different layers to leave at different rates. Notes retain their own release envelopes; v30.1 does not apply a whole-file volume fade.

## Advanced screen

```text
Load seed
Current seed: tap to copy
Export start
Export end
Export filename / song title
Export OGG
Export FLAC
Metadata Editor
Cancel Export
```

Live playback is indefinite. Export Start and End define a finite timeline excerpt and do not limit, restart, or otherwise alter live playback.

Both formats snapshot the current generated sound and render it offline in parallel with listening:

```text
OGG:
  compact, lossy Opus audio in an OGG container

FLAC:
  lossless 48 kHz stereo audio
  larger files
  deterministic built-in encoder with no external codec dependency
```

Successful files are published to:

```text
Music/<filename>.ogg
Music/<filename>.flac
```

The Metadata Editor shows the current identity under Notes immediately, for example:

```text
Seed: 141414419
Engine: 30
```

Optional user notes are edited below that automatic identity. Save persists the user fields; seed and engine are regenerated from the actual current composition. Unsaved editor text survives a screen rotation. Back discards unsaved edits. Existing handwritten notes are preserved.

Both formats capture one immutable metadata snapshot at the start of export. Later changes to playback or the editor do not alter an export already in progress. Defaults are:

```text
Artist: Technomatic 2105
Album Artist: Technomatic 2105
Title: <filename>
Album: MONTHNAME DD YYYY
Genre: metadata value or Generative music
Comment/Description: seed + engine + user notes + export range
```

OGG physically embeds OpusTags; FLAC physically embeds Vorbis comments. Both include explicit SEED, export range, ENGINE_VERSION, OUTPUT_MASTER_VERSION, COMPOSITION_INTENT and TECHNOMATIC_SONG_DATA fields. The full recipe retains the winning candidate as well as the visible seed. Legacy CHANNEL_* fields remain explicitly neutral for older readers; they do not name or activate a sound profile in v30.

These commands inspect the actual file after it is copied from the phone:

```sh
opusinfo track.ogg
mediainfo track.ogg
ffprobe -v error -show_entries stream_tags:format_tags -of json track.ogg
metaflac --export-tags-to=- track.flac
mediainfo track.flac
```

The existing public Music export implementation requires Android 10 or newer. Playback supports Android 8 or newer. On an older supported phone, export explains its requirement before rendering.

## Version compatibility and validation

v30 changes the music-generation algorithm. Older seeds and recipes can be loaded, but v30 interprets them with the new engine and ignores retired channel selection; it does not recreate older waveforms exactly. Keep original audio and older projects when exact preservation matters. Within v30, complete saved recipes reconstruct the selected composition, and export uses an independent engine. Canonical edited recipes preserve the exact original override inputs separately from rounded display summaries, preventing repeated reloads from drifting. A full recipe retains the winning candidate and edit inputs; the visible seed alone is not a substitute for every saved setting.

Native, playback-control, and metadata validation are reproducible:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  app/src/main/cpp/MusicEngine.cpp tools/validate_v30.cpp \
  -Iapp/src/main/cpp -o /tmp/technomatic-validate-v30
/tmp/technomatic-validate-v30
python3 tools/test_fixed_output.py --output /tmp/technomatic-fixed-output-validation
python3 tools/test_audio_controls.py --output /tmp/technomatic-controls-validation
python3 tools/test_edited_snapshots.py --output /tmp/technomatic-edited-snapshots-validation
python3 tools/test_metadata.py --output /tmp/technomatic-metadata-validation
```

Metadata tests need a JDK and FFmpeg. MediaInfo, opusinfo, metaflac and flac are used when installed. The fixed-output tests cover the full-scale stateless curve, absence of gain riding, part-volume invariants, exact block-size/restart replay and unscaled finite exports. See `docs/V30_2_RELEASE_NOTES.txt` for this release's overview, `docs/OUTPUT_V30_2_DESIGN.txt` for the calibration, `docs/LOUDNESS_V30_2_VALIDATION.txt` for measured levels and replay checks, and `docs/BUILD_V30_2_VALIDATION.txt` for Android build results. The v30.1 reports retain that release's measurements and the explanation of its two original volume fixes.

The original v30 records remain in `docs/V30_VALIDATION.md`, `docs/EDITED_SNAPSHOT_V30_VALIDATION.txt`, `docs/METADATA_V30_VALIDATION.txt` and `docs/BUILD_V30_VALIDATION.txt`. `docs/SOURCE_AUDIT_V30.txt` scopes that release's asset/provenance review. Historical version reports and validators are retained as historical evidence; channel-specific older assertions do not describe v30 behavior. The host tests measure reproducibility, structural diversity and numerical safety; they do not replace listening on an Android phone.

## Android build

The included wrapper pins Gradle 8.9, which is compatible with Android Gradle Plugin 8.7.3. Use the wrapper rather than an unpinned system Gradle installation.

Required components:

```text
Android SDK platform: 33
Android build tools: 34.0.0
Android Gradle Plugin: 8.7.3
Gradle: 8.9, pinned by ./gradlew
JDK: 17
NDK: 30.0.14904198
CMake: 3.22.1
```

Open this directory in Android Studio and select the included Gradle wrapper, or build from a terminal.

The NDK pin matches the owner's installation at `/home/tutan/Android/Sdk/ndk/30.0.14904198`. Set Android Studio's SDK location to `/home/tutan/Android/Sdk` on that machine. The shared project uses `ndkVersion`, so other machines can use their own SDK root without modifying the project. `AGENTS.md` preserves this setting for future versions.

Debug build:

```sh
./gradlew assembleDebug
```

Unsigned release build:

```sh
./gradlew assembleRelease
```

Signed release build:

```sh
./gradlew assembleRelease \
  -PTECHNOMATIC_2105_RELEASE_STORE_FILE=/absolute/path/technomatic-2105-release.jks \
  -PTECHNOMATIC_2105_RELEASE_STORE_PASSWORD='store-password' \
  -PTECHNOMATIC_2105_RELEASE_KEY_ALIAS='technomatic-2105' \
  -PTECHNOMATIC_2105_RELEASE_KEY_PASSWORD='key-password'
```

Do not commit signing keys or passwords.

## Publication

Suggested tag:

```text
v0.30.2
```

Draft F-Droid metadata is under:

```text
fdroid/metadata/vip.thatiam.technomatic2105.yml
```
