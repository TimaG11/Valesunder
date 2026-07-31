#include "GameLoadingGameInstance.h"

#include "AssetCompilingManager.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/PrimitiveComponent.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "MoviePlayer.h"
#include "RenderingThread.h"
#include "ShaderPipelineCache.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	void PrepareLoadingScreenTextures(UUserWidget* LoadingWidget)
	{
		if (!LoadingWidget || !LoadingWidget->WidgetTree)
		{
			return;
		}

		TArray<UWidget*> AllWidgets;
		LoadingWidget->WidgetTree->GetAllWidgets(AllWidgets);

		int32 PreparedTextureCount = 0;
		for (UWidget* Widget : AllWidgets)
		{
			UImage* ImageWidget = Cast<UImage>(Widget);
			if (!ImageWidget)
			{
				continue;
			}

			UTexture2D* Texture = Cast<UTexture2D>(ImageWidget->GetBrush().GetResourceObject());
			if (!Texture)
			{
				continue;
			}

			Texture->SetForceMipLevelsToBeResident(30.0f);
			Texture->WaitForStreaming();

			if (!Texture->GetResource())
			{
				Texture->UpdateResource();
			}

			++PreparedTextureCount;
		}

		if (PreparedTextureCount > 0)
		{
			FlushRenderingCommands();
		}
	}

	void SetConsoleVariableInt(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByGameSetting);
		}
	}
}

void UGameLoadingGameInstance::BindButtonClickSounds(UUserWidget* RootWidget)
{
	if (!RootWidget || !RootWidget->WidgetTree)
	{
		return;
	}

	TArray<UWidget*> AllWidgets;
	RootWidget->WidgetTree->GetAllWidgets(AllWidgets);

	for (UWidget* Widget : AllWidgets)
	{
		UButton* Button = Cast<UButton>(Widget);
		if (!Button)
		{
			continue;
		}

		Button->OnClicked.RemoveDynamic(this, &UGameLoadingGameInstance::HandleUIButtonClicked);
		Button->OnClicked.AddDynamic(this, &UGameLoadingGameInstance::HandleUIButtonClicked);
	}
}

void UGameLoadingGameInstance::UnbindButtonClickSound(UButton* Button)
{
	if (!Button)
	{
		return;
	}

	Button->OnClicked.RemoveDynamic(this, &UGameLoadingGameInstance::HandleUIButtonClicked);
}

void UGameLoadingGameInstance::PlayUIButtonClickSound()
{
	if (!UIButtonClickSound)
	{
		return;
	}

	// Spawn a persistent UI sound so clicks that immediately call OpenLevel
	// (for example the result-screen Main Menu button) are not cut off by map travel.
	UGameplayStatics::SpawnSound2D(
		this,
		UIButtonClickSound,
		FMath::Max(0.0f, UIButtonClickVolume),
		1.0f,
		0.0f,
		nullptr,
		true,
		true
	);
}

void UGameLoadingGameInstance::HandleUIButtonClicked()
{
	PlayUIButtonClickSound();
}

void UGameLoadingGameInstance::Init()
{
	Super::Init();

	if (bEnablePersistentAccountProgression)
	{
		LoadAccountProgression();
	}
	else
	{
		// Static gameplay state can survive repeated PIE sessions inside one editor
		// process. Reset it once so disabled persistence really starts clean.
		UArmyBuilderWidget::ResetSessionAccountState();
		UE_LOG(LogTemp, Log, TEXT("Persistent account progression is disabled. Running with session-only progression."));
	}

	ApplySafePSOConsoleSettings();

	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UGameLoadingGameInstance::HandlePreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UGameLoadingGameInstance::HandlePostLoadMap);
}

void UGameLoadingGameInstance::Shutdown()
{
	if (bEnablePersistentAccountProgression && bAccountProgressionInitialized)
	{
		SaveAccountProgression();
	}

	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	CancelPostLoadWarmup();
	MovieLoadingWidget = nullptr;

	Super::Shutdown();
}

