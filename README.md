# Radio Breaker

Radio Breaker is an Android procedural electronic music app. It generates synthetic electronic tracks on-device without samples, network access, accounts, ads, analytics, or external audio assets.

v6 is a publication-readiness release. The music engine is intentionally kept close to v5; the focus is packaging, release builds, app identity, and F-Droid/GitHub preparation.

## Current controls

```text
PLAY / PAUSE
NEXT
TRACK SECONDS
```

`TRACK SECONDS` accepts an integer from 8 to 999999. Examples:

```text
1200 = 20 minutes
1800 = 30 minutes
3600 = 1 hour
```

NEXT skips the current generated composition immediately.

## Music model

The engine keeps the current composition split:

```text
INTER-SONG:
  high randomness
  new key, tempo, style, motif, progression, arrangement identity

INTRA-SONG:
  high stability
  fixed composition identity while the track runs

INTRA-SONG LITTLE RANDOMNESS:
  section changes
  hook returns
  breakdowns
  motif variation
  drum-only mutation
```

The compositional hierarchy is:

```text
song
  sections
    phrases
      motifs
```

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
app/src/main/java/vip/thatiam/radiobreaker/MainActivity.java
app/src/main/java/vip/thatiam/radiobreaker/AudioService.java
app/src/main/java/vip/thatiam/radiobreaker/NativeAudio.java
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
  -keystore radio-breaker-release.jks \
  -alias radio-breaker \
  -keyalg RSA \
  -keysize 4096 \
  -validity 10000
```

Then build with external signing properties:

```sh
gradle assembleRelease \
  -PRADIO_BREAKER_RELEASE_STORE_FILE=/absolute/path/radio-breaker-release.jks \
  -PRADIO_BREAKER_RELEASE_STORE_PASSWORD='store-password' \
  -PRADIO_BREAKER_RELEASE_KEY_ALIAS='radio-breaker' \
  -PRADIO_BREAKER_RELEASE_KEY_PASSWORD='key-password'
```

Do not commit the keystore or passwords. `.gitignore` blocks common signing file names.

## GitHub publication

Suggested first repository setup:

```sh
git init
git add .
git commit -m "Radio Breaker 0.6.0"
git branch -M main
git remote add origin git@github.com:r94/radio-breaker.git
git push -u origin main
git tag v0.6.0
git push origin v0.6.0
```

Change `r94/radio-breaker` if you use a different GitHub path.

This archive includes GitHub Actions workflows:

```text
.github/workflows/android-ci.yml
.github/workflows/android-release.yml
```

The release workflow expects these repository secrets if you want signed APK artifacts from GitHub Actions:

```text
ANDROID_KEYSTORE_BASE64
ANDROID_KEYSTORE_PASSWORD
ANDROID_KEY_ALIAS
ANDROID_KEY_PASSWORD
```

## F-Droid preparation

This archive includes:

```text
fdroid/metadata/vip.thatiam.radiobreaker.yml
fastlane/metadata/android/en-US/title.txt
fastlane/metadata/android/en-US/short_description.txt
fastlane/metadata/android/en-US/full_description.txt
fastlane/metadata/android/en-US/changelogs/6.txt
```

Before submitting to F-Droid, replace repository URL placeholders if your GitHub path is not `r94/radio-breaker`, push the `v0.6.0` tag, and verify that F-Droid can build the release from source.

## Preview renderer

A native preview renderer is included for desktop testing:

```sh
g++ -std=c++17 -O3 app/src/main/cpp/MusicEngine.cpp tools/render_preview.cpp -o render_preview
./render_preview radio_breaker_v6_preview.wav 180 0x52423630 1200
```

Arguments:

```text
1: output WAV path
2: render seconds
3: seed
4: generated track length in seconds
```

## License

Apache License 2.0. See `LICENSE` and `NOTICE`.
