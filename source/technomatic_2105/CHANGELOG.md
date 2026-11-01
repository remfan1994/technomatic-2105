# Changelog

## 0.30.1 - v30.1

- Corrected the quiet output reported in v30 with fixed output calibration shared by playback and offline exports.
- Removed kick-driven whole-mix ducking, RMS automatic gain control, the attenuating master setting, legacy transition fades/dead air and the export's whole-file tail fade. Android's system media volume remains under the listener's control.
- Removed coordinated volume envelopes from the composition's part amplitudes, including intro/outro ramps, Hook/Surge boosts and Shadow/OpenRelease dips. Preserved their note activity decisions, structural silence, fixed role balance and individual note accents/envelopes.
- Replaced the old roughly 64-percent digital ceiling with the full range up to 100 percent, using a memoryless peak curve without a moving gain controller, compressor recovery envelope or lookahead delay. Individual notes retain their synthesis envelopes, articulation and rests.
- Kept the v30 composition engine, instrument catalogue, ensemble size, slow-music grammar, theme development, history and retired-channel behavior.
- Kept engine=30 and added outputrev=1 to native snapshots and OUTPUT_MASTER_VERSION to embedded OGG/FLAC metadata. Old saved v30 recipes use the corrected output; their rendered waveforms change with the output treatment.
- Fixed both Next paths retaining the preceding composition's backbeat delay. Seed-local timing now agrees with a fresh load of the saved recipe without changing normal reset/load timing or composition randomness.
- Retained NDK 30.0.14904198 and recorded the fixed-output requirement in AGENTS.md for future versions.
- Bumped versionCode to 43 and versionName to 0.30.1.

## 0.30.0 - v30

- Pinned the owner's NDK version to 30.0.14904198 in Gradle and Android workflows. Recorded the installed path and future-version preference in AGENTS.md without hardcoding a machine-specific SDK path into the shared build.
- Discontinued Channels. Removed the selector and channel labels from playback, history and the Metadata Editor; retired channel preferences and controls cannot influence new or loaded music. Legacy recipe fields are neutralized for compatibility.
- Expanded the synthesized instrument catalogue from 12 to 20 bass models, 16 to 32 lead models, 10 to 18 harmony/pad models and 8 to 12 drum-kit families. The v29 support-count rule and ensemble roles remain; supporting roles share a bounded palette rather than adding instruments.
- Made instrument-family selection stable within a composition. Articulation and tone can evolve without changing to an unrelated synthesis family because of an incidental color offset.
- Added a shared foreground phrase plan with generated development cycles, short recognizable theme recalls, related melodic and rhythmic transformations, and exchanges between existing voices.
- Added Counterweight, Pocket Exchange, Held Question, Confluence and Open Release as coordinated musical devices. Climaxes and releases use the current ensemble, generated pitch relationships and dynamics rather than sound effects.
- Replaced fixed high sparkle/bell placement and revised legacy chiming tones. Removed the retired Velvet Circuit profile. All music remains local oscillator-based synthesis, with no imported recordings, soundfonts, MIDI songs or commercial cue assets.
- Gave slow music longer harmonic spans, a controlled melodic register, bounded voice leading, connected articulation and intermediate third/fifth/suspension endings. Applied register checks after cadence and arrival generation, including actual harmony voices and finite-export closing notes.
- Fixed sustained accompaniment pads blocking a harmony-owned foreground answer. The old pad releases before replacement without overlapping a second active pad during the handoff.
- Fixed edited-recipe drift on repeated loading by preserving exact original override inputs separately from readable derived summaries. Foreground ownership now respects the selected instrument's gain, so a muted lead cannot silence an existing bass answer; muted roles remain muted.
- Kept physically embedded OGG OpusTags and FLAC comments. Automatic Notes now show the actual seed and engine version; optional user notes remain editable. Legacy channel tags are explicitly neutral.
- Added primary-melody and actual-trigger palette audits, native and playback-control regressions, and independent metadata inspection. Historical validators and reports remain available as historical evidence.
- Bumped versionCode to 42 and versionName to 0.30.0. v30 reinterprets older seeds with the new engine; it does not reproduce v29 waveforms exactly.

## 0.29.0 - v29

