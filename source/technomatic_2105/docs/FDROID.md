# F-Droid notes

Technomatic 2105 is designed for source-built distribution:

```text
no Internet permission
no ads
no trackers
no accounts
no proprietary samples
no external sound assets
Apache-2.0 source license
```

The app depends on Oboe through Maven:

```text
com.google.oboe:oboe:1.10.0
```

Before submitting:

```text
1. publish the source repository
2. push tag v0.26.0
3. confirm repository URLs in fdroid metadata
4. run fdroid lint/build if available
5. submit the metadata to fdroiddata
```
