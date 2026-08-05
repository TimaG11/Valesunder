#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Engine-independent, bounded neural search for the hex bot. The Unreal layer
// only creates a POD snapshot and executes the validated first action. This is
// intentionally independent of UObject/AActor so search never clones the world.
namespace OtherBios::BotNeural
{
	constexpr std::size_t FeatureCount = 24;
	constexpr std::size_t HiddenCount = 32;
	constexpr std::size_t LearnedParameterCount =
		2 * (FeatureCount * HiddenCount + HiddenCount + HiddenCount + 1);

	enum class EActionKind : std::uint8_t
	{
		Attack,
		Heal,
		Move,
		EndTurn
	};

	struct FCoord
	{
		int Q = 0;
		int R = 0;

		bool operator==(const FCoord& Other) const noexcept
		{
			return Q == Other.Q && R == Other.R;
		}
	};

	inline std::uint64_t CoordKey(int Q, int R) noexcept
	{
		return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(Q)) << 32u) |
			static_cast<std::uint32_t>(R);
	}

	inline int HexDistance(const FCoord& A, const FCoord& B) noexcept
	{
		const int DQ = A.Q - B.Q;
		const int DR = A.R - B.R;
		return (std::abs(DQ) + std::abs(DR) + std::abs(DQ + DR)) / 2;
	}

	struct FTopology
	{
		std::vector<FCoord> Cells;
		std::unordered_set<std::uint64_t> ValidCellKeys;
		int Diameter = 1;

		void RebuildIndex()
		{
			ValidCellKeys.clear();
			ValidCellKeys.reserve(Cells.size() * 2 + 1);
			int MinQ = 0;
			int MaxQ = 0;
			int MinR = 0;
			int MaxR = 0;
			int MinS = 0;
			int MaxS = 0;
			bool bFirst = true;
			for (const FCoord& Cell : Cells)
			{
				ValidCellKeys.insert(CoordKey(Cell.Q, Cell.R));
				const int S = Cell.Q + Cell.R;
				if (bFirst)
				{
					MinQ = MaxQ = Cell.Q;
					MinR = MaxR = Cell.R;
					MinS = MaxS = S;
					bFirst = false;
				}
				else
				{
					MinQ = std::min(MinQ, Cell.Q);
					MaxQ = std::max(MaxQ, Cell.Q);
					MinR = std::min(MinR, Cell.R);
					MaxR = std::max(MaxR, Cell.R);
					MinS = std::min(MinS, S);
					MaxS = std::max(MaxS, S);
				}
			}
			Diameter = std::max(1, std::max({ MaxQ - MinQ, MaxR - MinR, MaxS - MinS }));
		}

		bool HasCell(const FCoord& Cell) const
		{
			return ValidCellKeys.find(CoordKey(Cell.Q, Cell.R)) != ValidCellKeys.end();
		}
	};

	struct FUnit
	{
		std::uint32_t StableKey = 0;
		std::uint8_t Team = 0;
		FCoord Cell;
		int Health = 1;
		int MaxHealth = 1;
		int Damage = 1;
		int AttackRange = 1;
		int MovementRange = 0;
		int MovementSpent = 0;
		int HealAmount = 0;
		int HealRange = 1;
		int HealCost = 1;
		float IncomingDamageScale = 1.0f;
		int LastStandSurviveHealth = 1;
		bool bAlive = true;
		bool bCanAct = true;
		bool bAttacked = false;
		bool bCanHeal = false;
		bool bLastStandActive = false;
	};

	struct FState
	{
		const FTopology* Topology = nullptr;
		std::vector<FUnit> Units;
		std::uint8_t SideToMove = 0;
		int ActionPoints = 6;
		int MaxActionPoints = 6;
		int MoveActionPointCost = 1;
		int AttackActionPointCost = 1;
		int KillActionPointBonus = 0;
		bool bUsePathLengthForMoveCost = false;
		bool bEnableKillActionPointBonus = false;
		bool bLimitKillBonusOncePerTurn = true;
		std::array<bool, 2> KillBonusUsed = { false, false };
	};

	struct FAction
	{
		EActionKind Kind = EActionKind::EndTurn;
		std::size_t ActorIndex = std::numeric_limits<std::size_t>::max();
		std::size_t TargetIndex = std::numeric_limits<std::size_t>::max();
		FCoord Destination;
		int PathLength = 0;
		int ActionPointCost = 0;
		std::uint32_t StableKey = 0;
		bool bSecuresKill = false;
		bool bPreventsDeath = false;
		bool bTerminalOutcome = false;
	};

	struct FSearchSettings
	{
		int Depth = 5;
		int TopK = 10;
		int HeuristicSafetyCandidates = 2;
		int NodeBudget = 5000;
		double TimeBudgetMilliseconds = 12.0;
		float NeuralPolicyBlend = 0.55f;
		float NeuralValueBlend = 0.55f;
	};

	struct FSearchResult
	{
		FAction Action;
		float Score = 0.0f;
		int NodesVisited = 0;
		int RootRawActions = 0;
		int RootExpandedActions = 0;
		double ElapsedMilliseconds = 0.0;
		bool bHasAction = false;
		bool bBudgetExhausted = false;
	};
}

#include "HexBotNeuralWeights.inl"

namespace OtherBios::BotNeural
{
	class FPolicyValueNetwork final
	{
	public:
		static constexpr std::size_t ParameterCount() noexcept
		{
			return LearnedParameterCount;
		}

		float ScoreAction(const std::array<float, FeatureCount>& Features) const noexcept
		{
			return Forward(
				Features,
				Weights::PolicyW1,
				Weights::PolicyB1,
				Weights::PolicyW2,
				Weights::PolicyB2,
				false
			);
		}

		float EvaluateState(const std::array<float, FeatureCount>& Features) const noexcept
		{
			return Forward(
				Features,
				Weights::ValueW1,
				Weights::ValueB1,
				Weights::ValueW2,
				Weights::ValueB2,
				true
			);
		}

