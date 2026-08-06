# Enemy bot: algorithms + tiny RL network

This work starts from commit `dc3488e0a5042debf43791c2cd8312a6731f2972`
on branch `codex/improve-opponent-bot`.

## Runtime architecture

The production bot is a hybrid, not a neural controller with access to Unreal
objects:

1. C++ keeps legal-action generation and hard tactical rules.
2. `HexBotNeuralPlanner.h` copies the live fight into an engine-independent POD
   state, applies actions without cloning Actors, and runs bounded alpha-beta
   search with a transposition cache.
3. A policy head orders/prunes legal branches; a value head evaluates search
   leaves. Both heads are `24 -> 32 -> 1` tanh MLPs.
4. The chosen first action is validated again against the live grid. If it is no
   longer legal, the existing Utility AI path remains the fallback.
5. Champion abilities stay behind their detailed C++ tactical rules because the
   compact simulator does not yet model their complete effects.

The two heads contain 1,666 learned `float` parameters in total (6,664 bytes).
Inference is CPU-only and has no PyTorch, CUDA, ONNX, or VRAM dependency in the
game. PyTorch is used only by the offline training script.

Search cost is bounded by editable Unreal properties:

- `EnemyBotNeuralSearchDepth`;
- `EnemyBotNeuralTopK`;
- `EnemyBotNeuralSafetyCandidates`;
- `EnemyBotNeuralNodeBudget`;
- `EnemyBotNeuralTimeBudgetMilliseconds`.

Forced terminal wins, immediate kills, and real life-saving heals are retained
before learned pruning. Candidate generation itself may cost more than a very
small time budget on an unusually large map, so the first hard-priority root
action is always evaluated before the deadline can stop search.

## No 10x10 or 5v5 assumption

The snapshot is built from the live `Cells`, `UnitsByCoord`, current HP, damage,
ranges, movement already spent, attack usage, healing, Last Stand, Marked for
Death, AP costs, path-cost mode, and kill-bonus settings. Feature normalization
uses the current topology diameter and the current armies' aggregate maxima and
totals. Unit type/role IDs are not inputs.

Training scenarios vary topology family (hexagonal and rectangular), holes,
stats, AP rules, and army size. The structural holdout deliberately uses ranges
outside training:

| Split | Map span | Units per team |
|---|---:|---:|
| Gradient/validation pool | 3–7 | 2–9 |
| Structural holdout | 8–12 | 10–18 |

These are generator ranges, not production limits.

## RL curriculum against a stronger greedy opponent

`HexBotNeuralBenchmark.cpp` generates Monte-Carlo returns using the same C++
simulator and legal actions as runtime. The learner plays a randomly selected
side. Its opponent is deterministic greedy Utility AI with configurable stat
advantages; the recorded curriculum uses `HP x1.12` and `damage x1.08`.

One generated board does not produce one scalar result. It evaluates up to
`CandidateCap` legal actions, performs `Rollouts` continuations per action, and
also emits value targets for both perspectives. With the recorded settings,
one position therefore yields up to 30 rollout outcomes plus two value rows.
Actions from one position always remain in the same split.

The offline fit is fitted Monte-Carlo policy iteration:

- policy targets are within-position action preferences derived from rollout
  returns, not a hand-written score;
- value targets are the best sampled continuation return from both team
  perspectives;
- early stopping sees only a group-disjoint validation subset of the small-map
  training distribution;
- the large-map/large-army structural holdout is never used for gradients or
  model selection.

Run a reproducible batched generation and fit:

```powershell
.\Tools\BotBenchmark\train_neural_selfplay.ps1 `
  -TrainPositions 800 `
  -HoldoutPositions 200 `
  -Rollouts 3 `
  -CandidateCap 10 `
  -Epochs 100 `
  -GreedyHealthMultiplier 1.12 `
  -GreedyDamageMultiplier 1.08 `
  -Workers 6
```

The generated CSV stays under `Saved/BotTraining`; the compact weights and the
metric report are exported to source control.

## C++ verification

Portable tests compile with MSVC `/std:c++17 /O2 /W4`:

```powershell
.\Tools\BotBenchmark\run_neural_benchmark.ps1 -TestsOnly
```

They cover finite/bounded inference on 20,000 variable real feature sets,
random legal transitions, invalid-action atomicity, forced kills under an
exhausted deadline, irregular topology, asymmetric armies, pruning, and the
hard node cap. These counts are cheap property/inference cases, **not simulated
full matches**.

