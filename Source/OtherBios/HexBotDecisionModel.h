#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// A small, engine-independent Utility AI kernel.  It deliberately has no Unreal
// types so the exact production decision code can also be compiled by the long
// running benchmark in Tools/BotBenchmark.
namespace OtherBios::BotAI
{
	enum class EDifficulty : std::uint8_t
	{
		WarmUp = 0,
		Challenge = 1,
		Ordeal = 2,
		Nightmare = 3
	};

	enum class EActionKind : std::uint8_t
	{
		Attack,
		Heal,
		Move,
		Ability
	};

	struct FDecisionFeatures
	{
		EActionKind Action = EActionKind::Move;

		// All feature values are expected in [0, 1], except BaseUtility which is
		// a compact [-3, 3] summary of older game-specific heuristics.
		float BaseUtility = 0.0f;
		float ImmediateGain = 0.0f;
		float TargetValue = 0.0f;
		float PlanAlignment = 0.0f;
		float FutureGain = 0.0f;
		float PositionProgress = 0.0f;
		float Cohesion = 0.0f;
		float Exposure = 0.0f;
		float ResourceCost = 0.0f;
		float Overcommit = 0.0f;

		bool bSecuresKill = false;
		bool bPreventsDeath = false;
		bool bTerminalOutcome = false;
	};

	struct FDecisionOption
	{
		float Utility = -std::numeric_limits<float>::infinity();
		bool bSecuresKill = false;
		bool bPreventsDeath = false;
		bool bTerminalOutcome = false;
		std::uint32_t StableKey = 0;
	};

	struct FDifficultyProfile
	{
		float BaseUtilityWeight;
		float ImmediateGainWeight;
		float TargetValueWeight;
		float KillWeight;
		float PreventDeathWeight;
		float TerminalWeight;
		float PlanAlignmentWeight;
		float FutureGainWeight;
		float PositionProgressWeight;
		float CohesionWeight;
		float ExposureWeight;
		float ResourceCostWeight;
		float OvercommitWeight;
		float Temperature;
		float MaximumRegret;
	};

	class FDecisionModel final
	{
	public:
		static constexpr float Clamp01(float Value) noexcept
		{
			return Value < 0.0f ? 0.0f : (Value > 1.0f ? 1.0f : Value);
		}

		static constexpr float ClampBaseUtility(float Value) noexcept
		{
			return Value < -3.0f ? -3.0f : (Value > 3.0f ? 3.0f : Value);
		}

		static constexpr FDifficultyProfile GetProfile(EDifficulty Difficulty) noexcept
		{
			switch (Difficulty)
			{
			case EDifficulty::WarmUp:
				return { 0.65f, 2.20f, 1.05f, 6.0f, 5.0f, 12.0f, 0.45f, 0.70f, 0.55f, 0.35f, 2.30f, 0.70f, 0.60f, 0.42f, 1.25f };
			case EDifficulty::Challenge:
				return { 0.82f, 2.80f, 1.35f, 7.5f, 6.5f, 14.0f, 0.75f, 1.10f, 0.75f, 0.55f, 3.00f, 0.90f, 0.85f, 0.22f, 0.75f };
			case EDifficulty::Ordeal:
				return { 0.94f, 3.10f, 1.55f, 9.0f, 7.5f, 16.0f, 1.00f, 1.45f, 0.90f, 0.75f, 3.65f, 1.10f, 1.10f, 0.08f, 0.22f };
			case EDifficulty::Nightmare:
			default:
				return { 1.00f, 3.35f, 1.75f, 11.0f, 8.5f, 18.0f, 1.20f, 1.75f, 1.05f, 0.90f, 4.25f, 1.25f, 1.35f, 0.0f, 0.0f };
			}
		}

