# Algorithms: research, plan, and what is implemented

This document explains how Poster Packer decides where posters go and how much to
rescale them, which research each technique comes from, and which parts are our
own adaptation. All code is in `src/core/`.

## 1. The problem

Posters are rectangles. Each can be rotated 90 degrees and rescaled within a
tolerance (default +/-15% of the size the user chose; a poster can also be locked).
They go onto identical A4 sheets (printable area = page minus margins, with a gap
between posters). This is a two-dimensional bin packing problem with an extra
continuous degree of freedom (the scale of each item).

**Goal, in strict priority order**

1. fewest sheets;
2. among equal sheet counts, the least rescaling (sizes stay close to what you chose);
3. among those, leave the emptiest sheet as empty as possible (all spare room
   concentrated on one sheet, so the other sheets are as full as possible).

This is one number, `computeFitness()` in `PackPlan.h`:
`sheets * 1000 + 10 * deviation + 0.05 * (fill fraction of emptiest sheet)`, where
`deviation = sum(weight * ln(scale)^2)`.

Why squared log-scale? Shrinking one poster by 20% costs more than shrinking two
posters by 10%, so shrinkage is spread out instead of ruining a single poster, and
growing by 10% costs the same as shrinking by about 9%.

Why "emptiest sheet" in the fitness? It is the "bins + least-load fraction" fitness
from Goncalves & Resende (2013). Plain bin count is a plateau: thousands of layouts
score the same. Adding the fill of the emptiest bin gives the search a slope
towards "this last sheet is nearly empty, so it might disappear soon".

Lower bounds (`sheetLowerBound`, `minimumPossibleSheets`): total area / sheet area,
or the number of "hog" posters (more than half a sheet in both directions, in both
orientations: any two of them overlap at the sheet centre). Searches stop as soon
as they reach the floor with every poster at its lowest scale.

## 2. Building blocks

| Piece | File | Role |
|---|---|---|
| `IPackStrategy` | `IPackStrategy.h` | arrange fixed-size rectangles |
| `MaxRectsBin` | `MaxRectsPacker.*` | one sheet's free space as maximal rectangles, **5 placement rules** |
| Shelf / Skyline / Guillotine / RandomRestart | `*Packer.*` | classic baselines (Guillotine = straight-cut layouts) |
| `ScaleOptimizer` | `ScaleOptimizer.h` | decide scales *and* layout |
| `UniformShrinkOptimizer` | | any packer, tried at 100%, 97.5%, ... of size |
| `BrkgaOptimizer` | `BrkgaOptimizer.*` | genetic algorithm family (section 3) |
| `RuinRecreateOptimizer` | `RuinRecreateOptimizer.*` | goal-driven ruin & recreate (section 4) |
| `PortfolioOptimizer` | | runs everything, keeps the winner (section 5) |

### Placement rules (MaxRects)

Which free spot should a poster take? Five rules, all in `MaxRectsBin::findBestFit`:

* best short side fit - leave the smallest short leftover gap;
* best long side fit - leave the smallest long leftover gap;
* best area fit - use the smallest free rectangle that fits;
* bottom-left - lowest then leftmost;
* contact point - maximise edge contact with the sheet border and other posters.

The genetic algorithm picks a rule *per poster* (one gene each).

## 3. Genetic algorithms (BRKGA family)

### Research behind it

* **Goncalves & Resende, "A biased random key genetic algorithm for 2D and 3D bin
  packing problems", Int. J. Production Economics 145 (2013) 500-510.** Chromosome
  of 3n random keys = box order, placement-heuristic choice, orientation; maximal-space
  placement decoder; fitness = number of bins plus the load fraction of the
  least-loaded bin. Reported results equal or better than other published methods on
  858 instances. *We reuse the chromosome idea (plus a 4th gene for scale), the
  rule-per-item gene, maximal-space decoding, and the fitness shape.*
* **"Biased Random-Key Genetic Algorithms: A Review" (arXiv 2312.00961).** Catalogue
  of enhancements, used below: island model, reset operator, shake operator, online
  parameter tuning, multi-parent crossover, implicit path relinking, hybridisation
  with VND. Notable conclusions we acted on: restarts/shaking matter a lot in
  practice; most packing BRKGAs use plain permutation decoders without local
  search, and BRKGA+VND (Zudio et al. 2018) reached better solutions in fewer
  generations than plain BRKGA.
* **Falkenauer (1996), grouping genetic algorithm for bin packing.** Group-aware
  operators plus a dominance-based local optimisation. *Not implemented as such*
  (our chromosome is item-based, not bin-based) but it is the motivation for
  adding local search to the genetic algorithm and for ruin-and-recreate, which is
  a group-level (sheet-level) move.

