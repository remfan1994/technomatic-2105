# F-Droid notes

Technomatic 2105 is prepared to be F-Droid-friendly:

```text
no Internet permission
no ads
no trackers
no accounts
no proprietary audio samples
no proprietary sound assets
Apache-2.0 source license
```

The app depends on Oboe through Maven:

```text
com.google.oboe:oboe:1.10.0
```

The Android build is Gradle + NDK + CMake. The draft metadata is:

```text
fdroid/metadata/vip.thatiam.technomatic2105.yml
```

Before opening an fdroiddata merge request:

```text
1. publish the source repository
2. push tag v0.22.0
3. update metadata repo URLs if needed
4. run fdroid lint/build locally if possible
5. submit the app metadata to fdroiddata
```
