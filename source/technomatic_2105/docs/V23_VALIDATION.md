# v23 validation

The native music core was compiled independently with GCC and Clang using C++17, optimization, and warning flags.

Validated behavior:

```text
2,000 generated seeds:
  unique lead identities: 2,000
  unique bass identities: 2,000
  unique harmonic identities: 1,198

saved seed reconstruction:
  maximum sample difference: 0

Continuous Radio:
  automatically changed seed after the current sound boundary

Hold Sound:
  preserved the current seed beyond its ordinary boundary

channels:
  No Channel and all 12 named channel mappings passed

legacy migration:
  old No Genre data maps to No Channel

history:
  capped at 20 entries
```

Runtime safety checks:

```text
GCC build: passed
Clang build: passed
AddressSanitizer: passed
UndefinedBehaviorSanitizer: passed
```

Host-only performance sanity checks:

```text
Automatic Continuous Radio transitions use the same full 48-candidate composition search as manually generated sounds.

180-second 48 kHz stereo preview:
  rendered in approximately 5.3 seconds
  approximately 34x realtime on the validation host
```

The host measurements are not Stratus C8 benchmarks. They establish that the v23 symbolic grammar expansion did not introduce a heavy rendering workload in this environment.
