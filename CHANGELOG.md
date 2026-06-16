# Changelog

## 0.21.2

- Changed Track History to Track Listing.
- Removed visible seed numbers from track rows.
- Kept long-press seed copy behavior.
- Added simple Genre Duration guide text.
- Changed history rows to full genre name plus duration.
- Reordered track listing newest-first.
- Stored actual selected genre mode in generated sound data for more accurate random-track labels.
- Bumped versionCode to 32 and versionName to 0.21.2.

## 0.21.0 - v21

- Added main-screen track history with tap-to-load and long-press seed copy.
- Changed Next to move forward through history before generating a new seed.
- Added Clear History.
- Added user-specified OGG filename field and public Music/<name>.ogg export target.
- Reordered Advanced so Load Seed is above Current Seed.
- Bumped versionCode to 30 and versionName to 0.21.0.

## 0.20.5

- Corrected OGG export semantics: export no longer restarts or alters live playback.
- Export now snapshots the current sound data and renders it offline as an independent job.
- Added candidate-index song data so snapshot export can regenerate the chosen composition more reliably instead of reselecting a different candidate.
- Stop/Next/Restart/genre/duration changes no longer affect an active export; only Cancel Export cancels it.
- Bumped versionCode to 29 and versionName to 0.20.5.

## 0.20.4

- Reworked OGG export as a fixed offline render of the current seed and duration.
- Reverted OGG packet timestamp handling to the on-device-working encoder behavior.
- Export no longer follows live playback into later generated sounds.
- Export status now shows render, encode, and publish phases.
- Export continues through Next/Restart/genre/duration UI changes unless Cancel Export or Stop is used.
- Bumped versionCode to 28 and versionName to 0.20.4.

## 0.20.3

- Fixed short-track OGG export cutoff behavior on 30-second exports.
- Export now captures a fixed duration before starting and validates the raw PCM length before OGG encoding.
- Restored a more conservative MediaCodec drain loop based on the earlier working exporter while keeping cancellation support.
- Offline export no longer cancels merely because live playback auto-advances; explicit transport actions still cancel export.
- Tightened short-track outro windows so a 30-second export does not spend too much of the piece in ending behavior.
- Bumped versionCode to 27 and versionName to 0.20.3.

## 0.20.2

- Fixed OGG export including several seconds of the next generated sound.
- Export rendering now disables live auto-advance and applies a file-end fade, so one export contains one generated sound only.
- Reduced MediaCodec dequeue waits to avoid very late export-complete popups on short tracks.
- Cancels an active export if the live source sound changes before export completes.
- Removed Share Last OGG and the share action from export-complete dialogs.
- Bumped versionCode to 26 and versionName to 0.20.2.

## 0.20.1

- Stability pass for the wrap-up build.
- Reset delay buffers, DC filter memory, texture state, voice state, and per-song recall memory when a generated sound changes.
- Changed genre-change restart to a native clean-regenerate path instead of tearing down the audio stream.
- Preserved recent symbolic anti-repetition hashes across manual Next and genre changes so sessions avoid repeated melodic/beat identities.
- Rechecked OGG-only export cancellation/publish path without adding WAV fallback.
- Bumped versionCode to 25 and versionName to 0.20.1.

## 0.20.0

- Reworked OGG export as a managed export job.
- Export button becomes Cancel Export while exporting.
- Stop, Restart, Previous, Next, Load Seed, and genre changes request export cancellation.
- Added native PCM-render cancellation for export jobs.
- Added OGG encoder cancellation checks and a no-progress stall timeout.
- Public export still targets Music through MediaStore.
- Bumped versionCode to 24 and versionName to 0.20.0.

## 0.19.0

- Expanded symbolic melody templates from 128 to 256 transformation variants.
- Expanded progression space to 80 paths and counter-line space to 48 shapes.
- Added per-song drum DNA overlays for more beat diversity without heavier audio DSP.
- Added recent motif-signature avoidance so consecutive generated sounds are less likely to reuse the same melodic grammar.
- Increased candidate composition search from 32 to 48 symbolic candidates.
- Cached per-style texture/delay traits to reduce per-sample profile recomputation.
- Added short seed/load-seed explanations to the Advanced screen.
- Bumped versionCode to 23 and versionName to 0.19.0.

## 0.18.2

- Fixed OGG export publishing. Exported files now publish through MediaStore into Music instead of app-specific external storage.
- Advanced screen now shows the last export path and offers Share Last OGG after a successful export.
- Export now shows a result dialog with a clear success path or failure reason instead of relying on a silent toast.
- Bumped versionCode to 22 and versionName to 0.18.2.

## 0.18.1

