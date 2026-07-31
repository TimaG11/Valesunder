#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameLoadingScreenWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UWidget;

UENUM(BlueprintType)
enum class EGameLoadingScreenType : uint8
{
	Automatic UMETA(DisplayName = "Automatic"),
	General   UMETA(DisplayName = "General"),
	MainMenu  UMETA(DisplayName = "Main Menu"),
	Battle    UMETA(DisplayName = "Battle")
};

/**
 * Base class for loading screen UMG widgets used by UGameLoadingGameInstance.
 *
 * The widget is rendered by MoviePlayer while the game thread is blocked by OpenLevel.
 * Keep this widget visually simple: static images, text and a progress bar.
 */
UCLASS()
class OTHERBIOS_API UGameLoadingScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ConfigureForLoad(
		EGameLoadingScreenType InScreenType,
		const FString& InMapName,
		const FText& InTitleOverride = FText::GetEmpty(),
		const FText& InStatusOverride = FText::GetEmpty()
	);

	// Used by GameInstance after map loading while PSOs are still compiling.
	void SetRuntimeLoadingProgress(
		const FText& InStatusText,
		float InProgress,
		bool bInUseMarquee
	);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* LoadingTitleText = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* LoadingStatusText = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* LoadingMapText = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UProgressBar* LoadingProgressBar = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* BattleOnlyRoot = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* MainMenuOnlyRoot = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading Screen")
	bool bUseMarqueeProgressBar = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading Screen")
	bool bShowMapName = false;

private:
	EGameLoadingScreenType ScreenType = EGameLoadingScreenType::General;
	FString MapName;
	FText TitleOverride;
	FText StatusOverride;

	void RefreshVisuals();
	FText GetDefaultTitle() const;
	FText GetDefaultStatus() const;
};