bool UGameLoadingGameInstance::LoadAccountProgression()
{
	bAccountProgressionInitialized = false;

	if (!bEnablePersistentAccountProgression)
	{
		UE_LOG(LogTemp, Log, TEXT("Account progression load skipped: persistence is disabled."));
		return false;
	}

	const FString SafeSlotName = AccountSaveSlot.IsEmpty()
		? FString(TEXT("AccountData"))
		: AccountSaveSlot;

	if (!UGameplayStatics::DoesSaveGameExist(SafeSlotName, AccountSaveUserIndex))
	{
		PersistentAccountUnitClassKeepAlive.Reset();
		UArmyBuilderWidget::ImportPersistentUnitProgress(TArray<FAccountUnitProgressRecord>());
		UArmyBuilderWidget::ImportPersistentArmyAndCoins(TArray<TSoftClassPtr<AHexUnitActor>>(), 0);
		bAccountProgressionInitialized = true;

		UE_LOG(LogTemp, Log, TEXT("No account save exists yet. A new one will be created after account changes. Slot=%s"),
			*SafeSlotName
		);
		return true;
	}

	USaveGame* LoadedObject = UGameplayStatics::LoadGameFromSlot(SafeSlotName, AccountSaveUserIndex);
	UAccountSaveGame* AccountSave = Cast<UAccountSaveGame>(LoadedObject);
	if (!AccountSave)
	{
		PersistentAccountUnitClassKeepAlive.Reset();
		UArmyBuilderWidget::ResetSessionAccountState();
		UE_LOG(LogTemp, Error, TEXT("Account progression load failed or save type is invalid. Session account state was cleared. Slot=%s"), *SafeSlotName);
		return false;
	}

	RebuildPersistentAccountClassKeepAlive(AccountSave);
	UArmyBuilderWidget::ImportPersistentUnitProgress(AccountSave->UnitProgress);
	UArmyBuilderWidget::ImportPersistentArmyAndCoins(AccountSave->SavedArmyUnitClasses, AccountSave->Coins);
	bAccountProgressionInitialized = true;

	UE_LOG(LogTemp, Log, TEXT("Account data loaded. Slot=%s Version=%d ProgressRecords=%d ArmyUnits=%d Coins=%d"),
		*SafeSlotName,
		AccountSave->SaveVersion,
		AccountSave->UnitProgress.Num(),
		AccountSave->SavedArmyUnitClasses.Num(),
		FMath::Max(0, AccountSave->Coins)
	);
	return true;
}

bool UGameLoadingGameInstance::SaveAccountProgression()
{
	if (!bEnablePersistentAccountProgression)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Account progression save skipped: persistence is disabled."));
		return false;
	}

	const FString SafeSlotName = AccountSaveSlot.IsEmpty()
		? FString(TEXT("AccountData"))
		: AccountSaveSlot;

	UAccountSaveGame* AccountSave = Cast<UAccountSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAccountSaveGame::StaticClass())
	);
	if (!AccountSave)
	{
		UE_LOG(LogTemp, Error, TEXT("Account progression save failed: could not create SaveGame object."));
		return false;
	}

	UArmyBuilderWidget::ExportPersistentUnitProgress(AccountSave->UnitProgress);
	UArmyBuilderWidget::ExportPersistentArmyAndCoins(AccountSave->SavedArmyUnitClasses, AccountSave->Coins);
	AccountSave->SaveVersion = 2;

	// Refresh hard references before the builder widget can be destroyed. This keeps
	// every class used by the saved roster/army alive for the rest of the session.
	RebuildPersistentAccountClassKeepAlive(AccountSave);

	const bool bSaved = UGameplayStatics::SaveGameToSlot(
		AccountSave,
		SafeSlotName,
		AccountSaveUserIndex
	);

	if (bSaved)
	{
		bAccountProgressionInitialized = true;
		UE_LOG(LogTemp, Log, TEXT("Account data saved. Slot=%s ProgressRecords=%d ArmyUnits=%d Coins=%d"),
			*SafeSlotName,
			AccountSave->UnitProgress.Num(),
			AccountSave->SavedArmyUnitClasses.Num(),
			AccountSave->Coins
		);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Account progression save failed. Slot=%s"), *SafeSlotName);
	}

	return bSaved;
}