	private:
		static float Forward(
			const std::array<float, FeatureCount>& Features,
			const std::array<float, FeatureCount * HiddenCount>& W1,
			const std::array<float, HiddenCount>& B1,
			const std::array<float, HiddenCount>& W2,
			float B2,
			bool bBoundOutput) noexcept
		{
			std::array<float, HiddenCount> Hidden{};
			for (std::size_t Output = 0; Output < HiddenCount; ++Output)
			{
				float Sum = B1[Output];
				for (std::size_t Input = 0; Input < FeatureCount; ++Input)
				{
					Sum += Features[Input] * W1[Output * FeatureCount + Input];
				}
				Hidden[Output] = std::tanh(Sum);
			}

			float Output = B2;
			for (std::size_t Index = 0; Index < HiddenCount; ++Index)
			{
				Output += Hidden[Index] * W2[Index];
			}
			return bBoundOutput ? std::tanh(Output) : Output;
		}
	};

	class FSimulator final
	{
	public:
		static bool HasAliveTeam(const FState& State, std::uint8_t Team)
		{
			for (const FUnit& Unit : State.Units)
			{
				if (Unit.bAlive && Unit.Health > 0 && Unit.Team == Team)
				{
					return true;
				}
			}
			return false;
		}

		static int AliveCount(const FState& State, std::uint8_t Team)
		{
			int Count = 0;
			for (const FUnit& Unit : State.Units)
			{
				Count += Unit.bAlive && Unit.Health > 0 && Unit.Team == Team ? 1 : 0;
			}
			return Count;
		}

		static bool IsOccupied(const FState& State, const FCoord& Cell, std::size_t IgnoreIndex = std::numeric_limits<std::size_t>::max())
		{
			for (std::size_t Index = 0; Index < State.Units.size(); ++Index)
			{
				const FUnit& Unit = State.Units[Index];
				if (Index != IgnoreIndex && Unit.bAlive && Unit.Health > 0 && Unit.Cell == Cell)
				{
					return true;
				}
			}
			return false;
		}

		static int IncomingDamage(const FState& State, std::size_t UnitIndex, const FCoord& Cell)
		{
			if (UnitIndex >= State.Units.size())
			{
				return 0;
			}
			const FUnit& Unit = State.Units[UnitIndex];
			int Damage = 0;
			for (const FUnit& Enemy : State.Units)
			{
				if (!Enemy.bAlive || Enemy.Health <= 0 || Enemy.Team == Unit.Team || Enemy.bAttacked)
				{
					continue;
				}
				if (HexDistance(Cell, Enemy.Cell) <= std::max(1, Enemy.AttackRange))
				{
					Damage += std::max(0, static_cast<int>(std::lround(Enemy.Damage * Unit.IncomingDamageScale)));
				}
			}
			return Damage;
		}

		static int NearestEnemyDistance(const FState& State, std::size_t UnitIndex, const FCoord& Cell)
		{
			if (UnitIndex >= State.Units.size())
			{
				return State.Topology ? std::max(1, State.Topology->Diameter) : 1;
			}
			const std::uint8_t Team = State.Units[UnitIndex].Team;
			int Best = State.Topology ? std::max(1, State.Topology->Diameter) : 1;
			for (const FUnit& Other : State.Units)
			{
				if (Other.bAlive && Other.Health > 0 && Other.Team != Team)
				{
					Best = std::min(Best, HexDistance(Cell, Other.Cell));
				}
			}
			return Best;
		}

		static int NearbyAllies(const FState& State, std::size_t UnitIndex, const FCoord& Cell, int Range)
		{
			if (UnitIndex >= State.Units.size())
			{
				return 0;
			}
			const std::uint8_t Team = State.Units[UnitIndex].Team;
			int Count = 0;
			for (std::size_t Index = 0; Index < State.Units.size(); ++Index)
			{
				const FUnit& Other = State.Units[Index];
				if (Index != UnitIndex && Other.bAlive && Other.Health > 0 && Other.Team == Team &&
					HexDistance(Cell, Other.Cell) <= Range)
				{
					++Count;
				}
			}
			return Count;
		}

		static float RawUnitValue(const FUnit& Unit) noexcept
		{
			return static_cast<float>(std::max(1, Unit.MaxHealth)) +
				static_cast<float>(std::max(0, Unit.Damage)) * (1.5f + 0.35f * std::max(1, Unit.AttackRange)) +
				static_cast<float>(std::max(0, Unit.HealAmount)) * 1.25f +
				static_cast<float>(std::max(0, Unit.MovementRange)) * 4.0f;
		}

		static float RelativeUnitValue(const FState& State, const FUnit& Unit) noexcept
		{
			float Maximum = 1.0f;
			for (const FUnit& Candidate : State.Units)
			{
				if (Candidate.bAlive && Candidate.Health > 0)
				{
					Maximum = std::max(Maximum, RawUnitValue(Candidate));
				}
			}
			return std::max(0.0f, std::min(1.0f, RawUnitValue(Unit) / Maximum));
		}

