# Technomatic 2105 v24 native validation

Validation is performed against the same `MusicEngine` used by Android.

## Tested properties

```text
2,500 generated seeds
lead identities effectively unique
hook identities effectively unique
verse identities effectively unique
bass statement identities effectively unique
bass verse identities effectively unique
bass answer identities effectively unique
counter, arp, pulse, and ornament identities effectively unique
broad harmonic diversity
exact seed/candidate reconstruction
playback-policy-independent initial identity
gradual bounded long-form evolution
recognizable hook grammar
freer verse grammar
bounded bass evolution
Hold Sound seed preservation and continuing development
Continuous Radio automatic sound replacement
No Channel unrestricted behavior
dominant Hybrid Channel persistence and isolation
20-entry native history cap
finite, bounded audio output
faster-than-realtime native render on the validation host
```

Run locally:

```sh
g++ -std=c++17 -O3 -Wall -Wextra \
  app/src/main/cpp/MusicEngine.cpp tools/validate_v24.cpp \
  -o validate_v24
./validate_v24
```

The host render benchmark is only a regression check. The final Android integration and realtime performance test must be performed on the target phone.
