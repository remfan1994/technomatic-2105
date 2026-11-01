TECHNOMATIC 2105 v30.2 final loudness evidence

The FINAL selected output is G=1.75, K=0.40, C=1.0.
This directory records host measurements of a fixed 16-seed sample.
No Android hardware, headphones, market-street recording or subjective
listening test is claimed. Equivalence to the owner's RFS/FM route
loudness cannot be established from host measurements alone.

baseline/: actual immutable v30.1 native renders, 16 x 88 seconds.
final/: actual frozen v30.2 native renders, 16 x 88 seconds.
calibration/: initial five-gain comparison, all at K=0.80; NOT final output.
calibration/wider_knees/: focused K=0.60/K=0.40 comparison at G=1.75.
calibration/wider_knee_codecs/: codec comparison that informed final choice.
comparison/: full score/amplitude/RNG identity and independent PCM equality.
codecs/: final lossless A/B FLAC equality and host libopus reconstruction.
final_source_sha256.txt: exact sources used for final results.

All source float files are 48000 Hz stereo. Measurements use complete
[0,88), startup [0,10), and settled [40,88) second windows, without added
normalization, gain tags or fades. FFmpeg's 0.1-unit printed LUFS and true-
peak measurements are retained with their complete meter logs. Native
sample peak and RMS are computed directly from float PCM.

The same voice sum, original per-song fixed drive, and DC filter produce
all calibration candidates. Independent G=1,K=0.80 output equals all
135168000 actual baseline float samples exactly. Independent final
G=1.75,K=0.40 output equals all 135168000 actual final float samples exactly.
This equality does NOT say the quieter baseline PCM and louder final PCM
are identical. It verifies that only the declared fixed output transfer
separates the rendered signals.

All 16 score/RNG/amplitude signatures match; 20247 scheduled events are
covered by the bounded scheduling observer. Recipe fields are identical
except outputrev=1 -> outputrev=2; the composition version remains 30.

Settled median paired loudness increase: +4.50 LU (range +3.7 to +4.7).
Settled median paired RMS increase: +4.519056 dB.
Settled median LUFS: -14.05 before, -9.60 after.
The subtraction of medians (-9.60 - -14.05 = 4.45) is deliberately
not described as the median of paired changes (+4.50).
Full-window median paired increase: +4.45 LU. Startup: +4.50 LU.
Largest native estimated reconstructed peak: +1.4 dBTP full, +1.3 settled.
All 135168000 native source float samples are finite and within [-1,1].
Largest observed absolute float sample: 0.998284101486 (-0.014917 dBFS).

The earlier K=0.80 prototype is retained only as calibration evidence.
Changing to K=0.40 reduced the intense host Opus overfull count from
153344 to 22254, an 85.49% reduction, while retaining almost all of the
measured median level increase. The ceiling remains 1.0; K is the point
where smooth rounding starts, not a second output cap. A wider curve
necessarily touches more samples; knee occupancy alone is not a ranking
of audible distortion or subjective quality across different knees.

The eight supplied matched FLAC excerpts preserve their exact PCM16
samples after decode; all 36864000 channel samples were compared. The app
PCM16 conversion is used: lrint(clamp(float,-1,1)*32767), with neither
side normalized. Excerpts are [40,88) seconds of live rendering, not
finite-export arrangement recordings. All four final host Opus files
use 128000 bit/s, 20 ms frames, audio mode and OpusHead output gain 0.

Codec overshoot remains a measured limitation. Final intense host Opus
has 22254 of 4608000 channel samples above full scale (0.482943%), decoded
peak 1.1724215 and estimated +1.4 dBTP. Final slow/groove/quiet-driving
counts are 209/54/67 samples; complete peaks and levels are retained in
codecs/codec_measurements.csv. An integer playback path can clip decoded
values above full scale. Host libopus does not validate Android MediaCodec.
No universal reconstructed-peak or codec-clipping-free claim is made.

Reproduction from the project root (large files stay outside source):

python3 tools/build_loudness_probe.py --engine-root /path/to/v30_1 --output /tmp/tm302-baseline
python3 tools/measure_loudness.py --renderer /tmp/tm302-baseline --audio /tmp/tm302-before-audio --output /tmp/tm302-before-results --label v301
python3 tools/build_loudness_probe.py --engine-root /path/to/v30_1 --output /tmp/tm302-raw --raw
python3 tools/measure_loudness.py --renderer /tmp/tm302-raw --audio /tmp/tm302-raw-audio --output /tmp/tm302-raw-results --label raw
g++ -std=c++17 -O2 -Wall -Wextra -Werror tools/loudness_tone_probe.cpp -o /tmp/tm302-tone
python3 tools/calibrate_output_v30_2.py --processor /tmp/tm302-tone --raw-audio /tmp/tm302-raw-audio --raw-metadata /tmp/tm302-raw-results --audio /tmp/tm302-calibration-audio --output /tmp/tm302-calibration-results --wider-knees
python3 tools/build_loudness_probe.py --engine-root . --output /tmp/tm302-final
python3 tools/measure_loudness.py --renderer /tmp/tm302-final --audio /tmp/tm302-after-audio --output /tmp/tm302-after-results --label v302
python3 tools/compare_loudness.py --baseline /tmp/tm302-before-results --current /tmp/tm302-after-results --current-audio /tmp/tm302-after-audio --expected-audio /tmp/tm302-calibration-audio --output /tmp/tm302-comparison --baseline-label v301 --current-label v302 --expected-label g175k040 --knee 0.40
python3 tools/make_loudness_auditions.py --before-audio /tmp/tm302-before-audio --after-audio /tmp/tm302-after-audio --before-metadata /tmp/tm302-before-results --after-metadata /tmp/tm302-after-results --output /tmp/tm302-auditions --evidence /tmp/tm302-codecs --work /tmp/tm302-codec-work --before-label v301 --after-label v302 --before-version v30.1 --after-version v30.2
