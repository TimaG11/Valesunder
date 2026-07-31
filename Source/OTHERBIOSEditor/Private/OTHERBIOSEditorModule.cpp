#include "Modules/ModuleManager.h"

#include "HexUnitSpawnInfoCustomization.h"
#include "PropertyEditorModule.h"

class FOTHERBIOSEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyEditorModule =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyEditorModule.RegisterCustomPropertyTypeLayout(
			TEXT("HexUnitSpawnInfo"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FHexUnitSpawnInfoCustomization::MakeInstance)
		);

		PropertyEditorModule.NotifyCustomizationModuleChanged();
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyEditorModule =
				FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

			PropertyEditorModule.UnregisterCustomPropertyTypeLayout(TEXT("HexUnitSpawnInfo"));
			PropertyEditorModule.NotifyCustomizationModuleChanged();
		}
	}
};

IMPLEMENT_MODULE(FOTHERBIOSEditorModule, OTHERBIOSEditor)