Run the paired equal-stat structural-holdout regression:

```powershell
.\Tools\BotBenchmark\run_neural_benchmark.ps1 `
  -Pairs 200 `
  -Nodes 300 `
  -Milliseconds 0 `
  -GreedyHealthMultiplier 1.0 `
  -GreedyDamageMultiplier 1.0 `
  -JsonPath Docs\metrics\neural_benchmark.json
```

`Milliseconds=0` makes the research regression deterministic and bounded only
by nodes. Production still uses its editable wall-clock deadline. A separate
handicap challenge uses the training multipliers and `-NoGate`. For each
generated army/map, the controllers swap sides; the greedy handicap, when
enabled, follows the greedy controller. Draws at the dynamic turn cap remain
draws; they are not converted to wins using remaining HP.

Build and run Unreal Automation tests on the locally available UE 5.8:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  OtherBiosEditor Win64 Development `
  '-Project=C:\Users\mikha\Desktop\tima\projects\Valesunder\OtherBios.uproject' `
  -WaitMutex -NoHotReloadFromIDE -OverrideBuildEnvironment

& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Users\mikha\Desktop\tima\projects\Valesunder\OtherBios.uproject' `
  -unattended -nop4 -NullRHI -nosplash `
  '-ExecCmds=Automation RunTests OtherBios.BotAI;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

## Metric interpretation

Policy metrics report top-action agreement, return regret, and pairwise order
only among legal actions from the same held-out state. Value metrics report MAE,
correlation, and sign accuracy only for decisive targets. None is called
"gameplay accuracy".

`bot_benchmark_10000.json` is retained as historical evidence for the earlier
Utility-AI change. Its 10,000 rows were simplified headless matches, not 10,000
depth-five Unreal game-tree searches and not a shipping-game win rate.

The neural benchmark is also a headless rules-model regression. Compilation and
Unreal Automation cover integration, but only an end-to-end Blueprint map run
can establish the final shipping-game win rate. The project declares UE 5.5;
the available verification engine is UE 5.8 with the project's 5.5 include
order retained.

## Recorded results

Training seed `1592594996` produced 29,487 rollout continuations: 23,538 from
800 training positions and 5,949 from 200 structural-holdout positions. The
dataset SHA-256 is
`114686cb39412d02c48bd4b17ff24b512152123bbf8cf9a4071a4dc923d8264b`.

The structural holdout, which was not used for gradients or early stopping,
reported:

| Learned head metric | Result |
|---|---:|
| Policy pair ordering, target delta >= 0.05 | 54.05% over 3,321 pairs |
| Policy top-action agreement | 19.50% (10 candidates maximum) |
| Policy mean top-action return regret | 0.2032 |
| Value decisive-outcome sign | 79.89% over 348 rows |
| Value Pearson correlation | 0.7145 |
| Value MAE | 0.4040 |

These are modest results, not near-perfect accuracy. The policy head is useful
as one ranking signal but is not reliable enough to own legal actions or safety;
that is why the default policy/value blends are 0.55 and the C++ tactical signal
remains active.

The final deterministic equal-stat regression used 50 paired scenarios (100
matches), depth 5, top-K 8, and a 300-node cap:

| Metric | Hybrid RL search | Greedy Utility |
|---|---:|---:|
| Win rate | 44.0% | 39.0% |
| Draw rate | 17.0% | 17.0% |
| Win rate as first side | 44.0% | — |
| Win rate as second side | 44.0% | — |
| Forced-kill conversion | 100.0% | 100.0% |
| Invalid actions | 0 | 0 |

The 5-point edge is positive but not statistically decisive at only 100
matches. Machine-readable results are in
`Docs/metrics/neural_benchmark.json`.

The separate 40-match curriculum challenge kept the greedy bot at `HP x1.12`
and `damage x1.08`:

| Metric | Hybrid RL search | Stronger greedy |
|---|---:|---:|
| Win rate | 32.5% | 55.0% |
| Draw rate | 12.5% | 12.5% |
| Average damage dealt | 2270.83 | 2169.28 |
| Forced-kill conversion | 100.0% | 100.0% |

Losing this deliberately disadvantaged challenge is recorded rather than
hidden. Its purpose is curriculum pressure and regression visibility, not a
required shipping gate. Full details are in
`Docs/metrics/neural_handicap_challenge.json`.
