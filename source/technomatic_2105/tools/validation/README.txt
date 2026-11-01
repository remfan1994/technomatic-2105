Validation evidence by version

Current v30.2 output evidence is in v30_2_loudness/ and v30_2_android_*;
see docs/LOUDNESS_V30_2_VALIDATION.txt and docs/BUILD_V30_2_VALIDATION.txt.
v30_1_* files document the prior release and are retained unchanged.

Files beginning v30_ describe the original version 30 source. See
docs/V30_VALIDATION.md and docs/BUILD_V30_VALIDATION.txt for methods, final
source hashes, counts and limitations. The current physical metadata and
edited-recipe reports are in docs/metadata-v30-results.

The following generic filenames were delivered with v29 and are retained
unchanged as historical evidence:

  engine_sha256.txt
  distribution_summary.csv
  audio-control-test-report.txt
  metadata-test-report.txt

Files beginning v28_ or v29_ also describe those historical engines. Their
channel configurations and old assertions do not describe current v30
behavior. In paired comparisons, v29 uses its full-range No Channel setting.

Primary development CSVs count chord-relative note/rest plans. They do not
count gain, tone, instrument identity or random performance misses as a new
melody. The main-lead series excludes turns handed to another instrument;
the foreground series follows whichever selected instrument has that turn.

Triggered palette CSVs run real scheduler/trigger code but drain voices for
each simulated step. They measure the number of synthesis families used,
not simultaneous polyphony or a full PCM render. The native validator also
observes palette stability during continuous rendered audio.

Scheduled foreground CSVs observe tagged emitted events. PCM and export
checks cover finite numerical output, replay and exact duration. None of
these metrics is a substitute for listening or testing Android hardware.
