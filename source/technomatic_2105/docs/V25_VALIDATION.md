# v25 native validation

The v25 validator compiles the same MusicEngine used by Android and checks the single indefinite live playback model.

Validated behavior:

```text
256 generated seeds
256 unique lead identities
256 unique hook identities
256 unique verse identities
256 unique bass statement identities
256 unique bass verse identities
256 unique bass answer identities
256 unique counter identities
256 unique arpeggio identities
256 unique pulse identities
256 unique ornament identities
210 unique harmonic identities
sample-exact seed and candidate reconstruction
600 seconds of live playback without automatic seed replacement
no automatic transition state during live playback
no live outro boundary
continuous elapsed time
bounded long-form development
recognizable hooks and freer verse development
bounded bass development
manual Next creates a new seed
No Channel remains unrestricted
Hybrid Channel dominance survives serialization and reload
native history remains capped at 20 entries
30-second PCM export writes the exact requested frame count
finite and bounded floating-point output
```

Compiler checks completed with GCC and Clang using `-Wall -Wextra -Werror`.

A reduced validation run also completed under AddressSanitizer and UndefinedBehaviorSanitizer.

Validation-host performance:

```text
30 seconds of stereo audio rendered in approximately 0.7 seconds
peak remained below 1.0
```

This is not a Stratus C8 benchmark. A full Android Gradle build and device playback test remain release requirements.
