#include "HexBotDecisionModel.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

using OtherBios::BotAI::EActionKind;
using OtherBios::BotAI::EDifficulty;
using OtherBios::BotAI::FDecisionFeatures;
using OtherBios::BotAI::FDecisionModel;
using OtherBios::BotAI::FDecisionOption;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHexBotForcedKillInvariantTest,
	"OtherBios.BotAI.DecisionModel.ForcedKillInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FHexBotForcedKillInvariantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FDecisionOption Options[] =
	{
		{ 100.0f, false, false, false, 1 },
		{ -100.0f, true, false, false, 2 }
	};

	for (int32 Step = 0; Step < 100; ++Step)
	{
		const size_t Selected = FDecisionModel::SelectIndex(
			Options,
			UE_ARRAY_COUNT(Options),
			EDifficulty::WarmUp,
			static_cast<float>(Step) / 100.0f
		);
		TestEqual(TEXT("WarmUp never randomizes away a forced kill"), Selected, static_cast<size_t>(1));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHexBotLifeSavingActionInvariantTest,
	"OtherBios.BotAI.DecisionModel.LifeSavingActionInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FHexBotLifeSavingActionInvariantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FDecisionOption Options[] =
	{
		{ 20.0f, false, false, false, 1 },
		{ -20.0f, false, true, false, 2 }
	};
	const size_t Selected = FDecisionModel::SelectIndex(
		Options,
		UE_ARRAY_COUNT(Options),
		EDifficulty::Challenge,
		0.5f
	);
	TestEqual(TEXT("A real life-saving action wins over ordinary utility"), Selected, static_cast<size_t>(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHexBotNightmareDeterminismTest,
	"OtherBios.BotAI.DecisionModel.NightmareDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FHexBotNightmareDeterminismTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FDecisionOption Options[] =
	{
		{ 2.0f, false, false, false, 3 },
		{ 2.0f, false, false, false, 1 },
		{ 1.9f, false, false, false, 2 }
	};
	for (int32 Step = 0; Step < 100; ++Step)
	{
		const size_t Selected = FDecisionModel::SelectIndex(
			Options,
			UE_ARRAY_COUNT(Options),
			EDifficulty::Nightmare,
			static_cast<float>(Step) / 100.0f
		);
		TestEqual(TEXT("Nightmare uses the stable best option"), Selected, static_cast<size_t>(1));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHexBotExposureScoringTest,
	"OtherBios.BotAI.DecisionModel.ExposureScoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FHexBotExposureScoringTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FDecisionFeatures SafeAttack;
	SafeAttack.Action = EActionKind::Attack;
	SafeAttack.ImmediateGain = 0.70f;
	SafeAttack.TargetValue = 0.80f;
	SafeAttack.Exposure = 0.10f;

	FDecisionFeatures SuicideAttack = SafeAttack;
	SuicideAttack.ImmediateGain = 0.80f;
	SuicideAttack.Exposure = 1.0f;

	TestTrue(
		TEXT("Ordeal prefers the safe trade over a tiny damage increase with lethal exposure"),
		FDecisionModel::Evaluate(SafeAttack, EDifficulty::Ordeal) >
		FDecisionModel::Evaluate(SuicideAttack, EDifficulty::Ordeal)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHexBotDecisionModelPropertyStressTest,
	"OtherBios.BotAI.DecisionModel.PropertyStress50k",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FHexBotDecisionModelPropertyStressTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FRandomStream Random(0x5EED1234);
	bool bForcedKillInvariantHeld = true;
	bool bNightmareMaximumHeld = true;
	bool bScoreMonotonicityHeld = true;

	for (int32 Trial = 0; Trial < 50000; ++Trial)
	{
		const int32 CandidateCount = Random.RandRange(2, 32);
		const int32 ForcedKillIndex = Random.RandRange(0, CandidateCount - 1);
		TArray<FDecisionOption> Options;
		Options.Reserve(CandidateCount);
		for (int32 Candidate = 0; Candidate < CandidateCount; ++Candidate)
		{
			Options.Add({
				static_cast<float>(Random.FRandRange(-1000.0, 1000.0)),
				Candidate == ForcedKillIndex,
				false,
				false,
				static_cast<uint32>(Candidate + 1)
			});
		}

		const size_t WarmUpSelection = FDecisionModel::SelectIndex(
			Options.GetData(),
			static_cast<size_t>(Options.Num()),
			EDifficulty::WarmUp,
			Random.FRand()
		);
		bForcedKillInvariantHeld &= WarmUpSelection == static_cast<size_t>(ForcedKillIndex);

		Options[ForcedKillIndex].bSecuresKill = false;
		int32 ExpectedMaximum = 0;
		for (int32 Candidate = 1; Candidate < CandidateCount; ++Candidate)
		{
			const FDecisionOption& Current = Options[Candidate];
			const FDecisionOption& Expected = Options[ExpectedMaximum];
			if (Current.Utility > Expected.Utility ||
				(Current.Utility == Expected.Utility && Current.StableKey < Expected.StableKey))
			{
				ExpectedMaximum = Candidate;
			}
		}
		const size_t NightmareSelection = FDecisionModel::SelectIndex(
			Options.GetData(),
			static_cast<size_t>(Options.Num()),
			EDifficulty::Nightmare,
			Random.FRand()
		);
		bNightmareMaximumHeld &= NightmareSelection == static_cast<size_t>(ExpectedMaximum);

		FDecisionFeatures Base;
		Base.Action = EActionKind::Attack;
		Base.ImmediateGain = Random.FRand();
		Base.TargetValue = Random.FRand();
		Base.FutureGain = Random.FRand();
		Base.Exposure = Random.FRand();
		Base.ResourceCost = Random.FRand();
		Base.Overcommit = Random.FRand();
		const float BaseScore = FDecisionModel::Evaluate(Base, EDifficulty::Ordeal);

		FDecisionFeatures MoreGain = Base;
		MoreGain.ImmediateGain = FMath::Min(1.0f, Base.ImmediateGain + 0.05f);
		MoreGain.TargetValue = FMath::Min(1.0f, Base.TargetValue + 0.05f);
		MoreGain.FutureGain = FMath::Min(1.0f, Base.FutureGain + 0.05f);
		FDecisionFeatures MoreRisk = Base;
		MoreRisk.Exposure = FMath::Min(1.0f, Base.Exposure + 0.05f);
		MoreRisk.ResourceCost = FMath::Min(1.0f, Base.ResourceCost + 0.05f);
		MoreRisk.Overcommit = FMath::Min(1.0f, Base.Overcommit + 0.05f);
		bScoreMonotonicityHeld &=
			FDecisionModel::Evaluate(MoreGain, EDifficulty::Ordeal) + KINDA_SMALL_NUMBER >= BaseScore &&
			FDecisionModel::Evaluate(MoreRisk, EDifficulty::Ordeal) <= BaseScore + KINDA_SMALL_NUMBER;
	}

	TestTrue(TEXT("50k randomized sets preserve forced kills"), bForcedKillInvariantHeld);
	TestTrue(TEXT("50k randomized sets preserve the exact Nightmare maximum"), bNightmareMaximumHeld);
	TestTrue(TEXT("50k randomized feature sets preserve score monotonicity"), bScoreMonotonicityHeld);

	const FDecisionOption InvalidCriticalOptions[] =
	{
		{ std::numeric_limits<float>::quiet_NaN(), true, true, true, 1 },
		{ 1.0f, false, false, false, 2 }
	};
	TestEqual(
		TEXT("A non-finite critical candidate cannot poison eligibility"),
		FDecisionModel::SelectIndex(
			InvalidCriticalOptions,
			UE_ARRAY_COUNT(InvalidCriticalOptions),
			EDifficulty::Nightmare,
			0.0f
		),
		static_cast<size_t>(1)
	);
	return true;
}

#endif