		static void GenerateActions(const FState& State, std::vector<FAction>& OutActions)
		{
			OutActions.clear();
			if (!State.Topology || State.ActionPoints <= 0 || !HasAliveTeam(State, State.SideToMove))
			{
				FAction End;
				End.Kind = EActionKind::EndTurn;
				OutActions.push_back(End);
				return;
			}

			for (std::size_t ActorIndex = 0; ActorIndex < State.Units.size(); ++ActorIndex)
			{
				const FUnit& Actor = State.Units[ActorIndex];
				if (!Actor.bAlive || Actor.Health <= 0 || !Actor.bCanAct || Actor.Team != State.SideToMove)
				{
					continue;
				}

				if (!Actor.bAttacked && State.ActionPoints >= State.AttackActionPointCost)
				{
					for (std::size_t TargetIndex = 0; TargetIndex < State.Units.size(); ++TargetIndex)
					{
						const FUnit& Target = State.Units[TargetIndex];
						if (!Target.bAlive || Target.Health <= 0 || Target.Team == Actor.Team ||
							HexDistance(Actor.Cell, Target.Cell) <= 0 ||
							HexDistance(Actor.Cell, Target.Cell) > std::max(1, Actor.AttackRange))
						{
							continue;
						}

						const int Damage = std::max(0, static_cast<int>(std::lround(Actor.Damage * Target.IncomingDamageScale)));
						FAction Action;
						Action.Kind = EActionKind::Attack;
						Action.ActorIndex = ActorIndex;
						Action.TargetIndex = TargetIndex;
						Action.Destination = Target.Cell;
						Action.ActionPointCost = State.AttackActionPointCost;
						Action.bSecuresKill = Damage >= Target.Health && !Target.bLastStandActive;
						Action.bTerminalOutcome = Action.bSecuresKill && AliveCount(State, Target.Team) == 1;
						Action.StableKey = Actor.StableKey * 16777619u ^ Target.StableKey ^ 0x10000000u;
						OutActions.push_back(Action);
					}
				}

				if (Actor.bCanHeal && Actor.HealAmount > 0 && State.ActionPoints >= Actor.HealCost)
				{
					for (std::size_t TargetIndex = 0; TargetIndex < State.Units.size(); ++TargetIndex)
					{
						const FUnit& Target = State.Units[TargetIndex];
						const int Distance = HexDistance(Actor.Cell, Target.Cell);
						if (!Target.bAlive || Target.Health <= 0 || Target.Team != Actor.Team || Target.Health >= Target.MaxHealth ||
							Distance <= 0 || Distance > std::max(1, Actor.HealRange))
						{
							continue;
						}

						const int Heal = std::min(std::max(0, Actor.HealAmount), Target.MaxHealth - Target.Health);
						const int Threat = IncomingDamage(State, TargetIndex, Target.Cell);
						FAction Action;
						Action.Kind = EActionKind::Heal;
						Action.ActorIndex = ActorIndex;
						Action.TargetIndex = TargetIndex;
						Action.Destination = Target.Cell;
						Action.ActionPointCost = Actor.HealCost;
						Action.bPreventsDeath = Threat >= Target.Health && Threat < Target.Health + Heal;
						Action.StableKey = Actor.StableKey * 16777619u ^ Target.StableKey ^ 0x20000000u;
						OutActions.push_back(Action);
					}
				}

				const int RemainingMovement = std::max(0, Actor.MovementRange - Actor.MovementSpent);
				if (RemainingMovement > 0)
				{
					GenerateMoveActions(State, ActorIndex, RemainingMovement, OutActions);
				}
			}

			FAction End;
			End.Kind = EActionKind::EndTurn;
			End.StableKey = 0xffffffffu;
			OutActions.push_back(End);
		}

		static bool ApplyAction(FState& State, const FAction& Action)
		{
			if (Action.Kind == EActionKind::EndTurn)
			{
				StartNextTurn(State);
				return true;
			}
			if (Action.ActorIndex >= State.Units.size() || State.ActionPoints < Action.ActionPointCost)
			{
				return false;
			}

			FUnit& Actor = State.Units[Action.ActorIndex];
			if (!Actor.bAlive || Actor.Health <= 0 || !Actor.bCanAct || Actor.Team != State.SideToMove)
			{
				return false;
			}

			switch (Action.Kind)
			{
			case EActionKind::Attack:
			{
				if (Actor.bAttacked || Action.TargetIndex >= State.Units.size() ||
					Action.ActionPointCost != std::max(0, State.AttackActionPointCost)) return false;
				FUnit& Target = State.Units[Action.TargetIndex];
				const int Distance = HexDistance(Actor.Cell, Target.Cell);
				if (!Target.bAlive || Target.Health <= 0 || Target.Team == Actor.Team || Distance <= 0 ||
					Distance > std::max(1, Actor.AttackRange)) return false;
				State.ActionPoints -= std::max(0, Action.ActionPointCost);
				const int Damage = std::max(0, static_cast<int>(std::lround(Actor.Damage * Target.IncomingDamageScale)));
				Actor.bAttacked = true;
				if (Damage >= Target.Health && Target.bLastStandActive)
				{
					Target.Health = std::max(1, std::min(Target.MaxHealth, Target.LastStandSurviveHealth));
					Target.bLastStandActive = false;
				}
				else
				{
					Target.Health = std::max(0, Target.Health - Damage);
					if (Target.Health <= 0)
					{
						Target.bAlive = false;
						AwardKillBonus(State, Actor.Team);
					}
				}
				break;
			}
			case EActionKind::Heal:
			{
				if (!Actor.bCanHeal || Action.TargetIndex >= State.Units.size() ||
					Action.ActionPointCost != std::max(0, Actor.HealCost)) return false;
				FUnit& Target = State.Units[Action.TargetIndex];
				const int Distance = HexDistance(Actor.Cell, Target.Cell);
				if (!Target.bAlive || Target.Health <= 0 || Target.Team != Actor.Team || Target.Health >= Target.MaxHealth ||
					Distance <= 0 || Distance > std::max(1, Actor.HealRange)) return false;
				State.ActionPoints -= std::max(0, Action.ActionPointCost);
				Target.Health = std::min(Target.MaxHealth, Target.Health + std::max(0, Actor.HealAmount));
				break;
			}
			case EActionKind::Move:
			{
				const int RemainingMovement = std::max(0, Actor.MovementRange - Actor.MovementSpent);
				const int VerifiedPathLength = Action.PathLength;
				const int VerifiedCost = std::max(0, State.MoveActionPointCost) *
					(State.bUsePathLengthForMoveCost ? VerifiedPathLength : 1);
				if (!State.Topology || !State.Topology->HasCell(Action.Destination) ||
					IsOccupied(State, Action.Destination, Action.ActorIndex) || VerifiedPathLength <= 0 ||
					VerifiedPathLength > RemainingMovement || HexDistance(Actor.Cell, Action.Destination) > VerifiedPathLength ||
					Action.ActionPointCost != VerifiedCost || State.ActionPoints < VerifiedCost) return false;
				State.ActionPoints -= std::max(0, Action.ActionPointCost);
				Actor.Cell = Action.Destination;
				Actor.MovementSpent = std::min(Actor.MovementRange, Actor.MovementSpent + std::max(0, Action.PathLength));
				break;
			}
			case EActionKind::EndTurn:
			default:
				return false;
			}

			if (State.ActionPoints <= 0 && HasAliveTeam(State, 0) && HasAliveTeam(State, 1))
			{
				StartNextTurn(State);
			}
			return true;
		}

