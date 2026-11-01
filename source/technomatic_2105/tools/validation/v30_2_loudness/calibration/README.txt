TECHNOMATIC 2105 v30.2 fixed-output calibration

The top-level files record the initial gain sweep with G=1.0,1.5,1.75,2.0,
2.25, all with K=0.80 and C=1.0. These are calibration candidates, not the
final release output. A G=1.75,K=0.80 provisional choice had measured
intense host Opus overfull samples at 3.328%, prompting the focused wider-
knee comparison. No other musical, playback or volume controller changed.

wider_knees/: G=1.75 with K=0.60 and K=0.40, plus the G=1,K=0.80 baseline.
wider_knee_codecs/: same four [40,88) second excerpts through host Opus.
The final selected output is G=1.75,K=0.40,C=1.0. Its actual app-native
measurements and exact independent reconstruction are in ../final and
../comparison. Top-level g175 rows still mean K=0.80, explicitly recorded
in each CSV/JSON row; g175k040 rows mean the chosen K=0.40.

All candidates use the same 16 raw voice-sum renders of 88 seconds.
The raw observer bypasses only applyMaster in a diagnostic source copy.
The separate processor reapplies retained fixed per-song drive/DC filter.
Direct baseline versus reconstructed G=1,K=0.80 output matches all
135168000 float channel samples exactly. See baseline_reconstruction.json.

Windows: full=[0,88), startup=[0,10), settled=[40,88), at 48000 Hz stereo.
true_peak_dbfs is FFmpeg ebur128 estimated reconstructed peak, in dBTP.
knee_sample_fraction is the channel-sample fraction with abs(G*precurve)>K.
above_099_fraction and fullscale_fraction refer to abs(processed)>=0.99
and >=1.0. An earlier knee necessarily increases knee occupancy; occupancy
alone does not rank distortion or sound quality across different knees.

peak_curve_residual_relative_to_ideal_rms is
 sqrt(mean((processed-G*precurve)^2)/mean((G*precurve)^2)).
This is deliberate waveform change relative to an unbounded linear signal,
not THD, an audio error count, or a subjective quality score.

Source samples are bounded; reconstructed/codec overshoots are measured
separately. No dynamic limiter, gain detector, attack/release envelope,
system-volume change or loudness metadata is part of this test.
No Android hardware, headphones, street noise or subjective audition is tested.
