# v27 validation

## Scope

```text
versionName: 0.27.0
versionCode: 39
```

v27 adds FLAC export and a broader generated electronic timbre grammar without reducing the 48-candidate composition search.

## Native engine checks

The host validator covers 256 deterministic seeds and checks:

```text
unique lead, hook, verse, bass, bass-verse, bass-answer, counter, arp, pulse, and ornament identities
harmonic and tension diversity
12/12 bass model reachability
16/16 lead model reachability
10/10 pad model reachability
8/8 drum-kit reachability
generated pad-voicing diversity
timbre-grammar uniqueness
absence of the former fixed pad-stab lattice
rare and non-stacking sustained pad behavior
sample-exact seed/candidate reconstruction
indefinite bounded evolution
recognizable hook continuity
freer verse development
bounded bass development
manual Next behavior
No Channel and Hybrid Channel isolation
20-entry history cap
exact finite PCM export length
finite, bounded, faster-than-realtime host rendering
```

## FLAC checks

The built-in Java FLAC writer was tested with generated 48 kHz stereo signed 16-bit PCM. The produced file:

```text
passes the reference flac decoder integrity test
decodes byte-for-byte to the original PCM
contains TITLE, ARTIST, ALBUMARTIST, ALBUM, GENRE, and COMMENT tags
includes the seed in the title and comment
uses the exact source sample count and source PCM MD5
```

The encoder deliberately favors deterministic reliability over aggressive compression. OGG remains available for compact files; FLAC is the lossless option.

## Hardware discipline

The realtime engine retains:

```text
fixed-size voice arrays
no dynamic allocation in the audio callback
no sample decoding
no file IO in the audio callback
no FFT
no convolution
no neural model
```

Timbre diversity is produced by oscillator models, articulation, voicing, and seed-stable parameters rather than unbounded polyphony.

## Remaining integration test

A complete Android SDK/NDK build and Stratus C8 device test remain the final integration checks for MediaStore publication, Android OGG encoding, FLAC playback discovery, background playback, and actual on-device realtime margin.

## Recorded host results

```text
v27 validator:
  256/256 unique timbre identities
  bass model coverage: 12/12
  lead model coverage: 16/16
  pad model coverage: 10/10
  drum-kit coverage: 8/8
  generated pad voicings: 44 distinct across 256 tested seeds
  30-second render: 0.816 seconds
  peak: 0.510885

128-seed level sweep, 8 seconds per seed:
  minimum RMS: 0.084138
  mean RMS: 0.095024
  maximum RMS: 0.132186
  maximum peak: 0.641663
  maximum absolute DC: 0.000372
  clipping or non-finite samples: 0

20-minute indefinite render:
  wall time: 32.906 seconds
  realtime factor: 36.47x
  RMS: 0.099511
  peak: 0.641663
  non-finite samples: 0
  engine elapsed time: 1200 seconds

180-second preview:
  peak: 0.509003
  RMS: 0.104361
  5-second RMS range: 0.083773 to 0.131466
  clipped samples: 0
  silent 5-second windows: 0
```

These host measurements are regression and safety indicators, not a Stratus C8 benchmark or a substitute for listening tests.