		static std::array<float, FeatureCount> StateFeatures(const FState& State, std::uint8_t PerspectiveTeam)
		{
			std::array<float, FeatureCount> F{};
			const std::uint8_t Opponent = PerspectiveTeam == 0 ? 1 : 0;
			struct FTeamSummary
			{
				float Alive = 0.0f;
				float Health = 0.0f;
				float MaxHealth = 0.0f;
				float Damage = 0.0f;
				float Healing = 0.0f;
				float Ready = 0.0f;
				float Mobility = 0.0f;
				float Threatened = 0.0f;
				float KillOpportunities = 0.0f;
				float Distance = 0.0f;
			};

			std::array<FTeamSummary, 2> Summary{};
			for (std::size_t Index = 0; Index < State.Units.size(); ++Index)
			{
				const FUnit& Unit = State.Units[Index];
				if (!Unit.bAlive || Unit.Health <= 0 || Unit.Team > 1) continue;
				FTeamSummary& S = Summary[Unit.Team];
				S.Alive += 1.0f;
				S.Health += static_cast<float>(Unit.Health);
				S.MaxHealth += static_cast<float>(std::max(1, Unit.MaxHealth));
				S.Damage += static_cast<float>(std::max(0, Unit.Damage));
				S.Healing += static_cast<float>(std::max(0, Unit.HealAmount));
				S.Ready += Unit.bCanAct && !Unit.bAttacked ? 1.0f : 0.0f;
				S.Mobility += static_cast<float>(std::max(0, Unit.MovementRange - Unit.MovementSpent));
				S.Threatened += IncomingDamage(State, Index, Unit.Cell) >= Unit.Health ? 1.0f : 0.0f;
				S.Distance += static_cast<float>(NearestEnemyDistance(State, Index, Unit.Cell));
				for (const FUnit& Target : State.Units)
				{
					if (Target.bAlive && Target.Health > 0 && Target.Team != Unit.Team && !Unit.bAttacked &&
						HexDistance(Unit.Cell, Target.Cell) <= Unit.AttackRange &&
						static_cast<int>(std::lround(Unit.Damage * Target.IncomingDamageScale)) >= Target.Health)
					{
						S.KillOpportunities += 1.0f;
						break;
					}
				}
			}

			const FTeamSummary& Own = Summary[PerspectiveTeam];
			const FTeamSummary& Enemy = Summary[Opponent];
			auto Clamp01 = [](float Value) { return std::max(0.0f, std::min(1.0f, Value)); };
			const float OwnHealth = Own.MaxHealth > 0.0f ? Own.Health / Own.MaxHealth : 0.0f;
			const float EnemyHealth = Enemy.MaxHealth > 0.0f ? Enemy.Health / Enemy.MaxHealth : 0.0f;
			const float TotalAlive = std::max(1.0f, Own.Alive + Enemy.Alive);
			const float TotalDamage = std::max(1.0f, Own.Damage + Enemy.Damage);
			const float TotalHealing = std::max(1.0f, Own.Healing + Enemy.Healing);
			const float Diameter = static_cast<float>(State.Topology ? std::max(1, State.Topology->Diameter) : 1);
			F[0] = Clamp01(Own.Alive / TotalAlive);
			F[1] = Clamp01(Enemy.Alive / TotalAlive);
			F[2] = OwnHealth;
			F[3] = EnemyHealth;
			F[4] = std::max(-1.0f, std::min(1.0f, OwnHealth - EnemyHealth));
			F[5] = Clamp01(Own.Damage / TotalDamage);
			F[6] = Clamp01(Enemy.Damage / TotalDamage);
			F[7] = std::max(-1.0f, std::min(1.0f, (Own.Damage - Enemy.Damage) / TotalDamage));
			F[8] = Clamp01(Own.Healing / TotalHealing);
			F[9] = Clamp01(Enemy.Healing / TotalHealing);
			F[10] = Own.Alive > 0.0f ? Clamp01(Own.Ready / Own.Alive) : 0.0f;
			F[11] = Enemy.Alive > 0.0f ? Clamp01(Enemy.Ready / Enemy.Alive) : 0.0f;
			F[12] = Own.Alive > 0.0f ? Clamp01(Own.Mobility / Own.Alive / Diameter) : 0.0f;
			F[13] = Enemy.Alive > 0.0f ? Clamp01(Enemy.Mobility / Enemy.Alive / Diameter) : 0.0f;
			F[14] = Own.Alive > 0.0f ? Clamp01(1.0f - Own.Distance / Own.Alive / Diameter) : 0.0f;
			F[15] = Enemy.Alive > 0.0f ? Clamp01(1.0f - Enemy.Distance / Enemy.Alive / Diameter) : 0.0f;
			F[16] = Own.Alive > 0.0f ? Clamp01(Own.KillOpportunities / Own.Alive) : 0.0f;
			F[17] = Enemy.Alive > 0.0f ? Clamp01(Enemy.KillOpportunities / Enemy.Alive) : 0.0f;
			F[18] = Own.Alive > 0.0f ? Clamp01(Own.Threatened / Own.Alive) : 0.0f;
			F[19] = Enemy.Alive > 0.0f ? Clamp01(Enemy.Threatened / Enemy.Alive) : 0.0f;
			F[20] = State.SideToMove == PerspectiveTeam ? 1.0f : -1.0f;
			F[21] = (State.SideToMove == PerspectiveTeam ? 1.0f : -1.0f) *
				Clamp01(static_cast<float>(State.ActionPoints) / std::max(1, State.MaxActionPoints));
			F[22] = Enemy.Alive <= 0.0f ? 1.0f : 0.0f;
			F[23] = Own.Alive <= 0.0f ? 1.0f : 0.0f;
			return F;
		}