void UGameLoadingGameInstance::RebuildPersistentAccountClassKeepAlive(const UAccountSaveGame* AccountSave)
{
	PersistentAccountUnitClassKeepAlive.Reset();

	if (!AccountSave)
	{
		return;
	}

	auto AddSoftClass = [this](const TSoftClassPtr<AHexUnitActor>& SoftUnitClass)
		{
			if (SoftUnitClass.IsNull())
			{
				return;
			}

			UClass* LoadedClass = SoftUnitClass.LoadSynchronous();
			if (!IsValid(LoadedClass) || !LoadedClass->IsChildOf(AHexUnitActor::StaticClass()))
			{
				UE_LOG(LogTemp, Warning, TEXT("Account keep-alive skipped missing unit class: %s"),
					*SoftUnitClass.ToSoftObjectPath().ToString());
				return;
			}

			PersistentAccountUnitClassKeepAlive.AddUnique(LoadedClass);
		};

	for (const FAccountUnitProgressRecord& Record : AccountSave->UnitProgress)
	{
		AddSoftClass(Record.UnitClass);
	}

	for (const TSoftClassPtr<AHexUnitActor>& SoftUnitClass : AccountSave->SavedArmyUnitClasses)
	{
		AddSoftClass(SoftUnitClass);
	}

	UE_LOG(LogTemp, Log, TEXT("Persistent account class keep-alive rebuilt: Classes=%d."),
		PersistentAccountUnitClassKeepAlive.Num());
}

void UGameLoadingGameInstance::PrepareBattleLoadingScreen()
{
	PendingScreenType = EGameLoadingScreenType::Battle;
	PendingTitleOverride = FText::GetEmpty();
	PendingStatusOverride = FText::GetEmpty();
}

void UGameLoadingGameInstance::CaptureBattleArmySnapshot()
{
	BattlePlayerArmyClasses = UArmyBuilderWidget::GetSavedPlayerArmyUnitClasses();
	BattleAvailableUnitClasses = UArmyBuilderWidget::GetSavedAvailableUnitClasses();
	BattleDeploymentSlots = UArmyBuilderWidget::GetSavedPlayerArmyDeploymentSlots();
	BattlePlayerProgress = UArmyBuilderWidget::GetSavedPlayerArmyUnitProgressList();

	BattlePlayerArmyClasses.RemoveAll([](const TSubclassOf<AHexUnitActor>& UnitClass)
		{
			return !UnitClass;
		});

	BattleAvailableUnitClasses.RemoveAll([](const TSubclassOf<AHexUnitActor>& UnitClass)
		{
			return !UnitClass;
		});

	UE_LOG(LogTemp, Warning, TEXT(
		"Battle army snapshot captured in GameInstance: PlayerClasses=%d AvailableClasses=%d DeploymentSlots=%d Progress=%d"
	),
		BattlePlayerArmyClasses.Num(),
		BattleAvailableUnitClasses.Num(),
		BattleDeploymentSlots.Num(),
		BattlePlayerProgress.Num()
	);
}

bool UGameLoadingGameInstance::HasBattleArmySnapshot() const
{
	return !BattlePlayerArmyClasses.IsEmpty();
}

void UGameLoadingGameInstance::PrepareNextLoadingScreen(
	EGameLoadingScreenType InScreenType,
	FText InTitleOverride,
	FText InStatusOverride
)
{
	PendingScreenType = InScreenType;
	PendingTitleOverride = MoveTemp(InTitleOverride);
	PendingStatusOverride = MoveTemp(InStatusOverride);
}

