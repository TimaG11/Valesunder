#include "HexBotNeuralPlanner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	using namespace OtherBios::BotNeural;

	constexpr std::uint32_t SeedStride = 7919u;

	struct FDistribution
	{
		const char* Name = "training";
		int MinSpan = 3;
		int MaxSpan = 7;
		int MinUnitsPerTeam = 2;
		int MaxUnitsPerTeam = 9;
		int MinMaxActionPoints = 3;
		int MaxMaxActionPoints = 8;
		int MinHealth = 55;
		int MaxHealth = 185;
		int MinDamage = 12;
		int MaxDamage = 58;
		int MaxRange = 4;
		int MaxMovement = 6;
		int HolePercent = 8;
	};

	const FDistribution TrainingDistribution{};
	const FDistribution StructuralHoldoutDistribution{
		"structural_holdout", 8, 12, 10, 18, 2, 11, 38, 260, 8, 82, 7, 10, 22
	};

	bool Expect(bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "FAILED: " << Message << '\n';
			return false;
		}
		return true;
	}

	template <typename T>
	T RandomInt(std::mt19937& Random, T Minimum, T Maximum)
	{
		return std::uniform_int_distribution<T>(Minimum, Maximum)(Random);
	}

	float RandomFloat(std::mt19937& Random, float Minimum, float Maximum)
	{
		return std::uniform_real_distribution<float>(Minimum, Maximum)(Random);
	}

	void KeepLargestConnectedComponent(FTopology& Topology)
	{
		static constexpr int Directions[6][2] = {
			{ 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 }
		};
		Topology.RebuildIndex();
		std::unordered_set<std::uint64_t> Visited;
		std::vector<FCoord> Largest;
		for (const FCoord& Start : Topology.Cells)
		{
			if (!Visited.insert(CoordKey(Start.Q, Start.R)).second) continue;
			std::vector<FCoord> Component;
			std::queue<FCoord> Pending;
			Pending.push(Start);
			while (!Pending.empty())
			{
				const FCoord Current = Pending.front();
				Pending.pop();
				Component.push_back(Current);
				for (const auto& Direction : Directions)
				{
					const FCoord Next{ Current.Q + Direction[0], Current.R + Direction[1] };
					const std::uint64_t Key = CoordKey(Next.Q, Next.R);
					if (Topology.HasCell(Next) && Visited.insert(Key).second) Pending.push(Next);
				}
			}
			if (Component.size() > Largest.size()) Largest = std::move(Component);
		}
		Topology.Cells = std::move(Largest);
		Topology.RebuildIndex();
	}

	FTopology MakeTopology(std::mt19937& Random, const FDistribution& Distribution)
	{
		FTopology Topology;
		const int Span = RandomInt(Random, Distribution.MinSpan, Distribution.MaxSpan);
		const bool bHexagon = (Random() & 1u) == 0u;
		if (bHexagon)
		{
			for (int Q = -Span; Q <= Span; ++Q)
			{
				for (int R = -Span; R <= Span; ++R)
				{
					const int S = -Q - R;
					if (std::max({ std::abs(Q), std::abs(R), std::abs(S) }) <= Span)
					{
						Topology.Cells.push_back({ Q, R });
					}
				}
			}
		}
		else
		{
			const int Width = Span * 2 + 1;
			const int Height = std::max(3, Span + RandomInt(Random, -1, 2));
			for (int Q = -Width / 2; Q <= Width / 2; ++Q)
			{
				for (int R = -Height / 2; R <= Height / 2; ++R)
				{
					Topology.Cells.push_back({ Q, R });
				}
			}
		}

		std::shuffle(Topology.Cells.begin(), Topology.Cells.end(), Random);
		const std::size_t MinimumCells = static_cast<std::size_t>(Distribution.MaxUnitsPerTeam * 3);
		const std::size_t Removable = Topology.Cells.size() > MinimumCells ? Topology.Cells.size() - MinimumCells : 0;
		const std::size_t RequestedHoles = Topology.Cells.size() * static_cast<std::size_t>(Distribution.HolePercent) / 100u;
		Topology.Cells.resize(Topology.Cells.size() - std::min(Removable, RequestedHoles));
		KeepLargestConnectedComponent(Topology);
		return Topology;
	}

	FUnit MakeRandomUnit(
		std::uint32_t Key,
		std::uint8_t Team,
		FCoord Cell,
		std::mt19937& Random,
		const FDistribution& Distribution)
	{
		FUnit Unit;
		Unit.StableKey = Key;
		Unit.Team = Team;
		Unit.Cell = Cell;
		Unit.MaxHealth = RandomInt(Random, Distribution.MinHealth, Distribution.MaxHealth);
		Unit.Health = Unit.MaxHealth;
		Unit.Damage = RandomInt(Random, Distribution.MinDamage, Distribution.MaxDamage);
		Unit.AttackRange = RandomInt(Random, 1, Distribution.MaxRange);
		Unit.MovementRange = RandomInt(Random, 1, Distribution.MaxMovement);
		Unit.IncomingDamageScale = RandomFloat(Random, 0.72f, 1.38f);
		if (RandomInt(Random, 0, 99) < 24)
		{
			Unit.bCanHeal = true;
			Unit.HealAmount = RandomInt(Random, std::max(1, Distribution.MinHealth / 5),
				std::max(2, Distribution.MaxHealth / 2));
			Unit.HealRange = RandomInt(Random, 1, std::max(1, Distribution.MaxRange));
			Unit.HealCost = RandomInt(Random, 1, 3);
		}
		if (RandomInt(Random, 0, 99) < 9)
		{
			Unit.bLastStandActive = true;
			Unit.LastStandSurviveHealth = RandomInt(Random, 1, std::max(1, Unit.MaxHealth / 4));
		}
		return Unit;
	}

	FState MakeScenario(const FTopology& Topology, std::mt19937& Random, const FDistribution& Distribution)
	{
		FState State;
		State.Topology = &Topology;
		State.MaxActionPoints = RandomInt(Random, Distribution.MinMaxActionPoints, Distribution.MaxMaxActionPoints);
		State.ActionPoints = State.MaxActionPoints;
		State.MoveActionPointCost = RandomInt(Random, 1, std::max(1, State.MaxActionPoints / 3));
		State.AttackActionPointCost = RandomInt(Random, 1, std::max(1, State.MaxActionPoints / 3));
		State.bUsePathLengthForMoveCost = RandomInt(Random, 0, 99) < 35;
		State.bEnableKillActionPointBonus = RandomInt(Random, 0, 99) < 65;
		State.KillActionPointBonus = RandomInt(Random, 0, std::max(1, State.MaxActionPoints / 2));
		State.bLimitKillBonusOncePerTurn = RandomInt(Random, 0, 99) < 70;
		State.SideToMove = static_cast<std::uint8_t>(RandomInt(Random, 0, 1));

		std::vector<FCoord> Ordered = Topology.Cells;
		std::stable_sort(Ordered.begin(), Ordered.end(), [](const FCoord& Left, const FCoord& Right)
		{
			if (Left.Q != Right.Q) return Left.Q < Right.Q;
			return Left.R < Right.R;
		});
		const int MaximumByCells = std::max(1, static_cast<int>(Ordered.size() / 3));
		const int Team0Count = RandomInt(Random, Distribution.MinUnitsPerTeam,
			std::min(Distribution.MaxUnitsPerTeam, MaximumByCells));
		const int Team1Count = RandomInt(Random, Distribution.MinUnitsPerTeam,
			std::min(Distribution.MaxUnitsPerTeam, MaximumByCells));
		const int EdgePoolSize = std::min(static_cast<int>(Ordered.size() / 2),
			std::max({ Team0Count * 2, Team1Count * 2, 1 }));
		std::vector<FCoord> Left(Ordered.begin(), Ordered.begin() + EdgePoolSize);
		std::vector<FCoord> Right(Ordered.end() - EdgePoolSize, Ordered.end());
		std::shuffle(Left.begin(), Left.end(), Random);
		std::shuffle(Right.begin(), Right.end(), Random);
		std::uint32_t Key = 1;
		for (int Index = 0; Index < Team0Count; ++Index)
		{
			State.Units.push_back(MakeRandomUnit(Key++, 0, Left[static_cast<std::size_t>(Index)], Random, Distribution));
		}
		for (int Index = 0; Index < Team1Count; ++Index)
		{
			State.Units.push_back(MakeRandomUnit(Key++, 1, Right[static_cast<std::size_t>(Index)], Random, Distribution));
		}
		return State;
	}

	int TeamHealth(const FState& State, std::uint8_t Team)
	{
		int Health = 0;
		for (const FUnit& Unit : State.Units)
		{
			if (Unit.bAlive && Unit.Health > 0 && Unit.Team == Team) Health += Unit.Health;
		}
		return Health;
	}

	float OutcomeForTeam(const FState& State, std::uint8_t Team)
	{
		const std::uint8_t Opponent = Team == 0 ? 1 : 0;
		if (!FSimulator::HasAliveTeam(State, Team)) return -1.0f;
		if (!FSimulator::HasAliveTeam(State, Opponent)) return 1.0f;
		float Own = 0.0f;
		float Enemy = 0.0f;
		for (const FUnit& Unit : State.Units)
		{
			if (!Unit.bAlive || Unit.Health <= 0) continue;
			const float HealthFraction = static_cast<float>(Unit.Health) / std::max(1, Unit.MaxHealth);
			const float Power = FSimulator::RawUnitValue(Unit) * (0.25f + 0.75f * HealthFraction);
			(Unit.Team == Team ? Own : Enemy) += Power;
		}
		return std::max(-0.95f, std::min(0.95f, (Own - Enemy) / std::max(1.0f, Own + Enemy)));
	}

	void ApplyGreedyHandicap(FState& State, std::uint8_t GreedyTeam, float HealthMultiplier, float DamageMultiplier)
	{
		for (FUnit& Unit : State.Units)
		{
			if (Unit.Team != GreedyTeam) continue;
			const float HealthFraction = static_cast<float>(Unit.Health) / std::max(1, Unit.MaxHealth);
			Unit.MaxHealth = std::max(1, static_cast<int>(std::lround(Unit.MaxHealth * std::max(1.0f, HealthMultiplier))));
			Unit.Health = std::max(1, static_cast<int>(std::lround(Unit.MaxHealth * HealthFraction)));
			Unit.Damage = std::max(0, static_cast<int>(std::lround(Unit.Damage * std::max(1.0f, DamageMultiplier))));
		}
	}

	FAction ChooseScoredAction(const FState& State, std::mt19937* Random, float Exploration, bool bUseNetwork)
	{
		std::vector<FAction> Actions;
		FSimulator::GenerateActions(State, Actions);
		if (Actions.empty()) return FAction{};
		std::vector<const FAction*> Restricted;
		auto Collect = [&](auto Predicate)
		{
			for (const FAction& Action : Actions) if (Predicate(Action)) Restricted.push_back(&Action);
		};
		Collect([](const FAction& Action) { return Action.bTerminalOutcome; });
		if (Restricted.empty()) Collect([](const FAction& Action)
		{
			return Action.Kind == EActionKind::Attack && Action.bSecuresKill;
		});
		if (Restricted.empty()) Collect([](const FAction& Action)
		{
			return Action.Kind == EActionKind::Heal && Action.bPreventsDeath;
		});
		if (Restricted.empty()) for (const FAction& Action : Actions) Restricted.push_back(&Action);

		if (Random && RandomFloat(*Random, 0.0f, 1.0f) < Exploration)
		{
			return *Restricted[static_cast<std::size_t>(RandomInt(*Random, 0, static_cast<int>(Restricted.size()) - 1))];
		}
		FPolicyValueNetwork Network;
		const FAction* Best = Restricted.front();
		float BestScore = -std::numeric_limits<float>::infinity();
		for (const FAction* Action : Restricted)
		{
			const auto Features = FSimulator::ActionFeatures(State, *Action);
			float Score = FSimulator::TacticalFallbackScore(Features);
			if (bUseNetwork) Score += 0.65f * Network.ScoreAction(Features);
			if (Score > BestScore + 1.0e-6f ||
				(std::abs(Score - BestScore) <= 1.0e-6f && Action->StableKey < Best->StableKey))
			{
				Best = Action;
				BestScore = Score;
			}
		}
		return *Best;
	}

	float Rollout(FState State, std::uint8_t LearnerTeam, std::mt19937& Random, int MaxHalfTurns)
	{
		int HalfTurns = 0;
		int Decisions = 0;
		const int DecisionLimit = MaxHalfTurns * std::max(4, State.MaxActionPoints * 4);
		while (FSimulator::HasAliveTeam(State, 0) && FSimulator::HasAliveTeam(State, 1) &&
			HalfTurns < MaxHalfTurns && Decisions < DecisionLimit)
		{
			const std::uint8_t Before = State.SideToMove;
			const bool bLearnerTurn = State.SideToMove == LearnerTeam;
			const FAction Action = bLearnerTurn
				? ChooseScoredAction(State, &Random, 0.14f, true)
				: ChooseScoredAction(State, nullptr, 0.0f, false);
			if (!FSimulator::ApplyAction(State, Action))
			{
				FAction End;
				End.Kind = EActionKind::EndTurn;
				FSimulator::ApplyAction(State, End);
			}
			if (State.SideToMove != Before) ++HalfTurns;
			++Decisions;
		}
		return OutcomeForTeam(State, LearnerTeam);
	}

	std::vector<FAction> TrainingCandidates(const FState& State, int CandidateCap, std::mt19937& Random)
	{
		std::vector<FAction> Actions;
		FSimulator::GenerateActions(State, Actions);
		std::stable_sort(Actions.begin(), Actions.end(), [&State](const FAction& Left, const FAction& Right)
		{
			const int LeftHard = Left.bTerminalOutcome ? 3 : (Left.bSecuresKill ? 2 : (Left.bPreventsDeath ? 1 : 0));
			const int RightHard = Right.bTerminalOutcome ? 3 : (Right.bSecuresKill ? 2 : (Right.bPreventsDeath ? 1 : 0));
			if (LeftHard != RightHard) return LeftHard > RightHard;
			const float LeftScore = FSimulator::TacticalFallbackScore(FSimulator::ActionFeatures(State, Left));
			const float RightScore = FSimulator::TacticalFallbackScore(FSimulator::ActionFeatures(State, Right));
			if (LeftScore != RightScore) return LeftScore > RightScore;
			return Left.StableKey < Right.StableKey;
		});
		if (static_cast<int>(Actions.size()) <= CandidateCap) return Actions;
		const int RandomSlots = std::min(2, std::max(0, CandidateCap / 4));
		std::vector<FAction> Selected(Actions.begin(), Actions.begin() + CandidateCap - RandomSlots);
		std::shuffle(Actions.begin() + CandidateCap - RandomSlots, Actions.end(), Random);
		for (int Index = 0; Index < RandomSlots; ++Index)
		{
			Selected.push_back(Actions[static_cast<std::size_t>(CandidateCap - RandomSlots + Index)]);
		}
		return Selected;
	}

	void WarmUpPosition(FState& State, std::uint8_t LearnerTeam, std::mt19937& Random)
	{
		const int MaximumSteps = std::max(1, (State.Topology ? State.Topology->Diameter : 1) * 3);
		const int Steps = RandomInt(Random, 0, MaximumSteps);
		for (int Step = 0; Step < Steps && FSimulator::HasAliveTeam(State, 0) && FSimulator::HasAliveTeam(State, 1); ++Step)
		{
			const bool bLearnerTurn = State.SideToMove == LearnerTeam;
			const FAction Action = bLearnerTurn
				? ChooseScoredAction(State, &Random, 0.22f, true)
				: ChooseScoredAction(State, nullptr, 0.0f, false);
			if (!FSimulator::ApplyAction(State, Action)) break;
		}
		for (FUnit& Unit : State.Units)
		{
			if (!Unit.bAlive || Unit.Health <= 0) continue;
			if (RandomInt(Random, 0, 99) < 38)
			{
				Unit.Health = RandomInt(Random, std::max(1, Unit.MaxHealth / 4), Unit.MaxHealth);
			}
		}
	}

	void WriteFeatureRow(
		std::ostream& Out,
		const char* Split,
		char Head,
		std::uint64_t Group,
		float Target,
		const std::array<float, FeatureCount>& Features)
	{
		Out << Split << ',' << Head << ',' << Group << ',' << std::setprecision(9) << Target;
		for (float Feature : Features) Out << ',' << Feature;
		Out << '\n';
	}

	struct FDataCounts
	{
		long long Positions = 0;
		long long PolicyRows = 0;
		long long ValueRows = 0;
		long long Rollouts = 0;
	};

	struct FPositionData
	{
		std::string Rows;
		FDataCounts Counts;
	};

	FPositionData GeneratePosition(
		const char* Split,
		const FDistribution& Distribution,
		int Position,
		int RolloutsPerAction,
		int CandidateCap,
		float GreedyHealthMultiplier,
		float GreedyDamageMultiplier,
		std::uint32_t Seed,
		std::uint64_t GroupOffset)
	{
		FPositionData Data;
		std::ostringstream Rows;
		std::mt19937 Random(Seed + static_cast<std::uint32_t>(Position) * SeedStride);
		FTopology Topology = MakeTopology(Random, Distribution);
		FState State = MakeScenario(Topology, Random, Distribution);
		const std::uint8_t LearnerTeam = static_cast<std::uint8_t>(RandomInt(Random, 0, 1));
		const std::uint8_t GreedyTeam = LearnerTeam == 0 ? 1 : 0;
		ApplyGreedyHandicap(State, GreedyTeam, GreedyHealthMultiplier, GreedyDamageMultiplier);
		WarmUpPosition(State, LearnerTeam, Random);
		if (!FSimulator::HasAliveTeam(State, 0) || !FSimulator::HasAliveTeam(State, 1))
		{
			State = MakeScenario(Topology, Random, Distribution);
			ApplyGreedyHandicap(State, GreedyTeam, GreedyHealthMultiplier, GreedyDamageMultiplier);
		}
		if (State.SideToMove != LearnerTeam)
		{
			FAction End;
			End.Kind = EActionKind::EndTurn;
			FSimulator::ApplyAction(State, End);
		}
		const std::uint64_t Group = GroupOffset + static_cast<std::uint64_t>(Position);
		const std::vector<FAction> Candidates = TrainingCandidates(State, CandidateCap, Random);
		float BestReturn = -1.0f;
		for (std::size_t ActionIndex = 0; ActionIndex < Candidates.size(); ++ActionIndex)
		{
			FState After = State;
			if (!FSimulator::ApplyAction(After, Candidates[ActionIndex])) continue;
			float Return = 0.0f;
			for (int RolloutIndex = 0; RolloutIndex < RolloutsPerAction; ++RolloutIndex)
			{
				std::mt19937 RolloutRandom(
					Seed ^ static_cast<std::uint32_t>(Group * 2654435761ull) ^
					static_cast<std::uint32_t>(RolloutIndex * 3266489917ull));
				const int MaxHalfTurns = std::max(24, Topology.Diameter * 2 + static_cast<int>(State.Units.size()));
				Return += Rollout(After, LearnerTeam, RolloutRandom, MaxHalfTurns);
				++Data.Counts.Rollouts;
			}
			Return /= static_cast<float>(RolloutsPerAction);
			BestReturn = std::max(BestReturn, Return);
			WriteFeatureRow(Rows, Split, 'P', Group, Return,
				FSimulator::ActionFeatures(State, Candidates[ActionIndex]));
			++Data.Counts.PolicyRows;
		}
		WriteFeatureRow(Rows, Split, 'V', Group, BestReturn,
			FSimulator::StateFeatures(State, LearnerTeam));
		WriteFeatureRow(Rows, Split, 'V', Group, -BestReturn,
			FSimulator::StateFeatures(State, LearnerTeam == 0 ? 1 : 0));
		Data.Counts.ValueRows = 2;
		Data.Counts.Positions = 1;
		Data.Rows = Rows.str();
		return Data;
	}

	FDataCounts GenerateSplit(
		std::ofstream& Out,
		const char* Split,
		const FDistribution& Distribution,
		int Positions,
		int RolloutsPerAction,
		int CandidateCap,
		float GreedyHealthMultiplier,
		float GreedyDamageMultiplier,
		int WorkerCount,
		std::uint32_t Seed,
		std::uint64_t GroupOffset)
	{
		FDataCounts Counts;
		const int SafeWorkerCount = std::max(1, std::min(WorkerCount, Positions));
		for (int BatchStart = 0; BatchStart < Positions; BatchStart += SafeWorkerCount)
		{
			const int BatchEnd = std::min(Positions, BatchStart + SafeWorkerCount);
			std::vector<std::future<FPositionData>> Futures;
			Futures.reserve(static_cast<std::size_t>(BatchEnd - BatchStart));
			for (int Position = BatchStart; Position < BatchEnd; ++Position)
			{
				Futures.emplace_back(std::async(std::launch::async, [&, Position]()
				{
					return GeneratePosition(Split, Distribution, Position, RolloutsPerAction, CandidateCap,
						GreedyHealthMultiplier, GreedyDamageMultiplier, Seed, GroupOffset);
				}));
			}
			for (int Offset = 0; Offset < BatchEnd - BatchStart; ++Offset)
			{
				const FPositionData Data = Futures[static_cast<std::size_t>(Offset)].get();
				Out << Data.Rows;
				Counts.Positions += Data.Counts.Positions;
				Counts.PolicyRows += Data.Counts.PolicyRows;
				Counts.ValueRows += Data.Counts.ValueRows;
				Counts.Rollouts += Data.Counts.Rollouts;
			}
			if (BatchEnd % 100 == 0 || BatchEnd == Positions)
			{
				std::cout << "generated " << Split << " positions=" << BatchEnd << '/' << Positions << '\n';
			}
		}
		return Counts;
	}

	void GenerateDataset(
		const std::string& Path,
		int TrainPositions,
		int HoldoutPositions,
		int Rollouts,
		int CandidateCap,
		float GreedyHealthMultiplier,
		float GreedyDamageMultiplier,
		int WorkerCount,
		std::uint32_t Seed)
	{
		std::ofstream Out(Path, std::ios::trunc);
		if (!Out) throw std::runtime_error("cannot open dataset output: " + Path);
		Out << "split,head,group,target";
		for (std::size_t Index = 0; Index < FeatureCount; ++Index) Out << ",f" << Index;
		Out << '\n';
		const FDataCounts Train = GenerateSplit(Out, "train", TrainingDistribution, TrainPositions,
			Rollouts, CandidateCap, GreedyHealthMultiplier, GreedyDamageMultiplier, WorkerCount, Seed, 0);
		const FDataCounts Holdout = GenerateSplit(Out, "holdout", StructuralHoldoutDistribution,
			HoldoutPositions, Rollouts, CandidateCap, GreedyHealthMultiplier, GreedyDamageMultiplier,
			WorkerCount, Seed ^ 0xa5a5a5a5u, 1ull << 40u);
		Out.close();

		std::ofstream Meta(Path + ".meta.json", std::ios::trunc);
		Meta << "{\n"
			<< "  \"target_source\": \"monte_carlo_rl_vs_greedy_returns\",\n"
			<< "  \"seed\": " << Seed << ",\n"
			<< "  \"rollouts_per_action\": " << Rollouts << ",\n"
			<< "  \"candidate_cap\": " << CandidateCap << ",\n"
			<< "  \"workers\": " << WorkerCount << ",\n"
			<< "  \"rollout_half_turn_cap\": \"max(24, topology_diameter * 2 + total_units)\",\n"
			<< "  \"non_terminal_target\": \"normalized_remaining_material_in_[-0.95,0.95]\",\n"
			<< "  \"opponent\": \"deterministic_greedy\",\n"
			<< "  \"greedy_health_multiplier\": " << GreedyHealthMultiplier << ",\n"
			<< "  \"greedy_damage_multiplier\": " << GreedyDamageMultiplier << ",\n"
			<< "  \"training\": {\"positions\": " << Train.Positions << ", \"policy_rows\": " << Train.PolicyRows
			<< ", \"value_rows\": " << Train.ValueRows << ", \"rollouts\": " << Train.Rollouts
			<< ", \"map_span\": [" << TrainingDistribution.MinSpan << ", " << TrainingDistribution.MaxSpan
			<< "], \"units_per_team\": [" << TrainingDistribution.MinUnitsPerTeam << ", "
			<< TrainingDistribution.MaxUnitsPerTeam << "]},\n"
			<< "  \"structural_holdout\": {\"positions\": " << Holdout.Positions << ", \"policy_rows\": "
			<< Holdout.PolicyRows << ", \"value_rows\": " << Holdout.ValueRows << ", \"rollouts\": "
			<< Holdout.Rollouts << ", \"map_span\": [" << StructuralHoldoutDistribution.MinSpan << ", "
			<< StructuralHoldoutDistribution.MaxSpan << "], \"units_per_team\": ["
			<< StructuralHoldoutDistribution.MinUnitsPerTeam << ", "
			<< StructuralHoldoutDistribution.MaxUnitsPerTeam << "]}\n"
			<< "}\n";
		std::cout << "dataset=" << Path << " train_positions=" << Train.Positions
			<< " holdout_positions=" << Holdout.Positions << " rollouts=" << (Train.Rollouts + Holdout.Rollouts) << '\n';
	}

	bool StateHasUniqueLivingCells(const FState& State)
	{
		std::unordered_set<std::uint64_t> Occupied;
		for (const FUnit& Unit : State.Units)
		{
			if (!Unit.bAlive || Unit.Health <= 0) continue;
			if (!State.Topology || !State.Topology->HasCell(Unit.Cell) ||
				!Occupied.insert(CoordKey(Unit.Cell.Q, Unit.Cell.R)).second) return false;
		}
		return true;
	}

	bool RunUnitTests()
	{
		bool Passed = true;
		Passed &= Expect(FPolicyValueNetwork::ParameterCount() == 1666,
			"the deployed policy/value model has exactly 1666 learned parameters");
		Passed &= Expect(sizeof(float) * FPolicyValueNetwork::ParameterCount() < 8 * 1024,
			"the deployed weights occupy less than 8 KiB");

		FPolicyValueNetwork Network;
		std::mt19937 Random(0x51a7f00du);
		bool bFinite = true;
		for (int Trial = 0; Trial < 20000; ++Trial)
		{
			const FDistribution& Distribution = (Trial & 1) ? TrainingDistribution : StructuralHoldoutDistribution;
			FTopology Topology = MakeTopology(Random, Distribution);
			FState State = MakeScenario(Topology, Random, Distribution);
			std::vector<FAction> Actions;
			FSimulator::GenerateActions(State, Actions);
			const FAction& Action = Actions[static_cast<std::size_t>(RandomInt(Random, 0, static_cast<int>(Actions.size()) - 1))];
			const float Policy = Network.ScoreAction(FSimulator::ActionFeatures(State, Action));
			const float Value = Network.EvaluateState(FSimulator::StateFeatures(State, static_cast<std::uint8_t>(Trial & 1)));
			bFinite = bFinite && std::isfinite(Policy) && std::isfinite(Value) && Value >= -1.0f && Value <= 1.0f;
		}
		Passed &= Expect(bFinite, "20k variable-state inference stability checks are finite and value-bounded");

		FTopology AtomicTopology;
		AtomicTopology.Cells = { {0, 0}, {1, 0}, {2, 0} };
		AtomicTopology.RebuildIndex();
		FState AtomicState;
		AtomicState.Topology = &AtomicTopology;
		AtomicState.ActionPoints = AtomicState.MaxActionPoints = 4;
		AtomicState.Units = {
			FUnit{ 1, 0, {0, 0}, 50, 50, 20, 1, 2 },
			FUnit{ 2, 1, {2, 0}, 50, 50, 20, 1, 2 }
		};
		FAction InvalidAttack;
		InvalidAttack.Kind = EActionKind::Attack;
		InvalidAttack.ActorIndex = 0;
		InvalidAttack.TargetIndex = 1;
		InvalidAttack.ActionPointCost = 1;
		const int AtomicApBefore = AtomicState.ActionPoints;
		Passed &= Expect(!FSimulator::ApplyAction(AtomicState, InvalidAttack) && AtomicState.ActionPoints == AtomicApBefore,
			"an invalid action cannot partially spend AP");

		std::mt19937 KillRandom(7);
		FTopology KillTopology = MakeTopology(KillRandom, TrainingDistribution);
		FState KillState;
		KillState.Topology = &KillTopology;
		KillState.ActionPoints = KillState.MaxActionPoints = 6;
		FUnit Killer = MakeRandomUnit(1, 0, KillTopology.Cells[0], KillRandom, TrainingDistribution);
		FUnit Victim = MakeRandomUnit(2, 1, KillTopology.Cells[1], KillRandom, TrainingDistribution);
		FUnit Other = MakeRandomUnit(3, 1, KillTopology.Cells.back(), KillRandom, TrainingDistribution);
		Victim.Cell = { Killer.Cell.Q + 1, Killer.Cell.R };
		if (!KillTopology.HasCell(Victim.Cell)) Victim.Cell = KillTopology.Cells[1];
		Killer.AttackRange = std::max(1, HexDistance(Killer.Cell, Victim.Cell));
		Killer.Damage = Victim.Health + 1;
		Victim.bLastStandActive = false;
		KillState.Units = { Killer, Victim, Other };
		FSearchSettings KillSettings;
		KillSettings.Depth = 5;
		KillSettings.TopK = 4;
		KillSettings.NodeBudget = 500;
		KillSettings.TimeBudgetMilliseconds = 0.0;
		FNeuralSearchPlanner Planner;
		const FSearchResult KillResult = Planner.FindBestAction(KillState, 0, KillSettings);
		Passed &= Expect(KillResult.bHasAction && KillResult.Action.Kind == EActionKind::Attack &&
			KillResult.Action.TargetIndex == 1 && KillResult.Action.bSecuresKill,
			"depth-5 planner preserves an immediate kill on generated topology");
		Passed &= Expect(KillResult.NodesVisited <= KillSettings.NodeBudget,
			"search cannot exceed its hard node budget");

		FTopology LargeTopology = MakeTopology(Random, StructuralHoldoutDistribution);
		FState LargeState = MakeScenario(LargeTopology, Random, StructuralHoldoutDistribution);
		FSearchSettings LargeSettings;
		LargeSettings.Depth = 5;
		LargeSettings.TopK = 8;
		LargeSettings.NodeBudget = 750;
		LargeSettings.TimeBudgetMilliseconds = 0.0;
		const FSearchResult LargeResult = Planner.FindBestAction(LargeState, LargeState.SideToMove, LargeSettings);
		Passed &= Expect(LargeResult.bHasAction && LargeResult.RootRawActions >= LargeResult.RootExpandedActions,
			"structural-holdout map action set is bounded before search");
		Passed &= Expect(LargeResult.NodesVisited <= LargeSettings.NodeBudget,
			"larger maps and armies remain bounded by nodes");

		bool bFuzzValid = true;
		long long AppliedActions = 0;
		for (int Scenario = 0; Scenario < 500; ++Scenario)
		{
			const FDistribution& Distribution = (Scenario & 1) ? TrainingDistribution : StructuralHoldoutDistribution;
			FTopology Topology = MakeTopology(Random, Distribution);
			FState State = MakeScenario(Topology, Random, Distribution);
			for (int Step = 0; Step < 40 && FSimulator::HasAliveTeam(State, 0) && FSimulator::HasAliveTeam(State, 1); ++Step)
			{
				std::vector<FAction> Actions;
				FSimulator::GenerateActions(State, Actions);
				const FAction Action = Actions[static_cast<std::size_t>(RandomInt(Random, 0, static_cast<int>(Actions.size()) - 1))];
				bFuzzValid = bFuzzValid && FSimulator::ApplyAction(State, Action) && State.ActionPoints >= 0 &&
					State.ActionPoints <= State.MaxActionPoints && StateHasUniqueLivingCells(State);
				++AppliedActions;
			}
		}
		Passed &= Expect(bFuzzValid, "randomized legal actions preserve state invariants across variable scenarios");

		std::cout << "inference_stability_cases=20000 transition_fuzz_actions=" << AppliedActions << '\n';
		std::cout << "holdout_raw_actions=" << LargeResult.RootRawActions
			<< " expanded=" << LargeResult.RootExpandedActions << " nodes=" << LargeResult.NodesVisited << '\n';
		std::cout << (Passed ? "All neural planner tests passed.\n" : "Neural planner tests failed.\n");
		return Passed;
	}

	struct FMatchMetrics
	{
		int Winner = -1;
		int HalfTurns = 0;
		int Damage[2] = { 0, 0 };
		int ForcedOpportunities[2] = { 0, 0 };
		int ForcedKills[2] = { 0, 0 };
		long long SearchNodes = 0;
		double SearchMilliseconds = 0.0;
		int SearchCalls = 0;
		int BudgetHits = 0;
		int InvalidActions = 0;
	};

	bool HasImmediateKill(const FState& State)
	{
		std::vector<FAction> Actions;
		FSimulator::GenerateActions(State, Actions);
		return std::any_of(Actions.begin(), Actions.end(), [](const FAction& Action)
		{
			return Action.Kind == EActionKind::Attack && Action.bSecuresKill;
		});
	}

	FMatchMetrics RunMatch(FState State, int NeuralTeam, const FSearchSettings& SearchSettings)
	{
		FMatchMetrics Metrics;
		FNeuralSearchPlanner Planner;
		const int MaxHalfTurns = std::max(36, (State.Topology ? State.Topology->Diameter : 1) * 3 +
			static_cast<int>(State.Units.size()) * 2);
		const int DecisionLimit = MaxHalfTurns * std::max(4, State.MaxActionPoints * 4);
		int Decisions = 0;
		while (FSimulator::HasAliveTeam(State, 0) && FSimulator::HasAliveTeam(State, 1) &&
			Metrics.HalfTurns < MaxHalfTurns && Decisions < DecisionLimit)
		{
			const std::uint8_t ActingTeam = State.SideToMove;
			const std::uint8_t EnemyTeam = ActingTeam == 0 ? 1 : 0;
			const bool bForcedKill = HasImmediateKill(State);
			Metrics.ForcedOpportunities[ActingTeam] += bForcedKill ? 1 : 0;
			const int EnemyHealthBefore = TeamHealth(State, EnemyTeam);
			const int EnemyAliveBefore = FSimulator::AliveCount(State, EnemyTeam);
			FAction Action;
			if (ActingTeam == NeuralTeam)
			{
				const FSearchResult Search = Planner.FindBestAction(State, ActingTeam, SearchSettings);
				Action = Search.bHasAction ? Search.Action : FAction{};
				// Production treats a neural EndTurn as "no useful modeled action" and
				// continues through the audited Utility AI fallback.
				if (Action.Kind == EActionKind::EndTurn)
				{
					Action = ChooseScoredAction(State, nullptr, 0.0f, false);
				}
				Metrics.SearchNodes += Search.NodesVisited;
				Metrics.SearchMilliseconds += Search.ElapsedMilliseconds;
				++Metrics.SearchCalls;
				Metrics.BudgetHits += Search.bBudgetExhausted ? 1 : 0;
			}
			else
			{
				Action = ChooseScoredAction(State, nullptr, 0.0f, false);
			}

			const std::uint8_t SideBefore = State.SideToMove;
			if (!FSimulator::ApplyAction(State, Action))
			{
				++Metrics.InvalidActions;
				FAction End;
				End.Kind = EActionKind::EndTurn;
				FSimulator::ApplyAction(State, End);
			}
			const int EnemyHealthAfter = TeamHealth(State, EnemyTeam);
			const int EnemyAliveAfter = FSimulator::AliveCount(State, EnemyTeam);
			Metrics.Damage[ActingTeam] += std::max(0, EnemyHealthBefore - EnemyHealthAfter);
			Metrics.ForcedKills[ActingTeam] += bForcedKill && EnemyAliveAfter < EnemyAliveBefore ? 1 : 0;
			if (State.SideToMove != SideBefore) ++Metrics.HalfTurns;
			++Decisions;
		}

		const bool Team0Alive = FSimulator::HasAliveTeam(State, 0);
		const bool Team1Alive = FSimulator::HasAliveTeam(State, 1);
		if (Team0Alive != Team1Alive) Metrics.Winner = Team0Alive ? 0 : 1;
		return Metrics;
	}

	struct FAggregate
	{
		int Matches = 0;
		int NeuralWins = 0;
		int UtilityWins = 0;
		int Draws = 0;
		int NeuralFirstWins = 0;
		int NeuralSecondWins = 0;
		long long NeuralDamage = 0;
		long long UtilityDamage = 0;
		long long NeuralForced = 0;
		long long NeuralForcedTaken = 0;
		long long UtilityForced = 0;
		long long UtilityForcedTaken = 0;
		long long Nodes = 0;
		double Milliseconds = 0.0;
		long long Calls = 0;
		long long BudgetHits = 0;
		long long InvalidActions = 0;
		long long TotalCells = 0;
		long long TotalUnits = 0;
	};

	void Add(FAggregate& Aggregate, const FMatchMetrics& Match, int NeuralTeam, int Cells, int Units)
	{
		++Aggregate.Matches;
		if (Match.Winner < 0) ++Aggregate.Draws;
		else if (Match.Winner == NeuralTeam)
		{
			++Aggregate.NeuralWins;
			if (NeuralTeam == 0) ++Aggregate.NeuralFirstWins; else ++Aggregate.NeuralSecondWins;
		}
		else ++Aggregate.UtilityWins;
		Aggregate.NeuralDamage += Match.Damage[NeuralTeam];
		Aggregate.UtilityDamage += Match.Damage[1 - NeuralTeam];
		Aggregate.NeuralForced += Match.ForcedOpportunities[NeuralTeam];
		Aggregate.NeuralForcedTaken += Match.ForcedKills[NeuralTeam];
		Aggregate.UtilityForced += Match.ForcedOpportunities[1 - NeuralTeam];
		Aggregate.UtilityForcedTaken += Match.ForcedKills[1 - NeuralTeam];
		Aggregate.Nodes += Match.SearchNodes;
		Aggregate.Milliseconds += Match.SearchMilliseconds;
		Aggregate.Calls += Match.SearchCalls;
		Aggregate.BudgetHits += Match.BudgetHits;
		Aggregate.InvalidActions += Match.InvalidActions;
		Aggregate.TotalCells += Cells;
		Aggregate.TotalUnits += Units;
	}

	void Merge(FAggregate& Target, const FAggregate& Source)
	{
		Target.Matches += Source.Matches;
		Target.NeuralWins += Source.NeuralWins;
		Target.UtilityWins += Source.UtilityWins;
		Target.Draws += Source.Draws;
		Target.NeuralFirstWins += Source.NeuralFirstWins;
		Target.NeuralSecondWins += Source.NeuralSecondWins;
		Target.NeuralDamage += Source.NeuralDamage;
		Target.UtilityDamage += Source.UtilityDamage;
		Target.NeuralForced += Source.NeuralForced;
		Target.NeuralForcedTaken += Source.NeuralForcedTaken;
		Target.UtilityForced += Source.UtilityForced;
		Target.UtilityForcedTaken += Source.UtilityForcedTaken;
		Target.Nodes += Source.Nodes;
		Target.Milliseconds += Source.Milliseconds;
		Target.Calls += Source.Calls;
		Target.BudgetHits += Source.BudgetHits;
		Target.InvalidActions += Source.InvalidActions;
		Target.TotalCells += Source.TotalCells;
		Target.TotalUnits += Source.TotalUnits;
	}

	double Percent(long long Part, long long Total)
	{
		return Total > 0 ? static_cast<double>(Part) * 100.0 / static_cast<double>(Total) : 0.0;
	}

	FAggregate Benchmark(
		int Pairs,
		std::uint32_t Seed,
		const FSearchSettings& Settings,
		float GreedyHealthMultiplier,
		float GreedyDamageMultiplier,
		int WorkerCount)
	{
		FAggregate Aggregate;
		const int SafeWorkerCount = std::max(1, std::min(WorkerCount, Pairs));
		for (int BatchStart = 0; BatchStart < Pairs; BatchStart += SafeWorkerCount)
		{
			const int BatchEnd = std::min(Pairs, BatchStart + SafeWorkerCount);
			std::vector<std::future<FAggregate>> Futures;
			for (int Pair = BatchStart; Pair < BatchEnd; ++Pair)
			{
				Futures.emplace_back(std::async(std::launch::async, [=]()
				{
					FAggregate PairAggregate;
					std::mt19937 Random(Seed + static_cast<std::uint32_t>(Pair) * SeedStride);
					FTopology Topology = MakeTopology(Random, StructuralHoldoutDistribution);
					FState BaseState = MakeScenario(Topology, Random, StructuralHoldoutDistribution);
					BaseState.SideToMove = 0;
					BaseState.ActionPoints = BaseState.MaxActionPoints;
					FState NeuralFirst = BaseState;
					ApplyGreedyHandicap(NeuralFirst, 1, GreedyHealthMultiplier, GreedyDamageMultiplier);
					Add(PairAggregate, RunMatch(NeuralFirst, 0, Settings), 0,
						static_cast<int>(Topology.Cells.size()), static_cast<int>(BaseState.Units.size()));
					FState NeuralSecond = BaseState;
					ApplyGreedyHandicap(NeuralSecond, 0, GreedyHealthMultiplier, GreedyDamageMultiplier);
					Add(PairAggregate, RunMatch(NeuralSecond, 1, Settings), 1,
						static_cast<int>(Topology.Cells.size()), static_cast<int>(BaseState.Units.size()));
					return PairAggregate;
				}));
			}
			for (auto& Future : Futures) Merge(Aggregate, Future.get());
		}
		return Aggregate;
	}

	void Print(
		const FAggregate& Aggregate,
		std::uint32_t Seed,
		float GreedyHealthMultiplier,
		float GreedyDamageMultiplier)
	{
		std::cout << std::fixed << std::setprecision(2);
		std::cout << "Neural search benchmark (unseen structural distribution, optional greedy handicap)\n";
		std::cout << "seed=" << Seed << " matches=" << Aggregate.Matches
			<< " avg_cells=" << static_cast<double>(Aggregate.TotalCells) / std::max(1, Aggregate.Matches)
			<< " avg_units=" << static_cast<double>(Aggregate.TotalUnits) / std::max(1, Aggregate.Matches)
			<< " greedy_hp_x=" << GreedyHealthMultiplier << " greedy_damage_x=" << GreedyDamageMultiplier << '\n';
		std::cout << "neural_win_rate=" << Percent(Aggregate.NeuralWins, Aggregate.Matches)
			<< "% utility_win_rate=" << Percent(Aggregate.UtilityWins, Aggregate.Matches)
			<< "% draws=" << Percent(Aggregate.Draws, Aggregate.Matches) << "%\n";
		std::cout << "side_check neural_as_first=" << Percent(Aggregate.NeuralFirstWins, Aggregate.Matches / 2)
			<< "% neural_as_second=" << Percent(Aggregate.NeuralSecondWins, Aggregate.Matches / 2) << "%\n";
		std::cout << "avg_damage neural=" << static_cast<double>(Aggregate.NeuralDamage) / Aggregate.Matches
			<< " utility=" << static_cast<double>(Aggregate.UtilityDamage) / Aggregate.Matches << '\n';
		std::cout << "forced_kill_conversion neural=" << Percent(Aggregate.NeuralForcedTaken, Aggregate.NeuralForced)
			<< "% utility=" << Percent(Aggregate.UtilityForcedTaken, Aggregate.UtilityForced) << "%\n";
		std::cout << "search avg_nodes=" << static_cast<double>(Aggregate.Nodes) / std::max<long long>(1, Aggregate.Calls)
			<< " avg_ms=" << Aggregate.Milliseconds / std::max<long long>(1, Aggregate.Calls)
			<< " budget_hit_rate=" << Percent(Aggregate.BudgetHits, Aggregate.Calls)
			<< "% invalid_actions=" << Aggregate.InvalidActions << "\n";
	}

	void WriteJson(
		const std::string& Path,
		const FAggregate& Aggregate,
		std::uint32_t Seed,
		const FSearchSettings& Settings,
		float GreedyHealthMultiplier,
		float GreedyDamageMultiplier)
	{
		std::ofstream Out(Path, std::ios::trunc);
		Out << std::fixed << std::setprecision(4)
			<< "{\n"
			<< "  \"evaluation_distribution\": \"structural_holdout\",\n"
			<< "  \"seed\": " << Seed << ",\n"
			<< "  \"matches\": " << Aggregate.Matches << ",\n"
			<< "  \"map_span_range\": [" << StructuralHoldoutDistribution.MinSpan << ", " << StructuralHoldoutDistribution.MaxSpan << "],\n"
			<< "  \"units_per_team_range\": [" << StructuralHoldoutDistribution.MinUnitsPerTeam << ", " << StructuralHoldoutDistribution.MaxUnitsPerTeam << "],\n"
			<< "  \"greedy_health_multiplier\": " << GreedyHealthMultiplier << ",\n"
			<< "  \"greedy_damage_multiplier\": " << GreedyDamageMultiplier << ",\n"
			<< "  \"depth\": " << Settings.Depth << ",\n"
			<< "  \"top_k\": " << Settings.TopK << ",\n"
			<< "  \"node_budget\": " << Settings.NodeBudget << ",\n"
			<< "  \"time_budget_ms\": " << Settings.TimeBudgetMilliseconds << ",\n"
			<< "  \"neural_policy_blend\": " << Settings.NeuralPolicyBlend << ",\n"
			<< "  \"neural_value_blend\": " << Settings.NeuralValueBlend << ",\n"
			<< "  \"parameters\": " << FPolicyValueNetwork::ParameterCount() << ",\n"
			<< "  \"average_cells\": " << static_cast<double>(Aggregate.TotalCells) / std::max(1, Aggregate.Matches) << ",\n"
			<< "  \"average_units\": " << static_cast<double>(Aggregate.TotalUnits) / std::max(1, Aggregate.Matches) << ",\n"
			<< "  \"neural_win_rate\": " << Percent(Aggregate.NeuralWins, Aggregate.Matches) << ",\n"
			<< "  \"utility_win_rate\": " << Percent(Aggregate.UtilityWins, Aggregate.Matches) << ",\n"
			<< "  \"draw_rate\": " << Percent(Aggregate.Draws, Aggregate.Matches) << ",\n"
			<< "  \"neural_win_rate_as_first\": " << Percent(Aggregate.NeuralFirstWins, Aggregate.Matches / 2) << ",\n"
			<< "  \"neural_win_rate_as_second\": " << Percent(Aggregate.NeuralSecondWins, Aggregate.Matches / 2) << ",\n"
			<< "  \"neural_average_damage\": " << static_cast<double>(Aggregate.NeuralDamage) / std::max(1, Aggregate.Matches) << ",\n"
			<< "  \"utility_average_damage\": " << static_cast<double>(Aggregate.UtilityDamage) / std::max(1, Aggregate.Matches) << ",\n"
			<< "  \"neural_forced_kill_conversion\": " << Percent(Aggregate.NeuralForcedTaken, Aggregate.NeuralForced) << ",\n"
			<< "  \"utility_forced_kill_conversion\": " << Percent(Aggregate.UtilityForcedTaken, Aggregate.UtilityForced) << ",\n"
			<< "  \"average_search_nodes\": " << static_cast<double>(Aggregate.Nodes) / std::max<long long>(1, Aggregate.Calls) << ",\n"
			<< "  \"average_search_ms\": " << Aggregate.Milliseconds / std::max<long long>(1, Aggregate.Calls) << ",\n"
			<< "  \"budget_hit_rate\": " << Percent(Aggregate.BudgetHits, Aggregate.Calls) << ",\n"
			<< "  \"invalid_actions\": " << Aggregate.InvalidActions << "\n"
			<< "}\n";
	}
}