		static std::array<float, FeatureCount> ActionFeatures(const FState& State, const FAction& Action)
		{
			std::array<float, FeatureCount> F{};
			F[0] = Action.Kind == EActionKind::Attack ? 1.0f : 0.0f;
			F[1] = Action.Kind == EActionKind::Heal ? 1.0f : 0.0f;
			F[2] = Action.Kind == EActionKind::Move ? 1.0f : 0.0f;
			F[3] = Action.Kind == EActionKind::EndTurn ? 1.0f : 0.0f;
			if (Action.Kind == EActionKind::EndTurn || Action.ActorIndex >= State.Units.size())
			{
				F[23] = 1.0f;
				return F;
			}

			auto Clamp01 = [](float Value) { return std::max(0.0f, std::min(1.0f, Value)); };
			const FUnit& Actor = State.Units[Action.ActorIndex];
			const FUnit* Target = Action.TargetIndex < State.Units.size() ? &State.Units[Action.TargetIndex] : nullptr;
			F[4] = Clamp01(static_cast<float>(Actor.Health) / std::max(1, Actor.MaxHealth));
			int MaximumDamage = 1;
			int MaximumHealing = 1;
			for (const FUnit& Unit : State.Units)
			{
				if (!Unit.bAlive || Unit.Health <= 0) continue;
				MaximumDamage = std::max(MaximumDamage, Unit.Damage);
				MaximumHealing = std::max(MaximumHealing, Unit.HealAmount);
			}
			const float Diameter = static_cast<float>(State.Topology ? std::max(1, State.Topology->Diameter) : 1);
			F[5] = Clamp01(static_cast<float>(Actor.Damage) / MaximumDamage);
			F[6] = Clamp01(static_cast<float>(Actor.HealAmount) / MaximumHealing);
			F[7] = Clamp01(static_cast<float>(Actor.AttackRange) / Diameter);
			F[8] = Clamp01(static_cast<float>(std::max(0, Actor.MovementRange - Actor.MovementSpent)) /
				std::max(1, Actor.MovementRange));
			F[9] = RelativeUnitValue(State, Actor);
			F[10] = Target ? Clamp01(static_cast<float>(Target->Health) / std::max(1, Target->MaxHealth)) : 0.0f;
			F[11] = Target ? RelativeUnitValue(State, *Target) : 0.0f;

			if (Action.Kind == EActionKind::Attack && Target)
			{
				const int Damage = std::min(Target->Health, std::max(0, static_cast<int>(std::lround(Actor.Damage * Target->IncomingDamageScale))));
				F[12] = Clamp01(static_cast<float>(Damage) / std::max(1, Target->Health));
			}
			else if (Action.Kind == EActionKind::Heal && Target)
			{
				const int Heal = std::min(std::max(0, Actor.HealAmount), std::max(0, Target->MaxHealth - Target->Health));
				F[12] = Clamp01(static_cast<float>(Heal) / std::max(1, Target->MaxHealth));
			}
			F[13] = Action.bSecuresKill ? 1.0f : 0.0f;
			F[14] = Action.bTerminalOutcome ? 1.0f : 0.0f;
			F[15] = Action.bPreventsDeath ? 1.0f : 0.0f;
			F[16] = Clamp01(static_cast<float>(Action.ActionPointCost) / std::max(1, State.MaxActionPoints));
			const int BeforeDistance = NearestEnemyDistance(State, Action.ActorIndex, Actor.Cell);
			const FCoord ResultCell = Action.Kind == EActionKind::Move ? Action.Destination : Actor.Cell;
			const int AfterDistance = NearestEnemyDistance(State, Action.ActorIndex, ResultCell);
			F[17] = Clamp01(static_cast<float>(std::max(0, BeforeDistance - AfterDistance)) / std::max(1, Actor.MovementRange));
			F[18] = Clamp01(static_cast<float>(IncomingDamage(State, Action.ActorIndex, ResultCell)) / std::max(1, Actor.Health));
			F[19] = Clamp01(static_cast<float>(NearbyAllies(State, Action.ActorIndex, ResultCell, 2)) /
				std::max(1, AliveCount(State, Actor.Team) - 1));
			F[20] = Clamp01(static_cast<float>(AfterDistance) / Diameter);
			const auto StateF = StateFeatures(State, Actor.Team);
			F[21] = StateF[4];
			F[22] = Clamp01(static_cast<float>(std::max(0, State.ActionPoints - Action.ActionPointCost)) /
				std::max(1, State.MaxActionPoints));
			F[23] = Action.Kind == EActionKind::EndTurn ? 1.0f : 0.0f;
			return F;
		}

		// Transparent fallback/safety score. It is deliberately not a training target:
		// learned weights are fitted only to self-play returns.
		static float TacticalFallbackScore(const std::array<float, FeatureCount>& F) noexcept
		{
			return 1.4f * F[0] + 0.8f * F[1] + 0.45f * F[2] - 1.2f * F[3] +
				0.35f * F[4] + 0.45f * F[9] + 0.85f * F[11] + 2.4f * F[12] +
				7.0f * F[13] + 12.0f * F[14] + 5.5f * F[15] - 1.0f * F[16] +
				1.1f * F[17] - 2.8f * F[18] + 0.45f * F[19] - 0.55f * F[20] +
				0.45f * F[21] + 0.30f * F[22];
		}

