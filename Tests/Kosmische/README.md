# Kosmische – tests and kosgen

Kosmische is RetroSeq's sequence generator (Berlin School / Kosmische Musik style). Its core lives in `Source/Kosmische` and is built as the static library **KosCore**, which RetroSeq links. The core has no JUCE dependency.

Two extra targets are built from the same `CMakeLists.txt` (option `RETROSEQ_KOS_TOOLS`, on by default):

| Target | What it does |
|---|---|
| `KosTests` | Generator tests: determinism, notes in scale and range, no overlaps, harmony (chord changes / drone), mutation, swing, randomize with locks, and golden values that must be identical on MSVC, GCC and Clang. Exit code 0 = all passed. Also runs under CTest (*Test → Run CTests* in Visual Studio). |
| `kosgen` | Writes a demo patch (7 tracks) as MIDI files so the generator can be heard before the UI exists: one type 0 file per track (like RetroSeq's Export) plus `00 All tracks.mid` (type 1). |
| `KosBridgeTests` | Tests the bridge to RetroSeq's Engine: writing notes to a track, ownership (asks before overwriting data Kosmische did not write), Freeze, song change, Trans/Qnt reset on takeover and loop playback. Needs no audio device. |

## kosgen

```
kosgen [--out DIR] [--seed N] [--root A] [--scale dorian] [--bars 32] [--tempo 112]
       [--harmony changes|drone] [--prog "0 6 3 0"] [--bars-per-chord 2]
       [--swing 0..50] [--random-params] [--random-prog]
```

- `--prog` is scale degrees from 0 (0 6 3 0 = i VII IV i).
- `--random-params` / `--random-prog` do what F3 / F4 will do in the UI.
- The same seed always gives the same result.

Examples:

```
kosgen --seed 7
kosgen --seed 31 --root E --scale phrygian --harmony drone
kosgen --seed 12 --scale minor --random-prog --random-params --out test2
```

Load a file into RetroSeq with FILES → Import (one track) or open `00 All tracks.mid`.
