#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ArmyBuilderWidget.h"
#include "HexUnitActor.h"
#include "ArmyDeploymentWidget.generated.h"

class UArmyDeploymentCellWidget;
class UArmyUnitCardWidget;
class UButton;
class UCanvasPanel;
class UPanelWidget;
class UTextBlock;

UCLASS()
class OTHERBIOS_API UArmyDeploymentWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Army Deployment")
	void InitializeDeployment(UArmyBuilderWidget* InOwnerArmyBuilder, const TArray<TSubclassOf<AHexUnitActor>>& InUnitClasses, const TArray<FArmyBuilderDeploymentSlot>& InSavedDeploymentSlots);

	UFUNCTION(BlueprintCallable, Category = "Army Deployment")
	void SelectUnitForDeployment(int32 UnitIndex);

	UFUNCTION(BlueprintCallable, Category = "Army Deployment")
	void HandleDeploymentCellClicked(int32 Q, int32 R);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	UPanelWidget* DeploymentUnitsPanel = nullptr;

	// In WBP_ArmyDeployment this must be a CanvasPanel named exactly DeploymentGridPanel.
	UPROPERTY(meta = (BindWidgetOptional))
	UCanvasPanel* DeploymentGridPanel = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* DeploymentHintTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* EnemyDirectionTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* PlayerDirectionTextBlock = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* SaveDeploymentButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ClearDeploymentButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CloseDeploymentButton = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Widgets")
	TSubclassOf<UArmyUnitCardWidget> DeploymentUnitCardWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Widgets")
	TSubclassOf<UArmyDeploymentCellWidget> DeploymentCellWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	int32 DisplayQMin = -8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	int32 DisplayQMax = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	int32 DisplayRMin = -4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	int32 DisplayRMax = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid", meta = (ClampMin = "1"))
	int32 DisplayCoordStep = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	float HexCellWidth = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	float HexCellHeight = 62.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	float HexHorizontalSpacing = 56.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	float HexVerticalSpacing = 44.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	float GridPaddingX = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	float GridPaddingY = 10.0f;

	// Runtime size for portraits inside tiny deployment cells. Prevents large portrait textures from overflowing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid", meta = (ClampMin = "1.0"))
	float GridUnitPortraitSize = 42.0f;

	// Keep true for a readable UI grid: columns are staggered like a proper flat-top hex map.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	bool bUseStaggeredPreviewGrid = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Rules")
	int32 AllowedDeploymentMaxQ = -2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Rules")
	TArray<FArmyBuilderDeploymentSlot> AllowedDeploymentCoords;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Rules")
	bool bAutoFillEmptyDeployment = true;

private:
	UPROPERTY()
	UArmyBuilderWidget* OwnerArmyBuilder = nullptr;

	UPROPERTY()
	TArray<TSubclassOf<AHexUnitActor>> UnitClasses;

	UPROPERTY()
	TArray<FArmyBuilderDeploymentSlot> DeploymentSlots;

	UPROPERTY()
	TArray<UArmyDeploymentCellWidget*> CellWidgets;

	UPROPERTY()
	TArray<UArmyUnitCardWidget*> DeploymentUnitCardWidgets;

	int32 SelectedUnitIndex = INDEX_NONE;

	UFUNCTION()
	void HandleSaveClicked();

	UFUNCTION()
	void HandleClearClicked();

	UFUNCTION()
	void HandleCloseClicked();

	void BindButtons();
	void RebuildUnitCards();
	void RebuildGrid();
	void RefreshAllVisuals();
	void RefreshHintText();
	void AutoFillDeploymentIfNeeded();

	bool IsCoordAllowed(int32 Q, int32 R) const;
	bool IsCoordUsed(int32 Q, int32 R, int32 IgnoreUnitIndex = INDEX_NONE) const;
	bool IsDeploymentLineCell(int32 Q, int32 R) const;

	int32 FindSlotIndexForUnit(int32 UnitIndex) const;
	int32 FindUnitIndexAtCoord(int32 Q, int32 R) const;
	bool HasCompleteDeployment() const;

	TArray<FIntPoint> GetAllowedCoordsInDisplayOrder() const;
};
