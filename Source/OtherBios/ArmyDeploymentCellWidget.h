#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HexUnitActor.h"
#include "ArmyDeploymentCellWidget.generated.h"

class UArmyDeploymentWidget;
class UButton;
class UImage;
class UTextBlock;
class UWidget;

UCLASS()
class OTHERBIOS_API UArmyDeploymentCellWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Army Deployment Cell")
	void InitializeCell(UArmyDeploymentWidget* InOwnerDeploymentWidget, int32 InQ, int32 InR, bool bInAllowedCell);

	UFUNCTION(BlueprintCallable, Category = "Army Deployment Cell")
	void SetPlacedUnit(TSubclassOf<AHexUnitActor> InUnitClass, int32 InUnitIndex);

	UFUNCTION(BlueprintCallable, Category = "Army Deployment Cell")
	void SetSelectedCell(bool bInSelectedCell);

	UFUNCTION(BlueprintCallable, Category = "Army Deployment Cell")
	void SetDeploymentLineCell(bool bInDeploymentLineCell);

	UFUNCTION(BlueprintCallable, Category = "Army Deployment Cell")
	void SetRuntimeCellSize(float InCellWidth, float InCellHeight);

	UFUNCTION(BlueprintCallable, Category = "Army Deployment Cell")
	void SetPortraitSize(float InPortraitSize);

	UFUNCTION(BlueprintPure, Category = "Army Deployment Cell")
	int32 GetQ() const { return Q; }

	UFUNCTION(BlueprintPure, Category = "Army Deployment Cell")
	int32 GetR() const { return R; }

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CellButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UImage* UnitPortraitImage = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* UnitNameTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* CoordTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* BlockedOverlay = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* SelectedOverlay = nullptr;

	// Optional in WBP_ArmyDeploymentCell. Highlights the last allowed deployment column/edge.
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* DeploymentLineOverlay = nullptr;

	// This fixes the common WBP problem where the root SizeBox is accidentally left at 36x36.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment Cell|Layout", meta = (ClampMin = "1.0"))
	float RuntimeCellWidth = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment Cell|Layout", meta = (ClampMin = "1.0"))
	float RuntimeCellHeight = 62.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment Cell|Layout", meta = (ClampMin = "1.0"))
	float RuntimePortraitSize = 42.0f;

	// Keep these false for the real UI. Turning them on is only useful while debugging coordinates.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment Cell|Debug")
	bool bShowDebugCoords = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment Cell|Debug")
	bool bShowUnitNameOnGrid = false;

	// If true, blocked cells show an extra overlay. Usually false looks cleaner, because the cell opacity already shows disabled cells.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment Cell|Visual")
	bool bShowBlockedOverlay = false;

private:
	UPROPERTY()
	UArmyDeploymentWidget* OwnerDeploymentWidget = nullptr;

	UPROPERTY()
	TSubclassOf<AHexUnitActor> PlacedUnitClass;

	int32 Q = 0;
	int32 R = 0;
	int32 PlacedUnitIndex = INDEX_NONE;

	bool bAllowedCell = false;
	bool bSelectedCell = false;
	bool bDeploymentLineCell = false;

	UFUNCTION()
	void HandleCellClicked();

	void BindButton();
	void ApplyTransparentButtonStyle();
	void ApplyRuntimeLayout();
	void RefreshVisuals();
};
