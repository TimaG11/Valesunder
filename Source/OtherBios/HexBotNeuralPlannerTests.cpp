#include "HexBotNeuralPlanner.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace
{
	using namespace OtherBios::BotNeural;

	FTopology MakeIrregularTopology(int32 Radius, int32 HoleModulo)
	{
		FTopology Topology;
		for (int32 Q = -Radius; Q <= Radius; ++Q)
		{
			for (int32 R = -Radius; R <= Radius; ++R)
			{
				const int32 S = -Q - R;
				if (FMath::Max3(FMath::Abs(Q), FMath::Abs(R), FMath::Abs(S)) <= Radius &&
					(HoleModulo <= 1 || FMath::Abs(Q * 31 + R * 17) % HoleModulo != 0))
				{
					Topology.Cells.push_back({ Q, R });
				}
			}
		}
		Topology.RebuildIndex();
		return Topology;
	}

	FUnit MakeUnit(std::uint32_t Key, std::uint8_t Team, FCoord Cell, int Health, int Damage)
	{
		FUnit Unit;
		Unit.StableKey = Key;
		Unit.Team = Team;
		Unit.Cell = Cell;
		Unit.Health = Health;
		Unit.MaxHealth = Health;
		Unit.Damage = Damage;
		Unit.AttackRange = 1;
		Unit.MovementRange = 4;
		return Unit;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHexBotNeuralFootprintTest,
	"OtherBios.BotAI.NeuralPlanner.Footprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBotNeuralFootprintTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("Policy/value learned parameter count"), FPolicyValueNetwork::ParameterCount(), static_cast<size_t>(1666));
	TestTrue(TEXT("Float weights remain under 8 KiB"),
		FPolicyValueNetwork::ParameterCount() * sizeof(float) < static_cast<size_t>(8 * 1024));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHexBotNeuralDynamicStateTest,
	"OtherBios.BotAI.NeuralPlanner.DynamicTopologyAndArmy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBotNeuralDynamicStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FTopology Topology = MakeIrregularTopology(11, 13);
	FState State;
	State.Topology = &Topology;
	State.MaxActionPoints = 9;
	State.ActionPoints = 7;
	State.MoveActionPointCost = 2;
	State.AttackActionPointCost = 3;
	for (int32 Index = 0; Index < 31; ++Index)
	{
		const FCoord Cell = Topology.Cells[static_cast<size_t>(Index * 3) % Topology.Cells.size()];
		State.Units.push_back(MakeUnit(
			static_cast<std::uint32_t>(Index + 1),
			static_cast<std::uint8_t>(Index < 13 ? 0 : 1),
			Cell,
			70 + Index * 3,
			15 + Index));
	}

	const auto Features = FSimulator::StateFeatures(State, 0);
	bool bFinite = true;
	for (float Feature : Features) bFinite &= FMath::IsFinite(Feature);
	TestTrue(TEXT("Aggregate features accept an irregular map and asymmetric 13v18 army"), bFinite);

	FSearchSettings Settings;
	Settings.Depth = 5;
	Settings.TopK = 7;
	Settings.NodeBudget = 420;
	Settings.TimeBudgetMilliseconds = 0.0;
	FNeuralSearchPlanner Planner;
	const FSearchResult Result = Planner.FindBestAction(State, State.SideToMove, Settings);
	TestTrue(TEXT("Planner returns a legal candidate on an irregular large state"), Result.bHasAction);
	TestTrue(TEXT("Planner respects the node cap"), Result.NodesVisited <= Settings.NodeBudget);
	TestTrue(TEXT("Candidate expansion is bounded"), Result.RootExpandedActions <= Settings.TopK + Settings.HeuristicSafetyCandidates + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHexBotNeuralHardInvariantTest,
	"OtherBios.BotAI.NeuralPlanner.ForcedKillAndAtomicValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHexBotNeuralHardInvariantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTopology Topology;
	Topology.Cells = { {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0} };
	Topology.RebuildIndex();
	FState State;
	State.Topology = &Topology;
	State.ActionPoints = State.MaxActionPoints = 4;
	State.Units = {
		MakeUnit(1, 0, {0, 0}, 100, 50),
		MakeUnit(2, 1, {1, 0}, 20, 10),
		MakeUnit(3, 1, {4, 0}, 100, 10)
	};

	FSearchSettings Settings;
	Settings.Depth = 5;
	Settings.TopK = 3;
	Settings.NodeBudget = 1;
	Settings.TimeBudgetMilliseconds = 0.001;
	FNeuralSearchPlanner Planner;
	const FSearchResult Result = Planner.FindBestAction(State, 0, Settings);
	TestTrue(TEXT("A forced kill survives an intentionally exhausted budget"),
		Result.bHasAction && Result.Action.Kind == EActionKind::Attack && Result.Action.bSecuresKill &&
		Result.Action.TargetIndex == 1);

	FAction Invalid;
	Invalid.Kind = EActionKind::Attack;
	Invalid.ActorIndex = 0;
	Invalid.TargetIndex = 2;
	Invalid.ActionPointCost = 1;
	const int BeforeAp = State.ActionPoints;
	const int BeforeHealth = State.Units[2].Health;
	TestFalse(TEXT("Out-of-range attack is rejected"), FSimulator::ApplyAction(State, Invalid));
	TestEqual(TEXT("Rejected attack cannot spend AP"), State.ActionPoints, BeforeAp);
	TestEqual(TEXT("Rejected attack cannot mutate target"), State.Units[2].Health, BeforeHealth);
	return true;
}

#endif
