# v26 native validation

v26 replaces the universal fixed pad-stab tension sentence with generated per-seed tension grammar while preserving the v25 playback and composition model.

## Targeted regression

```text
seed: 3290437499
selected candidate: 44
reported interval: approximately 1:22-1:32
result: no sustained pad onset in the interval
```

The candidate remains unchanged so the regression is testing the tension-device rewrite rather than substituting another composition.

## Tension grammar checks

```text
256 tested seeds
256 unique tension grammar identities
199 unique harmonic rhythm/articulation identities
old 2/6/10/14 tension lattice: 0 occurrences
Pad Breath selected: 14 of 256
sustained chord articulation selected: 10 of 256
maximum concurrent sustained pad voices: 1
```

## Preserved engine behavior

```text
lead/hook/verse identity diversity: 256/256 each
bass statement/verse/answer diversity: 256/256 each
counter/arp/pulse/ornament diversity: 256/256 each
seed and candidate reconstruction: sample-exact
indefinite live playback: seed remains fixed
long-form evolution: active and bounded
hook recognition: preserved
verse freedom: greater than or equal to hook variation
hybrid Channel isolation and primary dominance: passed
history cap: 20 entries
30-second PCM export: exact requested byte count
render output: finite and bounded
```

## Build checks

```text
GCC -Wall -Wextra -Werror: passed
Clang -Wall -Wextra -Werror smoke build: passed
AddressSanitizer and UndefinedBehaviorSanitizer smoke test: passed
```

A complete Android Gradle build requires an Android SDK/NDK environment and network access for the pinned Gradle/Oboe dependencies.
