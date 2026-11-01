V30.1 LOUDNESS EVIDENCE

The final release result is docs/LOUDNESS_V30_1_VALIDATION.txt.
Use final/, comparison/, codecs/ and the final check reports for release claims.
The frozen source hashes are in final_source_sha256.txt.

baseline/ is the delivered-v30 comparison, not the new output.
raw/ and calibration_fullscale/ are SUPERSEDED CALIBRATION: these were generated
before the user-requested final removal of phrase-amplitude macros. They support
the engineering history only and must not be cited as final release levels.
No dynamic limiter or AGC prototype was used for final performance evidence.

Full float audio, encoded auditions and host executables are stored outside the
source project. The tools scripts reproduce them from the specified sources.