int main(int ArgCount, char** Arguments)
{
	int Pairs = 200;
	std::uint32_t Seed = 0x5eed1234u;
	std::string JsonPath;
	std::string DataPath;
	bool bTestsOnly = false;
	bool bNoGate = false;
	int TrainPositions = 1200;
	int HoldoutPositions = 300;
	int Rollouts = 2;
	int CandidateCap = 10;
	int WorkerCount = 6;
	float GreedyHealthMultiplier = 1.0f;
	float GreedyDamageMultiplier = 1.0f;
	FSearchSettings Settings;
	Settings.Depth = 5;
	Settings.TopK = 8;
	Settings.HeuristicSafetyCandidates = 2;
	Settings.NodeBudget = 600;
	Settings.TimeBudgetMilliseconds = 5.0;

	for (int Index = 1; Index < ArgCount; ++Index)
	{
		const std::string Argument = Arguments[Index];
		if (Argument == "--test") bTestsOnly = true;
		else if (Argument == "--no-gate") bNoGate = true;
		else if (Argument == "--pairs" && Index + 1 < ArgCount) Pairs = std::max(1, std::stoi(Arguments[++Index]));
		else if (Argument == "--seed" && Index + 1 < ArgCount) Seed = static_cast<std::uint32_t>(std::stoul(Arguments[++Index]));
		else if (Argument == "--json" && Index + 1 < ArgCount) JsonPath = Arguments[++Index];
		else if (Argument == "--nodes" && Index + 1 < ArgCount) Settings.NodeBudget = std::max(1, std::stoi(Arguments[++Index]));
		else if (Argument == "--milliseconds" && Index + 1 < ArgCount) Settings.TimeBudgetMilliseconds = std::max(0.0, std::stod(Arguments[++Index]));
		else if (Argument == "--generate-data" && Index + 1 < ArgCount) DataPath = Arguments[++Index];
		else if (Argument == "--train-positions" && Index + 1 < ArgCount) TrainPositions = std::max(1, std::stoi(Arguments[++Index]));
		else if (Argument == "--holdout-positions" && Index + 1 < ArgCount) HoldoutPositions = std::max(1, std::stoi(Arguments[++Index]));
		else if (Argument == "--rollouts" && Index + 1 < ArgCount) Rollouts = std::max(1, std::stoi(Arguments[++Index]));
		else if (Argument == "--candidate-cap" && Index + 1 < ArgCount) CandidateCap = std::max(2, std::stoi(Arguments[++Index]));
		else if (Argument == "--greedy-health" && Index + 1 < ArgCount) GreedyHealthMultiplier = std::max(1.0f, std::stof(Arguments[++Index]));
		else if (Argument == "--greedy-damage" && Index + 1 < ArgCount) GreedyDamageMultiplier = std::max(1.0f, std::stof(Arguments[++Index]));
		else if (Argument == "--workers" && Index + 1 < ArgCount) WorkerCount = std::max(1, std::stoi(Arguments[++Index]));
		else if (Argument == "--policy-blend" && Index + 1 < ArgCount) Settings.NeuralPolicyBlend = std::max(0.0f, std::min(1.0f, std::stof(Arguments[++Index])));
		else if (Argument == "--value-blend" && Index + 1 < ArgCount) Settings.NeuralValueBlend = std::max(0.0f, std::min(1.0f, std::stof(Arguments[++Index])));
	}

	if (!RunUnitTests()) return 1;
	if (bTestsOnly) return 0;
	if (!DataPath.empty())
	{
		GenerateDataset(DataPath, TrainPositions, HoldoutPositions, Rollouts, CandidateCap,
			GreedyHealthMultiplier, GreedyDamageMultiplier, WorkerCount, Seed);
		return 0;
	}

	const FAggregate Metrics = Benchmark(Pairs, Seed, Settings, GreedyHealthMultiplier, GreedyDamageMultiplier, WorkerCount);
	Print(Metrics, Seed, GreedyHealthMultiplier, GreedyDamageMultiplier);
	if (!JsonPath.empty()) WriteJson(JsonPath, Metrics, Seed, Settings, GreedyHealthMultiplier, GreedyDamageMultiplier);
	const bool bPass = Metrics.InvalidActions == 0 &&
		Percent(Metrics.NeuralForcedTaken, Metrics.NeuralForced) >= 99.999 &&
		Percent(Metrics.NeuralWins, Metrics.Matches) + 1.0e-6 >= Percent(Metrics.UtilityWins, Metrics.Matches);
	if (!bPass && !bNoGate)
	{
		std::cerr << "FAILED: neural search regression gates were not met.\n";
		return 2;
	}
	return 0;
}
