# Enemy bot decision model

This implementation is based on commit
`dc3488e0a5042debf43791c2cd8312a6731f2972` and lives on branch
`codex/improve-opponent-bot`.

## Why Utility AI instead of a neural network

There is no recorded corpus of full game states and expert actions in this
repository. A neural network trained without that data would only hide manually
chosen weights behind a harder-to-debug model. The bot therefore uses a small,
engine-independent Utility AI model with bounded Boltzmann selection. It is
probabilistic where variation is safe, deterministic on Nightmare, and keeps
hard tactical invariants ahead of probability.

The model ranks normalized features for expected damage or healing, target
value, plan alignment, future gain, positional progress, cohesion, exposure,
action-point cost, and overcommitment. Difficulty profiles change their weights,
temperature, and maximum allowed regret. The existing detailed game-specific
heuristics remain in place and become one input to this model.

Hard priorities are applied before sampling:

1. A terminal win cannot be randomized away.
2. A guaranteed kill cannot be randomized away.
3. If neither exists, a genuinely life-saving action cannot be randomized away.
4. Only near-optimal candidates inside the difficulty's regret bound may be
   sampled.

The turn planner also detects `move -> guaranteed kill` sequences before an
unrelated chip attack can consume the shared action points. Random score damage
and deliberate skipped kills from the previous implementation were removed.
The legacy `EnemyBotRandomScoreJitter` property remains serialized but unused so
existing Blueprints do not lose a field.

## Automated C++ tests

Two test layers compile the exact same header-only decision kernel:

- `Source/OtherBios/HexBotDecisionModelTests.cpp` contains Unreal Automation
  tests for forced kills, life-saving actions, Nightmare determinism, exposure
  scoring, and 50,000 randomized property trials.
- `Tools/BotBenchmark/HexBotBenchmark.cpp` is a standalone C++17 executable. It
  repeats 50,000 randomized trials for the forced-kill invariant, exact
  Nightmare maximum, and score monotonicity. It also checks invalid/NaN and empty
  candidate input.

Run only the portable stress tests:

```powershell
.\Tools\BotBenchmark\run_bot_benchmark.ps1 -TestsOnly
```

Run the aggressive metric regression suite (10,000 matches):

```powershell
.\Tools\BotBenchmark\run_bot_benchmark.ps1 `
  -Pairs 5000 `
  -Seed 1592594996 `
  -JsonPath Docs\metrics\bot_benchmark_10000.json
```

The script locates MSVC with `vswhere`, builds with `/std:c++17 /O2 /W4`, runs
the property tests, runs the paired benchmark, writes JSON, and returns a
non-zero exit code if a gate fails.

Run the Unreal tests after building the editor target:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  OtherBiosEditor Win64 Development `
  '-Project=C:\Users\mikha\Desktop\tima\projects\Valesunder\OtherBios.uproject' `
  -WaitMutex -NoHotReloadFromIDE -OverrideBuildEnvironment

& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Users\mikha\Desktop\tima\projects\Valesunder\OtherBios.uproject' `
  -unattended -nop4 -nosplash -NullRHI `
  '-ExecCmds=Automation RunTests OtherBios.BotAI.DecisionModel;Quit' `
  -TestExit='Automation Test Queue Empty' -log
```

## Benchmark design and gates

The benchmark is a deterministic, headless tactical model: an axial hex field,
shared action points, movement, attacks, healing, exposure, and five roles per
side (vanguard, archer, healer, champion, and skirmisher). For every generated
scenario it runs two battles and swaps which side uses the improved policy. This
reduces first-move and deployment bias.

The improved policy mirrors the production action ladder and uses the production
decision kernel. The legacy opponent intentionally approximates the previous
fixed ladder: take any immediate attack first, heal low-health allies, then move
greedily. The benchmark does **not** execute the full Blueprint map or claim a
shipping-game win rate; it is a reproducible regression test of the policy
difference. Full integration is covered separately by compiling
`HexGridActor.cpp` and running the Unreal Automation tests.

Every long run must satisfy all of these gates:

- lower bound of the two-sided 95% Wilson interval for improved win rate >= 75%;
- improved win rate >= 75% both as the first and as the second side;
- forced-kill conversion at least 10 percentage points above legacy;
- average remaining health at least 75 above legacy;
- total damage no lower than legacy.

## Recorded result

Fixed seed `1592594996`, 5,000 paired scenarios, 10,000 matches:

| Metric | Improved | Legacy |
|---|---:|---:|
| Win rate | 84.25% | 15.75% |
| 95% Wilson lower bound | 83.52% | - |
| Win rate as first side | 88.84% | - |
| Win rate as second side | 79.66% | - |
| Average remaining HP | 184.39 | 26.52 |
| Average damage | 549.00 | 499.53 |
| Forced-kill conversion | 70.72% | 56.94% |
| Move-to-kill setups | 18,358 | 0 |

Average battle length was 9.01 full turns and there were no draws. The
machine-readable evidence is in
`Docs/metrics/bot_benchmark_10000.json`.

## Verification boundary

The project declares Unreal Engine 5.5, which was not installed on the test
machine. The editor target compiled successfully under UE 5.8 while retaining
the project's UE 5.5 include order, and all five `OtherBios.BotAI.DecisionModel`
tests passed headlessly. An early run made before the Git LFS checkout had
finished produced missing-asset and Blueprint errors. After all 807 missing LFS
assets were restored, the final headless run produced no Blueprint compile
errors and again completed all five bot tests with exit code 0. A complete
Blueprint gameplay-map battle is still not claimed because this benchmark does
not drive that map end to end.
