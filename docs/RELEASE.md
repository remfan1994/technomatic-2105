# Release procedure

This file is for maintainers preparing public APK releases.

## 1. Confirm version

Check:

```text
app/build.gradle
CHANGELOG.md
fastlane/metadata/android/en-US/changelogs/<versionCode>.txt
fdroid/metadata/vip.thatiam.radiobreaker.yml
```

For v6:

```text
versionName: 0.6.0
versionCode: 6
tag: v0.6.0
```

## 2. Build locally

Debug sanity build:

```sh
gradle assembleDebug
```

Unsigned release build:

```sh
gradle assembleRelease
```

Signed release build:

```sh
gradle assembleRelease \
  -PRADIO_BREAKER_RELEASE_STORE_FILE=/absolute/path/radio-breaker-release.jks \
  -PRADIO_BREAKER_RELEASE_STORE_PASSWORD='store-password' \
  -PRADIO_BREAKER_RELEASE_KEY_ALIAS='radio-breaker' \
  -PRADIO_BREAKER_RELEASE_KEY_PASSWORD='key-password'
```

## 3. Device test checklist

```text
install fresh
launch from app list icon
PLAY starts generated music
PAUSE stops generated music
NEXT changes composition
TRACK SECONDS persists after app restart
screen off keeps audio running
switch app keeps audio running
notification shows Radio Breaker icon/text
notification Next works
notification Pause works
headphones route audio correctly
volume remains steady between generated pieces
```

## 4. Tag release

```sh
git tag v0.6.0
git push origin v0.6.0
```

## 5. GitHub release

Create a GitHub release from tag `v0.6.0`.

Attach:

```text
signed APK, if produced locally or by the release workflow
source archive, optional because GitHub provides tag source archives automatically
```

Do not attach or publish keystores.

## 6. F-Droid submission notes

F-Droid builds and signs apps from source. The fdroiddata merge request should point to the public Git repository and tag. Use the draft metadata file under `fdroid/metadata/` as the starting point.

If your repository path is not `r94/radio-breaker`, update these fields:

```text
SourceCode
IssueTracker
Changelog
Repo
```