### The chromosome

Four numbers per poster, vector layout `[keys | rules | turns | scales]`:

* **key** - posters are packed in descending key order;
* **rule** - gene in [0,1) mapped to one of the 5 placement rules;
* **turn** - above 0.5 means pre-rotate 90 degrees before packing (MaxRects may still rotate);
* **scale** - the size factor, held inside the poster's allowed range.

Decoder (`Decoder::decode`): apply scales/turns, sort by key, run MaxRects with the
per-poster rules. Fitness as in section 1.

### The engine and its switches (`BrkgaConfig`)

| Feature | Source | Switch |
|---|---|---|
| Elite / mutant / biased-crossover generation (classic BRKGA) | Gonçalves & Resende 2011/2013 | always |
| Islands: independent populations, ring migration of elites | review §"Island model" | `islands`, `migrationInterval` |
| Implicit path relinking between neighbouring islands' bests (back and forth) | Andrade et al. 2021 | `pathRelinking` |
| Shake: perturb elites, re-randomise the rest, after an island stalls | Andrade et al. 2019 | `shakeAfterStall` |
| Reset: restart everything but the best after a global stall | Toso & Resende 2015 | `resetAfterStall` |
| Annealed parameters: many mutants / low elite bias early, few mutants / high elite bias late | Adaptive BRKGA, Chaves et al. 2018 (simple linear schedule, not their full controller) | `annealParameters` |
| Rank-biased multi-parent crossover (gene from parent r with probability ~ 1/r) | Andrade et al. 2021 (Bresina bias) | `parentsPerChild >= 3` |
| VND local search on each island's best: swap keys, move poster to front/back, flip turn, change rule, restore shrunk scale | Zudio et al. 2018 (BRKGA+VND); neighbourhood set is ours | `localSearch` |
| Per-gene mutation on children (helps the continuous scale genes; classic BRKGA has none) | ours | `geneMutationProbability` |
| Restore pass: after evolution, push every shrunk poster back towards its desired size, by binary search, as long as the sheet count holds | ours | always |

### The four variants (presets)

| Name | Settings | Best at |
|---|---|---|
| `BRKGA` | 1 population of 80, stops after 60 stalled generations | quick solid result |
| `BRKGA-Islands` | 4 x 40, migration, shake, reset, annealing | escaping plateaus |
| `BRKGA-Memetic` | 2 x 50, VND on elites, annealing | cutting scale deviation |
| `BRKGA-Max` | 4 x 40, multi-parent, path relinking, VND, shake, reset, annealing | hardest instances |

## 4. Ruin and recreate

### Research

**Gardeyn & Wauters, "A goal-driven ruin and recreate heuristic for the 2D
variable-sized bin packing problem with guillotine constraints", EJOR 301(2), 2022
(arXiv 2508.19306).** Key ideas, as we use them:

* **Ruin:** remove a random number of posters (uniform in 1..2*mu-1, mean mu = 8 for
  up to ~100 items in the paper); we add a "related" mode that removes a poster's
  spatial neighbours on the same sheet.
* **Recreate:** reinsert excluded items **most-constrained first** (fewest insertion
  options), random tie-breaking, and with probability 0.05 skip the best option
  ("blink").
* **Late-acceptance hill climbing (LAHC):** accept a candidate if it is no worse than
  the layout accepted L steps ago (L = 200 in our implementation; the paper tunes 500-2000
  for long runs) or no worse than the current one. No temperature to tune.
* **Goal-driven:** instead of minimising total bin area directly, set a target
  (here: one fewer sheet), allow solutions that leave posters *excluded*, and use
  the excluded area as a smooth progress signal (lexicographic: excluded area
  first, then the secondary objective). When nothing is excluded any more, the
  target tightens.

### Our adaptation (`RuinRecreateOptimizer`)

1. start from a feasible plan (the best found so far, or plain MaxRects);
2. exclude the emptiest sheet's posters, then loop ruin -> recreate -> LAHC accept;
3. when all posters are placed again: record it and exclude the next sheet;
4. **scales (our extension; the paper has none):**
   * removed posters re-enter at their desired size and **shrink step by step (1.25%)**
     within tolerance only if no sheet has room;
   * **squeeze move**: shrink a few placed posters in place by 5% (top-left corner fixed) to create room
     for excluded ones; they regrow when ruin touches them later;
5. the last 30% of the time budget only polishes the best layout (cost = scale
   deviation) without removing sheets.

