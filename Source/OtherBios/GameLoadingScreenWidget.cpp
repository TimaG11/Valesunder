#include "GameLoadingScreenWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"

void UGameLoadingScreenWidget::ConfigureForLoad(
	EGameLoadingScreenType InScreenType,
	const FString& InMapName,
	const FText& InTitleOverride,
	const FText& InStatusOverride
)
{
	ScreenType = InScreenType;
	MapName = InMapName;
	TitleOverride = InTitleOverride;
	StatusOverride = InStatusOverride;

	RefreshVisuals();
}

void UGameLoadingScreenWidget::SetRuntimeLoadingProgress(
	const FText& InStatusText,
	float InProgress,
	bool bInUseMarquee
)
{
	if (LoadingStatusText)
	{
		LoadingStatusText->SetText(InStatusText);
	}

	if (LoadingProgressBar)
	{
		LoadingProgressBar->SetIsMarquee(bInUseMarquee);
		LoadingProgressBar->SetPercent(FMath::Clamp(InProgress, 0.0f, 1.0f));
	}
}

void UGameLoadingScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshVisuals();
}

void UGameLoadingScreenWidget::RefreshVisuals()
{
	if (LoadingTitleText)
	{
		LoadingTitleText->SetText(TitleOverride.IsEmpty() ? GetDefaultTitle() : TitleOverride);
	}

	if (LoadingStatusText)
	{
		LoadingStatusText->SetText(StatusOverride.IsEmpty() ? GetDefaultStatus() : StatusOverride);
	}

	if (LoadingMapText)
	{
		const bool bCanShowMapName = bShowMapName && !MapName.IsEmpty();
		LoadingMapText->SetText(bCanShowMapName ? FText::FromString(MapName) : FText::GetEmpty());
		LoadingMapText->SetVisibility(bCanShowMapName ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (LoadingProgressBar)
	{
		LoadingProgressBar->SetIsMarquee(bUseMarqueeProgressBar);
		LoadingProgressBar->SetPercent(0.0f);
	}

	if (BattleOnlyRoot)
	{
		BattleOnlyRoot->SetVisibility(
			ScreenType == EGameLoadingScreenType::Battle
				? ESlateVisibility::SelfHitTestInvisible
				: ESlateVisibility::Collapsed
		);
	}

	if (MainMenuOnlyRoot)
	{
		MainMenuOnlyRoot->SetVisibility(
			ScreenType == EGameLoadingScreenType::MainMenu
				? ESlateVisibility::SelfHitTestInvisible
				: ESlateVisibility::Collapsed
		);
	}
}

FText UGameLoadingScreenWidget::GetDefaultTitle() const
{
	switch (ScreenType)
	{
	case EGameLoadingScreenType::Battle:
		return NSLOCTEXT("GameLoading", "BattleTitle", "PREPARING FOR BATTLE");

	case EGameLoadingScreenType::MainMenu:
		return NSLOCTEXT("GameLoading", "MainMenuTitle", "LOADING");

	case EGameLoadingScreenType::Automatic:
	case EGameLoadingScreenType::General:
	default:
		return NSLOCTEXT("GameLoading", "GeneralTitle", "LOADING");
	}
}

FText UGameLoadingScreenWidget::GetDefaultStatus() const
{
	switch (ScreenType)
	{
	case EGameLoadingScreenType::Battle:
		return NSLOCTEXT("GameLoading", "BattleStatus", "Loading battlefield and units...");

	case EGameLoadingScreenType::MainMenu:
		return NSLOCTEXT("GameLoading", "MainMenuStatus", "Preparing main menu...");

	case EGameLoadingScreenType::Automatic:
	case EGameLoadingScreenType::General:
	default:
		return NSLOCTEXT("GameLoading", "GeneralStatus", "Preparing game...");
	}
}
