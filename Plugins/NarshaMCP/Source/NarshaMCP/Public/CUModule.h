// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Misc/ITransaction.h"  // UE 5.8: ETransactionStateEventType + FTransactionContext (transitive include broken in 5.8)

class IAssetRegistry;
struct FAssetData;  // UE 5.6: FAssetData is a struct, not a class
class UCUWebSocketEditorSubsystem;  // Issue #1437: UEditorSubsystem migration
class UTransBuffer;                 // Issue #8564: Transaction logging

class FCUModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Run narshamcp setup asynchronously (Issue #8908) */
	void RunPostBuildInitialization(const FString& PluginDir, const FString& ProjectDir, const FString& EngineDir);

	/** Run GameplayAbility Tag extraction Commandlet */
	void RunGameplayAbilityTagExtraction(const FString& PluginDir, const FString& ProjectDir, const FString& EngineDir);

	/** Run Blueprint Metadata extraction Commandlet */
	void RunBlueprintMetadataExtraction(const FString& PluginDir, const FString& ProjectDir, const FString& EngineDir);

	/**
	 * Phase 6-8 (Issue #29): Real-time Editor Asset Update
	 * Handle asset update events and generate Delta JSON files
	 */
	void OnAssetUpdated(const FAssetData& AssetData);

	/** Issue #8564: Editor transaction finalized → JSONL entry */
	void OnTransactionStateChanged(const FTransactionContext& Context, ETransactionStateEventType State);

	/** Issue #8564: Object property changes → JSONL entry */
	void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event);

	/** Generate Delta JSON for updated Blueprint asset */
	void GenerateDeltaJSON(const FAssetData& AssetData);

	/**
	 * Phase 4 (Issue #96): Atomic File Write Helper
	 *
	 * Writes content to file atomically using temp file + rename pattern.
	 * Prevents partial writes and corruption during crashes/interruptions.
	 *
	 * @param FilePath Target file path
	 * @param Content String content to write
	 * @return true if write succeeded, false otherwise
	 */
	bool AtomicWriteFile(const FString& FilePath, const FString& Content);

	/** Cache paths for Delta JSON generation */
	FString CachedPluginDir;
	FString CachedProjectDir;
	FString CachedEngineDir;

	/** Delegate handle for OnAssetUpdated */
	FDelegateHandle AssetUpdatedHandle;

	/** Issue #8564: Transaction delegate handles */
	FDelegateHandle TransactionStateHandle;
	FDelegateHandle ObjectTransactedHandle;
	FString TransactionLogPath;

	/** Issue #8564 Phase K: Propagate transaction context to OnObjectTransacted.
	 *  Set in OnTransactionStateChanged(Finalized), read in OnObjectTransacted(Finalized).
	 *  Safe: both callbacks run on GameThread, Finalized fires before object Finalized. */
	FString CurrentTransactionContext;

	/** Issue #8564 M2: Asset lifecycle delegate handles */
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetRenamedHandle;

	/** Issue #8564 M2: Asset lifecycle → JSONL */
	void OnAssetAdded(const FAssetData& AssetData);
	void OnAssetRemoved(const FAssetData& AssetData);
	void OnAssetRenamed(const FAssetData& AssetData, const FString& OldPath);

	/** Issue #8564 L1-L4: Editor event delegate handles */
	FDelegateHandle BlueprintCompiledHandle;
	FDelegateHandle BeginPIEHandle;
	FDelegateHandle EndPIEHandle;
	FDelegateHandle MapOpenedHandle;

	/** Write a simple event entry to the transaction JSONL */
	void WriteEventEntry(const FString& EventType, const FString& Detail);

	/** Issue #8834 Part B: OnObjectPropertyChanged — captures property edits outside transactions
	 *  (e.g., Python set_editor_property() which doesn't use FScopedTransaction) */
	FDelegateHandle ObjectPropertyChangedHandle;
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

	/** Issue #8834 Part B / #9122: nested transaction depth. Readers check >0.
	 *  Previously a bool, which flipped to false on any TransactionCanceled — breaking
	 *  nested-transaction deduplication. Saturates at 0 on unmatched Cancel/Finalize. */
	int32 TransactionDepth = 0;

	/** Issue #8834 B1: Diagnostic mode — log all events pre-filter to diagnostics.jsonl */
	bool bDiagnosticMode = false;
	FString DiagnosticLogPath;
	void WriteDiagnosticEntry(UObject* Object, const FTransactionObjectEvent& Event, const FString& Phase);

	/** Issue #8834 B2: Level actor lifecycle delegates (captures outside-transaction events) */
	FDelegateHandle LevelActorAddedHandle;
	FDelegateHandle LevelActorDeletedHandle;
	FDelegateHandle ActorMovedHandle;

	void OnLevelActorAdded(AActor* Actor);
	void OnLevelActorDeleted(AActor* Actor);
	void OnActorMoved(AActor* Actor);

	// Issue #1437: WebSocketServer removed - now using UCUWebSocketEditorSubsystem
	// Access via: GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>()
};
