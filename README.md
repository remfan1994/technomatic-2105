# Technomatic 2105

Technomatic 2105 is an Android procedural electronic music app. It generates synthetic electronic tracks on-device without samples, network access, accounts, ads, analytics, trackers, or external audio assets.

v16 keeps the phone interface focused on immediate listening. The editor, saved-sound system, generated names, and counter remain removed. The main player uses Start/Stop, Next, explicit tappable genre and duration controls, and a dedicated Genre Selector screen using original Technomatic style-family names plus No Genre. v16 adds Infinite duration and Pool/Hybrid style-family selection.

## Current screens

```text
Main Player:
  Start / Stop
  Next
  Current Genre (tap to change)
  Duration / elapsed time (tap to change)
```

Tap the Duration line to choose:

```text
30 sec
1 min
3 min
5 min
10 min
20 min
1 hour
Custom
Random
Infinite
```

Custom opens two numeric fields:

```text
Minutes: 0-60
Seconds: 0-60
```

Values above 60 are clamped to 60. Infinite keeps the current composition alive until NEXT is pressed.

Genre Selector:

```text
Random
Pool / Hybrid
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
No Genre
```

Removed from the phone UI:

```text
saved sounds
song naming
load/delete library
track counter
generator-data editor
track-seconds text field
apply buttons
```

The phone app is aimed at simple listening: choose a style family or leave Random on, press START, and press NEXT when you want a new generated composition.

## Genre selector behavior

```text
Random checked:
  full engine selection
  individual style boxes are dimmed
  tapping any style box disables Random and selects that style

Random unchecked:
  one or more style families can be selected
  Pool mode selects one chosen style for each new piece
  Hybrid mode blends chosen styles while preserving a dominant identity
  unselecting the last style falls back to Random
  changes are applied automatically
  if music is playing, a clean new generated piece starts automatically
```

The selector does not retrieve old compositions. Pool mode selects the eligible style-family pool for newly generated tracks. Hybrid mode makes a new style-family mixture from the selected styles. The main player shows the actual current style identity.

## Music engine

The sound engine is fully synthetic/electronic. It uses no samples and does not try to imitate acoustic instruments.

v16 keeps the v11-v15 engine improvements and adds another music-focused pass:

```text
candidate composition scoring before playback
stronger motif/theme selection
stronger melody and counter-melody emphasis
more call-and-response pressure
more long-memory pressure inside tracks
24 internal abstract style families
expanded synthetic lane bank
larger motif-shape library
expanded harmonic progression pool
stronger phrase-contour scoring
stronger answer-contrast scoring
more counter-line shapes
clean forced regeneration when genre settings change
Pool and Hybrid style-family generation
Infinite composition duration mode
conclusive outros and planned pre-end fades
```

The visible Genre Selector maps user-facing Technomatic style families onto internal engine style-family pools. For example, Deep Magnet and Heavy Orbit bias toward low-register, bass-centered internal families; Pixel Ritual and Cold Arcade bias toward harder grid and bright pulse behavior.

The engine remains conservative in the audio callback: no allocation, no file IO, no neural model, no sample decoding, no convolution reverb.

## Android build

Open this directory in Android Studio and build the `app` module.

Required Android components:

```text
Android SDK platform: 33
Android Gradle Plugin: 8.7.3
Gradle: 8.9 recommended
JDK: 17
NDK: 26.3.11579264
CMake: 3.22.1
```

The app uses:

```text
Java: Activity and foreground playback service
C++17: procedural audio engine
Oboe: low-latency Android audio output
Kotlin: none
Samples: none
Network: none
```

Main files:

```text
app/src/main/cpp/MusicEngine.cpp
app/src/main/cpp/MusicEngine.h
app/src/main/cpp/AudioEngine.cpp
app/src/main/cpp/native-lib.cpp
app/src/main/java/vip/thatiam/technomatic2105/MainActivity.java
app/src/main/java/vip/thatiam/technomatic2105/AudioService.java
app/src/main/java/vip/thatiam/technomatic2105/NativeAudio.java
```

## Debug build

```sh
gradle assembleDebug
```

or build the debug variant from Android Studio.

## Unsigned release build

```sh
gradle assembleRelease
```

If no signing properties are provided, Gradle produces an unsigned release APK under:

```text
app/build/outputs/apk/release/
```

Unsigned APKs are useful for F-Droid submission review, but ordinary users cannot install an unsigned APK directly.

## Signed release build

Create a release keystore outside the repository:

```sh
keytool -genkeypair \
  -v \
  -keystore technomatic-2105-release.jks \
  -alias technomatic-2105 \
  -keyalg RSA \
  -keysize 4096 \
  -validity 10000
```

Then build with external signing properties:

```sh
gradle assembleRelease \
  -PTECHNOMATIC_2105_RELEASE_STORE_FILE=/absolute/path/technomatic-2105-release.jks \
  -PTECHNOMATIC_2105_RELEASE_STORE_PASSWORD='store-password' \
  -PTECHNOMATIC_2105_RELEASE_KEY_ALIAS='technomatic-2105' \
  -PTECHNOMATIC_2105_RELEASE_KEY_PASSWORD='key-password'
```

Do not commit the keystore or passwords. `.gitignore` blocks common signing file names.

## GitHub publication

Suggested repository setup:

```sh
git init
git add .
git commit -m "Technomatic 2105 0.16.0"
git branch -M main
git remote add origin git@github.com:r94/technomatic-2105.git
git push -u origin main
git tag v0.16.0
git push origin v0.16.0
```

Change `r94/technomatic-2105` if you use a different GitHub path.

## F-Droid preparation

This archive includes:

```text
fdroid/metadata/vip.thatiam.technomatic2105.yml
fastlane/metadata/android/en-US/title.txt
fastlane/metadata/android/en-US/short_description.txt
fastlane/metadata/android/en-US/full_description.txt
fastlane/metadata/android/en-US/changelogs/16.txt
```

Before submitting to F-Droid, replace repository URL placeholders if your GitHub path is not `r94/technomatic-2105`, push the `v0.16.0` tag, and verify that F-Droid can build the release from source.

## Preview renderer

A native preview renderer is included for desktop testing:

```sh
g++ -std=c++17 -O3 app/src/main/cpp/MusicEngine.cpp tools/render_preview.cpp -o render_preview
./render_preview technomatic_2105_v16_preview.wav 180 5 180 0 0
```

Arguments:

```text
1: output WAV path
2: render seconds
3: seed
4: generated track length in seconds, or 0 for random duration mode
5: genre mask, 0 for Random/full engine
```

## License

Apache License 2.0. See `LICENSE` and `NOTICE`.
