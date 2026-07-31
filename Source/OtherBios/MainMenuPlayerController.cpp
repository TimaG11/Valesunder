#include "MainMenuPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

void AMainMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	const FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(this, true);
	if (!MainMenuLevelName.IsNone() && CurrentLevelName != MainMenuLevelName.ToString())
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		return;
	}

	StartMenuMusic();

	if (!MainMenuWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("MainMenuWidgetClass is not set in MainMenuPlayerController."));
		return;
	}

	MainMenuWidget = CreateWidget<UUserWidget>(this, MainMenuWidgetClass);
	if (!MainMenuWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create main menu widget."));
		return;
	}

	MainMenuWidget->AddToViewport(0);

	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}

void AMainMenuPlayerController::StartMenuMusic()
{
	if (!MenuMusic)
	{
		UE_LOG(LogTemp, Warning, TEXT("MenuMusic is not set in MainMenuPlayerController."));
		return;
	}

	if (MenuMusicComponent && MenuMusicComponent->IsPlaying())
	{
		return;
	}

	MenuMusicComponent = UGameplayStatics::CreateSound2D(
		this,
		MenuMusic,
		MenuMusicVolume,
		1.0f,
		0.0f,
		nullptr,
		false,
		false
	);

	if (!MenuMusicComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create menu music component."));
		return;
	}

	MenuMusicComponent->bIsUISound = true;

	if (MenuMusicFadeInTime > 0.0f)
	{
		MenuMusicComponent->FadeIn(MenuMusicFadeInTime, MenuMusicVolume, 0.0f);
	}
	else
	{
		MenuMusicComponent->Play(0.0f);
	}
}

void AMainMenuPlayerController::StopMenuMusic(float FadeOutTime)
{
	if (!MenuMusicComponent || !MenuMusicComponent->IsPlaying())
	{
		return;
	}

	if (FadeOutTime > 0.0f)
	{
		MenuMusicComponent->FadeOut(FadeOutTime, 0.0f);
	}
	else
	{
		MenuMusicComponent->Stop();
	}
}