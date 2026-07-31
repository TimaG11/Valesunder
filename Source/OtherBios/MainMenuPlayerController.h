#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MainMenuPlayerController.generated.h"

class UAudioComponent;
class USoundBase;
class UUserWidget;

UCLASS()
class OTHERBIOS_API AMainMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Main Menu|Audio")
	void StartMenuMusic();

	UFUNCTION(BlueprintCallable, Category = "Main Menu|Audio")
	void StopMenuMusic(float FadeOutTime = 0.5f);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Main Menu")
	TSubclassOf<UUserWidget> MainMenuWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Main Menu")
	FName MainMenuLevelName = TEXT("L_MainMenu");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Main Menu|Audio")
	USoundBase* MenuMusic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Main Menu|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MenuMusicVolume = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Main Menu|Audio", meta = (ClampMin = "0.0"))
	float MenuMusicFadeInTime = 1.0f;

	UPROPERTY()
	UUserWidget* MainMenuWidget = nullptr;

	UPROPERTY()
	UAudioComponent* MenuMusicComponent = nullptr;
};