#include "AccountSaveGame.h"

#include "ArmyBuilderWidget.h"

void UAccountSaveGame::Serialize(FArchive& Ar)
{
	// Existing GameInstance code does not need to know about faction effects.
	// SaveGameToSlot calls Serialize, so capture the current synergy snapshot
	// immediately before Unreal serializes this save object.
	if (Ar.IsSaving())
	{
		UArmyBuilderWidget::ExportPersistentFactionEffects(FactionEffects);
		UArmyBuilderWidget::ExportPersistentDeployment(SavedDeploymentSlots);
		SaveVersion = FMath::Max(SaveVersion, 4);
	}

	Super::Serialize(Ar);

	// LoadGameFromSlot also goes through Serialize. Restore the snapshot early;
	// ImportPersistentArmyAndCoins will then validate/rebuild it from the army.
	if (Ar.IsLoading())
	{
		UArmyBuilderWidget::ImportPersistentFactionEffects(FactionEffects);
		UArmyBuilderWidget::ImportPersistentDeployment(SavedDeploymentSlots);
	}
}