- Engine-focused release built on the v0.18.0 UI.
- Expanded internal style families from 24 to 32 with new abstract engine states: Copper Chord, Ghost Meter, Obsidian Bloom, Voltage Moth, Quartz Tide, Static Cathedral, Mercury Thread, and Night Latch.
- Increased symbolic candidate search from 24 to 32 candidates.
- Expanded motif template space to 128 templates with additional contour transforms.
- Expanded harmonic progression pool from 40 to 52 paths.
- Expanded counter-line shapes from 24 to 32.
- Added two larger phrase-form structures for more long-form development.
- Added theme-braid and bass-answer behaviors for more layer conversation.
- Bumped versionCode to 21 and versionName to 0.18.1.

## 0.18.0

- Reworked main screen around Start/Stop, Genre, Elapsed, Previous, Restart, Next, and Advanced.
- Removed saved-name/list UI again in favor of seed-based Advanced controls.
- Added Advanced seed copy/load flow.
- Added experimental OGG export from the beginning of the current sound.
- Added one-line genre descriptions.
- Added stronger phrase devices, expanded candidate search, motif variants, progressions, and counter-lines.


## 0.17.0

- Restored lightweight saved sounds with user naming, loading, renaming, deleting, and hidden generator data.
- Added sound lists with add-to-list, play-list, delete, open, move up/down, and remove item controls.
- Added service-side list playback so notification Next advances list items while the app is backgrounded.
- Reworked main, library, list, and genre screens to scroll in landscape and use wider layouts on wider screens.
- Moved -- No Genre -- to the top of the genre list as a special style-family option.
- Added a native genre-state hard reset path to reduce first-track genre carryover after selector changes.
- Deepened the music engine with more symbolic candidate search, motif variants, harmonic paths, counter-line shapes, and long-memory theme behavior.
- Bumped versionCode to 19 and versionName to 0.17.0.

## 0.16.2

- Fixed main-screen genre/duration touchbox text clipping.
- Fixed Genre Selector layout on narrow screens.
- Random now has its own row; Pool and Hybrid sit underneath.
- Shortened tappable main-screen labels to two lines and increased touchbox heights.
- Bumped versionCode to 18 and versionName to 0.16.2.

## 0.16.0

- Added Infinite duration mode so a composition can continue until NEXT is pressed.
- Changed Custom duration to Minutes and Seconds only, with overflow clamped to 60 for each field and a minimum usable duration of 8 seconds.
- Added Pool and Hybrid generation modes to the Genre Selector.
- Pool mode chooses one selected style family per new generated piece; Hybrid mode blends selected style families through a dominant-primary influence model.
- Made Current Genre and Duration rows more obviously interactive on the main screen.
- Improved Random/genre checkbox behavior: tapping a dimmed genre disables Random, and unselecting the final genre falls back to Random with a visible message.
- Fixed genre-change carryover by forcing a clean composition, voice, delay, AGC, transition, and hash-state reset when style selection changes while playing.
- Added composition endings: some pieces now form conclusive outros, while non-conclusive pieces fade out before the duration boundary.
- Deepened the music engine with more symbolic candidates, motif variants, progressions, counter-line shapes, and theme-memory behavior.
- Bumped versionCode to 16 and versionName to 0.16.0.

## 0.15.0

- Made duration and current-genre controls explicit tappable buttons.
- Changed genre selection from mood labels to original Technomatic style-family pools.
- Added No Genre as a selectable raw-engine option.
- Made selected genre checkboxes auto-activate when tapped from Random mode.
- Changed unselecting the last genre to fall back to Random with a visible message.
- Main screen now shows the actual current genre mode for the playing piece.
- Genre changes while playing now force a clean new piece to prevent old-style carryover.
- Custom duration now uses separate Hours, Minutes, and Seconds inputs with capped values.
- Deepened the music engine with a larger motif-shape library and expanded harmonic progression pool.
- Increased symbolic candidate search before playback and strengthened scoring for phrase contour, answer contrast, strong anchors, and motif recognizability.
- Expanded counter-line shape behavior and strengthened long-track theme recall logic.


## 0.14.0

- Replaced the generic mood selector labels with original Technomatic style families: Chrome Pulse, Velvet Circuit, Glass Trap, Dust Machine, Liquid Grid, Neon Drift, Broken Speaker, Deep Magnet, Pixel Ritual, Soft Voltage, Heavy Orbit, and Cold Arcade.
- Removed the genre Apply button; selector changes now save immediately.
- When music is playing, genre changes automatically start a new generated piece.
- Made the genre selector honor Android back by returning to the main player instead of leaving the app.
- Changed the main playback button from Play/Pause to Start/Stop.
- Replaced the track-seconds text field and Apply Seconds button with a tappable elapsed/total-time line.
- Added preset duration choices: 30 sec, 1 min, 3 min, 5 min, 10 min, 20 min, Custom, and Random.
- Added native random-duration mode for new generated pieces.
- Updated the bottom main-screen notice text.
- Bumped versionCode to 14 and versionName to 0.14.0.