		static float TacticalStateValue(const FState& State, std::uint8_t PerspectiveTeam) noexcept
		{
			const std::uint8_t Opponent = PerspectiveTeam == 0 ? 1 : 0;
			if (!HasAliveTeam(State, PerspectiveTeam)) return -1.0f;
			if (!HasAliveTeam(State, Opponent)) return 1.0f;
			float Own = 0.0f;
			float Enemy = 0.0f;
			for (const FUnit& Unit : State.Units)
			{
				if (!Unit.bAlive || Unit.Health <= 0) continue;
				const float HealthFraction = static_cast<float>(Unit.Health) / std::max(1, Unit.MaxHealth);
				const float Power = RawUnitValue(Unit) * (0.25f + 0.75f * HealthFraction);
				(Unit.Team == PerspectiveTeam ? Own : Enemy) += Power;
			}
			return std::max(-0.95f, std::min(0.95f, (Own - Enemy) / std::max(1.0f, Own + Enemy)));
		}

		static std::uint64_t HashState(const FState& State)
		{
			std::uint64_t Hash = 1469598103934665603ull;
			auto Mix = [&Hash](std::uint64_t Value)
			{
				Hash ^= Value + 0x9e3779b97f4a7c15ull + (Hash << 6u) + (Hash >> 2u);
			};
			Mix(State.SideToMove);
			Mix(static_cast<std::uint64_t>(State.ActionPoints));
			Mix(State.KillBonusUsed[0] ? 1u : 0u);
			Mix(State.KillBonusUsed[1] ? 1u : 0u);
			for (const FUnit& Unit : State.Units)
			{
				Mix(Unit.StableKey);
				Mix(CoordKey(Unit.Cell.Q, Unit.Cell.R));
				Mix(static_cast<std::uint64_t>(std::max(0, Unit.Health)));
				Mix(static_cast<std::uint64_t>(std::max(0, Unit.MovementSpent)));
				Mix(Unit.bAlive ? 1u : 0u);
				Mix(Unit.bAttacked ? 1u : 0u);
				Mix(Unit.bLastStandActive ? 1u : 0u);
			}
			return Hash;
		}

	private:
		static void GenerateMoveActions(const FState& State, std::size_t ActorIndex, int MaxSteps, std::vector<FAction>& OutActions)
		{
			static constexpr int Directions[6][2] = {
				{ 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 }
			};
			const FUnit& Actor = State.Units[ActorIndex];
			std::deque<FCoord> Queue;
			std::unordered_map<std::uint64_t, int> Distance;
			Queue.push_back(Actor.Cell);
			Distance.emplace(CoordKey(Actor.Cell.Q, Actor.Cell.R), 0);
			while (!Queue.empty())
			{
				const FCoord Current = Queue.front();
				Queue.pop_front();
				const int CurrentDistance = Distance[CoordKey(Current.Q, Current.R)];
				if (CurrentDistance >= MaxSteps) continue;
				for (const auto& Direction : Directions)
				{
					const FCoord Next{ Current.Q + Direction[0], Current.R + Direction[1] };
					const std::uint64_t Key = CoordKey(Next.Q, Next.R);
					if (Distance.find(Key) != Distance.end() || !State.Topology->HasCell(Next) || IsOccupied(State, Next, ActorIndex))
					{
						continue;
					}
					const int NextDistance = CurrentDistance + 1;
					const int Cost = std::max(0, State.MoveActionPointCost) *
						(State.bUsePathLengthForMoveCost ? NextDistance : 1);
					Distance.emplace(Key, NextDistance);
					Queue.push_back(Next);
					if (State.ActionPoints < Cost) continue;
					FAction Action;
					Action.Kind = EActionKind::Move;
					Action.ActorIndex = ActorIndex;
					Action.Destination = Next;
					Action.PathLength = NextDistance;
					Action.ActionPointCost = Cost;
					Action.StableKey = Actor.StableKey * 16777619u ^
						static_cast<std::uint32_t>(Key ^ (Key >> 32u)) ^ 0x30000000u;
					for (std::size_t TargetIndex = 0; TargetIndex < State.Units.size(); ++TargetIndex)
					{
						const FUnit& Target = State.Units[TargetIndex];
						const int Damage = std::max(0, static_cast<int>(std::lround(Actor.Damage * Target.IncomingDamageScale)));
						if (Target.bAlive && Target.Team != Actor.Team && !Actor.bAttacked &&
							State.ActionPoints >= Cost + State.AttackActionPointCost &&
							HexDistance(Next, Target.Cell) <= Actor.AttackRange && Damage >= Target.Health &&
							!Target.bLastStandActive)
						{
							Action.TargetIndex = TargetIndex;
							Action.bSecuresKill = true;
							// The move only creates a forced follow-up; it is not terminal yet.
							Action.bTerminalOutcome = false;
							break;
						}
					}
					OutActions.push_back(Action);
				}
			}
		}

		static void AwardKillBonus(FState& State, std::uint8_t Team)
		{
			if (!State.bEnableKillActionPointBonus || State.KillActionPointBonus <= 0 || Team > 1)
			{
				return;
			}
			if (State.bLimitKillBonusOncePerTurn && State.KillBonusUsed[Team])
			{
				return;
			}
			State.ActionPoints = std::min(State.MaxActionPoints, State.ActionPoints + State.KillActionPointBonus);
			State.KillBonusUsed[Team] = true;
		}

		static void StartNextTurn(FState& State)
		{
			State.SideToMove = State.SideToMove == 0 ? 1 : 0;
			State.ActionPoints = std::max(0, State.MaxActionPoints);
			if (State.SideToMove <= 1) State.KillBonusUsed[State.SideToMove] = false;
			for (FUnit& Unit : State.Units)
			{
				if (Unit.bAlive && Unit.Team == State.SideToMove)
				{
					Unit.bAttacked = false;
					Unit.MovementSpent = 0;
				}
			}
		}
	};