void UGameLoadingGameInstance::HandlePreLoadMap(const FString& MapName)
{
	if (IsRunningDedicatedServer())
	{
		return;
	}

	CancelPostLoadWarmup();
	ApplySafePSOConsoleSettings();
	FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Fast);

	CurrentMapName = FPackageName::GetShortName(MapName);
	CurrentScreenType = ResolveScreenType(CurrentMapName);
	CurrentTitleOverride = PendingTitleOverride;
	CurrentStatusOverride = PendingStatusOverride;

	const TSubclassOf<UGameLoadingScreenWidget> WidgetClass = ResolveWidgetClass(CurrentScreenType);

	FLoadingScreenAttributes LoadingScreen;
	LoadingScreen.MinimumLoadingScreenDisplayTime = FMath::Max(0.0f, MinimumLoadingScreenDisplayTime);
	LoadingScreen.bMoviesAreSkippable = false;

	// MoviePlayer handles only the blocking map load. It must auto-close.
	// Holding it manually after OpenLevel caused the permanent Finalizing screen in UE 5.5.
	LoadingScreen.bAutoCompleteWhenLoadingCompletes = true;
	LoadingScreen.bWaitForManualStop = false;
	LoadingScreen.bAllowEngineTick = false;

	MovieLoadingWidget = nullptr;

	if (WidgetClass)
	{
		MovieLoadingWidget = CreateWidget<UGameLoadingScreenWidget>(this, WidgetClass);
		if (MovieLoadingWidget)
		{
			MovieLoadingWidget->ConfigureForLoad(
				CurrentScreenType,
				CurrentMapName,
				CurrentTitleOverride,
				CurrentStatusOverride
			);

			LoadingScreen.WidgetLoadingScreen = MovieLoadingWidget->TakeWidget();
			PrepareLoadingScreenTextures(MovieLoadingWidget);
		}
	}

	if (!LoadingScreen.WidgetLoadingScreen.IsValid())
	{
		LoadingScreen.WidgetLoadingScreen = BuildFallbackSlateWidget(CurrentScreenType);
	}

	if (GetMoviePlayer())
	{
		GetMoviePlayer()->SetupLoadingScreen(LoadingScreen);
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("Loading phase 1 prepared. Map=%s Type=%d AutoClose=true"),
		*CurrentMapName,
		static_cast<int32>(CurrentScreenType)
	);

	PendingScreenType = EGameLoadingScreenType::Automatic;
	PendingTitleOverride = FText::GetEmpty();
	PendingStatusOverride = FText::GetEmpty();
}

void UGameLoadingGameInstance::HandlePostLoadMap(UWorld* LoadedWorld)
{
	MovieLoadingWidget = nullptr;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("Map package loaded. World=%s PostLoadWarmup=%s"),
		*GetNameSafe(LoadedWorld),
		bUsePostLoadShaderWarmup ? TEXT("true") : TEXT("false")
	);

	if (!bUsePostLoadShaderWarmup || !LoadedWorld || IsRunningDedicatedServer())
	{
		FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Background);
		return;
	}

	StartPostLoadWarmup(LoadedWorld);
}

void UGameLoadingGameInstance::StartPostLoadWarmup(UWorld* LoadedWorld)
{
	CancelPostLoadWarmup();

	PostLoadWorld = LoadedWorld;
	PostLoadWarmupStartSeconds = FPlatformTime::Seconds();
	OverlayShownSeconds = 0.0;
	LastPostLoadPollSeconds = 0.0;
	HighestObservedPSOCount = 0;
	HighestObservedAssetCount = 0;
	ConsecutiveZeroPolls = 0;
	LastLoggedCountdownSecond = INDEX_NONE;
	bPostLoadOverlayShown = false;
	bAssetCompilationFinishedSynchronously = false;
	bWorldWasPausedBeforeWarmup = false;

	FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Fast);

	PostLoadTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UGameLoadingGameInstance::TickPostLoadWarmup),
		FMath::Max(0.01f, PostLoadPollInterval)
	);

	UE_LOG(LogTemp, Log, TEXT("Loading phase 2 scheduled for world %s."), *GetNameSafe(LoadedWorld));
}

