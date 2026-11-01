# Release procedure

## Version

```text
versionName: 0.30.2
versionCode: 44
tag: v0.30.2
```

## Build

Use the included Gradle 8.9 wrapper. Do not use an unpinned Gradle 9 installation with Android Gradle Plugin 8.7.3.

NDK is pinned to `30.0.14904198`, matching the owner's installed toolchain at `/home/tutan/Android/Sdk/ndk/30.0.14904198`. See `AGENTS.md` before changing this pin in a future version. Build tools are 34.0.0, platform 33, CMake 3.22.1 and JDK 17.

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
fresh install uses the full generator with no Channels control
upgrading ignores old channel preferences while preserving user metadata
there is no Continuous Radio or Hold Sound control
Start begins playback and screen-off playback continues
app playback gain stays fixed and Android media volume remains user-controlled
no whole-mix kick ducking, automatic gain riding or musical volume fades
no coordinated part-volume ramps or phrase/section boosts and dips
compare v30.1 and v30.2 slow, sparse and dense seeds at the same system volume
check peak shaping on dense songs as well as the quieter slow and sparse songs
playback and exported files use the same fixed output treatment
current seed remains unchanged during extended unattended playback
elapsed time continues without a live duration boundary
motifs, bass, layers, and arrangement continue evolving within the current identity
Next creates a new sound only when no forward history entry exists
Previous and Next navigate the latest-20 history correctly
Restart returns the current seed to its beginning
the full musical range includes slow and intense seeds
the larger synthesis catalogue does not increase ensemble roles or per-song palette
instrument families remain fixed for the selected roles throughout playback
foreground phrases develop across sections instead of recycling only accompaniment
theme returns remain recognizable while replies develop rhythm and pitch
slow phrases use an appropriate register and can carry tension across bar lines
generated openings vary in length, reveal order, and musical geometry
finite exports use multi-phrase conclusive or dissolving endings rather than a rushed final-second fade
legacy channel API calls have no effect on sound, seed, time or history
loading a legacy recipe cannot restore a retired named-channel profile
start-end export writes the exact requested timeline interval
Metadata Editor shows the actual seed and engine automatically
rotate the Metadata screen while typing: draft values and cursor survive
OGG and FLAC both embed metadata readable after copying the actual file to a computer
changing metadata during an export does not retag the running job
track rows show seed and listened duration; load on tap and copy seed on long press
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
slow tunes retain musical rests during long playback
no noise/zap events, background hiss beds or free-running delay
no prolonged silence, obvious slowdown, or spontaneous seed replacement
listen for objectionable distortion in dense passages and exported codec playback
```

## Tag

```sh
git tag v0.30.2
git push origin v0.30.2
```