		static float Evaluate(const FDecisionFeatures& Features, EDifficulty Difficulty) noexcept
		{
			const FDifficultyProfile Profile = GetProfile(Difficulty);
			float Score = ClampBaseUtility(Features.BaseUtility) * Profile.BaseUtilityWeight;
			Score += Clamp01(Features.ImmediateGain) * Profile.ImmediateGainWeight;
			Score += Clamp01(Features.TargetValue) * Profile.TargetValueWeight;
			Score += Features.bSecuresKill ? Profile.KillWeight : 0.0f;
			Score += Features.bPreventsDeath ? Profile.PreventDeathWeight : 0.0f;
			Score += Features.bTerminalOutcome ? Profile.TerminalWeight : 0.0f;
			Score += Clamp01(Features.PlanAlignment) * Profile.PlanAlignmentWeight;
			Score += Clamp01(Features.FutureGain) * Profile.FutureGainWeight;
			Score += Clamp01(Features.PositionProgress) * Profile.PositionProgressWeight;
			Score += Clamp01(Features.Cohesion) * Profile.CohesionWeight;
			Score -= Clamp01(Features.Exposure) * Profile.ExposureWeight;
			Score -= Clamp01(Features.ResourceCost) * Profile.ResourceCostWeight;
			Score -= Clamp01(Features.Overcommit) * Profile.OvercommitWeight;
			return Score;
		}

		// Boltzmann selection is limited to near-optimal candidates.  Hard tactical
		// invariants are applied first, so temperature can create varied play but
		// can never turn a forced win/kill/save into an obviously losing action.
		static std::size_t SelectIndex(
			const FDecisionOption* Options,
			std::size_t Count,
			EDifficulty Difficulty,
			float Random01,
			float TemperatureScale = 1.0f) noexcept
		{
			if (Options == nullptr || Count == 0)
			{
				return Count;
			}

			bool bRequireTerminal = false;
			bool bRequireKill = false;
			bool bRequireSave = false;
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				if (!std::isfinite(Options[Index].Utility))
				{
					continue;
				}
				bRequireTerminal = bRequireTerminal || Options[Index].bTerminalOutcome;
				bRequireKill = bRequireKill || Options[Index].bSecuresKill;
				bRequireSave = bRequireSave || Options[Index].bPreventsDeath;
			}

			auto IsEligible = [bRequireTerminal, bRequireKill, bRequireSave](const FDecisionOption& Option) noexcept
			{
				if (bRequireTerminal)
				{
					return Option.bTerminalOutcome;
				}
				if (bRequireKill)
				{
					return Option.bSecuresKill;
				}
				if (bRequireSave)
				{
					return Option.bPreventsDeath;
				}
				return true;
			};

			std::size_t BestIndex = Count;
			float BestUtility = -std::numeric_limits<float>::infinity();
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				const FDecisionOption& Option = Options[Index];
				if (!IsEligible(Option) || !std::isfinite(Option.Utility))
				{
					continue;
				}

				if (BestIndex == Count || Option.Utility > BestUtility + 1.0e-5f ||
					(std::abs(Option.Utility - BestUtility) <= 1.0e-5f && Option.StableKey < Options[BestIndex].StableKey))
				{
					BestIndex = Index;
					BestUtility = Option.Utility;
				}
			}

			if (BestIndex == Count)
			{
				return Count;
			}

			const FDifficultyProfile Profile = GetProfile(Difficulty);
			const float Temperature = Profile.Temperature * std::max(0.0f, TemperatureScale);
			if (Temperature <= 1.0e-5f || Profile.MaximumRegret <= 0.0f)
			{
				return BestIndex;
			}

			float TotalWeight = 0.0f;
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				const FDecisionOption& Option = Options[Index];
				const float Regret = BestUtility - Option.Utility;
				if (IsEligible(Option) && std::isfinite(Option.Utility) && Regret <= Profile.MaximumRegret)
				{
					TotalWeight += std::exp(-std::max(0.0f, Regret) / Temperature);
				}
			}

			if (!(TotalWeight > 0.0f) || !std::isfinite(TotalWeight))
			{
				return BestIndex;
			}

			const float SafeRandom = std::min(0.99999994f, std::max(0.0f, Random01));
			const float Threshold = SafeRandom * TotalWeight;
			float Accumulated = 0.0f;
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				const FDecisionOption& Option = Options[Index];
				const float Regret = BestUtility - Option.Utility;
				if (!IsEligible(Option) || !std::isfinite(Option.Utility) || Regret > Profile.MaximumRegret)
				{
					continue;
				}

				Accumulated += std::exp(-std::max(0.0f, Regret) / Temperature);
				if (Threshold < Accumulated)
				{
					return Index;
				}
			}

			return BestIndex;
		}
	};
}
