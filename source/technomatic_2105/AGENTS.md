# TECHNOMATIC 2105 project continuity

Read this file before changing this project. These are the owner's continuing
project preferences, updated for version 30.2 on 2026-10-20 UTC.

## Android toolchain

- Keep the NDK version pinned to `30.0.14904198` in `app/build.gradle` and CI.
- The owner's installed NDK is `/home/tutan/Android/Sdk/ndk/30.0.14904198`.
- The owner's SDK root is `/home/tutan/Android/Sdk`.
- Use `ndkVersion` so Android Studio resolves that installation. Do not put
  the owner's absolute home path into a shared `ndkPath` or `local.properties`.
- Do not revert to NDK 26 or silently replace this pin in a later version.
  Change it only in response to a new owner instruction or a concrete build
  incompatibility that has been explained to the owner.

## Musical purpose

- Generate original music locally, without imported songs, commercial cues,
  sample packs, or imitation of named artists, franchises, or recordings.
- Preserve recognizable thematic relationships, complementary statements
  and answers, and substantial continuing improvisation in the current seed.
- The owner connects this reciprocal musical idea with remembrance of Allah
  and publishes the project Bismillah. Respect that intent without adding
  unrelated UI or making theological claims about software behavior.
- Keep the music itself central. Do not add special effects, noise beds,
  reward jingles, or a separate demonstrational mode.
- Offer slow, relaxed, driving and intense music through the same generator.
- Version 30 expands the available instrument vocabulary across songs while
  preserving version 29's per-song palette size and ensemble bounds.
- Channels are discontinued in version 30. Do not restore their controls or
  named sound profiles. Legacy channel fields may remain inert for file/API
  compatibility.
- Avoid the legacy high sparkling/bell accents that the owner associated
  with game reward cues. General musical scales and harmony remain available.

## Output level

- The owner listens through ordinary earbuds in busy commercial streets.
  Version 30's output was too quiet; preserve its music while correcting level.
- Keep app playback at a fixed full level under Android's system media volume.
  Do not change the system volume or bypass its controls or protections.
- The owner explicitly requests the full digital range up to 100 percent.
  Do not reintroduce the old 64-percent ceiling or another lower app-level cap.
  Keep source samples within [-1, 1]; distinguish sample limits from measured
  reconstructed/codec peaks instead of silently adding a lower ceiling.
- Do not raise or lower the whole mix for musical expression. No kick-driven
  ducking, automatic gain riding, master swells, fades or random dead air.
- This also prohibits coordinated part-volume envelopes in the composition
  code: no intro/outro gain ramps, Hook/Surge boosts or Shadow/OpenRelease dips.
  Phrase and section factors can decide note activity, entrances and exits;
  they must not scale positive scheduled note amplitudes together. Preserve
  structural silence, per-note accents and fixed instrument balance.
- Do not add a dynamic compressor or lookahead gain controller as a volume fix.
  Use a fixed output calibration and, when needed, a memoryless peak curve.
- Individual notes retain their attack, decay, articulation and musical rests.
  Those instrument behaviors are distinct from changing the master volume.
- Version 30.2 uses fixed gain 1.75, knee 0.40 and ceiling 1.0. It is a
  measured calibration, not a guarantee of identical loudness across seeds
  or clipping-free reconstructed/codec peaks. Do not add a moving controller.
- Apply the same output treatment to live playback and offline exports. Record
  an output revision separately from the composition engine version.
- Test quiet/sparse and dense music, average loudness and reconstructed peaks.
  Report measured changes without claiming a real street or phone listening test.

## Scope of this release

- Version 30.2 is a further output-level correction requested after v30.1.
- Preserve v30.1's composition, instrument voices, note amplitudes, controls,
  latest-20 history and memory behavior; this is not a new music engine.
- Scanner, rewind and progress-bar work was cancelled by the owner.
- Persistent track naming and the proposed v31 history work are on hold.
  Do not include those changes in a volume-only release.

## Integrity and delivery

- Keep automatic seed metadata and the full regeneration snapshot embedded
  in OGG/Opus and FLAC exports. Preserve user-entered metadata and notes.
- Validate the primary melodic line itself; aggregate-event fingerprints
  can conceal a repetitive foreground melody behind changing accompaniment.
- Measure instrument palette/ensemble bounds as well as timbral diversity.
- Distinguish structural audits, actual PCM tests and Android device tests.
  Do not claim listening quality or device testing that was not performed.
- Do not promise mathematically infinite uniqueness from finite tests.
- Keep user-facing reports and project continuity notes in ASCII.
