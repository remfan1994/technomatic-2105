# Technomatic 2105

Technomatic 2105 is an Android procedural electronic music player. It synthesizes music locally on the phone without samples, network access, accounts, advertising, analytics, trackers, or external audio assets.

Version 0.26.0 retains one live playback model: a generated sound continues indefinitely and develops within its own identity until the listener explicitly changes it. The former Continuous Radio and Hold Sound modes, their automatic seed replacement, and their live duration policy have been removed.

## Live playback model

Fresh installs begin with:

```text
Channel: No Channel
Playback: one indefinitely evolving sound
```

No Channel leaves the generator unrestricted. The current seed remains fixed during ordinary playback. The engine continues producing related statements, answers, returns, variations, layer changes, and gradual bounded development without automatically replacing the sound with another seed.

A new sound is created only by an explicit action such as:

```text
Next
loading a seed
loading a history entry
changing the Channel selection
```

Restart returns the current sound to its beginning. Previous and Next navigate the latest-20 history when corresponding entries exist; Next creates a new seed only at the forward end of history.

## Main screen

```text
Start / Stop
Channel: <current channel>
Elapsed: <time in current sound>
Previous / Restart / Next
Advanced
Track Listing (latest 20)
Clear History
```

Elapsed time is informational and has no live playback limit. History rows show the frozen Channel identity for each sound. Tap a row to load it. Long-press a row to copy its seed.

## Channels

The Channel selector is optional. It provides predictable character without replacing the unrestricted generator.

```text
No Channel
Chrome Pulse
Velvet Circuit
Glass Trap
Dust Machine
Liquid Grid
Neon Drift
Broken Speaker
Deep Magnet
Pixel Ritual
Soft Voltage
Heavy Orbit
Cold Arcade
```

No Channel is 100 percent unrestricted generation. Each named Channel is approximately:

```text
50 percent unrestricted generator
50 percent selected Channel character
```

Hybrid Channels combine several selected Channels. The first selected Channel remains dominant; later selections provide weaker secondary influence. Channel state is frozen into the generated sound, seed snapshot, history entry, and OGG export.

## Generated tension grammar

v26 removes the universal fixed pad-stab rhythm that previously made unrelated sounds share the same recurring low-mid tension gesture. Tension is now generated as part of each sound's identity.

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

## Music engine

The engine separates three musical layers:

```text
Identity:
  seed, tonal center, generated harmony, generated lead grammar,
  generated bass grammar, palette, pulse character, Channel bias

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

The audio path remains conservative for low-cost Android hardware: fixed-size voices, no sample decoding, no neural model, no convolution, and no file IO in the audio callback.

## Advanced screen

```text
Load seed
Current seed: tap to copy
OGG duration
OGG filename
Export to OGG
```

Live playback is indefinite. OGG duration is a separate finite export setting and does not limit or restart live playback.

Export snapshots the current generated sound and renders it offline in parallel with listening. Successful files are published to:

```text
Music/<filename>.ogg
```

Android MediaStore metadata is supplied as:

```text
Artist: Technomatic 2105
Title: <filename> [<seed>]
Album: MONTHNAME DD YYYY
Genre: current Channel
```

## Android build

The included wrapper pins Gradle 8.9, which is compatible with Android Gradle Plugin 8.7.3. Use the wrapper rather than an unpinned system Gradle installation.

Required components:

```text
Android SDK platform: 33
Android Gradle Plugin: 8.7.3
Gradle: 8.9, pinned by ./gradlew
JDK: 17
NDK: 26.3.11579264
CMake: 3.22.1
```

Open this directory in Android Studio and select the included Gradle wrapper, or build from a terminal.

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
v0.26.0
```

Draft F-Droid metadata is under:

```text
fdroid/metadata/vip.thatiam.technomatic2105.yml
```
