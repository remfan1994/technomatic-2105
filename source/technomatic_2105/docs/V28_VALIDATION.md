# Technomatic 2105 v28 validation

v28 validates the following release invariants:

- No Channel, single Channel, and dominant Hybrid Channel select the same winning candidate for a given seed.
- Channel renditions preserve harmony, lead and bass grammar, motif families, phrase form, opening grammar, and ending grammar.
- Channel rendition changes restart at 0:00 and create separate history entries without changing the seed.
- History records listened duration and remains capped at 20 entries.
- Export renders exact [Start, End) PCM duration independently of live playback.
- Opening grammar spans 3-8 phrases and all six opening geometries are reachable.
- Ending grammar spans 3-8 phrases and all five ending geometries are reachable.
- Both conclusive and dissolving ending families remain reachable.
- Snapshot reconstruction remains sample exact.
- Rendered audio is finite and unclipped.
- The full 48-candidate composition search remains present.

Run:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  app/src/main/cpp/MusicEngine.cpp tools/validate_v28.cpp \
  -Iapp/src/main/cpp -o validate_v28
./validate_v28
```
