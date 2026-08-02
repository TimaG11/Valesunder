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
class UImage;
class UPanelWidget;
class UTextBlock;
class UImage;
class UTexture2D;

UCLASS()
class OTHERBIOS_API UArmyDeploymentWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Army Deployment")
	void InitializeDeployment(
		UArmyBuilderWidget* InOwnerArmyBuilder,
		const TArray<TSubclassOf<AHexUnitActor>>& InUnitClasses,
		const TArray<FArmyBuilderDeploymentSlot>& InSavedDeploymentSlots
	);

	UFUNCTION(BlueprintCallable, Category = "Army Deployment")
	void SelectUnitForDeployment(int32 UnitIndex);

	UFUNCTION(BlueprintCallable, Category = "Army Deployment")
	void HandleDeploymentCellClicked(int32 Q, int32 R);

	UFUNCTION(BlueprintPure, Category = "Army Deployment|Progression")
	FArmyBuilderUnitProgress GetDeploymentUnitProgressAt(int32 UnitIndex) const;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	UPanelWidget* DeploymentUnitsPanel = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UCanvasPanel* DeploymentGridPanel = nullptr;

	// Background photo of the battlefield. In WBP_ArmyDeployment the Image must be
	// named exactly DeploymentFieldImage and must fill DeploymentGridPanel.
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* DeploymentFieldImage = nullptr;

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

	// Canonical coordinates represented by the imported field photo.
	// Defaults describe 14 columns x 9 rows. Only the four left columns are allowed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	int32 DisplayQMin = -7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	int32 DisplayQMax = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	int32 DisplayRMin = -4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	int32 DisplayRMax = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid", meta = (ClampMin = "1"))
	int32 DisplayCoordStep = 1;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid", meta = (ClampMin = "1.0"))
	float GridUnitPortraitSize = 42.0f;

	// PHOTO MODE: runtime hit cells are positioned by normalized anchors over the image,
	// so the photo and clickable cells stay aligned when the window is resized.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo")
	bool bUsePhotoAlignedGrid = true;

	// Optional. You can either assign the texture here or set it directly on
	// DeploymentFieldImage in the UMG designer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo")
	UTexture2D* DeploymentFieldTexture = nullptr;

	// Normalized bounds occupied by hex centres in the source photo. Tune these four
	// values only if the invisible click cells are a few pixels off the orange hexes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhotoGridLeft = 0.047f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhotoGridRight = 0.953f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhotoGridTop = 0.073f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhotoGridBottom = 0.910f;

	// Your field photo uses horizontally staggered rows.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo")
	bool bPhotoStaggerOddRowsToRight = true;


	// PHOTO GRID v3. These properties intentionally have NEW names so old serialized
	// WBP_ArmyDeployment values (DisplayQMin/DisplayCoordStep, etc.) cannot resurrect
	// the obsolete sparse procedural grid. Photo mode ignores the legacy Display* settings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo Grid", meta = (ClampMin = "1", ClampMax = "40"))
	int32 PhotoColumnCount = 14;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo Grid", meta = (ClampMin = "1", ClampMax = "30"))
	int32 PhotoRowCount = 11;

	// Q/R assigned to the top-left logical photo cell. Columns increment Q, rows increment R.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo Grid")
	int32 PhotoQMin = -7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo Grid")
	int32 PhotoRMin = -4;

	// Legacy v3 setting. Photo-grid v4 intentionally allows placement on EVERY visible photo cell.
	// Kept only so existing Blueprint assets do not lose serialized data.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo Grid|Legacy", meta = (ClampMin = "1", ClampMax = "20"))
	int32 PhotoAllowedPlayerColumns = 4;

	// The supplied field image alternates 13-cell and 14-cell rows; the top row is the short row.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo Grid")
	bool bPhotoEvenRowsAreShort = true;

	// Invisible hitbox size relative to the distance between neighbouring photo-cell centres.
	// Keep below 1.0 so adjacent hitboxes do not overlap.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo Grid", meta = (ClampMin = "0.25", ClampMax = "1.0"))
	float PhotoHitboxScale = 0.98f;

	// Size of the runtime portrait drawn directly over the photo grid. This is independent
	// from the old WBP_ArmyDeploymentCell portrait slot, so Blueprint clipping cannot shrink it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Photo Grid", meta = (ClampMin = "20.0", ClampMax = "100.0"))
	float PhotoUnitPortraitSize = 48.0f;

	// Legacy procedural preview mode is kept as a fallback.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Grid")
	bool bUseStaggeredPreviewGrid = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Rules")
	int32 AllowedDeploymentMaxQ = -4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Rules")
	TArray<FArmyBuilderDeploymentSlot> AllowedDeploymentCoords;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Army Deployment|Rules")
	bool bAutoFillEmptyDeployment = true;

private:
	// Runtime background is created directly inside DeploymentGridPanel so a designer SizeBox/Image
	// cannot cover or discard the assigned texture.
	UPROPERTY(Transient)
	UImage* RuntimeDeploymentFieldImage = nullptr;

	void EnsureDeploymentFieldBackground();

	UPROPERTY()
	UArmyBuilderWidget* OwnerArmyBuilder = nullptr;

	UPROPERTY()
	TArray<TSubclassOf<AHexUnitActor>> UnitClasses;

	UPROPERTY()
	TArray<FArmyBuilderDeploymentSlot> DeploymentSlots;

	UPROPERTY()
	TArray<UArmyDeploymentCellWidget*> CellWidgets;

	// Portraits are runtime siblings of the invisible cell hitboxes. This avoids the
	// tiny/clipped portrait slot from the legacy WBP_ArmyDeploymentCell.
	UPROPERTY(Transient)
	TArray<UImage*> RuntimePhotoPortraitImages;

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