## 0.13.0

- Replaced the main-screen single genre control with a dedicated Genre Selector screen.
- Added Random as the top genre selector checkbox, checked by default.
- Disabled individual genre checkboxes while Random is checked.
- Added multi-select genre masks so users can combine genre biases when Random is unchecked.
- Kept the v12 simplified player: Play/Pause, Next, elapsed time, track seconds, and no phone editor/save/load/counter UI.
- Bumped versionCode to 13 and versionName to 0.13.0.

## 0.12.0

- Removed phone-side generator editor, saved-sound naming/loading, and the track counter.
- Set default track duration to 180 seconds for public sharing while retaining manual seconds entry.
- Added native genre-mode biasing and symbolic candidate composition scoring.
- Strengthened melody, motif, call-and-response, counter-melody, and long-memory pressure.
- Explicitly permits Android audio playback capture for external recording tools.

## 0.11.0

- Deepened the music engine with theme-level generation above motif generation.
- Added long-term theme recall, call-and-response behavior, and counterpoint-like reply lines.
- Expanded internal abstract style families from 20 to 24.
- Added more synthetic musical lanes: pluck, bell, pulse, grain, comet, and rotor.
- Widened layer behavior so pieces can carry more simultaneous musical roles without samples.
- Updated the generator editor range and fields for the expanded engine.
- Bumped versionCode to 11 and versionName to 0.11.0.

## 0.10.0

- Renamed the app to Technomatic 2105 with package id `vip.thatiam.technomatic2105`.
- Cleaned up the main player around Play/Pause, Next, Current Sound, elapsed time, track counter, Save Sound, Load Sound, and Sound Data Editor.
- Removed automatic phonetic song names from the user interface.
- Added user-named saved sounds backed by hidden generator data.
- Added a separate saved-sounds screen with load and delete confirmation.
- Added a human-readable generator-data editor for seed, duration, musical values, and instrument-lane values.
- Made edited generator values affect the native engine instead of acting as seed-only labels.
- Replaced the old human-template arrangement device with original arrangement devices: suspension, orbit, mirror, cascade, and surge.
- Expanded style families from 16 to 20.
- Expanded simultaneous instrument lanes and synthetic timbre variety.
- Increased melody/motif prominence while keeping all sounds locally synthesized.
- Replaced launcher artwork with a no-bolt Technomatic 2105 icon.
- Bumped versionCode to 10 and versionName to 0.10.0.

## 0.8.0

- Added reversible phonetic song names for remembered/generated tracks.
- Added REMEMBER and LOAD SONG NAME controls.
- Added native seed/duration loading path so a song name can regenerate the same composition.
- Expanded from 12 to 16 internal abstract electronic style families.
- Widened the per-song instrument window so generated tracks use fuller arrangements.
- Added auxiliary arp, counter-melody, chord-stab, drone, sparkle, and FX lanes.
- Increased occasional breakdown/drop/return drama while preserving intra-song grammar.
- Bumped versionCode to 8 and versionName to 0.8.0.

## 0.7.0

- Renamed app to Technomatic 2105.
- Changed package/application id to `vip.thatiam.technomatic2105` for first public publishing.
- Expanded the music engine to 12 internal abstract electronic style families.
- Added per-composition instrument palette selection so songs use different subsets of synthetic voices.
- Broadened synthesized bass, lead, pad, percussion, and texture timbres without samples.
- Increased occasional dramatic breakdown, hold, hook, and climax behavior while preserving intra-song grammar.
- Updated GitHub/F-Droid/Fastlane metadata for the new name.
- Bumped versionCode to 7 and versionName to 0.7.0.

## 0.6.0

- Added custom launcher icon and round launcher icon.
- Added custom notification status icon.
- Added release build configuration with optional external signing properties.
- Added GitHub Actions workflows for CI and signed release builds.
- Added F-Droid metadata draft.
- Added Fastlane-style store metadata.
- Added license, privacy, release, and publication documentation.

## 0.5.0

- Added user-set integer track length in seconds.
- Improved perceived loudness stability.
- Added section-level composition grammar for long tracks.
- Retained background and screen-off playback.

## 0.4.0

- Introduced per-piece musical grammar.
- Added main motif, answer motif, variation motif, and phrase roles.
- Added NEXT behavior.

## 0.3.0

- Added multi-style scene generation.
- Added fade/dead-air transitions between scenes.
- Added stronger lead, arp, and phrase lanes.