bool UGameLoadingGameInstance::TickPostLoadWarmup(float DeltaTime)
{
	(void)DeltaTime;

	UWorld* World = PostLoadWorld.Get();
	if (!World)
	{
		FinishPostLoadWarmup(true, TEXT("World became invalid"));
		return false;
	}

	// PostLoadMapWithWorld can fire before BeginPlay and before the first player controller exists.
	// Wait until a normal viewport widget can safely be added.
	if (!World->HasBegunPlay() || !World->GetFirstPlayerController())
	{
		const double WaitingSeconds = FPlatformTime::Seconds() - PostLoadWarmupStartSeconds;
		if (WaitingSeconds >= FMath::Max(5.0f, MaximumPostLoadWarmupTime))
		{
			FinishPostLoadWarmup(true, TEXT("World did not begin play"));
			return false;
		}
		return true;
	}

	if (!bPostLoadOverlayShown)
	{
		const int32 InitialRemainingAssets = FMath::Max(0, FAssetCompilingManager::Get().GetNumRemainingAssets());
		const uint32 InitialRemainingPSOs = FShaderPipelineCache::NumPrecompilesRemaining();

		HighestObservedAssetCount = FMath::Max(HighestObservedAssetCount, InitialRemainingAssets);
		HighestObservedPSOCount = FMath::Max(HighestObservedPSOCount, InitialRemainingPSOs);

		// Do not flash a second loading screen when there is no real post-load work.
		// The phase-1 MoviePlayer screen has already covered the blocking map load.
		if (InitialRemainingAssets == 0 && InitialRemainingPSOs == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("Loading phase 2 skipped: no shader, asset or PSO work is pending."));
			FinishPostLoadWarmup(false, TEXT("No post-load compilation work"));
			return false;
		}

		return ShowPostLoadOverlay();
	}

	const double CurrentSeconds = FPlatformTime::Seconds();
	if (!bAssetCompilationFinishedSynchronously)
	{
		if (CurrentSeconds - OverlayShownSeconds < static_cast<double>(FMath::Max(0.0f, OverlayPaintDelay)))
		{
			return true;
		}

		ForceFinishAssetCompilation();
		bAssetCompilationFinishedSynchronously = true;
		LastPostLoadPollSeconds = 0.0;
		return true;
	}

	const double SafeInterval = static_cast<double>(FMath::Max(0.01f, PostLoadPollInterval));
	if (LastPostLoadPollSeconds > 0.0 && CurrentSeconds - LastPostLoadPollSeconds < SafeInterval)
	{
		return true;
	}
	LastPostLoadPollSeconds = CurrentSeconds;

	const int32 RemainingAssets = FMath::Max(0, FAssetCompilingManager::Get().GetNumRemainingAssets());
	const uint32 RemainingPSOs = FShaderPipelineCache::NumPrecompilesRemaining();

	HighestObservedAssetCount = FMath::Max(HighestObservedAssetCount, RemainingAssets);
	HighestObservedPSOCount = FMath::Max(HighestObservedPSOCount, RemainingPSOs);

	if (RemainingAssets == 0 && RemainingPSOs == 0)
	{
		++ConsecutiveZeroPolls;
	}
	else
	{
		ConsecutiveZeroPolls = 0;
	}

	const float EffectiveTimeout = FMath::Clamp(MaximumPostLoadWarmupTime, 1.0f, 180.0f);
	const double ElapsedSeconds = CurrentSeconds - PostLoadWarmupStartSeconds;
	const int32 CountdownSeconds = FMath::Max(
		0,
		FMath::CeilToInt(EffectiveTimeout - static_cast<float>(ElapsedSeconds))
	);

	if (CountdownSeconds != LastLoggedCountdownSecond)
	{
		LastLoggedCountdownSecond = CountdownSeconds;
		UE_LOG(
			LogTemp,
			Log,
			TEXT("Loading phase 2: Assets=%d PSOs=%u Stable=%d/%d TimeLeft=%ds"),
			RemainingAssets,
			RemainingPSOs,
			ConsecutiveZeroPolls,
			FMath::Max(1, RequiredStableZeroPolls),
			CountdownSeconds
		);
	}

	if (PostLoadLoadingWidget)
	{
		FText StatusText;
		if (RemainingAssets > 0)
		{
			StatusText = FText::Format(
				NSLOCTEXT("GameLoading", "PostLoadAssets", "Compiling shaders... {0}"),
				FText::AsNumber(RemainingAssets)
			);
		}
		else if (RemainingPSOs > 0)
		{
			StatusText = FText::Format(
				NSLOCTEXT("GameLoading", "PostLoadPSOs", "Compiling shaders... {0}"),
				FText::AsNumber(RemainingPSOs)
			);
		}
		else
		{
			StatusText = NSLOCTEXT("GameLoading", "PostLoadFinalizing", "Finalizing shaders...");
		}

		const int32 HighestTotal = HighestObservedAssetCount + static_cast<int32>(HighestObservedPSOCount);
		const int32 RemainingTotal = RemainingAssets + static_cast<int32>(RemainingPSOs);
		const float Progress = HighestTotal > 0
			? 1.0f - static_cast<float>(RemainingTotal) / static_cast<float>(HighestTotal)
			: 0.0f;

		PostLoadLoadingWidget->SetRuntimeLoadingProgress(
			StatusText,
			FMath::Clamp(Progress, 0.0f, 1.0f),
			HighestTotal <= 0
		);
	}

	const bool bStableFinished = ConsecutiveZeroPolls >= FMath::Max(1, RequiredStableZeroPolls);
	const bool bTimedOut = ElapsedSeconds >= EffectiveTimeout;

	if (bStableFinished)
	{
		FinishPostLoadWarmup(false, TEXT("All compilation queues reached stable zero"));
		return false;
	}

	if (bTimedOut)
	{
		FinishPostLoadWarmup(true, TEXT("Safety timeout"));
		return false;
	}

	return true;
}