- Fixed OGG metadata: exported Opus files now contain real OpusTags, rather than only Android MediaStore catalog values. OGG and FLAC share one immutable metadata snapshot captured before rendering.
- Added automatic Notes showing the current seed, selected channels, and dominant hybrid channel, plus separate optional user notes. Both formats embed COMMENT and DESCRIPTION, explicit seed/channel/range fields, engine version, composition intent, and the complete regeneration recipe.
- Channel changes while stopped now update the actual rendition and metadata immediately without starting audio. Serialized stream-close/control operations and stale-callback handling are covered by 71 host checks.
- Added a streaming Ogg tag writer that preserves audio packets and timing, verifies page checksums, and handles metadata growth/shrinkage across page boundaries. Export cancellation removes partial output.
- Replaced the dense-arrangement bias in the 48-candidate search with seed-selected listening intent and target-relative musical scoring. Slow, relaxed, driving, and intense compositions have distinct tempo, activity, articulation, and ensemble ranges.
- Replaced fixed drum-pattern and drum-DNA tables with generated rhythmic sentences, related answers, intentional rests, and bounded long-form development.
- Fixed indefinite drum-density creep: long playback preserves intentional rests through bounded variations around the generated reference rhythm.
- Removed the minimum 12/15 active-lane floor. A lead/bass/harmony foundation receives a generated supporting ensemble with phrase-level call and response.
- Removed continuous texture hiss, noise/zap effects, free-running delay, and noise in pitched lead voices. Strengthened pitched fundamentals and harmonic relationships while preserving generated synthesis and articulation variation.
- Expanded tonal color to nine scale families, including major, Lydian, Mixolydian, and major pentatonic alongside the existing minor modes.
- Renamed No Channel to Auto (full range), added All channels, and added hold-to-select the dominant channel. Existing channel preferences are retained.
- Reworked channel color to preserve the selected composition's slow/intense range. Hybrids retain the dominant channel and choose a weaker secondary color from the selected set per rendition, avoiding an average of all colors.
- Fixed multi-channel Single selection consuming composition randomness and changing the winning candidate.
- Preserved indefinite playback, recurring themes, continuing improvisation, latest-20 history, same-core channel renditions, exact v29 snapshot reconstruction, and finite export ranges.
- Added native distribution/development validation and independent OGG/FLAC metadata tests with FFmpeg, MediaInfo, opusinfo, metaflac, and flac.
- Added an early Android 10 requirement message for public Music export; playback still supports Android 8 and newer.
- Bumped versionCode to 41 and versionName to 0.29.0. The new engine changes the interpretation of old seeds; v28 audio is not reproduced sample-for-sample by v29.

## 0.28.0 - v28

- Reworked Channels as renditions of the current generated composition: changing Channels now preserves the visible seed, winning candidate, harmony, lead and bass grammar, motifs, form, and musical memory.
- Channel changes restart the same composition at 0:00 with a clean audio state, allowing direct comparison of the same introduction under No Channel, a named Channel, or a dominant Hybrid Channel.
- Kept Next as the explicit new-seed operation; Channel selection no longer creates an unrelated seed.
- Expanded latest-20 history rows to show Channel, seed, and actual listened duration, with separate entries for same-seed Channel renditions.
- Replaced single export duration with Start and End time controls for OGG and FLAC timeline excerpts from an indefinitely evolving sound.
- Added a separate Metadata Editor for Artist, Album Artist, Album, Genre, and Comment; export filename remains the song title.
- Rebuilt openings as 3-8 phrase, seed-specific reveal grammars with six geometries and independently ordered drum, bass, lead, and harmony entrances.
- Rebuilt finite export endings as 3-8 phrase generated arrivals or staggered dissolves, with five ending geometries and phrase-level cadence placement instead of a rushed final-second fade.
- Preserved the full 48-candidate composition search and all-local electronic synthesis.
- Bumped versionCode to 40 and versionName to 0.28.0.

## 0.27.0 - v27

- Added FLAC as a second offline export format alongside OGG.
- Added a deterministic built-in 48 kHz stereo lossless FLAC writer with embedded title, artist, album artist, album date, genre, comment, and seed metadata.
- Unified OGG and FLAC under one snapshot export job with shared duration, filename, progress, cancellation, MediaStore publishing, and live-playback independence.
- Added separate Export OGG and Export FLAC controls in Advanced.
- Added a generated per-sound timbre grammar while preserving the full 48-candidate composition search.
- Expanded the electronic synthesis bank to 12 bass models, 16 lead models, 10 pad models, and 8 drum-kit families.
- Added generated pad voicings plus seed-stable attack, release, glide, vibrato, modulation, width, motion, body, metallic, and noise characteristics.
- Folded timbre identity into session anti-repetition so otherwise related note grammars can be distinguished by their performed sound.
- Kept all sound local and synthetic: no samples, network audio, neural generation, FFT, or convolution.
- Added v27 validation for model coverage, timbre uniqueness, pad-voicing diversity, exact reconstruction, evolution, channels, history, export length, FLAC integrity, and render safety.
- Bumped versionCode to 39 and versionName to 0.27.0.

## 0.26.0 - v26

