# Poster Packer

Pack PNG/JPEG posters onto as few A4 sheets as possible and export print-ready PDFs.

You give it images and choose a print size for each. It searches for the layout that
uses the **fewest sheets**, rotating posters 90 degrees and, where it actually saves a
sheet, rescaling them slightly (default +/-15%; some can be locked). Ties are broken by
rescaling as little as possible, then by leaving one sheet as empty as possible.

* Native Windows app, plain C++14, **no installs and no DLLs** (Win32 + GDI+ ship with Windows). ~0.4 MB exe.
* JPEGs are embedded in the PDF byte-for-byte, so print quality is exactly the original.
* PDF pages are exactly A4, posters at true physical size.

## Build

Needs only MinGW g++ (tested with GCC 6.3 / `mingw32-make`).

```bash
mingw32-make all      # builds build/posterpacker.exe, posterpacker_cli.exe, make_test_images.exe
mingw32-make test     # correctness tests (layout validity, scale bounds, locks, ...)
mingw32-make bench    # algorithm comparison, ~1 minute
```

## Use the GUI

```bash
build\make_test_images.exe testimgs 14     # optional: make sample posters
build\posterpacker.exe
```

1. **Add images**: "Add images..." or drag files/folders onto the window.
2. **Size each poster**: pick one in the list, type its print width in mm, click **Set**.
   The preview shows it on an A4 page. Use **Next >** / **< Prev** to step through them all.
   *Lock size* stops the optimiser changing that poster. *Apply width to all* sets everyone at once.
3. **Settings** (top bar): *Tolerance %* (how far posters may be rescaled), *Margin mm* (printer-safe
   border), *Gap mm* (space between posters for cutting), *Search s* (how long the optimiser may think).
4. **Pack**. A worker thread searches; the status line then shows sheets used vs the theoretical
   minimum, the winning algorithm, how many posters were rescaled, and each algorithm's sheet count.
   Flip through sheets with **< Prev** / **Next >**; rescaled posters carry a yellow % tag. Click a poster
   in the list to jump to its sheet. **Back to sizing** returns to editing.
5. **Export PDF...**: one multi-page PDF, or tick *One PDF per sheet*. Print at 100% / "Actual size".

## Use the command line

```bash
build\posterpacker_cli.exe -o out -w 90 -t 15 -s 8 folder_of_images
```

`-o` output base name, `-w` print width in mm for every poster, `-t` tolerance %, `-s` search seconds,
`--per-sheet` one PDF per sheet. Prints each algorithm's result, the winner, and which posters were rescaled.

## How it chooses

Strict priority: **1. fewest sheets, 2. least rescaling, 3. emptiest last sheet.**
(Maximising "page usage" and minimising sheets are the same thing when sizes are fixed; with rescaling
allowed, the tie-break above is what keeps posters close to your chosen sizes.)

The `PortfolioOptimizer` runs everything and keeps the best:

| Stage | Algorithms |
|---|---|
| instant | Shelf, Skyline, Guillotine, MaxRects, RandRestart |
| genetic (70% of time) | BRKGA, BRKGA-Islands, BRKGA-Memetic, BRKGA-Max |
| improver (30%) | RuinRecreate, seeded with the best plan so far |

The genetic algorithms evolve poster order, per-poster placement rule, 90-degree turn and scale together.
Variants add islands + migration, path relinking, shake/reset, annealed parameters, multi-parent
crossover and VND local search. Ruin-and-recreate removes groups of posters and reinserts them, shrinking
only when something else would not fit. Full explanation, sources and benchmark results:
**[docs/ALGORITHMS.md](docs/ALGORITHMS.md)**.

## Code map

```
src/core/      pure C++, no GUI/OS dependencies (unit-testable)
  Types.h, Poster.h, PackPlan.*      data types, scale bounds, fitness, lower bounds
  IPackStrategy.h                    interface for fixed-size packers
  ShelfPacker, SkylinePacker, GuillotinePacker, MaxRectsPacker, RandomRestartPacker
  MultiSheetPacking.h                shared multi-sheet loop + placement rules
  ScaleOptimizer.h                   interface for algorithms that also choose scales
  UniformShrinkOptimizer, BrkgaOptimizer, RuinRecreateOptimizer, PortfolioOptimizer
  LayoutValidator.*                  independent referee used by the tests
src/io/        ImageFile (PNG/JPEG headers, EXIF), PdfWriter, PdfExporter, PosterLoader, FileUtil
src/platform/  GdiPlusDecoder (Windows image decoding for the few images that cannot be embedded directly)
gui/main.cpp   the window (Win32 + GDI+); all logic lives in src/
tools/         command-line app, test-image generator
tests/         packer_harness, optimizer_harness (correctness), benchmark
docs/          ALGORITHMS.md
```

## Known limits

* Mirrored EXIF photos are treated as plain rotations. PNGs with transparency are flattened onto white
  and embedded uncompressed (large PDFs).
* The search is single-threaded inside the optimiser (the installed MinGW 6.3 has no `std::thread`); the GUI
  runs it on a separate thread so the window stays responsive.
* No per-poster tolerance box yet (`Poster::minScale/maxScale` exist; only the global tolerance and *lock* are in the UI).
* Posters larger than an A4 sheet are shrunk to fit; no tiling across sheets.