Honest findings from our benchmark: stand-alone, ruin-and-recreate is a strong
*scale polisher* (lowest deviation) but not the best at finding fewer sheets from a
cold start; the genetic variants and random restarts are. That is why the portfolio
runs it last, seeded with the best plan so far.

## 5. The portfolio

`PortfolioOptimizer(totalSeconds)` runs, in order, each time showing the best plan
so far to improvers:

1. Shelf, Skyline, Guillotine, MaxRects, RandRestart (instant; each at a ladder of uniform shrink levels);
2. BRKGA, BRKGA-Islands, BRKGA-Memetic, BRKGA-Max (70% of the budget: 10/15/20/25%);
3. RuinRecreate seeded with the best (30%).

**Winner = lowest fitness** (section 1): fewest sheets, then least rescaling, then
emptiest last sheet. The comparison table (sheets, deviation, ms per algorithm) is
shown in the GUI status line and CLI output, so you can see which algorithms earn
their place. Searches stop early when the theoretical floor is reached.

## 6. Benchmark (what actually worked here)

`mingw32-make bench` (`tests/benchmark.cpp`): 8 random instances (4 with posters of 60-180 mm, 4 with
"awkward" 75-125 mm posters; 21-38 posters each), +/-15% tolerance, every searching
algorithm gets the same 1.0 s per instance. Totals over the 8 instances (lower is better;
`floor` total = 39 sheets):

| Algorithm | Sheets | Scale deviation | Best-or-tied instances |
|---|---:|---:|---:|
| MaxRects (uniform-shrink ladder) | 53 | 3.59 | 0 |
| RandRestart (300 tries per level) | **49** | 4.73 | 1 |
| BRKGA (classic) | 50 | 1.90 | 5 |
| BRKGA-Islands | 50 | 2.01 | 1 |
| BRKGA-Memetic | 50 | 2.08 | 0 |
| BRKGA-Max | 50 | 2.10 | 0 |
| RuinRecreate (cold start) | 53 | **0.89** | 1 |

What we learned (single fixed set of instances and a short budget, so treat as indicative, not
conclusive):

* All BRKGA variants beat plain MaxRects by 3 sheets and use ~1/2 the scale change of RandRestart.
* One instance needs *every* poster at its minimum scale to reach the floor; only RandRestart
  (whole-layout shrink + many orders) found it. That is why the portfolio keeps it, and why the BRKGA
  got a coordinated "shrink everything together" mutation and VND move. (Adding it moved
  two more instances from 7 to 6 sheets.)
* Island model, path relinking and VND did not beat the simple BRKGA at 1 s on these small instances.
  They are designed for longer runs and bigger jobs (more local optima, more generations);
  at the portfolio's default 6 s they get 15-25% of the time each. They are kept because the published
  evidence says they pay off when the search stalls, and because the portfolio costs nothing if they do not win.
* Ruin-and-recreate is the best *polisher*: seeded with another algorithm's plan it cuts scale
  deviation by a further 10-30% at the same sheet count.
* The genetic variants can resume from the best plan so far (chromosomes are stored in `PackPlan::genes`),
  and ruin-and-recreate resumes from any plan.

## 7. Not implemented (ideas)

* **Group-level crossover (Falkenauer GGA):** swap whole sheets between parents and reinsert leftovers.
* **Pattern-based set covering:** enumerate good single-sheet patterns and pick the best cover with an
  exact/LP method - strongest when many posters share a size.
* **Exact search** for tiny jobs (<= ~8 posters).
* **Adaptive operator selection / Q-learning parameter control** (BRKGA-QL, Schenekemberg et al. 2022).
* **Multi-threaded islands** (needs a toolchain with `std::thread`; the installed MinGW 6.3 has none).

## 8. Sources

* Goncalves, J.F., Resende, M.G.C. (2013). A biased random key genetic algorithm for 2D and 3D bin packing problems. *Int. J. Production Economics* 145(2), 500-510. https://mauricio.resende.info/abstracts/brkga-binpacking.html
* Biased Random-Key Genetic Algorithms: A Review. https://arxiv.org/abs/2312.00961
* Gardeyn, J., Wauters, T. (2022). A goal-driven ruin and recreate heuristic for the 2D variable-sized bin packing problem with guillotine constraints. *EJOR* 301(2), 432-444. https://arxiv.org/abs/2508.19306
* Falkenauer, E. (1996). A hybrid grouping genetic algorithm for bin packing. *J. Heuristics* 2, 5-30.
* Lodi, A., Martello, S., Monaci, M. (2002). Two-dimensional packing problems: a survey. *EJOR* 141(2), 241-252.
* Jylanki, J. (2010). A thousand ways to pack the bin (MaxRects, Skyline, Guillotine heuristics).