- Removed the universal 2/6/10/14 fixed pad-stab lattice that produced the recurring low-mid "DOO" tension effect across unrelated sounds.
- Replaced fixed stab and section-boundary pad behavior with a generated per-seed tension grammar.
- Added original tension devices: Vacuum, Convergence, Hinge, Shadow, Afterimage, and rare Pad Breath.
- Generated asymmetric tension timing, pitch relationships, duration, accent, cycle, phase, and articulation from each sound's existing identity seeds.
- Reworked chord, Bloom, Rotor, and Surge pressure events into short electronic chord gestures instead of repeated slow pad strikes.
- Limited sustained pad concurrency and made long pad articulation uncommon, preventing overlapping pad walls.
- Preserved all 48 composition candidates and the established lead, hook, verse, bass, harmony, channel, history, and export behavior.
- Added a regression test for seed 3290437499 around 1:27; candidate 44 remains selected while the reported sustained pad onset is absent.
- Added v26 validation for tension uniqueness, absence of the fixed lattice, pad rarity, exact reconstruction, evolution, channels, history, export duration, and render safety.
- Bumped versionCode to 38 and versionName to 0.26.0.

## 0.25.0 - v25

- Removed Continuous Radio and Hold Sound as separate playback policies.
- Made one indefinitely evolving current sound the only live playback behavior.
- Removed automatic seed replacement, live sound lifetimes, live duration settings, and mode controls.
- Kept bounded long-form motif, bass, harmony, layer, and arrangement development inside the current seed.
- Kept manual Next, seed loading, history loading, and Channel changes as explicit sound-change operations.
- Kept finite duration exclusively in the offline OGG exporter.
- Added a v25 native validator covering indefinite seed preservation, evolution, manual Next, channels, history, reconstruction, export duration, and render safety.
- Added an included Gradle 8.9 wrapper to prevent the Gradle 9 / Android Gradle Plugin 8.7.3 synchronization failure.
- Bumped versionCode to 37 and versionName to 0.25.0.

## 0.24.0 - v24

- Added explicit composition memory for generated motifs, section roles, recalls, and development history.
- Added gradual bounded evolution across lead, bass, counter, arpeggio, pulse, ornament, harmony, density, register, and palette behavior.
- Kept hook-like material highly recognizable while allowing verse-like and exploratory sections to vary more freely.
- Expanded fully generated pitched-layer grammar: lead statement, hook, verse, bass statement/verse/answer, counter-line, arp, pulse, and ornament phrases are generated per seed rather than selected as finished note sequences.
- Made Hold Sound preserve the current seed and identity while its internal musical development continues indefinitely.
- Restored dominant Hybrid Channels: the first selected channel remains primary and later selections contribute weaker secondary influence.
- Kept No Channel plus Continuous Radio as the default, unrestricted listening mode.
- Preserved the full 48-candidate composition search for every new sound.
- Added v24 native validation covering uniqueness, memory/evolution bounds, Hold Sound, Continuous Radio, hybrid dominance, exact reconstruction, history limits, and realtime render performance.
- Bumped versionCode to 36 and versionName to 0.24.0.

## 0.23.1 - v23.1

- Restored the full 48-candidate composition search for initial generation and automatic Continuous Radio transitions.
- Removed the reduced-candidate automatic-transition path introduced during v23 development.
- Kept the v23 generated bass and lead grammars, No Channel default, Continuous Radio behavior, Channels, and Hold Sound behavior.
- Bumped versionCode to 35 and versionName to 0.23.1.

## 0.23.0 - v23

- Made No Channel and Continuous Radio the default listening configuration.
- Replaced stored bassline skeletons with generated bass rhythm/pitch grammar.
- Replaced stored lead ancestors with generated melodic contour, rhythm, apex, cadence, and interval grammar.
- Derived answers, variations, fragments, recalls, counter-lines, and arpeggios from each sound's newly generated identity.
- Added bounded long-form melodic and bass evolution while preserving recognizable hooks.
- Reworked named genres into optional Channels: one selected channel contributes 50 percent character over the unrestricted engine.
- Removed Random, Pool, and Hybrid channel UI; one channel or No Channel is selected directly.
- Added Continuous Radio and Hold Sound playback modes.
- Moved finite duration control entirely to OGG export; main playback elapsed time is noninteractive.
- Updated track history, seed loading, export metadata, and landscape layout for the new model.
- Bumped versionCode to 34 and versionName to 0.23.0.

## 0.22.0 - v22

- Froze genre mask/blend data into each generated piece so later selector changes cannot bias the current or loaded history track.
- Fixed a likely genre-bleed source where profile rendering used the live selector rather than the piece's saved genre state.
- Capped track listing to the latest 20 entries to avoid UI slowdown.
- Made current genre display derive from the current generated sound data while playing.
- Bumped versionCode to 33 and versionName to 0.22.0.

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
