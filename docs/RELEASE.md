# Release procedure

This file is for maintainers preparing public APK releases.

## 1. Confirm version

Check:

```text
app/build.gradle
CHANGELOG.md
fastlane/metadata/android/en-US/changelogs/<versionCode>.txt
fdroid/metadata/vip.thatiam.technomatic2105.yml
```

For v0.21.0:

```text
versionName: 0.21.0
versionCode: 30
tag: v0.21.0
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
  -PTECHNOMATIC_2105_RELEASE_STORE_FILE=/absolute/path/technomatic-2105-release.jks \
  -PTECHNOMATIC_2105_RELEASE_STORE_PASSWORD='store-password' \
  -PTECHNOMATIC_2105_RELEASE_KEY_ALIAS='technomatic-2105' \
  -PTECHNOMATIC_2105_RELEASE_KEY_PASSWORD='key-password'
```

## 3. Device test checklist

```text
install fresh
launch from app list icon
START starts generated music and elapsed time advances
STOP stops generated music
NEXT changes generated composition
Genre Selector opens its own screen
Random is checked by default
tapping a dimmed genre while Random is checked disables Random and selects that genre
Pool and Hybrid mode buttons are visible
Pool mode shows Sounds pooled from:
Hybrid mode shows Sounds hybridized from:
selecting one or more genre checkboxes immediately starts a clean new generated track while playing
tapping elapsed/total time opens 30 sec, 1 min, 3 min, 5 min, 10 min, 20 min, 1 hour, Custom, Random, and Infinite choices
Custom duration uses Minutes and Seconds only and clamps each field to 60
Infinite duration keeps the current composition alive until NEXT is pressed
track length defaults to 180 seconds
No editor, save screen, load screen, or counter is visible
screen off keeps audio running
switch app keeps audio running
notification shows Technomatic 2105 icon/text
notification Next works
notification Stop works
Advanced screen opens
Seed copies on tap
Load Seed starts the requested generated sound
Export to OGG starts export and changes to Cancel Export
Cancel Export cancels without force quitting
completed export appears at Music/
headphones route audio correctly
volume remains steady between generated pieces
```

## 4. Tag release

```sh
git tag v0.21.0
git push origin v0.21.0
```

## 5. GitHub release

Create a GitHub release from tag `v0.21.0`.

Attach:

```text
signed APK, if produced locally or by the release workflow
source archive, optional because GitHub provides tag source archives automatically
```

Do not attach or publish keystores.

## 6. F-Droid submission notes

F-Droid builds and signs apps from source. The fdroiddata merge request should point to the public Git repository and tag. Use the draft metadata file under `fdroid/metadata/` as the starting point.

If your repository path is not `r94/technomatic-2105`, update these fields:

```text
SourceCode
IssueTracker
Changelog
Repo
```