bool UGameLoadingGameInstance::ShowPostLoadOverlay()
{
	UWorld* World = PostLoadWorld.Get();
	if (!World)
	{
		return false;
	}

	const TSubclassOf<UGameLoadingScreenWidget> WidgetClass = ResolveWidgetClass(CurrentScreenType);
	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Loading phase 2 cannot show: no loading widget class is configured."));
		FinishPostLoadWarmup(true, TEXT("Missing loading widget class"));
		return false;
	}

	PostLoadLoadingWidget = CreateWidget<UGameLoadingScreenWidget>(this, WidgetClass);
	if (!PostLoadLoadingWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("Loading phase 2 cannot show: CreateWidget failed."));
		FinishPostLoadWarmup(true, TEXT("CreateWidget failed"));
		return false;
	}

	PostLoadLoadingWidget->ConfigureForLoad(
		CurrentScreenType,
		CurrentMapName,
		CurrentTitleOverride,
		CurrentStatusOverride
	);
	PostLoadLoadingWidget->SetRuntimeLoadingProgress(
		NSLOCTEXT("GameLoading", "PostLoadPreparing", "Compiling shaders..."),
		0.0f,
		true
	);
	PostLoadLoadingWidget->AddToViewport(100000);

	bWorldWasPausedBeforeWarmup = UGameplayStatics::IsGamePaused(World);
	if (!bWorldWasPausedBeforeWarmup)
	{
		UGameplayStatics::SetGamePaused(World, true);
	}

	bPostLoadOverlayShown = true;
	OverlayShownSeconds = FPlatformTime::Seconds();

	UE_LOG(LogTemp, Log, TEXT("Loading phase 2 overlay shown for world %s."), *GetNameSafe(World));
	return true;
}

void UGameLoadingGameInstance::ForceFinishAssetCompilation()
{
	if (PostLoadLoadingWidget)
	{
		PostLoadLoadingWidget->SetRuntimeLoadingProgress(
			NSLOCTEXT("GameLoading", "PostLoadBlockingCompile", "Compiling shaders..."),
			0.0f,
			true
		);
	}

	const int32 BeforeCount = FMath::Max(0, FAssetCompilingManager::Get().GetNumRemainingAssets());
	UE_LOG(LogTemp, Log, TEXT("Finishing async asset compilation behind loading overlay. RemainingBefore=%d"), BeforeCount);

	// This is deliberately synchronous. In Standalone-from-Editor this consumes the
	// material/shader/mesh jobs that otherwise produce progress text over the main menu
	// and can leave skeletal meshes without a ready render state.
	FAssetCompilingManager::Get().FinishAllCompilation();

	const int32 AfterCount = FMath::Max(0, FAssetCompilingManager::Get().GetNumRemainingAssets());
	UE_LOG(LogTemp, Log, TEXT("Async asset compilation finished. RemainingAfter=%d"), AfterCount);

	RefreshWorldRenderState();
}

