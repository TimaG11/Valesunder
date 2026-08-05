#include "AccountSaveGame.h"

#include "ArmyBuilderWidget.h"

namespace
{
	constexpr int32 ArmyPresetCount = 5;

	void CopyActivePresetToLegacyFields(UAccountSaveGame& SaveGame)
	{
		SaveGame.ActiveArmyPresetIndex = FMath::Clamp(
			SaveGame.ActiveArmyPresetIndex,
			0,
			ArmyPresetCount - 1
		);

		if (!SaveGame.ArmyPresets.IsValidIndex(SaveGame.ActiveArmyPresetIndex))
		{
			SaveGame.SavedArmyUnitClasses.Reset();
			SaveGame.SavedDeploymentSlots.Reset();
			SaveGame.FactionEffects.Reset();
			return;
		}

		const FAccountArmyPresetRecord& ActivePreset = SaveGame.ArmyPresets[SaveGame.ActiveArmyPresetIndex];
		SaveGame.SavedArmyUnitClasses = ActivePreset.UnitClasses;
		SaveGame.SavedDeploymentSlots = ActivePreset.DeploymentSlots;
		SaveGame.FactionEffects = ActivePreset.FactionEffects;
	}
}

void UAccountSaveGame::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		// Keep all five templates in this save object. The legacy single-army fields
		// are also filled from the active template so old GameInstance code and older
		// game builds continue to receive the army selected for battle.
		UArmyBuilderWidget::ExportPersistentArmyPresets(ArmyPresets, ActiveArmyPresetIndex);
		CopyActivePresetToLegacyFields(*this);
		SaveVersion = FMath::Max(SaveVersion, 5);
	}

	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		// Migration from v4 and older: the former single army becomes template 1.
		if (ArmyPresets.IsEmpty())
		{
			ArmyPresets.SetNum(ArmyPresetCount);
			ArmyPresets[0].UnitClasses = SavedArmyUnitClasses;
			ArmyPresets[0].DeploymentSlots = SavedDeploymentSlots;
			ArmyPresets[0].FactionEffects = FactionEffects;
			ActiveArmyPresetIndex = 0;
		}
		else
		{
			ArmyPresets.SetNum(ArmyPresetCount);
			ActiveArmyPresetIndex = FMath::Clamp(ActiveArmyPresetIndex, 0, ArmyPresetCount - 1);
		}

		CopyActivePresetToLegacyFields(*this);

		// Restore all templates immediately. The existing GameInstance may still call
		// ImportPersistentArmyAndCoins with the legacy fields afterwards; those fields
		// now deliberately contain the same active template.
		UArmyBuilderWidget::ImportPersistentArmyPresets(ArmyPresets, ActiveArmyPresetIndex);
		UArmyBuilderWidget::ImportPersistentFactionEffects(FactionEffects);
		UArmyBuilderWidget::ImportPersistentDeployment(SavedDeploymentSlots);
	}
}