	class FNeuralSearchPlanner final
	{
	public:
		FSearchResult FindBestAction(const FState& InitialState, std::uint8_t RootTeam, const FSearchSettings& InSettings)
		{
			Settings = InSettings;
			Settings.Depth = std::max(1, Settings.Depth);
			Settings.TopK = std::max(1, Settings.TopK);
			Settings.HeuristicSafetyCandidates = std::max(0, Settings.HeuristicSafetyCandidates);
			Settings.NodeBudget = std::max(1, Settings.NodeBudget);
			Settings.NeuralPolicyBlend = std::max(0.0f, std::min(1.0f, Settings.NeuralPolicyBlend));
			Settings.NeuralValueBlend = std::max(0.0f, std::min(1.0f, Settings.NeuralValueBlend));
			Root = RootTeam;
			Nodes = 0;
			bBudgetHit = false;
			Cache.clear();
			Start = std::chrono::steady_clock::now();

			FSearchResult Result;
			std::vector<FAction> RawActions;
			FSimulator::GenerateActions(InitialState, RawActions);
			Result.RootRawActions = static_cast<int>(RawActions.size());
			std::vector<FScoredAction> Candidates = SelectCandidates(InitialState, RawActions);
			Result.RootExpandedActions = static_cast<int>(Candidates.size());
			const bool bMaximizing = InitialState.SideToMove == Root;
			float Best = bMaximizing ? -2.0f : 2.0f;
			std::uint32_t BestKey = std::numeric_limits<std::uint32_t>::max();

			for (const FScoredAction& Candidate : Candidates)
			{
				// Candidate generation itself can consume a tiny time budget on a very
				// large board. Always validate/evaluate the first (hard-priority) root
				// action so a forced kill can never degrade into EndTurn.
				if (Result.bHasAction && BudgetReached()) break;
				FState Next = InitialState;
				if (!FSimulator::ApplyAction(Next, Candidate.Action)) continue;
				const float Score = Search(Next, Settings.Depth - 1, -2.0f, 2.0f);
				const bool bBetter = bMaximizing
					? Score > Best + 1.0e-6f || (std::abs(Score - Best) <= 1.0e-6f && Candidate.Action.StableKey < BestKey)
					: Score < Best - 1.0e-6f || (std::abs(Score - Best) <= 1.0e-6f && Candidate.Action.StableKey < BestKey);
				if (!Result.bHasAction || bBetter)
				{
					Best = Score;
					BestKey = Candidate.Action.StableKey;
					Result.Action = Candidate.Action;
					Result.bHasAction = true;
				}
			}

			Result.Score = Result.bHasAction ? Best : Evaluate(InitialState);
			Result.NodesVisited = Nodes;
			Result.bBudgetExhausted = bBudgetHit;
			Result.ElapsedMilliseconds = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - Start).count();
			return Result;
		}

	private:
		struct FScoredAction
		{
			FAction Action;
			float NeuralScore = 0.0f;
			float HeuristicScore = 0.0f;
		};

		struct FCacheEntry
		{
			int Depth = -1;
			float Value = 0.0f;
		};

		FPolicyValueNetwork Network;
		FSearchSettings Settings;
		std::uint8_t Root = 0;
		int Nodes = 0;
		bool bBudgetHit = false;
		std::chrono::steady_clock::time_point Start;
		std::unordered_map<std::uint64_t, FCacheEntry> Cache;