void UGameLoadingGameInstance::RefreshWorldRenderState() const
{
	UWorld* World = PostLoadWorld.Get();
	if (!World)
	{
		return;
	}

	int32 RefreshedPrimitiveCount = 0;
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* Primitive = *It;
		if (!IsValid(Primitive) || Primitive->GetWorld() != World || !Primitive->IsRegistered())
		{
			continue;
		}

		Primitive->MarkRenderStateDirty();
		++RefreshedPrimitiveCount;
	}

	FlushRenderingCommands();
	UE_LOG(LogTemp, Log, TEXT("Render states refreshed after shader warmup. Primitives=%d"), RefreshedPrimitiveCount);
}

void UGameLoadingGameInstance::FinishPostLoadWarmup(bool bTimedOut, const TCHAR* Reason)
{
	UWorld* World = PostLoadWorld.Get();

	if (PostLoadLoadingWidget)
	{
		PostLoadLoadingWidget->SetRuntimeLoadingProgress(
			bTimedOut
				? NSLOCTEXT("GameLoading", "PostLoadTimeout", "Starting game...")
				: NSLOCTEXT("GameLoading", "PostLoadReady", "Ready."),
			1.0f,
			false
		);
	}

	ApplySafePSOConsoleSettings();
	RefreshWorldRenderState();
	FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Background);

	if (PostLoadLoadingWidget)
	{
		PostLoadLoadingWidget->RemoveFromParent();
		PostLoadLoadingWidget = nullptr;
	}

	if (World && !bWorldWasPausedBeforeWarmup && UGameplayStatics::IsGamePaused(World))
	{
		UGameplayStatics::SetGamePaused(World, false);
	}

	const int32 RemainingAssetsAtFinish = FMath::Max(0, FAssetCompilingManager::Get().GetNumRemainingAssets());
	const uint32 RemainingPSOsAtFinish = FShaderPipelineCache::NumPrecompilesRemaining();

	if (bTimedOut)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Loading phase 2 finished. TimedOut=true Reason=%s RemainingAssets=%d RemainingPSOs=%u"),
			Reason ? Reason : TEXT("Unknown"),
			RemainingAssetsAtFinish,
			RemainingPSOsAtFinish
		);
	}
	else
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("Loading phase 2 finished. TimedOut=false Reason=%s RemainingAssets=%d RemainingPSOs=%u"),
			Reason ? Reason : TEXT("Unknown"),
			RemainingAssetsAtFinish,
			RemainingPSOsAtFinish
		);
	}

	// TickPostLoadWarmup returns false immediately after this function, which removes
	// the current ticker safely. Do not remove the currently executing ticker here.
	PostLoadTickerHandle.Reset();

	PostLoadWorld.Reset();
	bPostLoadOverlayShown = false;
	bAssetCompilationFinishedSynchronously = false;
	bWorldWasPausedBeforeWarmup = false;
}

void UGameLoadingGameInstance::CancelPostLoadWarmup()
{
	UWorld* World = PostLoadWorld.Get();

	if (PostLoadTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PostLoadTickerHandle);
		PostLoadTickerHandle.Reset();
	}

	if (PostLoadLoadingWidget)
	{
		PostLoadLoadingWidget->RemoveFromParent();
		PostLoadLoadingWidget = nullptr;
	}

	if (World && !bWorldWasPausedBeforeWarmup && UGameplayStatics::IsGamePaused(World))
	{
		UGameplayStatics::SetGamePaused(World, false);
	}

	PostLoadWorld.Reset();
	bPostLoadOverlayShown = false;
	bAssetCompilationFinishedSynchronously = false;
	bWorldWasPausedBeforeWarmup = false;
}

