#include "HexBotDecisionModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace
{
	using OtherBios::BotAI::EActionKind;
	using OtherBios::BotAI::EDifficulty;
	using OtherBios::BotAI::FDecisionFeatures;
	using OtherBios::BotAI::FDecisionModel;
	using OtherBios::BotAI::FDecisionOption;

	struct FHex
	{
		int Q = 0;
		int R = 0;
	};

	int HexDistance(const FHex& A, const FHex& B)
	{
		const int DQ = A.Q - B.Q;
		const int DR = A.R - B.R;
		return (std::abs(DQ) + std::abs(DR) + std::abs(DQ + DR)) / 2;
	}

	bool IsOnBoard(const FHex& Cell)
	{
		const int S = -Cell.Q - Cell.R;
		return std::max({ std::abs(Cell.Q), std::abs(Cell.R), std::abs(S) }) <= 6;
	}

	enum class ERole : std::uint8_t
	{
		Vanguard,
		Archer,
		Healer,
		Champion,
		Skirmisher
	};

	struct FUnit
	{
		int Id = 0;
		int Team = 0;
		ERole Role = ERole::Vanguard;
		int Health = 1;
		int MaxHealth = 1;
		int Damage = 1;
		int Range = 1;
		int Movement = 2;
		int Heal = 0;
		FHex Cell;
		bool bMoved = false;
		bool bAttacked = false;
		bool bHealed = false;

		bool IsAlive() const { return Health > 0; }
	};

	float RoleValue(ERole Role)
	{
		switch (Role)
		{
		case ERole::Champion: return 1.0f;
		case ERole::Healer: return 0.9f;
		case ERole::Archer: return 0.78f;
		case ERole::Skirmisher: return 0.64f;
		case ERole::Vanguard:
		default: return 0.58f;
		}
	}

	struct FAction
	{
		EActionKind Kind = EActionKind::Move;
		int Actor = -1;
		int Target = -1;
		FHex Destination;
		FDecisionFeatures Features;
		FDecisionOption Option;
	};

	struct FBattleMetrics
	{
		int Winner = -1;
		int HalfTurns = 0;
		int DamageByTeam[2] = { 0, 0 };
		int KillsByTeam[2] = { 0, 0 };
		int HealedByTeam[2] = { 0, 0 };
		int MoveKillSetupsByTeam[2] = { 0, 0 };
		int ForcedKillOpportunitiesByTeam[2] = { 0, 0 };
		int ForcedKillsTakenByTeam[2] = { 0, 0 };
		int WastedActionsByTeam[2] = { 0, 0 };
		int RemainingHealth[2] = { 0, 0 };
	};

	struct FScenario
	{
		std::vector<FUnit> Units;
	};

	FUnit MakeUnit(int Id, int Team, ERole Role, const FHex& Cell, std::mt19937& Random)
	{
		FUnit Unit;
		Unit.Id = Id;
		Unit.Team = Team;
		Unit.Role = Role;
		Unit.Cell = Cell;

		switch (Role)
		{
		case ERole::Vanguard:
			Unit.MaxHealth = 145; Unit.Damage = 29; Unit.Range = 1; Unit.Movement = 2;
			break;
		case ERole::Archer:
			Unit.MaxHealth = 82; Unit.Damage = 34; Unit.Range = 3; Unit.Movement = 2;
			break;
		case ERole::Healer:
			Unit.MaxHealth = 88; Unit.Damage = 17; Unit.Range = 2; Unit.Movement = 2; Unit.Heal = 38;
			break;
		case ERole::Champion:
			Unit.MaxHealth = 122; Unit.Damage = 39; Unit.Range = 1; Unit.Movement = 3;
			break;
		case ERole::Skirmisher:
			Unit.MaxHealth = 96; Unit.Damage = 31; Unit.Range = 1; Unit.Movement = 4;
			break;
		}

		std::uniform_int_distribution<int> StatJitter(-6, 6);
		Unit.MaxHealth += StatJitter(Random);
		Unit.Damage += StatJitter(Random) / 2;
		Unit.Health = Unit.MaxHealth;
		return Unit;
	}

	FScenario MakeScenario(std::uint32_t Seed)
	{
		std::mt19937 Random(Seed);
		FScenario Scenario;
		const std::array<ERole, 5> Roles = {
			ERole::Vanguard, ERole::Archer, ERole::Healer, ERole::Champion, ERole::Skirmisher
		};
		const std::array<FHex, 5> Left = { FHex{-4, 0}, FHex{-4, 1}, FHex{-4, -1}, FHex{-5, 1}, FHex{-5, 0} };
		const std::array<FHex, 5> Right = { FHex{4, 0}, FHex{4, -1}, FHex{4, 1}, FHex{5, -1}, FHex{5, 0} };

		for (int Team = 0; Team < 2; ++Team)
		{
			for (int Index = 0; Index < static_cast<int>(Roles.size()); ++Index)
			{
				Scenario.Units.push_back(MakeUnit(
					Team * 5 + Index,
					Team,
					Roles[static_cast<std::size_t>(Index)],
					Team == 0 ? Left[static_cast<std::size_t>(Index)] : Right[static_cast<std::size_t>(Index)],
					Random
				));
			}
		}

		return Scenario;
	}

	class FBattle
	{
	public:
		FBattle(FScenario Scenario, int ImprovedTeam, std::uint32_t Seed)
			: Units(std::move(Scenario.Units)), ImprovedSide(ImprovedTeam), Random(Seed)
		{
		}

		FBattleMetrics Run()
		{
			constexpr int MaxHalfTurns = 100;
			for (int HalfTurn = 0; HalfTurn < MaxHalfTurns; ++HalfTurn)
			{
				const int Team = HalfTurn % 2;
				if (!HasAliveUnits(0) || !HasAliveUnits(1))
				{
					Metrics.HalfTurns = HalfTurn;
					break;
				}

				RunTeamTurn(Team, Team == ImprovedSide);
				Metrics.HalfTurns = HalfTurn + 1;
			}

			const bool Team0Alive = HasAliveUnits(0);
			const bool Team1Alive = HasAliveUnits(1);
			if (Team0Alive != Team1Alive)
			{
				Metrics.Winner = Team0Alive ? 0 : 1;
			}
			else
			{
				const int Health0 = TeamHealth(0);
				const int Health1 = TeamHealth(1);
				Metrics.Winner = Health0 == Health1 ? -1 : (Health0 > Health1 ? 0 : 1);
			}

			Metrics.RemainingHealth[0] = TeamHealth(0);
			Metrics.RemainingHealth[1] = TeamHealth(1);
			return Metrics;
		}

	private:
		static constexpr int ActionPointsPerTurn = 6;
		std::vector<FUnit> Units;
		int ImprovedSide = 0;
		std::mt19937 Random;
		FBattleMetrics Metrics;

		bool HasAliveUnits(int Team) const
		{
			return std::any_of(Units.begin(), Units.end(), [Team](const FUnit& Unit)
			{
				return Unit.Team == Team && Unit.IsAlive();
			});
		}

		int TeamHealth(int Team) const
		{
			int Result = 0;
			for (const FUnit& Unit : Units)
			{
				if (Unit.Team == Team && Unit.IsAlive())
				{
					Result += Unit.Health;
				}
			}
			return Result;
		}

		int AliveCount(int Team) const
		{
			int Result = 0;
			for (const FUnit& Unit : Units)
			{
				Result += Unit.Team == Team && Unit.IsAlive() ? 1 : 0;
			}
			return Result;
		}

		bool IsOccupied(const FHex& Cell, int IgnoreUnit = -1) const
		{
			for (std::size_t Index = 0; Index < Units.size(); ++Index)
			{
				if (static_cast<int>(Index) != IgnoreUnit && Units[Index].IsAlive() &&
					Units[Index].Cell.Q == Cell.Q && Units[Index].Cell.R == Cell.R)
				{
					return true;
				}
			}
			return false;
		}

		int NearestEnemyDistance(int UnitIndex, const FHex& From) const
		{
			int Best = std::numeric_limits<int>::max();
			const int Team = Units[static_cast<std::size_t>(UnitIndex)].Team;
			for (const FUnit& Other : Units)
			{
				if (Other.IsAlive() && Other.Team != Team)
				{
					Best = std::min(Best, HexDistance(From, Other.Cell));
				}
			}
			return Best;
		}

		int NearbyAllies(int UnitIndex, const FHex& From, int Radius) const
		{
			int Count = 0;
			const int Team = Units[static_cast<std::size_t>(UnitIndex)].Team;
			for (std::size_t Index = 0; Index < Units.size(); ++Index)
			{
				if (static_cast<int>(Index) != UnitIndex && Units[Index].IsAlive() && Units[Index].Team == Team &&
					HexDistance(From, Units[Index].Cell) <= Radius)
				{
					++Count;
				}
			}
			return Count;
		}

		int ProjectedIncomingDamage(int UnitIndex, const FHex& At) const
		{
			const FUnit& Unit = Units[static_cast<std::size_t>(UnitIndex)];
			int Damage = 0;
			for (const FUnit& Enemy : Units)
			{
				if (!Enemy.IsAlive() || Enemy.Team == Unit.Team)
				{
					continue;
				}
				if (HexDistance(At, Enemy.Cell) <= Enemy.Range + Enemy.Movement)
				{
					Damage += Enemy.Damage;
				}
			}
			return Damage;
		}

		int CurrentAttackersForTarget(int Team, int TargetIndex) const
		{
			int Count = 0;
			for (const FUnit& Unit : Units)
			{
				if (Unit.IsAlive() && Unit.Team == Team && !Unit.bAttacked &&
					HexDistance(Unit.Cell, Units[static_cast<std::size_t>(TargetIndex)].Cell) <= Unit.Range)
				{
					++Count;
				}
			}
			return Count;
		}

		int FindKillTargetFromCell(int UnitIndex, const FHex& Cell) const
		{
			const FUnit& Unit = Units[static_cast<std::size_t>(UnitIndex)];
			int BestTarget = -1;
			float BestValue = -1.0f;
			for (std::size_t Index = 0; Index < Units.size(); ++Index)
			{
				const FUnit& Target = Units[Index];
				if (!Target.IsAlive() || Target.Team == Unit.Team || HexDistance(Cell, Target.Cell) > Unit.Range ||
					Unit.Damage < Target.Health)
				{
					continue;
				}
				const float Value = RoleValue(Target.Role) + static_cast<float>(Target.MaxHealth - Target.Health) / Target.MaxHealth;
				if (Value > BestValue)
				{
					BestValue = Value;
					BestTarget = static_cast<int>(Index);
				}
			}
			return BestTarget;
		}

		void AddAttackActions(int Team, std::vector<FAction>& Actions) const
		{
			for (std::size_t ActorIndex = 0; ActorIndex < Units.size(); ++ActorIndex)
			{
				const FUnit& Actor = Units[ActorIndex];
				if (!Actor.IsAlive() || Actor.Team != Team || Actor.bAttacked)
				{
					continue;
				}

				for (std::size_t TargetIndex = 0; TargetIndex < Units.size(); ++TargetIndex)
				{
					const FUnit& Target = Units[TargetIndex];
					if (!Target.IsAlive() || Target.Team == Team || HexDistance(Actor.Cell, Target.Cell) > Actor.Range)
					{
						continue;
					}

					FAction Action;
					Action.Kind = EActionKind::Attack;
					Action.Actor = static_cast<int>(ActorIndex);
					Action.Target = static_cast<int>(TargetIndex);
					Action.Features.Action = EActionKind::Attack;
					Action.Features.BaseUtility = RoleValue(Target.Role) +
						static_cast<float>(Target.MaxHealth - Target.Health) / Target.MaxHealth;
					Action.Features.ImmediateGain = static_cast<float>(std::min(Actor.Damage, Target.Health)) / Target.Health;
					Action.Features.TargetValue = RoleValue(Target.Role);
					Action.Features.PlanAlignment = Target.Role == ERole::Champion || Target.Role == ERole::Healer ? 1.0f : 0.55f;
					Action.Features.FutureGain = std::min(1.0f, CurrentAttackersForTarget(Team, static_cast<int>(TargetIndex)) / 3.0f);
					Action.Features.Exposure = static_cast<float>(ProjectedIncomingDamage(static_cast<int>(ActorIndex), Actor.Cell)) / Actor.Health;
					Action.Features.ResourceCost = 1.0f / ActionPointsPerTurn;
					Action.Features.Overcommit = std::max(0, CurrentAttackersForTarget(Team, static_cast<int>(TargetIndex)) - 2) / 3.0f;
					Action.Features.bSecuresKill = Actor.Damage >= Target.Health;
					Action.Features.bTerminalOutcome = Action.Features.bSecuresKill && AliveCount(1 - Team) == 1;
					Action.Option.Utility = FDecisionModel::Evaluate(Action.Features, EDifficulty::Ordeal);
					Action.Option.bSecuresKill = Action.Features.bSecuresKill;
					Action.Option.bTerminalOutcome = Action.Features.bTerminalOutcome;
					Action.Option.StableKey = static_cast<std::uint32_t>(Actor.Id * 31 + Target.Id);
					Actions.push_back(Action);
				}
			}
		}

		void AddHealActions(int Team, std::vector<FAction>& Actions) const
		{
			for (std::size_t ActorIndex = 0; ActorIndex < Units.size(); ++ActorIndex)
			{
				const FUnit& Actor = Units[ActorIndex];
				if (!Actor.IsAlive() || Actor.Team != Team || Actor.Heal <= 0 || Actor.bHealed)
				{
					continue;
				}

				for (std::size_t TargetIndex = 0; TargetIndex < Units.size(); ++TargetIndex)
				{
					const FUnit& Target = Units[TargetIndex];
					if (!Target.IsAlive() || Target.Team != Team || Target.Health >= Target.MaxHealth ||
						HexDistance(Actor.Cell, Target.Cell) > 2)
					{
						continue;
					}

					const int EffectiveHeal = std::min(Actor.Heal, Target.MaxHealth - Target.Health);
					const int Incoming = ProjectedIncomingDamage(static_cast<int>(TargetIndex), Target.Cell);
					FAction Action;
					Action.Kind = EActionKind::Heal;
					Action.Actor = static_cast<int>(ActorIndex);
					Action.Target = static_cast<int>(TargetIndex);
					Action.Features.Action = EActionKind::Heal;
					Action.Features.BaseUtility = RoleValue(Target.Role) + static_cast<float>(EffectiveHeal) / Target.MaxHealth;
					Action.Features.ImmediateGain = static_cast<float>(EffectiveHeal) / Target.MaxHealth;
					Action.Features.TargetValue = RoleValue(Target.Role);
					Action.Features.FutureGain = Incoming > 0 ? std::min(1.0f, static_cast<float>(EffectiveHeal) / Incoming) : 0.25f;
					Action.Features.Exposure = static_cast<float>(ProjectedIncomingDamage(static_cast<int>(ActorIndex), Actor.Cell)) / Actor.Health;
					Action.Features.ResourceCost = 1.0f / ActionPointsPerTurn;
					Action.Features.bPreventsDeath = Incoming >= Target.Health && Incoming < Target.Health + EffectiveHeal;
					Action.Option.Utility = FDecisionModel::Evaluate(Action.Features, EDifficulty::Ordeal);
					Action.Option.bPreventsDeath = Action.Features.bPreventsDeath;
					Action.Option.StableKey = static_cast<std::uint32_t>(Actor.Id * 31 + Target.Id);
					Actions.push_back(Action);
				}
			}
		}

		void AddMoveActions(int Team, int ActionPoints, std::vector<FAction>& Actions) const
		{
			for (std::size_t ActorIndex = 0; ActorIndex < Units.size(); ++ActorIndex)
			{
				const FUnit& Actor = Units[ActorIndex];
				if (!Actor.IsAlive() || Actor.Team != Team || Actor.bMoved)
				{
					continue;
				}

				const int CurrentDistance = NearestEnemyDistance(static_cast<int>(ActorIndex), Actor.Cell);
				for (int DQ = -Actor.Movement; DQ <= Actor.Movement; ++DQ)
				{
					for (int DR = -Actor.Movement; DR <= Actor.Movement; ++DR)
					{
						const FHex Destination{ Actor.Cell.Q + DQ, Actor.Cell.R + DR };
						const int MoveDistance = HexDistance(Actor.Cell, Destination);
						if (MoveDistance <= 0 || MoveDistance > Actor.Movement || !IsOnBoard(Destination) ||
							IsOccupied(Destination, static_cast<int>(ActorIndex)))
						{
							continue;
						}

						const int CandidateDistance = NearestEnemyDistance(static_cast<int>(ActorIndex), Destination);
						const int KillTarget = !Actor.bAttacked && ActionPoints >= 2
							? FindKillTargetFromCell(static_cast<int>(ActorIndex), Destination)
							: -1;
						int BestTarget = KillTarget;
						if (BestTarget < 0)
						{
							float BestTargetValue = -1.0f;
							for (std::size_t TargetIndex = 0; TargetIndex < Units.size(); ++TargetIndex)
							{
								const FUnit& Target = Units[TargetIndex];
								if (Target.IsAlive() && Target.Team != Team && HexDistance(Destination, Target.Cell) <= Actor.Range)
								{
									const float Value = RoleValue(Target.Role);
									if (Value > BestTargetValue)
									{
										BestTargetValue = Value;
										BestTarget = static_cast<int>(TargetIndex);
									}
								}
							}
						}

						const int Incoming = ProjectedIncomingDamage(static_cast<int>(ActorIndex), Destination);
						const int Progress = std::max(0, CurrentDistance - CandidateDistance);
						FAction Action;
						Action.Kind = EActionKind::Move;
						Action.Actor = static_cast<int>(ActorIndex);
						Action.Target = BestTarget;
						Action.Destination = Destination;
						Action.Features.Action = EActionKind::Move;
						Action.Features.BaseUtility = Progress * 0.35f + (BestTarget >= 0 ? 0.8f : 0.0f) - MoveDistance * 0.08f;
						Action.Features.ImmediateGain = BestTarget >= 0
							? static_cast<float>(std::min(Actor.Damage, Units[static_cast<std::size_t>(BestTarget)].Health)) /
								Units[static_cast<std::size_t>(BestTarget)].Health
							: 0.0f;
						Action.Features.TargetValue = BestTarget >= 0 ? RoleValue(Units[static_cast<std::size_t>(BestTarget)].Role) : 0.0f;
						Action.Features.PlanAlignment = CandidateDistance <= CurrentDistance ? 1.0f : 0.0f;
						Action.Features.FutureGain = BestTarget >= 0 ? 1.0f : static_cast<float>(Progress) / std::max(1, Actor.Movement);
						Action.Features.PositionProgress = static_cast<float>(Progress) / std::max(1, Actor.Movement);
						Action.Features.Cohesion = std::min(1.0f, NearbyAllies(static_cast<int>(ActorIndex), Destination, 2) / 2.0f);
						Action.Features.Exposure = static_cast<float>(Incoming) / Actor.Health;
						Action.Features.ResourceCost = static_cast<float>(MoveDistance) / std::max(1, Actor.Movement);
						Action.Features.Overcommit = std::max(0, NearbyAllies(static_cast<int>(ActorIndex), Destination, 1) - 2) / 3.0f;
						Action.Features.bSecuresKill = KillTarget >= 0;
						Action.Features.bTerminalOutcome = KillTarget >= 0 && AliveCount(1 - Team) == 1;
						Action.Option.Utility = FDecisionModel::Evaluate(Action.Features, EDifficulty::Ordeal);
						Action.Option.bSecuresKill = Action.Features.bSecuresKill;
						Action.Option.bTerminalOutcome = Action.Features.bTerminalOutcome;
						Action.Option.StableKey = static_cast<std::uint32_t>((Actor.Id + 1) * 65537 + (Destination.Q + 6) * 17 + Destination.R + 6);
						Actions.push_back(Action);
					}
				}
			}
		}

		bool HasForcedKill(int Team, int ActionPoints) const
		{
			if (ActionPoints <= 0)
			{
				return false;
			}
			for (std::size_t ActorIndex = 0; ActorIndex < Units.size(); ++ActorIndex)
			{
				const FUnit& Actor = Units[ActorIndex];
				if (!Actor.IsAlive() || Actor.Team != Team || Actor.bAttacked)
				{
					continue;
				}
				if (FindKillTargetFromCell(static_cast<int>(ActorIndex), Actor.Cell) >= 0)
				{
					return true;
				}
				if (ActionPoints >= 2 && !Actor.bMoved)
				{
					for (int DQ = -Actor.Movement; DQ <= Actor.Movement; ++DQ)
					{
						for (int DR = -Actor.Movement; DR <= Actor.Movement; ++DR)
						{
							const FHex Destination{ Actor.Cell.Q + DQ, Actor.Cell.R + DR };
							if (HexDistance(Actor.Cell, Destination) > 0 && HexDistance(Actor.Cell, Destination) <= Actor.Movement &&
								IsOnBoard(Destination) && !IsOccupied(Destination, static_cast<int>(ActorIndex)) &&
								FindKillTargetFromCell(static_cast<int>(ActorIndex), Destination) >= 0)
							{
								return true;
							}
						}
					}
				}
			}
			return false;
		}

		bool ExecuteAction(const FAction& Action, int Team)
		{
			if (Action.Actor < 0 || Action.Actor >= static_cast<int>(Units.size()))
			{
				return false;
			}
			FUnit& Actor = Units[static_cast<std::size_t>(Action.Actor)];
			if (!Actor.IsAlive() || Actor.Team != Team)
			{
				return false;
			}

			switch (Action.Kind)
			{
			case EActionKind::Attack:
			{
				if (Actor.bAttacked || Action.Target < 0 || Action.Target >= static_cast<int>(Units.size())) return false;
				FUnit& Target = Units[static_cast<std::size_t>(Action.Target)];
				if (!Target.IsAlive() || Target.Team == Team || HexDistance(Actor.Cell, Target.Cell) > Actor.Range) return false;
				const int Damage = std::min(Actor.Damage, Target.Health);
				const bool bKilled = Actor.Damage >= Target.Health;
				Target.Health -= Actor.Damage;
				Actor.bAttacked = true;
				Metrics.DamageByTeam[Team] += Damage;
				Metrics.KillsByTeam[Team] += bKilled ? 1 : 0;
				return true;
			}
			case EActionKind::Heal:
			{
				if (Actor.bHealed || Actor.Heal <= 0 || Action.Target < 0 || Action.Target >= static_cast<int>(Units.size())) return false;
				FUnit& Target = Units[static_cast<std::size_t>(Action.Target)];
				if (!Target.IsAlive() || Target.Team != Team || HexDistance(Actor.Cell, Target.Cell) > 2) return false;
				const int Amount = std::min(Actor.Heal, Target.MaxHealth - Target.Health);
				if (Amount <= 0) return false;
				Target.Health += Amount;
				Actor.bHealed = true;
				Metrics.HealedByTeam[Team] += Amount;
				return true;
			}
			case EActionKind::Move:
				if (Actor.bMoved || !IsOnBoard(Action.Destination) || IsOccupied(Action.Destination, Action.Actor) ||
					HexDistance(Actor.Cell, Action.Destination) > Actor.Movement) return false;
				if (Action.Features.bSecuresKill) Metrics.MoveKillSetupsByTeam[Team]++;
				Actor.Cell = Action.Destination;
				Actor.bMoved = true;
				return true;
			case EActionKind::Ability:
			default:
				return false;
			}
		}

		bool ExecuteImprovedAction(int Team, int ActionPoints)
		{
			std::vector<FAction> Actions;
			Actions.reserve(256);
			AddAttackActions(Team, Actions);
			AddHealActions(Team, Actions);
			AddMoveActions(Team, ActionPoints, Actions);
			if (Actions.empty())
			{
				return false;
			}

			std::uniform_real_distribution<float> Uniform(0.0f, 1.0f);
			auto SelectAndExecute = [this, Team, &Actions, &Uniform](auto Predicate) -> bool
			{
				std::vector<std::size_t> ActionIndices;
				std::vector<FDecisionOption> Options;
				for (std::size_t Index = 0; Index < Actions.size(); ++Index)
				{
					if (Predicate(Actions[Index]))
					{
						ActionIndices.push_back(Index);
						Options.push_back(Actions[Index].Option);
					}
				}
				if (Options.empty())
				{
					return false;
				}
				const std::size_t Selected = FDecisionModel::SelectIndex(
					Options.data(), Options.size(), EDifficulty::Ordeal, Uniform(Random)
				);
				return Selected < ActionIndices.size() && ExecuteAction(Actions[ActionIndices[Selected]], Team);
			};

			// Mirrors the production planner: the model ranks candidates inside a
			// tactically safe action ladder instead of comparing unrelated raw scores.
			if (SelectAndExecute([](const FAction& Action) { return Action.Kind == EActionKind::Attack && Action.Features.bSecuresKill; })) return true;
			if (SelectAndExecute([](const FAction& Action) { return Action.Kind == EActionKind::Heal && Action.Features.bPreventsDeath; })) return true;
			if (SelectAndExecute([](const FAction& Action) { return Action.Kind == EActionKind::Move && Action.Features.bSecuresKill; })) return true;
			if (SelectAndExecute([](const FAction& Action) { return Action.Kind == EActionKind::Attack; })) return true;
			if (SelectAndExecute([](const FAction& Action) { return Action.Kind == EActionKind::Heal; })) return true;
			return SelectAndExecute([](const FAction& Action) { return Action.Kind == EActionKind::Move; });
		}

		bool ExecuteLegacyAction(int Team, int ActionPoints)
		{
			// Approximation of the old fixed action ladder: any current attack is spent
			// before considering a move->kill sequence.
			for (std::size_t ActorIndex = 0; ActorIndex < Units.size(); ++ActorIndex)
			{
				const FUnit& Actor = Units[ActorIndex];
				if (!Actor.IsAlive() || Actor.Team != Team || Actor.bAttacked) continue;
				int BestTarget = -1;
				float BestScore = -1.0f;
				for (std::size_t TargetIndex = 0; TargetIndex < Units.size(); ++TargetIndex)
				{
					const FUnit& Target = Units[TargetIndex];
					if (!Target.IsAlive() || Target.Team == Team || HexDistance(Actor.Cell, Target.Cell) > Actor.Range) continue;
					const float Score = RoleValue(Target.Role) + (Actor.Damage >= Target.Health ? 2.0f : 0.0f) +
						static_cast<float>(Target.MaxHealth - Target.Health) / Target.MaxHealth;
					if (Score > BestScore) { BestScore = Score; BestTarget = static_cast<int>(TargetIndex); }
				}
				if (BestTarget >= 0)
				{
					FAction Action; Action.Kind = EActionKind::Attack; Action.Actor = static_cast<int>(ActorIndex); Action.Target = BestTarget;
					return ExecuteAction(Action, Team);
				}
			}

			for (std::size_t ActorIndex = 0; ActorIndex < Units.size(); ++ActorIndex)
			{
				const FUnit& Healer = Units[ActorIndex];
				if (!Healer.IsAlive() || Healer.Team != Team || Healer.Heal <= 0 || Healer.bHealed) continue;
				for (std::size_t TargetIndex = 0; TargetIndex < Units.size(); ++TargetIndex)
				{
					const FUnit& Target = Units[TargetIndex];
					if (Target.IsAlive() && Target.Team == Team && Target.Health * 100 <= Target.MaxHealth * 35 &&
						HexDistance(Healer.Cell, Target.Cell) <= 2)
					{
						FAction Action; Action.Kind = EActionKind::Heal; Action.Actor = static_cast<int>(ActorIndex); Action.Target = static_cast<int>(TargetIndex);
						return ExecuteAction(Action, Team);
					}
				}
			}

			int BestActor = -1;
			FHex BestDestination;
			float BestMoveScore = -std::numeric_limits<float>::infinity();
			for (std::size_t ActorIndex = 0; ActorIndex < Units.size(); ++ActorIndex)
			{
				const FUnit& Actor = Units[ActorIndex];
				if (!Actor.IsAlive() || Actor.Team != Team || Actor.bMoved) continue;
				const int CurrentDistance = NearestEnemyDistance(static_cast<int>(ActorIndex), Actor.Cell);
				for (int DQ = -Actor.Movement; DQ <= Actor.Movement; ++DQ)
				{
					for (int DR = -Actor.Movement; DR <= Actor.Movement; ++DR)
					{
						const FHex Destination{ Actor.Cell.Q + DQ, Actor.Cell.R + DR };
						if (HexDistance(Actor.Cell, Destination) <= 0 || HexDistance(Actor.Cell, Destination) > Actor.Movement ||
							!IsOnBoard(Destination) || IsOccupied(Destination, static_cast<int>(ActorIndex))) continue;
						const int CandidateDistance = NearestEnemyDistance(static_cast<int>(ActorIndex), Destination);
						const float Score = static_cast<float>(CurrentDistance - CandidateDistance) * 2.0f +
							NearbyAllies(static_cast<int>(ActorIndex), Destination, 2) * 0.25f -
							ProjectedIncomingDamage(static_cast<int>(ActorIndex), Destination) / static_cast<float>(Actor.Health);
						if (Score > BestMoveScore)
						{
							BestMoveScore = Score; BestActor = static_cast<int>(ActorIndex); BestDestination = Destination;
						}
					}
				}
			}
			if (BestActor >= 0)
			{
				FAction Action; Action.Kind = EActionKind::Move; Action.Actor = BestActor; Action.Destination = BestDestination;
				return ExecuteAction(Action, Team);
			}
			(void)ActionPoints;
			return false;
		}

		void RunTeamTurn(int Team, bool bImproved)
		{
			for (FUnit& Unit : Units)
			{
				if (Unit.Team == Team)
				{
					Unit.bMoved = false; Unit.bAttacked = false; Unit.bHealed = false;
				}
			}

			for (int ActionPoints = ActionPointsPerTurn; ActionPoints > 0; --ActionPoints)
			{
				const bool bForcedKillBefore = HasForcedKill(Team, ActionPoints);
				Metrics.ForcedKillOpportunitiesByTeam[Team] += bForcedKillBefore ? 1 : 0;
				const int KillsBefore = Metrics.KillsByTeam[Team];
				const bool bExecuted = bImproved
					? ExecuteImprovedAction(Team, ActionPoints)
					: ExecuteLegacyAction(Team, ActionPoints);
				if (!bExecuted)
				{
					break;
				}
				if (bForcedKillBefore && Metrics.KillsByTeam[Team] > KillsBefore)
				{
					Metrics.ForcedKillsTakenByTeam[Team]++;
				}
				if (!HasAliveUnits(1 - Team))
				{
					break;
				}
			}
		}
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

	bool RunModelTests()
	{
		bool Passed = true;
		{
			const std::array<FDecisionOption, 2> Options = {
				FDecisionOption{ 100.0f, false, false, false, 1 },
				FDecisionOption{ -100.0f, true, false, false, 2 }
			};
			for (int Step = 0; Step < 100; ++Step)
			{
				Passed &= Expect(FDecisionModel::SelectIndex(Options.data(), Options.size(), EDifficulty::WarmUp, Step / 100.0f) == 1,
					"forced kill must never be randomized away");
			}
		}
		{
			const std::array<FDecisionOption, 2> Options = {
				FDecisionOption{ 20.0f, false, false, false, 1 },
				FDecisionOption{ -20.0f, false, true, false, 2 }
			};
			Passed &= Expect(FDecisionModel::SelectIndex(Options.data(), Options.size(), EDifficulty::Challenge, 0.5f) == 1,
				"life-saving action must dominate ordinary utility");
		}
		{
			const std::array<FDecisionOption, 3> Options = {
				FDecisionOption{ 2.0f, false, false, false, 3 },
				FDecisionOption{ 2.0f, false, false, false, 1 },
				FDecisionOption{ 1.9f, false, false, false, 2 }
			};
			Passed &= Expect(FDecisionModel::SelectIndex(Options.data(), Options.size(), EDifficulty::Nightmare, 0.99f) == 1,
				"Nightmare must be deterministic with stable tie-break");
		}
		{
			const std::array<FDecisionOption, 3> Options = {
				FDecisionOption{ 2.0f, false, false, false, 1 },
				FDecisionOption{ 1.9f, false, false, false, 2 },
				FDecisionOption{ -5.0f, false, false, false, 3 }
			};
			std::array<int, 3> Counts = { 0, 0, 0 };
			for (int Step = 0; Step < 10000; ++Step)
			{
				const float Random01 = (Step + 0.5f) / 10000.0f;
				const std::size_t Index = FDecisionModel::SelectIndex(Options.data(), Options.size(), EDifficulty::WarmUp, Random01);
				Counts[Index]++;
			}
			Passed &= Expect(Counts[0] > 5000 && Counts[1] > 2500, "WarmUp should vary between near-optimal actions");
			Passed &= Expect(Counts[2] == 0, "bounded exploration must reject high-regret actions");
		}
		{
			FDecisionFeatures SafeAttack;
			SafeAttack.Action = EActionKind::Attack;
			SafeAttack.ImmediateGain = 0.7f;
			SafeAttack.TargetValue = 0.8f;
			SafeAttack.Exposure = 0.1f;
			FDecisionFeatures SuicideAttack = SafeAttack;
			SuicideAttack.ImmediateGain = 0.8f;
			SuicideAttack.Exposure = 1.0f;
			Passed &= Expect(
				FDecisionModel::Evaluate(SafeAttack, EDifficulty::Ordeal) > FDecisionModel::Evaluate(SuicideAttack, EDifficulty::Ordeal),
				"expected survival must outweigh a small damage increase"
			);
		}
		{
			std::mt19937 PropertyRandom(0x51a7f00du);
			std::uniform_real_distribution<float> Utility(-10.0f, 10.0f);
			std::uniform_real_distribution<float> UnitRandom(0.0f, 1.0f);
			bool bKillInvariantHeld = true;
			bool bNightmareMaximumHeld = true;
			bool bMonotonicityHeld = true;

			for (int Trial = 0; Trial < 50000; ++Trial)
			{
				const std::size_t Count = 2 + (PropertyRandom() % 31);
				std::vector<FDecisionOption> Options(Count);
				for (std::size_t Index = 0; Index < Count; ++Index)
				{
					Options[Index].Utility = Utility(PropertyRandom);
					Options[Index].StableKey = static_cast<std::uint32_t>(Index + 1);
				}

				const std::size_t KillIndex = PropertyRandom() % Count;
				Options[KillIndex].bSecuresKill = true;
				const std::size_t SelectedKill = FDecisionModel::SelectIndex(
					Options.data(), Options.size(), EDifficulty::WarmUp, UnitRandom(PropertyRandom)
				);
				bKillInvariantHeld = bKillInvariantHeld && SelectedKill == KillIndex;

				Options[KillIndex].bSecuresKill = false;
				std::size_t ExpectedBest = 0;
				for (std::size_t Index = 1; Index < Count; ++Index)
				{
					if (Options[Index].Utility > Options[ExpectedBest].Utility + 1.0e-5f ||
						(std::abs(Options[Index].Utility - Options[ExpectedBest].Utility) <= 1.0e-5f &&
						 Options[Index].StableKey < Options[ExpectedBest].StableKey))
					{
						ExpectedBest = Index;
					}
				}
				const std::size_t SelectedBest = FDecisionModel::SelectIndex(
					Options.data(), Options.size(), EDifficulty::Nightmare, UnitRandom(PropertyRandom)
				);
				bNightmareMaximumHeld = bNightmareMaximumHeld && SelectedBest == ExpectedBest;

				FDecisionFeatures Base;
				Base.BaseUtility = Utility(PropertyRandom) * 0.25f;
				Base.ImmediateGain = UnitRandom(PropertyRandom);
				Base.TargetValue = UnitRandom(PropertyRandom);
				Base.FutureGain = UnitRandom(PropertyRandom);
				Base.PositionProgress = UnitRandom(PropertyRandom);
				Base.Cohesion = UnitRandom(PropertyRandom);
				Base.Exposure = UnitRandom(PropertyRandom);
				Base.ResourceCost = UnitRandom(PropertyRandom);
				Base.Overcommit = UnitRandom(PropertyRandom);
				const float BaseScore = FDecisionModel::Evaluate(Base, EDifficulty::Ordeal);

				FDecisionFeatures MoreGain = Base;
				MoreGain.ImmediateGain = std::min(1.0f, Base.ImmediateGain + 0.05f);
				MoreGain.TargetValue = std::min(1.0f, Base.TargetValue + 0.05f);
				MoreGain.FutureGain = std::min(1.0f, Base.FutureGain + 0.05f);
				FDecisionFeatures MoreRisk = Base;
				MoreRisk.Exposure = std::min(1.0f, Base.Exposure + 0.05f);
				MoreRisk.ResourceCost = std::min(1.0f, Base.ResourceCost + 0.05f);
				MoreRisk.Overcommit = std::min(1.0f, Base.Overcommit + 0.05f);
				bMonotonicityHeld = bMonotonicityHeld &&
					FDecisionModel::Evaluate(MoreGain, EDifficulty::Ordeal) + 1.0e-5f >= BaseScore &&
					FDecisionModel::Evaluate(MoreRisk, EDifficulty::Ordeal) <= BaseScore + 1.0e-5f;
			}

			Passed &= Expect(bKillInvariantHeld, "50k randomized candidate sets preserved the forced-kill invariant");
			Passed &= Expect(bNightmareMaximumHeld, "50k randomized candidate sets selected the exact Nightmare maximum");
			Passed &= Expect(bMonotonicityHeld, "50k randomized feature sets preserved score monotonicity");
		}
		{
			const FDecisionOption InvalidOptions[] =
			{
				{ std::numeric_limits<float>::quiet_NaN(), true, true, true, 1 },
				{ 1.0f, false, false, false, 2 }
			};
			Passed &= Expect(FDecisionModel::SelectIndex(nullptr, 0, EDifficulty::Nightmare, 0.0f) == 0,
				"empty candidate input returns the count sentinel");
			Passed &= Expect(FDecisionModel::SelectIndex(InvalidOptions, 2, EDifficulty::Nightmare, 0.0f) == 1,
				"non-finite critical candidate cannot poison eligibility or be selected");
		}

		std::cout << (Passed ? "All decision-model tests passed.\n" : "Decision-model tests failed.\n");
		return Passed;
	}

	struct FAggregateMetrics
	{
		int Matches = 0;
		int ImprovedWins = 0;
		int LegacyWins = 0;
		int Draws = 0;
		int ImprovedFirstSideMatches = 0;
		int ImprovedFirstSideWins = 0;
		int ImprovedSecondSideMatches = 0;
		int ImprovedSecondSideWins = 0;
		long long ImprovedHealth = 0;
		long long LegacyHealth = 0;
		long long HalfTurns = 0;
		long long ImprovedDamage = 0;
		long long LegacyDamage = 0;
		long long ImprovedMoveKills = 0;
		long long LegacyMoveKills = 0;
		long long ImprovedForcedOpportunities = 0;
		long long ImprovedForcedKills = 0;
		long long LegacyForcedOpportunities = 0;
		long long LegacyForcedKills = 0;
	};

	void AddResult(FAggregateMetrics& Aggregate, const FBattleMetrics& Result, int ImprovedTeam)
	{
		Aggregate.Matches++;
		const bool bImprovedWon = Result.Winner == ImprovedTeam;
		if (Result.Winner < 0) Aggregate.Draws++;
		else if (bImprovedWon) Aggregate.ImprovedWins++;
		else Aggregate.LegacyWins++;
		if (ImprovedTeam == 0)
		{
			Aggregate.ImprovedFirstSideMatches++;
			if (bImprovedWon) Aggregate.ImprovedFirstSideWins++;
		}
		else
		{
			Aggregate.ImprovedSecondSideMatches++;
			if (bImprovedWon) Aggregate.ImprovedSecondSideWins++;
		}
		Aggregate.ImprovedHealth += Result.RemainingHealth[ImprovedTeam];
		Aggregate.LegacyHealth += Result.RemainingHealth[1 - ImprovedTeam];
		Aggregate.HalfTurns += Result.HalfTurns;
		Aggregate.ImprovedDamage += Result.DamageByTeam[ImprovedTeam];
		Aggregate.LegacyDamage += Result.DamageByTeam[1 - ImprovedTeam];
		Aggregate.ImprovedMoveKills += Result.MoveKillSetupsByTeam[ImprovedTeam];
		Aggregate.LegacyMoveKills += Result.MoveKillSetupsByTeam[1 - ImprovedTeam];
		Aggregate.ImprovedForcedOpportunities += Result.ForcedKillOpportunitiesByTeam[ImprovedTeam];
		Aggregate.ImprovedForcedKills += Result.ForcedKillsTakenByTeam[ImprovedTeam];
		Aggregate.LegacyForcedOpportunities += Result.ForcedKillOpportunitiesByTeam[1 - ImprovedTeam];
		Aggregate.LegacyForcedKills += Result.ForcedKillsTakenByTeam[1 - ImprovedTeam];
	}

	FAggregateMetrics RunBenchmark(int PairCount, std::uint32_t Seed)
	{
		FAggregateMetrics Aggregate;
		for (int Pair = 0; Pair < PairCount; ++Pair)
		{
			const std::uint32_t ScenarioSeed = Seed + static_cast<std::uint32_t>(Pair * 7919);
			const FScenario Scenario = MakeScenario(ScenarioSeed);
			AddResult(Aggregate, FBattle(Scenario, 0, ScenarioSeed ^ 0x9e3779b9u).Run(), 0);
			AddResult(Aggregate, FBattle(Scenario, 1, ScenarioSeed ^ 0x85ebca6bu).Run(), 1);
		}
		return Aggregate;
	}

	double Percent(long long Numerator, long long Denominator)
	{
		return Denominator > 0 ? 100.0 * static_cast<double>(Numerator) / static_cast<double>(Denominator) : 0.0;
	}

	double WilsonLowerBoundPercent(long long Successes, long long Trials)
	{
		if (Trials <= 0) return 0.0;
		constexpr double Z = 1.959963984540054;
		const double N = static_cast<double>(Trials);
		const double P = static_cast<double>(Successes) / N;
		const double ZSquared = Z * Z;
		const double Centre = P + ZSquared / (2.0 * N);
		const double Radius = Z * std::sqrt((P * (1.0 - P) + ZSquared / (4.0 * N)) / N);
		return 100.0 * (Centre - Radius) / (1.0 + ZSquared / N);
	}

	void PrintBenchmark(const FAggregateMetrics& Metrics, std::uint32_t Seed)
	{
		std::cout << std::fixed << std::setprecision(2);
		std::cout << "Bot benchmark (paired policy swap)\n";
		std::cout << "seed=" << Seed << " matches=" << Metrics.Matches << '\n';
		std::cout << "improved_win_rate=" << Percent(Metrics.ImprovedWins, Metrics.Matches) << "%"
			<< " legacy_win_rate=" << Percent(Metrics.LegacyWins, Metrics.Matches) << "%"
			<< " draws=" << Percent(Metrics.Draws, Metrics.Matches) << "%\n";
		std::cout << "improved_win_rate_95pct_lower_bound="
			<< WilsonLowerBoundPercent(Metrics.ImprovedWins, Metrics.Matches) << "%\n";
		std::cout << "side_check improved_as_first="
			<< Percent(Metrics.ImprovedFirstSideWins, Metrics.ImprovedFirstSideMatches) << "%"
			<< " improved_as_second="
			<< Percent(Metrics.ImprovedSecondSideWins, Metrics.ImprovedSecondSideMatches) << "%\n";
		std::cout << "avg_remaining_hp improved=" << static_cast<double>(Metrics.ImprovedHealth) / Metrics.Matches
			<< " legacy=" << static_cast<double>(Metrics.LegacyHealth) / Metrics.Matches << '\n';
		std::cout << "avg_damage improved=" << static_cast<double>(Metrics.ImprovedDamage) / Metrics.Matches
			<< " legacy=" << static_cast<double>(Metrics.LegacyDamage) / Metrics.Matches << '\n';
		std::cout << "avg_full_turns=" << static_cast<double>(Metrics.HalfTurns) / Metrics.Matches / 2.0 << '\n';
		std::cout << "move_to_kill_setups improved=" << Metrics.ImprovedMoveKills
			<< " legacy=" << Metrics.LegacyMoveKills << '\n';
		std::cout << "forced_kill_conversion improved="
			<< Percent(Metrics.ImprovedForcedKills, Metrics.ImprovedForcedOpportunities) << "%"
			<< " legacy=" << Percent(Metrics.LegacyForcedKills, Metrics.LegacyForcedOpportunities) << "%\n";
	}

	void WriteJson(const std::string& Path, const FAggregateMetrics& Metrics, std::uint32_t Seed)
	{
		std::ofstream Output(Path, std::ios::trunc);
		Output << std::fixed << std::setprecision(4);
		Output << "{\n"
			<< "  \"seed\": " << Seed << ",\n"
			<< "  \"matches\": " << Metrics.Matches << ",\n"
			<< "  \"improved_win_rate\": " << Percent(Metrics.ImprovedWins, Metrics.Matches) << ",\n"
			<< "  \"legacy_win_rate\": " << Percent(Metrics.LegacyWins, Metrics.Matches) << ",\n"
			<< "  \"draw_rate\": " << Percent(Metrics.Draws, Metrics.Matches) << ",\n"
			<< "  \"improved_win_rate_95pct_lower_bound\": " << WilsonLowerBoundPercent(Metrics.ImprovedWins, Metrics.Matches) << ",\n"
			<< "  \"improved_win_rate_as_first_side\": " << Percent(Metrics.ImprovedFirstSideWins, Metrics.ImprovedFirstSideMatches) << ",\n"
			<< "  \"improved_win_rate_as_second_side\": " << Percent(Metrics.ImprovedSecondSideWins, Metrics.ImprovedSecondSideMatches) << ",\n"
			<< "  \"improved_avg_remaining_hp\": " << static_cast<double>(Metrics.ImprovedHealth) / Metrics.Matches << ",\n"
			<< "  \"legacy_avg_remaining_hp\": " << static_cast<double>(Metrics.LegacyHealth) / Metrics.Matches << ",\n"
			<< "  \"improved_avg_damage\": " << static_cast<double>(Metrics.ImprovedDamage) / Metrics.Matches << ",\n"
			<< "  \"legacy_avg_damage\": " << static_cast<double>(Metrics.LegacyDamage) / Metrics.Matches << ",\n"
			<< "  \"average_full_turns\": " << static_cast<double>(Metrics.HalfTurns) / Metrics.Matches / 2.0 << ",\n"
			<< "  \"improved_move_to_kill_setups\": " << Metrics.ImprovedMoveKills << ",\n"
			<< "  \"legacy_move_to_kill_setups\": " << Metrics.LegacyMoveKills << ",\n"
			<< "  \"improved_forced_kill_conversion\": " << Percent(Metrics.ImprovedForcedKills, Metrics.ImprovedForcedOpportunities) << ",\n"
			<< "  \"legacy_forced_kill_conversion\": " << Percent(Metrics.LegacyForcedKills, Metrics.LegacyForcedOpportunities) << "\n"
			<< "}\n";
	}
}

int main(int ArgCount, char** Arguments)
{
	int PairCount = 5000;
	std::uint32_t Seed = 0x5eed1234u;
	std::string JsonPath;
	bool bTestsOnly = false;

	for (int Index = 1; Index < ArgCount; ++Index)
	{
		const std::string Argument = Arguments[Index];
		if (Argument == "--test") bTestsOnly = true;
		else if (Argument == "--pairs" && Index + 1 < ArgCount) PairCount = std::max(1, std::stoi(Arguments[++Index]));
		else if (Argument == "--seed" && Index + 1 < ArgCount) Seed = static_cast<std::uint32_t>(std::stoul(Arguments[++Index]));
		else if (Argument == "--json" && Index + 1 < ArgCount) JsonPath = Arguments[++Index];
	}

	if (!RunModelTests())
	{
		return 1;
	}
	if (bTestsOnly)
	{
		return 0;
	}

	const FAggregateMetrics Metrics = RunBenchmark(PairCount, Seed);
	PrintBenchmark(Metrics, Seed);
	if (!JsonPath.empty()) WriteJson(JsonPath, Metrics, Seed);

	// This is a regression gate, not a claim about the final Unreal win rate.
	// Paired simulation must show a material advantage and better kill conversion.
	const double ImprovedAverageHealth = static_cast<double>(Metrics.ImprovedHealth) / Metrics.Matches;
	const double LegacyAverageHealth = static_cast<double>(Metrics.LegacyHealth) / Metrics.Matches;
	const bool bPassesMetrics = WilsonLowerBoundPercent(Metrics.ImprovedWins, Metrics.Matches) >= 75.0 &&
		Percent(Metrics.ImprovedFirstSideWins, Metrics.ImprovedFirstSideMatches) >= 75.0 &&
		Percent(Metrics.ImprovedSecondSideWins, Metrics.ImprovedSecondSideMatches) >= 75.0 &&
		Percent(Metrics.ImprovedForcedKills, Metrics.ImprovedForcedOpportunities) >=
		Percent(Metrics.LegacyForcedKills, Metrics.LegacyForcedOpportunities) + 10.0 &&
		ImprovedAverageHealth >= LegacyAverageHealth + 75.0 &&
		Metrics.ImprovedDamage >= Metrics.LegacyDamage;
	if (!bPassesMetrics)
	{
		std::cerr << "FAILED: benchmark regression thresholds were not met.\n";
		return 2;
	}
	return 0;
}
