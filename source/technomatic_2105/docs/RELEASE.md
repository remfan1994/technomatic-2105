# Release procedure

## Version

```text
versionName: 0.28.0
versionCode: 40
tag: v0.28.0
```

## Build

Use the included Gradle 8.9 wrapper. Do not use an unpinned Gradle 9 installation with Android Gradle Plugin 8.7.3.

```sh
./gradlew assembleDebug
./gradlew assembleRelease
```

Signed release:

```sh
./gradlew assembleRelease \
  -PTECHNOMATIC_2105_RELEASE_STORE_FILE=/absolute/path/technomatic-2105-release.jks \
  -PTECHNOMATIC_2105_RELEASE_STORE_PASSWORD='store-password' \
  -PTECHNOMATIC_2105_RELEASE_KEY_ALIAS='technomatic-2105' \
  -PTECHNOMATIC_2105_RELEASE_KEY_PASSWORD='key-password'
```

## Device test checklist

```text
fresh install defaults to No Channel
there is no Continuous Radio or Hold Sound control
Start begins playback and screen-off playback continues
current seed remains unchanged during extended unattended playback
elapsed time continues without a live duration boundary
motifs, bass, layers, and arrangement continue evolving within the current identity
Next creates a new sound only when no forward history entry exists
Previous and Next navigate the latest-20 history correctly
Restart returns the current seed to its beginning
No Channel is unrestricted
all twelve named Channels reinterpret the current seed and restart it at 0:00
Hybrid Channel keeps the first selected Channel dominant while preserving the same composition core
Channel screen Back returns to main
generated openings vary in length, reveal order, and musical geometry
finite exports use multi-phrase conclusive or dissolving endings rather than a rushed final-second fade
Channel change preserves seed, candidate, motifs, harmony, bass grammar, and form
same seed can appear in history as distinct Channel renditions
start-end export writes the exact requested timeline interval
Metadata Editor values appear in the media library, and FLAC embeds them
track rows show Channel, seed, and listened duration; load on tap and copy seed on long press
Clear History remains responsive
Advanced seed copy/load works
export Start and End are separate from live playback
OGG export does not restart or follow live playback
FLAC export does not restart or follow live playback
Cancel Export works for both formats
finished OGG appears under Music/
finished FLAC appears under Music/
FLAC decodes losslessly and contains embedded metadata
export title comes from the filename; seed and range remain in export metadata/comment
background notification Next and Stop work
reported seed 3290437499 no longer produces the recurring pad-stab wall near 1:27
fixed 2/6/10/14 pad-stab lattice is absent across several seeds
tension gestures vary by seed and do not recur as one universal device
sustained Pad Breath remains rare and never stacks with another slow pad
no clipping, prolonged silence, obvious slowdown, or spontaneous seed replacement
```

## Tag

```sh
git tag v0.28.0
git push origin v0.28.0
```