void UGameLoadingGameInstance::ApplySafePSOConsoleSettings() const
{
	// Never hide skeletal/static mesh render proxies while their PSO is pending.
	// The post-load overlay already protects the player from seeing compilation.
	SetConsoleVariableInt(TEXT("r.PSOPrecache.ProxyCreationWhenPSOReady"), 0);

	// During the loading phases, wait for the complete queue rather than only
	// high-priority requests. Old config values cannot silently undo this now.
	SetConsoleVariableInt(TEXT("r.PSOPrecaching.WaitForHighPriorityRequestsOnly"), 0);
}

EGameLoadingScreenType UGameLoadingGameInstance::ResolveScreenType(const FString& ShortMapName) const
{
	if (PendingScreenType != EGameLoadingScreenType::Automatic)
	{
		return PendingScreenType;
	}

	if (
		!MainMenuMapName.IsNone()
		&& ShortMapName.EndsWith(MainMenuMapName.ToString(), ESearchCase::IgnoreCase)
	)
	{
		return EGameLoadingScreenType::MainMenu;
	}

	return EGameLoadingScreenType::General;
}

TSubclassOf<UGameLoadingScreenWidget> UGameLoadingGameInstance::ResolveWidgetClass(
	EGameLoadingScreenType ScreenType
) const
{
	switch (ScreenType)
	{
	case EGameLoadingScreenType::Battle:
		return BattleLoadingScreenWidgetClass
			? BattleLoadingScreenWidgetClass
			: GeneralLoadingScreenWidgetClass;

	case EGameLoadingScreenType::MainMenu:
		return MainMenuLoadingScreenWidgetClass
			? MainMenuLoadingScreenWidgetClass
			: GeneralLoadingScreenWidgetClass;

	case EGameLoadingScreenType::Automatic:
	case EGameLoadingScreenType::General:
	default:
		return GeneralLoadingScreenWidgetClass;
	}
}

TSharedRef<SWidget> UGameLoadingGameInstance::BuildFallbackSlateWidget(
	EGameLoadingScreenType ScreenType
) const
{
	const FText Title = PendingTitleOverride.IsEmpty()
		? GetFallbackTitle(ScreenType)
		: PendingTitleOverride;

	const FText Status = PendingStatusOverride.IsEmpty()
		? GetFallbackStatus(ScreenType)
		: PendingStatusOverride;

	return SNew(SBorder)
		.BorderBackgroundColor(FLinearColor::Black)
		.Padding(FMargin(40.0f))
		[
			SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 0.0f, 0.0f, 18.0f)
					[
						SNew(STextBlock)
							.Text(Title)
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 30))
							.ColorAndOpacity(FLinearColor::White)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 0.0f, 0.0f, 18.0f)
					[
						SNew(STextBlock)
							.Text(Status)
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 18))
							.ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f, 1.0f))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(SThrobber)
							.NumPieces(5)
					]
				]
		];
}

FText UGameLoadingGameInstance::GetFallbackTitle(EGameLoadingScreenType ScreenType) const
{
	if (ScreenType == EGameLoadingScreenType::Battle)
	{
		return NSLOCTEXT("GameLoading", "FallbackBattleTitle", "PREPARING FOR BATTLE");
	}

	return NSLOCTEXT("GameLoading", "FallbackGeneralTitle", "LOADING");
}

FText UGameLoadingGameInstance::GetFallbackStatus(EGameLoadingScreenType ScreenType) const
{
	switch (ScreenType)
	{
	case EGameLoadingScreenType::Battle:
		return NSLOCTEXT("GameLoading", "FallbackBattleStatus", "Loading battlefield and units...");

	case EGameLoadingScreenType::MainMenu:
		return NSLOCTEXT("GameLoading", "FallbackMainMenuStatus", "Preparing main menu...");

	case EGameLoadingScreenType::Automatic:
	case EGameLoadingScreenType::General:
	default:
		return NSLOCTEXT("GameLoading", "FallbackGeneralStatus", "Preparing game...");
	}
}