		bool BudgetReached()
		{
			if (Nodes >= Settings.NodeBudget)
			{
				bBudgetHit = true;
				return true;
			}
			if ((Nodes & 7) == 0 && Settings.TimeBudgetMilliseconds > 0.0)
			{
				const double Elapsed = std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - Start).count();
				if (Elapsed >= Settings.TimeBudgetMilliseconds)
				{
					bBudgetHit = true;
					return true;
				}
			}
			return false;
		}

		float Evaluate(const FState& State) const
		{
			if (!FSimulator::HasAliveTeam(State, Root)) return -1.0f;
			if (!FSimulator::HasAliveTeam(State, Root == 0 ? 1 : 0)) return 1.0f;
			const float Neural = Network.EvaluateState(FSimulator::StateFeatures(State, Root));
			const float Tactical = FSimulator::TacticalStateValue(State, Root);
			return Settings.NeuralValueBlend * Neural + (1.0f - Settings.NeuralValueBlend) * Tactical;
		}

		float Search(const FState& State, int Depth, float Alpha, float Beta)
		{
			++Nodes;
			if (Depth <= 0 || BudgetReached() || !FSimulator::HasAliveTeam(State, 0) || !FSimulator::HasAliveTeam(State, 1))
			{
				return Evaluate(State);
			}

			const std::uint64_t Hash = FSimulator::HashState(State);
			const auto Cached = Cache.find(Hash);
			if (Cached != Cache.end() && Cached->second.Depth >= Depth)
			{
				return Cached->second.Value;
			}

			std::vector<FAction> RawActions;
			FSimulator::GenerateActions(State, RawActions);
			std::vector<FScoredAction> Candidates = SelectCandidates(State, RawActions);
			if (Candidates.empty()) return Evaluate(State);
			const bool bMaximizing = State.SideToMove == Root;
			float Best = bMaximizing ? -2.0f : 2.0f;
			bool bCutoff = false;
			for (const FScoredAction& Candidate : Candidates)
			{
				if (BudgetReached()) break;
				FState Next = State;
				if (!FSimulator::ApplyAction(Next, Candidate.Action)) continue;
				const float Score = Search(Next, Depth - 1, Alpha, Beta);
				if (bMaximizing)
				{
					Best = std::max(Best, Score);
					Alpha = std::max(Alpha, Best);
				}
				else
				{
					Best = std::min(Best, Score);
					Beta = std::min(Beta, Best);
				}
				if (Beta <= Alpha)
				{
					bCutoff = true;
					break;
				}
			}
			if (Best < -1.5f || Best > 1.5f) Best = Evaluate(State);
			if (!bCutoff && !bBudgetHit) Cache[Hash] = { Depth, Best };
			return Best;
		}

		std::vector<FScoredAction> SelectCandidates(const FState& State, const std::vector<FAction>& Actions) const
		{
			std::vector<FScoredAction> Scored;
			Scored.reserve(Actions.size());
			for (const FAction& Action : Actions)
			{
				const auto Features = FSimulator::ActionFeatures(State, Action);
				const float Neural = Network.ScoreAction(Features);
				const float Heuristic = FSimulator::TacticalFallbackScore(Features);
				const float BoundedHeuristic = std::tanh(Heuristic / 4.0f);
				const float Combined = Settings.NeuralPolicyBlend * Neural +
					(1.0f - Settings.NeuralPolicyBlend) * BoundedHeuristic;
				Scored.push_back({ Action, Combined, Heuristic });
			}
			const bool bHasTerminal = std::any_of(Scored.begin(), Scored.end(), [](const FScoredAction& Candidate)
			{
				return Candidate.Action.bTerminalOutcome;
			});
			const bool bHasImmediateKill = std::any_of(Scored.begin(), Scored.end(), [](const FScoredAction& Candidate)
			{
				return Candidate.Action.Kind == EActionKind::Attack && Candidate.Action.bSecuresKill;
			});
			const bool bHasLifeSavingHeal = std::any_of(Scored.begin(), Scored.end(), [](const FScoredAction& Candidate)
			{
				return Candidate.Action.Kind == EActionKind::Heal && Candidate.Action.bPreventsDeath;
			});
			const bool bHasMoveToKill = std::any_of(Scored.begin(), Scored.end(), [](const FScoredAction& Candidate)
			{
				return Candidate.Action.Kind == EActionKind::Move && Candidate.Action.bSecuresKill;
			});
			const bool bHasOrdinaryAttack = std::any_of(Scored.begin(), Scored.end(), [](const FScoredAction& Candidate)
			{
				return Candidate.Action.Kind == EActionKind::Attack;
			});
			Scored.erase(std::remove_if(Scored.begin(), Scored.end(), [&](const FScoredAction& Candidate)
			{
				if (bHasTerminal) return !Candidate.Action.bTerminalOutcome;
				if (bHasImmediateKill)
				{
					return Candidate.Action.Kind != EActionKind::Attack || !Candidate.Action.bSecuresKill;
				}
				if (bHasLifeSavingHeal)
				{
					return Candidate.Action.Kind != EActionKind::Heal || !Candidate.Action.bPreventsDeath;
				}
				if (bHasMoveToKill)
				{
					return Candidate.Action.Kind != EActionKind::Move || !Candidate.Action.bSecuresKill;
				}
				if (bHasOrdinaryAttack)
				{
					return Candidate.Action.Kind != EActionKind::Attack;
				}
				return false;
			}), Scored.end());
			std::stable_sort(Scored.begin(), Scored.end(), [](const FScoredAction& Left, const FScoredAction& Right)
			{
				if (Left.NeuralScore != Right.NeuralScore) return Left.NeuralScore > Right.NeuralScore;
				return Left.Action.StableKey < Right.Action.StableKey;
			});

			std::vector<FScoredAction> Selected;
			Selected.reserve(static_cast<std::size_t>(Settings.TopK + Settings.HeuristicSafetyCandidates + 8));
			auto AddUnique = [&Selected](const FScoredAction& Candidate)
			{
				const auto Existing = std::find_if(Selected.begin(), Selected.end(), [&Candidate](const FScoredAction& Current)
				{
					return Current.Action.StableKey == Candidate.Action.StableKey && Current.Action.Kind == Candidate.Action.Kind;
				});
				if (Existing == Selected.end()) Selected.push_back(Candidate);
			};

			for (const FScoredAction& Candidate : Scored)
			{
				if (Candidate.Action.bTerminalOutcome || Candidate.Action.bSecuresKill || Candidate.Action.bPreventsDeath)
				{
					AddUnique(Candidate);
				}
			}
			for (int Index = 0; Index < static_cast<int>(Scored.size()) && Index < Settings.TopK; ++Index)
			{
				AddUnique(Scored[static_cast<std::size_t>(Index)]);
			}

			std::vector<FScoredAction> ByHeuristic = Scored;
			std::stable_sort(ByHeuristic.begin(), ByHeuristic.end(), [](const FScoredAction& Left, const FScoredAction& Right)
			{
				if (Left.HeuristicScore != Right.HeuristicScore) return Left.HeuristicScore > Right.HeuristicScore;
				return Left.Action.StableKey < Right.Action.StableKey;
			});
			for (int Index = 0; Index < static_cast<int>(ByHeuristic.size()) && Index < Settings.HeuristicSafetyCandidates; ++Index)
			{
				AddUnique(ByHeuristic[static_cast<std::size_t>(Index)]);
			}
			for (const FScoredAction& Candidate : Scored)
			{
				if (Candidate.Action.Kind == EActionKind::EndTurn) AddUnique(Candidate);
			}

			std::stable_sort(Selected.begin(), Selected.end(), [](const FScoredAction& Left, const FScoredAction& Right)
			{
				if (Left.Action.bTerminalOutcome != Right.Action.bTerminalOutcome) return Left.Action.bTerminalOutcome;
				if (Left.Action.bSecuresKill != Right.Action.bSecuresKill) return Left.Action.bSecuresKill;
				if (Left.Action.bPreventsDeath != Right.Action.bPreventsDeath) return Left.Action.bPreventsDeath;
				if (Left.NeuralScore != Right.NeuralScore) return Left.NeuralScore > Right.NeuralScore;
				return Left.Action.StableKey < Right.Action.StableKey;
			});
			return Selected;
		}
	};
}
