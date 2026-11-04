// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUBlueprintMetadataCommandlet.h"
#include "CUCommandletHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Async/ParallelFor.h"  // Issue #23 Phase 1: Parallel processing
#include "Misc/Compression.h"  // GZIP compression support

// Additional K2Node types for complete flow tracking (v2.8.3+)
#include "K2Node.h"  // Base class for all Blueprint nodes
#include "K2Node_AsyncAction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Composite.h"
#include "K2Node_FunctionEntry.h"  // For finding owning functions (v2.8.6+)
#include "EdGraphSchema_K2.h"  // For PC_Exec, PC_Delegate constants

// Issue #108 Phase 0: Modular extractor namespaces (v4.7.0)
#include "BlueprintMetadata/BlueprintExtractors.h"
#include "BlueprintMetadata/AnimationExtractors.h"
#include "BlueprintMetadata/AIExtractors.h"
#include "BlueprintMetadata/InputExtractors.h"
#include "BlueprintMetadata/UMGExtractors.h"  // Issue #135 Phase 3
#include "BlueprintMetadata/BlueprintMetadataHelpers.h"

// Animation Blueprint support (v2.8.4+)
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"

// GameplayAbilities AnimNotify support
#include "GameplayTagContainer.h"

// StateTree support (Issue #27 Phase 1)
#if WITH_EDITOR
// Widget Blueprint support (Issue #135 Phase 3 - MUST be inside WITH_EDITOR)
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"

#if WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeEditorNode.h"
#include "StateTreeTypes.h"
#endif
#endif

// BehaviorTree support (Issue #27 Phase 2)
#if WITH_EDITOR
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BlackboardData.h"
#endif

// Enhanced Input support (Issue #20 Phase 7 - v3.4.0)
#if WITH_NARSHAMCP_ENHANCEDINPUT
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedActionKeyMapping.h"
#include "K2Node_InputAction.h"
#endif

// Async loading support (Issue #108 Phase 1 - v4.7.0)
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"

// Hash support for incremental build (Issue #108 Phase 1 Incremental)
#include "IO/IoHash.h"
#include "AssetRegistry/AssetRegistryState.h"

#include "CULog.h"

// Use namespace functions for modular extraction (Issue #108 Phase 0)
using namespace BlueprintExtractors;
using namespace AnimationExtractors;
using namespace AIExtractors;
using namespace InputExtractors;
using namespace BlueprintMetadataHelpers;

// FCUBlueprintProfilingData: Defined in CUBlueprintMetadataCommandlet.h (Lines 44-68)

UCUBlueprintMetadataCommandlet::UCUBlueprintMetadataCommandlet(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

// Override CommandletBase::RunExtraction()
int32 UCUBlueprintMetadataCommandlet::RunExtraction(const FString& OutputDir, IAssetRegistry& Registry)
{
    return RunExtractionStatic(OutputDir, Registry);
}

// Static method for batch commandlet
int32 UCUBlueprintMetadataCommandlet::RunExtractionStatic(const FString& OutputDir, IAssetRegistry& AssetRegistry)
{
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[BlueprintMetadata] Running extraction..."));

    // Issue #213 Phase 4: Parse -Profile flag for performance profiling
    bool bEnableProfiling = FParse::Param(FCommandLine::Get(), TEXT("Profile"));

    if (bEnableProfiling)
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[BlueprintMetadata] Profiling enabled - detailed statistics will be logged"));
    }

    // Issue #582: Reset property type statistics (prevent double-counting in batch mode)
    BlueprintExtractors::ResetPropertyTypeStats();

    // Find all Blueprint assets
    // Issue #1327 Phase 5: Profile Asset Registry enumeration
    if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.0.AssetEnumeration")); }

    FARFilter BPFilter;
    BPFilter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    BPFilter.bRecursiveClasses = true;

    TArray<FAssetData> BlueprintAssetList;
    AssetRegistry.GetAssets(BPFilter, BlueprintAssetList);

    if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.0.AssetEnumeration")); }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Found %d Blueprint assets"), BlueprintAssetList.Num());

    // ============================================================================
    // Issue #261 Phase 1: Timestamp-based Delta Tracking
    // ============================================================================
    // NEW v4.13.0: Skip extraction if no assets changed since last run
    // Performance: 91.4s → 0.5s (180x faster) when no changes detected
    // ============================================================================
    FString TimestampPath = OutputDir / TEXT("last_extraction.timestamp");
    FDateTime LastRunTime = FDateTime::MinValue();
    bool bHasTimestamp = false;

    if (FPaths::FileExists(TimestampPath))
    {
        FString TimestampStr;
        if (FFileHelper::LoadFileToString(TimestampStr, *TimestampPath))
        {
            // Parse ISO8601 format: "2025-10-26T10:30:00.123456Z"
            TimestampStr = TimestampStr.TrimStartAndEnd();
            if (TimestampStr.EndsWith(TEXT("Z")))
            {
                TimestampStr = TimestampStr.LeftChop(1);  // Remove 'Z' suffix
            }

            if (FDateTime::ParseIso8601(*TimestampStr, LastRunTime))
            {
                bHasTimestamp = true;
                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Last extraction: %s (%.0fs ago)"),
                       *LastRunTime.ToString(),
                       (FDateTime::UtcNow() - LastRunTime).GetTotalSeconds());
            }
            else
            {
                UE_LOG(LogNarshaMCPMetadata, Warning,
                       TEXT("[NarshaMCP] Failed to parse timestamp file, treating as first run"));
            }
        }
    }
    else
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] First run (no timestamp file)"));
    }

    // Filter assets by modification time (Delta Tracking)
    // Note: For Phase 1, we use conservative approach - delegate to Issue #108
    // hash comparison for actual change detection. This timestamp check provides
    // fast early-exit when JSON doesn't exist or is invalidated.
    //
    // Phase 1 Strategy:
    // - If no timestamp → Process all (first run)
    // - If timestamp exists → Delegate to Issue #108 incremental logic
    // - Issue #108 will filter by hash comparison (more accurate)
    //
    // Future Phase 2: FDirectoryWatcher will provide precise delta log

    // For Phase 1, we rely on existing Issue #108 incremental logic
    // which already does hash-based change detection. The timestamp
    // file serves as a marker for "extraction completed successfully".

    // ============================================================================
    // Issue #108 Phase 1 Incremental: Hash-based Change Detection
    // ============================================================================
    // Issue #1727 Phase 60: Commandlet outputs to _commandlet suffix (Rust Binary Parser is PRIMARY)
    FString JsonPath = OutputDir / TEXT("blueprint_metadata_commandlet.json");

    // Issue #1327 Phase 5: Profile LoadExistingJson (GZIP decompression + JSON parsing)
    if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.0.LoadExistingJson")); }
    TSharedPtr<FJsonObject> ExistingJson = LoadExistingJson(JsonPath);
    if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.0.LoadExistingJson")); }
    bool bIncrementalMode = ExistingJson.IsValid();

    TArray<FAssetData> AssetsToProcess;
    if (bIncrementalMode)
    {
        // Incremental build: Only process changed Blueprints
        // Issue #1327 Phase 5: Profile FindChangedBlueprints (hash comparison)
        if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.0.FindChangedBlueprints")); }
        AssetsToProcess = FindChangedBlueprints(BlueprintAssetList, ExistingJson, AssetRegistry);
        if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.0.FindChangedBlueprints")); }
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Incremental mode: Processing %d / %d changed Blueprints"),
               AssetsToProcess.Num(), BlueprintAssetList.Num());

        // ====================================================================
        // Issue #261 Phase 1: Early exit if no changes detected
        // ====================================================================
        if (AssetsToProcess.Num() == 0 && bHasTimestamp)
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("============================================================"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  ✓ No asset changes detected since last run"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("============================================================"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Last extraction: %s"), *LastRunTime.ToString());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Assets checked: %d"), BlueprintAssetList.Num());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Changes found: 0 (hash comparison)"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  "));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Skipping extraction (no work needed!)"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Time saved: ~91 seconds (vs full extraction)"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("============================================================"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));

            // Issue #1132: Update timestamp before returning (FIX: early exit was not updating timestamp)
            UpdateTimestampFile(TimestampPath);

            return 0;  // Success - no extraction needed!
        }
    }
    else
    {
        // Full build: Process all Blueprints
        AssetsToProcess = BlueprintAssetList;
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Full build mode: Processing all %d Blueprints"),
               AssetsToProcess.Num());
    }

    // Root JSON object
    TSharedPtr<FJsonObject> RootJson = ExistingJson.IsValid() ? ExistingJson : MakeShareable(new FJsonObject);
    TArray<TSharedPtr<FJsonValue>> BlueprintsArray;
    TArray<TSharedPtr<FJsonValue>> AnimBPsArray;
    TArray<TSharedPtr<FJsonValue>> WidgetBPsArray;  // Issue #135 Phase 3

    // Issue #23 Phase 1: Thread-safe counters and arrays
    TAtomic<int32> ProcessedCount(0);
    TAtomic<int32> ErrorCount(0);
    TAtomic<int32> AnimBPProcessedCount(0);
    TAtomic<int32> AnimBPErrorCount(0);
    TAtomic<int32> WidgetBPProcessedCount(0);  // Issue #135 Phase 3
    TAtomic<int32> WidgetBPErrorCount(0);  // Issue #135 Phase 3

    // Issue #141 Phase 2: Loading optimization statistics
    TAtomic<int32> OptimizedLoadCount(0);
    double TotalLoadingTime = 0.0;

    // Issue #213 Phase 2: Data-Only Blueprint skip statistics
    TAtomic<int32> DataOnlySkippedCount(0);

    // Issue #213 Phase 4: Direct Child BP skip statistics
    TAtomic<int32> DirectChildSkippedCount(0);
    TAtomic<int32> ActorDirectCount(0);
    TAtomic<int32> PCGDirectCount(0);
    TAtomic<int32> LandmassDirectCount(0);

    // ============================================================================
    // Issue #1327 Phase 2: High-Cost Blueprint Skip List (Configurable)
    // ============================================================================
    // Purpose: Skip known high-cost Blueprints identified from blueprint_profiling.csv
    // Default: PreCMCTick (4,347s = 72 min = 10.8% of total time)
    //
    // Configuration:
    // - Command-line: -SkipBlueprints="BP1,BP2,BP3" to add custom skip list
    // - Default list is always applied unless -NoDefaultSkipList is passed
    // ============================================================================
    TAtomic<int32> HighCostSkippedCount(0);

    // Build skip list: default + command-line additions
    TSet<FString> HighCostSkipList;

    // Default high-cost Blueprints (always skipped unless -NoDefaultSkipList)
    if (!FParse::Param(FCommandLine::Get(), TEXT("NoDefaultSkipList")))
    {
        HighCostSkipList.Add(TEXT("PreCMCTick"));  // ActorComponent - 4,347s single BP
    }

    // Parse additional skip list from command-line: -SkipBlueprints="BP1,BP2,BP3"
    FString SkipBlueprintsParam;
    if (FParse::Value(FCommandLine::Get(), TEXT("SkipBlueprints="), SkipBlueprintsParam))
    {
        TArray<FString> AdditionalSkips;
        SkipBlueprintsParam.ParseIntoArray(AdditionalSkips, TEXT(","), true);
        for (const FString& Skip : AdditionalSkips)
        {
            HighCostSkipList.Add(Skip.TrimStartAndEnd());
        }
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Added %d custom Blueprints to skip list from command-line"), AdditionalSkips.Num());
    }

    if (HighCostSkipList.Num() > 0)
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] High-Cost Skip List: %d entries"), HighCostSkipList.Num());
    }

    // Thread-safe JSON arrays (protected by FCriticalSection)
    FCriticalSection BlueprintsArrayLock;
    FCriticalSection AnimBPsArrayLock;
    FCriticalSection WidgetBPsArrayLock;  // Issue #135 Phase 3

    // Issue #1310 Phase 1: Blueprint cache for BlueprintNodeTracing optimization
    // Avoids double LoadObject (~6s savings on 500+ Blueprint projects)
    TMap<FString, UBlueprint*> LoadedBlueprintsCache;
    FCriticalSection BlueprintsCacheLock;

    // Issue #212: Blueprint profiling results
    TArray<FCUBlueprintProfilingData> ProfilingResults;
    FCriticalSection ProfilingResultsLock;

    // ============================================================================
    // Issue #108 Phase 1 Final v2: FAssetData::GetAsset() + Processing (Hybrid approach)
    // ============================================================================
    // Rationale: LoadPackage requires GameThread (cannot use in ParallelFor)
    //            - FAssetData::GetAsset() can load assets from worker threads
    //            - Combines loading + processing in single ParallelFor
    //            - Simpler than separate load/process phases
    // See: docs/issues/ISSUE_108_PHASE_1_FINAL_V2_PLAN.md
    // ============================================================================

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Phase 1 Final v2: FAssetData::GetAsset() + Processing %d Blueprint assets..."), BlueprintAssetList.Num());

    // PROFILING: Blueprint loading and processing
    // Issue #199 Phase 1: Commented out outer profiling section to enable fine-grained profiling
    // FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.BlueprintLoading+Processing"));

    FDateTime StartTime = FDateTime::UtcNow();
    TAtomic<int32> CompletedCount(0);
    int32 TotalCount = BlueprintAssetList.Num();
    int32 ProgressStep = FMath::Max(1, TotalCount / 10);  // 10% intervals

    // ============================================================================
    // Issue #1327 Phase 4: Pre-filter profiling accumulators
    // ============================================================================
    // Track time spent in pre-LoadObject checks to identify ~50s overhead source
    // ============================================================================
    double AccumIsDataOnlyCheck = 0.0;      // Time spent checking IsDataOnly tag
    double AccumDataOnlyPath = 0.0;         // Time spent in Data-Only fast path
    double AccumHighCostCheck = 0.0;        // Time spent checking High-Cost skip list
    double AccumHighCostPath = 0.0;         // Time spent in High-Cost fast path
    double AccumDirectChildCheck = 0.0;     // Time spent checking Direct Child
    double AccumDirectChildPath = 0.0;      // Time spent in Direct Child fast path
    double PreFilterSectionStart = 0.0;

    // Sequential processing (ParallelFor caused crashes with FAssetData::GetAsset())
    for (int32 Index = 0; Index < AssetsToProcess.Num(); ++Index)
    {
        const FAssetData& AssetData = AssetsToProcess[Index];

        // ============================================================================
        // Issue #213 Phase 2: Check IsDataOnly tag BEFORE LoadObject
        // ============================================================================
        // Rationale: Data-Only Blueprints have no nodes/functions/variables
        //            - No need to LoadObject (expensive 9.2ms avg)
        //            - Extract minimal metadata from FAssetData tags
        //            - Save to Low Priority JSON (will be merged by CUBatch)
        // Expected savings: 215 BPs × 9.2ms = ~2 seconds
        // ============================================================================

        // Issue #520 DEBUG: Track BP_SPTestActor processing path
#if UE_CODEGEN_BUILD_DEBUG
        if (AssetData.AssetName.ToString() == TEXT("BP_SPTestActor"))
        {
            UE_LOG(LogNarshaMCPMetadata, Error, TEXT("[Issue #520 CRITICAL] START Processing BP_SPTestActor"));
        }
#endif

        // Issue #1327 Phase 4: Profile IsDataOnly check
        if (bEnableProfiling) { PreFilterSectionStart = FPlatformTime::Seconds(); }

        FString IsDataOnlyStr;
        bool bIsDataOnly = AssetData.GetTagValue(FBlueprintTags::IsDataOnly, IsDataOnlyStr) && IsDataOnlyStr == TEXT("True");

        // Issue #520 DEBUG: Check IsDataOnly tag for BP_SPTestActor
#if UE_CODEGEN_BUILD_DEBUG
        if (AssetData.AssetName.ToString() == TEXT("BP_SPTestActor"))
        {
            UE_LOG(LogNarshaMCPMetadata, Error, TEXT("[Issue #520 CRITICAL] BP_SPTestActor IsDataOnly check: %s (tag value: %s)"),
                   bIsDataOnly ? TEXT("TRUE - Will use fast path") : TEXT("FALSE - Will use full extraction"),
                   *IsDataOnlyStr);
        }
#endif

        // Issue #1327 Phase 4: End IsDataOnly check timing
        if (bEnableProfiling) { AccumIsDataOnlyCheck += (FPlatformTime::Seconds() - PreFilterSectionStart); }

        if (bIsDataOnly)
        {
            // Issue #1327 Phase 4: Profile Data-Only fast path
            if (bEnableProfiling) { PreFilterSectionStart = FPlatformTime::Seconds(); }

            DataOnlySkippedCount++;

            UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[BlueprintMetadata] Auto Low Priority (Data-Only): %s"),
                   *AssetData.AssetName.ToString());

            // Issue #1339: Use helper function for minimal Blueprint entry (DRY refactoring)
            TSharedPtr<FJsonObject> BPJson = BlueprintMetadataHelpers::CreateMinimalBlueprintEntry(AssetData);

            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue #992] Generated Data-Only BP hash for: %s"),
                   *AssetData.GetObjectPathString());

            // Add to Low Priority Blueprints array (thread-safe)
            {
                FScopeLock Lock(&BlueprintsArrayLock);
                BlueprintsArray.Add(MakeShareable(new FJsonValueObject(BPJson)));
            }

            // Issue #1327 Phase 4: End Data-Only path timing
            if (bEnableProfiling) { AccumDataOnlyPath += (FPlatformTime::Seconds() - PreFilterSectionStart); }

            continue;  // Skip LoadObject!
        }

        // ============================================================================
        // Issue #1327 Phase 2: High-Cost Blueprint Skip List
        // ============================================================================
        // Some Blueprints take disproportionate time (PreCMCTick = 4,347s = 72 min)
        // Skip these known high-cost BPs to reduce total processing time
        // ============================================================================

        // Issue #1327 Phase 4: Profile HighCost check
        if (bEnableProfiling) { PreFilterSectionStart = FPlatformTime::Seconds(); }

        FString AssetName = AssetData.AssetName.ToString();
        if (HighCostSkipList.Contains(AssetName))
        {
            // Issue #1327 Phase 4: End HighCost check, start HighCost path timing
            if (bEnableProfiling)
            {
                AccumHighCostCheck += (FPlatformTime::Seconds() - PreFilterSectionStart);
                PreFilterSectionStart = FPlatformTime::Seconds();
            }
            HighCostSkippedCount++;

            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[BlueprintMetadata] HIGH-COST SKIP: %s (identified in blueprint_profiling.csv)"),
                   *AssetName);

            // Issue #1339: Use helper function for minimal Blueprint entry (DRY refactoring)
            // HashSuffix="HIGH_COST_SKIP", bHighCostSkipped=true
            TSharedPtr<FJsonObject> BPJson = BlueprintMetadataHelpers::CreateMinimalBlueprintEntry(
                AssetData,
                TEXT("HIGH_COST_SKIP"),  // HashSuffix
                true                      // bHighCostSkipped
            );

            // Add to Blueprints array (thread-safe)
            {
                FScopeLock Lock(&BlueprintsArrayLock);
                BlueprintsArray.Add(MakeShareable(new FJsonValueObject(BPJson)));
            }

            // Issue #1327 Phase 4: End HighCost path timing
            if (bEnableProfiling) { AccumHighCostPath += (FPlatformTime::Seconds() - PreFilterSectionStart); }

            continue;  // Skip LoadObject!
        }

        // Issue #1327 Phase 4: End HighCost check (not matched)
        if (bEnableProfiling) { AccumHighCostCheck += (FPlatformTime::Seconds() - PreFilterSectionStart); }

        // ============================================================================
        // End Issue #213 Phase 2

        // ============================================================================
        // ============================================================================
        // Issue #213 Phase 4: Check direct children of Low Priority classes
        // ============================================================================
        // Rationale: BPs directly inheriting from Actor/PCG/Landmass are typically
        //            editor tools, demos, or utilities - not core gameplay logic
        // Direct child = parent_class == native_parent_class (no intermediate BP inheritance)
        // Expected savings: 317 BPs × 110.7ms = ~33.78 seconds (25.8% improvement)
        // ============================================================================

        // Issue #1327 Phase 4: Profile DirectChild check
        if (bEnableProfiling) { PreFilterSectionStart = FPlatformTime::Seconds(); }

        FString ParentClassPath4, ParentClassName4, NativeParentClassPath4, NativeParentClassName4;
        bool bIsLowPriorityDirectChild = false;

if (AssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath4) &&
            AssetData.GetTagValue(FBlueprintTags::NativeParentClassPath, NativeParentClassPath4))
        {
            // Extract ParentClassName
            if (ParentClassPath4.Split(TEXT("."), nullptr, &ParentClassName4,
                                        ESearchCase::IgnoreCase, ESearchDir::FromEnd))
            {
                // Remove trailing apostrophe (format: "Class'/Script/Engine.Actor'")
                ParentClassName4.RemoveFromEnd(TEXT("'"));
            }
            else
            {
                ParentClassPath4.RemoveFromEnd(TEXT("'"));
                ParentClassName4 = ParentClassPath4;
            }

            // Extract NativeParentClassName
            if (NativeParentClassPath4.Split(TEXT("."), nullptr, &NativeParentClassName4,
                                             ESearchCase::IgnoreCase, ESearchDir::FromEnd))
            {
                // Remove trailing apostrophe
                NativeParentClassName4.RemoveFromEnd(TEXT("'"));
            }

            // Check if direct child (parent == native_parent)
            if (ParentClassName4 == NativeParentClassName4)
            {
                // Actor, PCG, Landmass direct children → Low Priority
                if (NativeParentClassName4 == TEXT("Actor"))
                {
                    bIsLowPriorityDirectChild = true;
                    ActorDirectCount++;
                }
                else if (NativeParentClassName4 == TEXT("PCGBlueprintElement"))
                {
                    bIsLowPriorityDirectChild = true;
                    PCGDirectCount++;
                }
                else if (NativeParentClassName4 == TEXT("LandmassActor"))
                {
                    bIsLowPriorityDirectChild = true;
                    LandmassDirectCount++;
                }
            }
        }

        // Issue #1327 Phase 4: End DirectChild check timing
        if (bEnableProfiling) { AccumDirectChildCheck += (FPlatformTime::Seconds() - PreFilterSectionStart); }

        if (bIsLowPriorityDirectChild)
        {
            // Issue #1327 Phase 4: Start DirectChild path timing
            if (bEnableProfiling) { PreFilterSectionStart = FPlatformTime::Seconds(); }
            // ============================================================================
            // Issue #520 Phase 2: Conditional LoadObject based on IsDataOnly tag
            // ============================================================================
            // Rationale: Direct Child of Actor/PCG/Landmass may still have content
            //            - IsDataOnly=True: Empty BP → Use fast path (skip LoadObject)
            //            - IsDataOnly=False: Has content → Use regular extraction
            // BP_SPTestActor case: IsDataOnly=False, has 5 functions → needs LoadObject
            // ============================================================================
            FString IsDataOnlyCheck;
            bool bIsTrulyDataOnly = AssetData.GetTagValue(FBlueprintTags::IsDataOnly, IsDataOnlyCheck) &&
                                    IsDataOnlyCheck == TEXT("True");

            if (bIsTrulyDataOnly)
            {
                // Truly empty Blueprint → Use fast path
                DirectChildSkippedCount++;

                UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[BlueprintMetadata] Auto Low Priority (Direct Child + Data-Only): %s (parent: %s)"),
                       *AssetData.AssetName.ToString(), *NativeParentClassName4);

                // Issue #1339: Use helper function for minimal Blueprint entry (DRY refactoring)
                // This also FIXES the missing saved_hash bug in DirectChild path!
                TSharedPtr<FJsonObject> BPJson = BlueprintMetadataHelpers::CreateMinimalBlueprintEntry(
                    AssetData,
                    TEXT(""),           // HashSuffix (empty = use ParentClassName)
                    false,              // bHighCostSkipped
                    ParentClassName4,   // PreExtractedParentClass
                    NativeParentClassName4  // PreExtractedNativeParentClass
                );

                // Add to Low Priority array (thread-safe)
                {
                    FScopeLock Lock(&BlueprintsArrayLock);
                    BlueprintsArray.Add(MakeShareable(new FJsonValueObject(BPJson)));
                }

                // Issue #1327 Phase 4: End DirectChild path timing
                if (bEnableProfiling) { AccumDirectChildPath += (FPlatformTime::Seconds() - PreFilterSectionStart); }

                continue;  // Skip LoadObject!
            }
            // else: Has content (IsDataOnly=False) → Fall through to regular extraction
        }
        // ============================================================================
        // End Issue #213 Phase 4
        // ============================================================================


        // Issue #212: Initialize profiling data for this Blueprint
        // Issue #213 Phase 4: Profiling toggle - declare variables with wider scope
        FCUBlueprintProfilingData ProfileData;
        double TotalStartTime = 0.0;

        if (bEnableProfiling)
        {
            ProfileData.BlueprintName = AssetData.AssetName.ToString();
            ProfileData.AssetPath = AssetData.GetObjectPathString();
            TotalStartTime = FPlatformTime::Seconds();
        }

        // Issue #199 Phase 1: Section 1 - LoadObject (expected 98.6% bottleneck)
        // Issue #1327: Conditional profiling to reduce ~15-20s overhead in non-profiling mode
        if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.1.LoadObject")); }

        // Issue #141 Phase 2: Optimized Blueprint loading
        // LOAD_DisableCompileOnLoad: Skip Blueprint compilation (30-40% faster)
        // Note: LOAD_DeferDependencyLoads NOT compatible with UBlueprint
        //       Engine assertion: check(!bIsBlueprintClass || !Object->HasAnyFlags(RF_NeedLoad) || !(LoadFlags & LOAD_DeferDependencyLoads))

        // Issue #213 Phase 4 Code Review 2: Always measure timing for logs and statistics
        double LoadStartTime = FPlatformTime::Seconds();

        UBlueprint* Blueprint = LoadObject<UBlueprint>(
            nullptr,
            *AssetData.GetObjectPathString(),
            nullptr,
            LOAD_DisableCompileOnLoad | LOAD_NoWarn
        );

        if (Blueprint)
        {
            OptimizedLoadCount++;

            // Issue #1310: Cache loaded Blueprint for BlueprintNodeTracing
            // Thread-safe: Uses lock since main loop may be parallelized
            {
                FScopeLock Lock(&BlueprintsCacheLock);
                LoadedBlueprintsCache.Add(AssetData.GetObjectPathString(), Blueprint);
            }
        }

        double LoadTimeMs = (FPlatformTime::Seconds() - LoadStartTime) * 1000.0;
        TotalLoadingTime += LoadTimeMs;  // Keep for statistics

        // Only save to profiling data when profiling is enabled
        if (bEnableProfiling)
        {
            ProfileData.LoadTime = LoadTimeMs;
        }

        if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.1.LoadObject")); }

        if (!Blueprint)
        {
            ErrorCount++;
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP] Failed to load Blueprint: %s (%.2f ms)"),
                   *AssetData.AssetName.ToString(), LoadTimeMs);
            continue;  // Skip to next iteration
        }

        // Performance logging (verbose level)
        UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[NarshaMCP][Perf] Blueprint loaded: %s (%.2f ms) [Optimized]"),
               *AssetData.AssetName.ToString(), LoadTimeMs);

        // Issue #199 Phase 1: Section 2 - Hash Generation
        if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.2.HashGeneration")); }

        // Issue #108 Phase 1 Incremental: Extract saved hash
        FString SavedHashStr;
        TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(AssetData.PackageName);
        if (PackageData.IsSet())
        {
            FIoHash SavedHash = PackageData->GetPackageSavedHash();
            // Issue #992 Priority 12 FIX: Only use hash if it's non-zero (valid)
            // Zero hashes occur for data-only blueprints - use fallback hash instead
            if (!SavedHash.IsZero())
            {
                SavedHashStr = LexToString(SavedHash);
            }
            else
            {
                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue #992] Zero hash detected for: %s (will use content-based hash)"),
                       *AssetData.GetObjectPathString());
            }
        }
        else
        {
            // Issue #992 Priority 12: PackageData not available (will use content-based fallback hash)
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue #992] No PackageData for: %s (will use content-based hash)"),
                   *AssetData.GetObjectPathString());
        }

        if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.2.HashGeneration")); }

        // Issue #199 Phase 1: Section 3 - Type Identification
        if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.3.TypeCast")); }

        // Check if this is an AnimBlueprint by casting
        UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
        UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Blueprint);  // Issue #135 Phase 3

        if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.3.TypeCast")); }

        if (AnimBP)
        {
            // Issue #212: Set Blueprint type and parent class
            // Issue #213 Phase 4: Only when profiling enabled
            if (bEnableProfiling)
            {
                ProfileData.BlueprintType = TEXT("AnimBlueprint");
                if (AnimBP->ParentClass)
                {
                    ProfileData.ParentClass = AnimBP->ParentClass->GetName();
                }
            }

            // Issue #199 Phase 1: Section 4A - AnimBP Extraction
            if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.4.AnimBP.Extract")); }

            // Issue #213 Phase 4 Code Review 2: Always measure timing
            double ExtractStartTime = FPlatformTime::Seconds();

            // Process as AnimBlueprint
            TSharedPtr<FJsonObject> AnimBPJson = MakeShareable(new FJsonObject);

            // Basic info
            AnimBPJson->SetStringField(TEXT("name"), AnimBP->GetName());
            AnimBPJson->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
            AnimBPJson->SetStringField(TEXT("type"), TEXT("AnimBlueprint"));

            // Parent class
            if (AnimBP->ParentClass)
            {
                AnimBPJson->SetStringField(TEXT("parent_class"), AnimBP->ParentClass->GetName());
            }

            // Issue #108 Phase 1 Incremental: Add saved hash
            if (!SavedHashStr.IsEmpty())
            {
                AnimBPJson->SetStringField(TEXT("saved_hash"), SavedHashStr);
            }

            // Extract AnimNotifies
            ExtractAnimBlueprintMetadata(AnimBP, AnimBPJson);

            // Only save to profiling data when profiling is enabled
            if (bEnableProfiling)
            {
                ProfileData.ExtractTime = (FPlatformTime::Seconds() - ExtractStartTime) * 1000.0;
            }

            if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.4.AnimBP.Extract")); }

            // Issue #199 Phase 1: Section 5 - JSON Aggregation
            if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.5.JsonAggregation")); }

            // Issue #213 Phase 4 Code Review 2: Always measure timing
            double SerializeStartTime = FPlatformTime::Seconds();

            // Thread-safe addition to AnimBPsArray
            {
                FScopeLock Lock(&AnimBPsArrayLock);
                AnimBPsArray.Add(MakeShareable(new FJsonValueObject(AnimBPJson)));
            }

            AnimBPProcessedCount++;

            // Only save to profiling data when profiling is enabled
            if (bEnableProfiling)
            {
                ProfileData.SerializeTime = (FPlatformTime::Seconds() - SerializeStartTime) * 1000.0;
            }

            if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.5.JsonAggregation")); }
        }
        else if (WidgetBP)  // Issue #135 Phase 3
        {
            // Issue #212: Set Blueprint type and parent class
            // Issue #213 Phase 4: Only when profiling enabled
            if (bEnableProfiling)
            {
                ProfileData.BlueprintType = TEXT("WidgetBlueprint");
                if (WidgetBP->ParentClass)
                {
                    ProfileData.ParentClass = WidgetBP->ParentClass->GetName();
                }
            }

            // Issue #199 Phase 1: Section 4B - WidgetBP Extraction
            if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.4.WidgetBP.Extract")); }

            // Issue #213 Phase 4 Code Review 2: Always measure timing
            double ExtractStartTime = FPlatformTime::Seconds();

            // Process as WidgetBlueprint
            TSharedPtr<FJsonObject> WidgetBPJson = MakeShareable(new FJsonObject);

            // Basic info
            WidgetBPJson->SetStringField(TEXT("name"), WidgetBP->GetName());
            WidgetBPJson->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
            WidgetBPJson->SetStringField(TEXT("type"), TEXT("WidgetBlueprint"));

            // Parent class
            if (WidgetBP->ParentClass)
            {
                WidgetBPJson->SetStringField(TEXT("parent_class"), WidgetBP->ParentClass->GetName());
            }

            // Issue #108 Phase 1 Incremental: Add saved hash
            if (!SavedHashStr.IsEmpty())
            {
                WidgetBPJson->SetStringField(TEXT("saved_hash"), SavedHashStr);
            }

            // Extract UMG widget tree and event bindings
            UMGExtractors::ExtractWidgetBlueprintMetadata(WidgetBP, WidgetBPJson);

            // Only save to profiling data when profiling is enabled
            if (bEnableProfiling)
            {
                ProfileData.ExtractTime = (FPlatformTime::Seconds() - ExtractStartTime) * 1000.0;
            }

            if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.4.WidgetBP.Extract")); }

            // Issue #199 Phase 1: Section 5 - JSON Aggregation
            if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.5.JsonAggregation")); }

            // Issue #213 Phase 4 Code Review 2: Always measure timing
            double SerializeStartTime = FPlatformTime::Seconds();

            // Thread-safe addition to WidgetBPsArray
            {
                FScopeLock Lock(&WidgetBPsArrayLock);
                WidgetBPsArray.Add(MakeShareable(new FJsonValueObject(WidgetBPJson)));
            }

            WidgetBPProcessedCount++;

            // Only save to profiling data when profiling is enabled
            if (bEnableProfiling)
            {
                ProfileData.SerializeTime = (FPlatformTime::Seconds() - SerializeStartTime) * 1000.0;
            }

            if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.5.JsonAggregation")); }
        }
        else
        {
            // Issue #212: Set Blueprint type and parent class
            // Issue #213 Phase 4: Only when profiling enabled
            if (bEnableProfiling)
            {
                ProfileData.BlueprintType = TEXT("Blueprint");
                if (Blueprint->ParentClass)
                {
                    ProfileData.ParentClass = Blueprint->ParentClass->GetName();
                }
            }

            // Issue #199 Phase 1: Section 4C - Blueprint Extraction
            if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.4.Blueprint.Extract")); }

            // Issue #213 Phase 4 Code Review 2: Always measure timing
            double ExtractStartTime = FPlatformTime::Seconds();

            // Issue #520 DEBUG: Track full extraction path for BP_SPTestActor
#if UE_CODEGEN_BUILD_DEBUG
            if (AssetData.AssetName.ToString() == TEXT("BP_SPTestActor"))
            {
                UE_LOG(LogNarshaMCPMetadata, Error, TEXT("[Issue #520 CRITICAL] BP_SPTestActor entering FULL EXTRACTION path"));
                UE_LOG(LogNarshaMCPMetadata, Error, TEXT("[Issue #520 CRITICAL] Blueprint valid: %s, GeneratedClass valid: %s"),
                       Blueprint ? TEXT("YES") : TEXT("NO"),
                       (Blueprint && Blueprint->GeneratedClass) ? TEXT("YES") : TEXT("NO"));
            }
#endif

            // Process as regular Blueprint
            TSharedPtr<FJsonObject> BPJson = ExtractBlueprintMetadata(Blueprint, AssetData);

            // Only save to profiling data when profiling is enabled
            if (bEnableProfiling)
            {
                ProfileData.ExtractTime = (FPlatformTime::Seconds() - ExtractStartTime) * 1000.0;
            }

            if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.4.Blueprint.Extract")); }

            // Issue #520 DEBUG: Check ExtractBlueprintMetadata result for BP_SPTestActor
#if UE_CODEGEN_BUILD_DEBUG
            if (AssetData.AssetName.ToString() == TEXT("BP_SPTestActor"))
            {
                UE_LOG(LogNarshaMCPMetadata, Error, TEXT("[Issue #520 CRITICAL] BP_SPTestActor ExtractBlueprintMetadata result: %s"),
                       BPJson.IsValid() ? TEXT("SUCCESS - Has metadata") : TEXT("FAILED - nullptr returned"));
            }
#endif

            if (BPJson.IsValid())
            {
                // Issue #212: Extract node/function/variable counts from JSON
                // Issue #213 Phase 4: Only when profiling enabled
                if (bEnableProfiling)
                {
                    ProfileData.NodeCount = BPJson->HasField(TEXT("node_count")) ?
                        (int32)BPJson->GetNumberField(TEXT("node_count")) : 0;
                    ProfileData.FunctionCount = BPJson->HasField(TEXT("function_count")) ?
                        (int32)BPJson->GetNumberField(TEXT("function_count")) : 0;
                    ProfileData.VariableCount = BPJson->HasField(TEXT("variable_count")) ?
                        (int32)BPJson->GetNumberField(TEXT("variable_count")) : 0;
                }

                // Issue #199 Phase 1: Section 5 - JSON Aggregation
                if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.5.JsonAggregation")); }

                // Issue #213 Phase 4 Code Review 2: Always measure timing
                double SerializeStartTime = FPlatformTime::Seconds();

                // Issue #108 Phase 1 Incremental: Add saved hash
                // Issue #992 Priority 12: Generate content-based fallback hash if PackageData unavailable
                if (SavedHashStr.IsEmpty())
                {
                    // For data-only blueprints without PackageData, generate a simple content hash
                    // using path + parent class (sufficient for detecting asset changes)
                    FString ContentToHash = AssetData.GetObjectPathString() + TEXT("|") +
                                           (Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));
                    SavedHashStr = FMD5::HashAnsiString(*ContentToHash);

                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue #992] Generated content-based hash for: %s"),
                           *AssetData.GetObjectPathString());
                }

                if (!SavedHashStr.IsEmpty())
                {
                    BPJson->SetStringField(TEXT("saved_hash"), SavedHashStr);
                }

                // Thread-safe addition to BlueprintsArray
                {
                    FScopeLock Lock(&BlueprintsArrayLock);
                    BlueprintsArray.Add(MakeShareable(new FJsonValueObject(BPJson)));
                }

                ProcessedCount++;

                // NOTE: Blueprint already cached at line ~663 via Issue #1310's FScopeLock + TMap.Add()

                // Only save to profiling data when profiling is enabled
                if (bEnableProfiling)
                {
                    ProfileData.SerializeTime = (FPlatformTime::Seconds() - SerializeStartTime) * 1000.0;
                }

                if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.5.JsonAggregation")); }
            }
            else
            {
                ErrorCount++;
            }
        }

        // Issue #212: Record total time and save profiling data
        // Issue #213 Phase 4: Only collect profiling data when -Profile flag is set (code review feedback)
        if (bEnableProfiling)
        {
            ProfileData.TotalTime = (FPlatformTime::Seconds() - TotalStartTime) * 1000.0;
            {
                FScopeLock Lock(&ProfilingResultsLock);
                ProfilingResults.Add(ProfileData);
            }
        }

        // Issue #23 Phase 4: Progress logging (10% intervals)
        int32 Completed = ++CompletedCount;
        if (Completed % ProgressStep == 0 || Completed == TotalCount)
        {
            float Progress = (float)Completed / TotalCount * 100.0f;
            float ElapsedSeconds = (FDateTime::UtcNow() - StartTime).GetTotalSeconds();
            float EstimatedTotal = (Progress > 0) ? (ElapsedSeconds / Progress * 100.0f) : 0.0f;
            float Remaining = EstimatedTotal - ElapsedSeconds;

            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Progress: %.1f%% (%d/%d) | Elapsed: %.1fs | Remaining: ~%.1fs"),
                   Progress, Completed, TotalCount, ElapsedSeconds, Remaining);
        }
    }

    // FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.BlueprintLoading+Processing"));

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Separated: %d regular Blueprints, %d AnimBlueprints, %d WidgetBlueprints"),
        ProcessedCount.Load(), AnimBPProcessedCount.Load(), WidgetBPProcessedCount.Load());

    // Issue #212: Save profiling results to CSV
    // Issue #213 Phase 4: Only save CSV when -Profile flag is set (code review feedback)
    if (bEnableProfiling)
    {
        SaveProfilingResultsToCSV(ProfilingResults, OutputDir);
    }

    // ============================================================
    // Issue #512: Standalone Animation Assets Extraction
    // ============================================================
    // Issue #1327 Phase 5: Profile Standalone Animation extraction
    if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.6.StandaloneAnimAssets")); }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Extracting standalone animation assets (AnimMontage, AnimComposite, AnimSequence)..."));

    // Search for all standalone animations (AnimMontage, AnimComposite, AnimSequence)
    FARFilter StandaloneAnimFilter;
    StandaloneAnimFilter.ClassPaths.Add(UAnimSequenceBase::StaticClass()->GetClassPathName());
    StandaloneAnimFilter.bRecursiveClasses = true;  // Include derived classes (AnimMontage, AnimComposite, etc.)

    TArray<FAssetData> StandaloneAnimAssets;
    AssetRegistry.GetAssets(StandaloneAnimFilter, StandaloneAnimAssets);

    // ============================================================================
    // Issue #1327 Phase 6: Engine Asset Skip List for StandaloneAnimAssets
    // ============================================================================
    // Purpose: Skip engine/plugin assets BEFORE expensive GetAsset() call
    // Rationale: Engine assets do not contribute to project-specific metadata
    //
    // Skipped paths:
    // - /Engine/   : Core UE animation assets (e.g., /Engine/Animation/)
    // - /Niagara/  : Niagara VFX plugin animations
    // - /Script/   : Script-generated assets (typically not user content)
    //
    // Performance: Saves ~20-100ms per skipped asset (GetAsset() is synchronous)
    // Note: FARFilter already filters by UAnimSequenceBase, so engine assets are rare
    // ============================================================================

    // Issue #1327 Phase 6: Diagnostic logging - count Engine vs Game assets BEFORE loading
    int32 EngineAnimAssetCount = 0;
    for (const FAssetData& AssetData : StandaloneAnimAssets)
    {
        FString PackagePath = AssetData.PackagePath.ToString();
        if (PackagePath.StartsWith(TEXT("/Engine/")) ||
            PackagePath.StartsWith(TEXT("/Niagara/")) ||
            PackagePath.StartsWith(TEXT("/Script/")))
        {
            EngineAnimAssetCount++;
        }
    }
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Found %d standalone animation assets (Engine: %d, Game: %d)"),
        StandaloneAnimAssets.Num(), EngineAnimAssetCount, StandaloneAnimAssets.Num() - EngineAnimAssetCount);

    // Track already-processed animations (from AnimBP packages) to avoid duplicates
    TSet<FString> ProcessedAnimPaths;
    // Note: This would require collecting paths from AnimBPMetadataArray during AnimBP processing
    // For now, we'll rely on asset loading check (if asset has notifies)

    TArray<TSharedPtr<FJsonValue>> StandaloneAnimsArray;
    int32 StandaloneAnimProcessedCount = 0;
    int32 StandaloneAnimSkippedCount = 0;
    int32 EngineSkippedCount = 0;  // Issue #1327: Track engine assets skipped

    for (const FAssetData& AssetData : StandaloneAnimAssets)
    {
        // Issue #1327 Phase 6: Engine Skip List - skip engine assets BEFORE expensive GetAsset() call
        FString PackagePath = AssetData.PackagePath.ToString();
        if (PackagePath.StartsWith(TEXT("/Engine/")) ||
            PackagePath.StartsWith(TEXT("/Niagara/")) ||
            PackagePath.StartsWith(TEXT("/Script/")))
        {
            EngineSkippedCount++;
            continue;  // Skip engine assets - no need to load them
        }

        // Load asset (required to access Notifies array)
        UAnimSequenceBase* AnimAsset = Cast<UAnimSequenceBase>(AssetData.GetAsset());
        if (!AnimAsset)
        {
            StandaloneAnimSkippedCount++;
            continue;  // Skip if failed to load
        }

        // Skip if no notifies (optimization - only extract animations with notifies)
        if (AnimAsset->Notifies.Num() == 0)
        {
            StandaloneAnimSkippedCount++;
            continue;
        }

        // Create JSON object for this animation asset
        TSharedPtr<FJsonObject> AnimJson = MakeShareable(new FJsonObject);
        AnimJson->SetStringField(TEXT("name"), AnimAsset->GetName());
        AnimJson->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
        AnimJson->SetStringField(TEXT("type"), AnimAsset->GetClass()->GetName());  // AnimMontage, AnimComposite, AnimSequence

        // Extract notifies (reuse existing logic from AnimationExtractors)
        TArray<TSharedPtr<FJsonValue>> NotifiesArray;
        for (const FAnimNotifyEvent& NotifyEvent : AnimAsset->Notifies)
        {
            TSharedPtr<FJsonObject> NotifyJson = MakeShareable(new FJsonObject);
            NotifyJson->SetStringField(TEXT("notify_name"), NotifyEvent.NotifyName.ToString());
            NotifyJson->SetNumberField(TEXT("trigger_time"), NotifyEvent.GetTriggerTime());
            NotifyJson->SetNumberField(TEXT("duration"), NotifyEvent.GetDuration());
            NotifyJson->SetStringField(TEXT("animation_sequence"), AnimAsset->GetName());

            // Extract notify type and gameplay tag using helper function
            FString GameplayTag;
            FString NotifyType;
            FString NotifyClass;
            AnimationExtractors::ExtractGameplayTagFromNotify(NotifyEvent, GameplayTag, NotifyType, NotifyClass);

            NotifyJson->SetStringField(TEXT("notify_type"), NotifyType);

            if (NotifyType == TEXT("gameplay_cue") && !GameplayTag.IsEmpty())
            {
                NotifyJson->SetStringField(TEXT("gameplay_tag"), GameplayTag);
            }
            else if (NotifyType == TEXT("generic") || NotifyType == TEXT("notify_state"))
            {
                NotifyJson->SetStringField(TEXT("notify_class"), NotifyClass);
            }

            NotifiesArray.Add(MakeShareable(new FJsonValueObject(NotifyJson)));
        }

        AnimJson->SetArrayField(TEXT("anim_notifies"), NotifiesArray);
        AnimJson->SetNumberField(TEXT("anim_notifies_count"), NotifiesArray.Num());

        StandaloneAnimsArray.Add(MakeShareable(new FJsonValueObject(AnimJson)));
        StandaloneAnimProcessedCount++;

        // Log progress every 50 assets
        if (StandaloneAnimProcessedCount % 50 == 0)
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Standalone animation progress: %d processed, %d skipped"),
                   StandaloneAnimProcessedCount, StandaloneAnimSkippedCount);
        }
    }

    // Store for later addition to RootJson (after BuildFinalJsonObject or MergeJsonData)
    // Note: We'll add this to RootJson after it's constructed (line ~1160)
    TArray<TSharedPtr<FJsonValue>> StandaloneAnimationsArrayForJson = StandaloneAnimsArray;
    int32 StandaloneAnimationsCountForJson = StandaloneAnimProcessedCount;

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Extracted %d standalone animation assets (%d skipped - no notifies, %d skipped - engine assets)"),
           StandaloneAnimProcessedCount, StandaloneAnimSkippedCount, EngineSkippedCount);

    // Issue #1327 Phase 5: End StandaloneAnimAssets profiling (BUG FIX - was missing!)
    if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.6.StandaloneAnimAssets")); }

#if WITH_EDITOR
    // PROFILING: StateTree processing
#if WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
    if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.StateTreeProcessing")); }

    // Find all StateTree assets (Issue #27 - v2.9.0+)
    FARFilter StateTreeFilter;
    StateTreeFilter.ClassPaths.Add(UStateTree::StaticClass()->GetClassPathName());
    StateTreeFilter.bRecursiveClasses = false;

    TArray<FAssetData> StateTreeAssetList;
    AssetRegistry.GetAssets(StateTreeFilter, StateTreeAssetList);

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Found %d StateTree assets"), StateTreeAssetList.Num());

    // Sequential StateTree processing (Issue #118 Bug Fix - ZenLoader crash)
    TArray<TSharedPtr<FJsonValue>> StateTreesArray;
    int32 StateTreeProcessedCount = 0;
    int32 StateTreeErrorCount = 0;

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Processing %d StateTrees sequentially..."), StateTreeAssetList.Num());

    // ParallelFor + GetAsset() causes "ZenLoader unable to FlushAsyncLoading" crashes
    for (int32 Index = 0; Index < StateTreeAssetList.Num(); ++Index)
    {
        const FAssetData& AssetData = StateTreeAssetList[Index];

        UStateTree* StateTree = Cast<UStateTree>(AssetData.GetAsset());
        if (!StateTree)
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP] Failed to load StateTree: %s"), *AssetData.GetObjectPathString());
            StateTreeErrorCount++;
            continue;
        }

        TSharedPtr<FJsonObject> STJson = ExtractStateTreeMetadata(StateTree, AssetData);
        if (STJson.IsValid())
        {
            StateTreesArray.Add(MakeShareable(new FJsonValueObject(STJson)));
            StateTreeProcessedCount++;
        }
        else
        {
            StateTreeErrorCount++;
        }
    }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] StateTrees - Processed: %d, Errors: %d"),
        StateTreeProcessedCount, StateTreeErrorCount);

    if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.StateTreeProcessing")); }
#else
    TArray<TSharedPtr<FJsonValue>> StateTreesArray;
    int32 StateTreeProcessedCount = 0;
    int32 StateTreeErrorCount = 0;
    {
        TSharedRef<FJsonObject> Unavailable = MakeShared<FJsonObject>();
        Unavailable->SetBoolField(TEXT("success"), false);
        Unavailable->SetStringField(TEXT("error_code"), TEXT("state_tree_plugin_unavailable"));
        Unavailable->SetStringField(TEXT("hint"),
            TEXT("Enable StateTree and PropertyBindingUtils in your .uproject, then rebuild."));
        StateTreesArray.Add(MakeShared<FJsonValueObject>(Unavailable));
    }
#endif

    // PROFILING: BehaviorTree processing
    if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.BehaviorTreeProcessing")); }

    // Find all BehaviorTree assets (Issue #27 Phase 2 - v2.9.0+)
    FARFilter BTFilter;
    BTFilter.ClassPaths.Add(UBehaviorTree::StaticClass()->GetClassPathName());
    BTFilter.bRecursiveClasses = false;

    TArray<FAssetData> BTAssetList;
    AssetRegistry.GetAssets(BTFilter, BTAssetList);

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Found %d BehaviorTree assets"), BTAssetList.Num());

    // Sequential BehaviorTree processing (Issue #118 Bug Fix - ZenLoader crash)
    TArray<TSharedPtr<FJsonValue>> BehaviorTreesArray;
    int32 BTProcessedCount = 0;
    int32 BTErrorCount = 0;

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Processing %d BehaviorTrees sequentially..."), BTAssetList.Num());

    for (int32 Index = 0; Index < BTAssetList.Num(); ++Index)
    {
        const FAssetData& AssetData = BTAssetList[Index];

        UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(AssetData.GetAsset());
        if (!BehaviorTree)
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP] Failed to load BehaviorTree: %s"), *AssetData.GetObjectPathString());
            BTErrorCount++;
            continue;
        }

        TSharedPtr<FJsonObject> BTJson = ExtractBehaviorTreeMetadata(BehaviorTree, AssetData);
        if (BTJson.IsValid())
        {
            BehaviorTreesArray.Add(MakeShareable(new FJsonValueObject(BTJson)));
            BTProcessedCount++;
        }
        else
        {
            BTErrorCount++;
        }
    }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] BehaviorTrees - Processed: %d, Errors: %d"),
        BTProcessedCount, BTErrorCount);

    if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.BehaviorTreeProcessing")); }

    TSharedPtr<FJsonObject> ValidationStats;
#if WITH_NARSHAMCP_ENHANCEDINPUT
    // PROFILING: InputAction processing
    if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.InputActionProcessing")); }

    // Find all InputAction assets (Issue #20 Phase 7 - v3.4.0)
    FARFilter InputActionFilter;
    InputActionFilter.ClassPaths.Add(UInputAction::StaticClass()->GetClassPathName());
    InputActionFilter.bRecursiveClasses = false;

    TArray<FAssetData> InputActionAssetList;
    AssetRegistry.GetAssets(InputActionFilter, InputActionAssetList);

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Found %d InputAction assets"), InputActionAssetList.Num());

    // Sequential InputAction processing (Issue #118 Bug Fix - ZenLoader crash)
    TArray<TSharedPtr<FJsonValue>> InputActionsArray;
    int32 InputActionProcessedCount = 0;
    int32 InputActionErrorCount = 0;

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Processing %d InputActions sequentially..."), InputActionAssetList.Num());

    for (int32 Index = 0; Index < InputActionAssetList.Num(); ++Index)
    {
        const FAssetData& AssetData = InputActionAssetList[Index];

        UInputAction* InputAction = Cast<UInputAction>(AssetData.GetAsset());
        if (!InputAction)
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP] Failed to load InputAction: %s"), *AssetData.GetObjectPathString());
            InputActionErrorCount++;
            continue;
        }

        TSharedPtr<FJsonObject> IAJson = ExtractInputActionMetadata(InputAction, AssetData);
        if (IAJson.IsValid())
        {
            InputActionsArray.Add(MakeShareable(new FJsonValueObject(IAJson)));
            InputActionProcessedCount++;
        }
        else
        {
            InputActionErrorCount++;
        }
    }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] InputActions - Processed: %d, Errors: %d"),
        InputActionProcessedCount, InputActionErrorCount);

    if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.InputActionProcessing")); }

    // PROFILING: InputMappingContext processing
    if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.InputMappingProcessing")); }

    // Find all InputMappingContext assets (Issue #20 Phase 7 - v3.4.0)
    FARFilter InputMappingFilter;
    InputMappingFilter.ClassPaths.Add(UInputMappingContext::StaticClass()->GetClassPathName());
    InputMappingFilter.bRecursiveClasses = false;

    TArray<FAssetData> InputMappingAssetList;
    AssetRegistry.GetAssets(InputMappingFilter, InputMappingAssetList);

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Found %d InputMappingContext assets"), InputMappingAssetList.Num());

    // Sequential InputMappingContext processing (Issue #118 Bug Fix - ZenLoader crash)
    TArray<TSharedPtr<FJsonValue>> InputMappingsArray;
    int32 InputMappingProcessedCount = 0;
    int32 InputMappingErrorCount = 0;

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Processing %d InputMappingContexts sequentially..."), InputMappingAssetList.Num());

    for (int32 Index = 0; Index < InputMappingAssetList.Num(); ++Index)
    {
        const FAssetData& AssetData = InputMappingAssetList[Index];

        UInputMappingContext* InputMapping = Cast<UInputMappingContext>(AssetData.GetAsset());
        if (!InputMapping)
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP] Failed to load InputMappingContext: %s"), *AssetData.GetObjectPathString());
            InputMappingErrorCount++;
            continue;
        }

        TSharedPtr<FJsonObject> IMCJson = ExtractInputMappingContextMetadata(InputMapping, AssetData);
        if (IMCJson.IsValid())
        {
            InputMappingsArray.Add(MakeShareable(new FJsonValueObject(IMCJson)));
            InputMappingProcessedCount++;
        }
        else
        {
            InputMappingErrorCount++;
        }
    }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] InputMappingContexts - Processed: %d, Errors: %d"),
        InputMappingProcessedCount, InputMappingErrorCount);

    if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.InputMappingProcessing")); }

    // ============================================================================
    // Issue #584 Phase 2A: Input Action to Ability Binding Extraction (v6.0.0)
    // ============================================================================
    if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.InputActionBindingExtraction")); }

    TArray<TSharedPtr<FJsonValue>> InputActionBindingsArray;
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Extracting Input Action to Ability bindings..."));

    // Call extractor function from BlueprintExtractors namespace
    ExtractInputActionBindings(AssetRegistry, InputActionBindingsArray);

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Input Action Bindings - Extracted: %d"), InputActionBindingsArray.Num());

    if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.InputActionBindingExtraction")); }

    // ============================================================================
    // Issue #584 Phase 2B: Blueprint Node Tracing (Input → ActivateAbility) (v6.0.0)
    // ============================================================================
    if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.BlueprintNodeTracing")); }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Starting Blueprint node tracing for Input Action bindings..."));

    // Issue #1327/#1310: Use cached Blueprints instead of re-loading (~15-25s savings)
    // Blueprints were already loaded and cached during the main extraction loop
    // Thread-safe extraction from TMap cache
    TArray<UBlueprint*> LoadedBlueprints;
    {
        FScopeLock Lock(&BlueprintsCacheLock);
        LoadedBlueprintsCache.GenerateValueArray(LoadedBlueprints);
    }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Using %d cached Blueprints for node tracing (Issue #1327/#1310 optimization)"), LoadedBlueprints.Num());

    // Call Phase 2B extractor function
    TArray<TSharedPtr<FJsonValue>> BlueprintBindingsArray;
    ExtractInputActionBindingsFromBlueprints(LoadedBlueprints, BlueprintBindingsArray);

    // Merge Phase 2A and Phase 2B results
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Phase 2A (Data Asset): %d bindings"), InputActionBindingsArray.Num());
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Phase 2B (Blueprint): %d bindings"), BlueprintBindingsArray.Num());

    // Append Blueprint bindings to Data Asset bindings
    InputActionBindingsArray.Append(BlueprintBindingsArray);

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Total Input Action Bindings: %d"), InputActionBindingsArray.Num());

    // ============================================================================
    // Issue #584 Phase 2F: Validation + Confidence Scoring (v6.0.0)
    // ============================================================================
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Phase 2F: Validating Input Action bindings..."));
    ValidateInputActionBindings(AssetRegistry, InputActionBindingsArray, ValidationStats);

    if (ValidationStats.IsValid())
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Validation complete:"));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP]   Total: %d"), (int32)ValidationStats->GetNumberField(TEXT("total_bindings")));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP]   Validated: %d"), (int32)ValidationStats->GetNumberField(TEXT("validated_bindings")));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP]   Failed: %d"), (int32)ValidationStats->GetNumberField(TEXT("failed_bindings")));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP]   Duplicates removed: %d"), (int32)ValidationStats->GetNumberField(TEXT("duplicate_bindings")));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP]   Final: %d"), (int32)ValidationStats->GetNumberField(TEXT("final_bindings")));
    }

    if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.BlueprintNodeTracing")); }

    // ============================================================================
    // Issue #155: Universal assets are now handled by separate UniversalAssetMetadataCommandlet
    // This keeps BlueprintMetadataCommandlet focused on Blueprint-specific data
    // and allows incremental mode optimization to remain active
    // ============================================================================
#else
    TArray<TSharedPtr<FJsonValue>> InputActionsArray;
    TArray<TSharedPtr<FJsonValue>> InputMappingsArray;
    TArray<TSharedPtr<FJsonValue>> InputActionBindingsArray;
    int32 InputActionProcessedCount = 0;
    int32 InputActionErrorCount = 0;
    int32 InputMappingProcessedCount = 0;
    int32 InputMappingErrorCount = 0;
    TSharedPtr<FJsonObject> Unavailable = MakeShared<FJsonObject>();
    Unavailable->SetBoolField(TEXT("success"), false);
    Unavailable->SetStringField(TEXT("error_code"), TEXT("enhanced_input_plugin_unavailable"));
    Unavailable->SetStringField(TEXT("hint"), TEXT("Enable EnhancedInput in your .uproject, then rebuild."));
    ValidationStats = Unavailable;
    InputActionBindingsArray.Add(MakeShared<FJsonValueObject>(Unavailable));
#endif
#endif

    // ============================================================================
    // Issue #108 Incremental: Merge new Blueprint data with existing JSON
    // ============================================================================
    if (bIncrementalMode && ExistingJson.IsValid())
    {
        // Issue #1327 Phase 5: Profile MergeJsonData
        if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.7.MergeJsonData")); }
        MergeJsonData(ExistingJson, BlueprintsArray, AnimBPsArray, WidgetBPsArray);
        if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.7.MergeJsonData")); }
        RootJson = ExistingJson;  // Use merged JSON

        // Issue #584 Phase 2A: Always rebuild Input/AI arrays in incremental mode
        // Rationale: These are project-level configs that change rarely,
        //            so full rebuild is acceptable vs complex merge logic
#if WITH_EDITOR
        RootJson->SetArrayField(TEXT("statetrees"), StateTreesArray);
        RootJson->SetArrayField(TEXT("behaviortrees"), BehaviorTreesArray);
        RootJson->SetArrayField(TEXT("input_actions"), InputActionsArray);
        RootJson->SetArrayField(TEXT("input_mappings"), InputMappingsArray);
        RootJson->SetArrayField(TEXT("input_ability_bindings"), InputActionBindingsArray);

        RootJson->SetNumberField(TEXT("total_statetrees"), StateTreesArray.Num());
        RootJson->SetNumberField(TEXT("total_behaviortrees"), BehaviorTreesArray.Num());
        RootJson->SetNumberField(TEXT("total_inputactions"), InputActionsArray.Num());
        RootJson->SetNumberField(TEXT("total_inputmappings"), InputMappingsArray.Num());
        RootJson->SetNumberField(TEXT("total_input_bindings"), InputActionBindingsArray.Num());
#endif

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Incremental mode: Merged %d new Blueprints with existing data"),
               AssetsToProcess.Num());
    }
    else
    {
        // Full build: Build final JSON from scratch
        // Issue #1327 Phase 5: Profile BuildFinalJsonObject
        if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.7.BuildFinalJson")); }
        TArray<TSharedPtr<FJsonValue>> EmptyAnimAssetsArray;
        RootJson = BuildFinalJsonObject(
        BlueprintsArray, AnimBPsArray, WidgetBPsArray, EmptyAnimAssetsArray,  // Issue #904: Keep empty (reserved for future AnimBP animation extraction)
#if WITH_EDITOR
        StateTreesArray, BehaviorTreesArray, InputActionsArray, InputMappingsArray, InputActionBindingsArray,  // Issue #584
#else
        TArray<TSharedPtr<FJsonValue>>(), TArray<TSharedPtr<FJsonValue>>(),
        TArray<TSharedPtr<FJsonValue>>(), TArray<TSharedPtr<FJsonValue>>(), TArray<TSharedPtr<FJsonValue>>(),
#endif
        ProcessedCount.Load(), ErrorCount.Load(),
        AnimBPProcessedCount.Load(), AnimBPErrorCount.Load(),
        WidgetBPProcessedCount.Load(), WidgetBPErrorCount.Load(),
        0, 0,  // Issue #904: AnimAsset counts (not implemented yet - reserved for future AnimBP animation extraction)
#if WITH_EDITOR
        StateTreeProcessedCount, StateTreeErrorCount,
        BTProcessedCount, BTErrorCount,
        InputActionProcessedCount, InputActionErrorCount,
        InputMappingProcessedCount, InputMappingErrorCount
#else
        0, 0, 0, 0, 0, 0, 0, 0  // Dummy values for non-editor builds
#endif
        );
        if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.7.BuildFinalJson")); }
    }

    // ============================================================================
    // Issue #584 Phase 2F: Add validation stats to JSON output
    // ============================================================================
#if WITH_EDITOR
    if (RootJson.IsValid() && ValidationStats.IsValid())
    {
        RootJson->SetObjectField(TEXT("input_binding_validation"), ValidationStats);
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Added validation stats to JSON output"));
    }
#endif

    // ============================================================================
    // Issue #512: Add standalone animation assets to RootJson
    // ============================================================================
    if (RootJson.IsValid())
    {
        RootJson->SetArrayField(TEXT("standalone_animations"), StandaloneAnimationsArrayForJson);
        RootJson->SetNumberField(TEXT("standalone_animations_count"), StandaloneAnimationsCountForJson);
        // Issue #904: Update total_anim_assets to match standalone_animations_count
        // - Full build: This OVERWRITES the 0 value set by BuildFinalJsonObject (line 1671)
        // - Incremental: This is the ONLY place where total_anim_assets gets updated
        RootJson->SetNumberField(TEXT("total_anim_assets"), StandaloneAnimationsCountForJson);
    }

    // ============================================================================
    // Issue #108 Phase 1 Incremental: Save JSON with metadata
    // ============================================================================

    // PROFILING: JSON save
    if (bEnableProfiling) { FCUCommandletHelpers::BeginProfileSection(TEXT("BlueprintMetadata.JsonSave")); }

    // Issue #1727 Phase 60: Commandlet outputs to _commandlet suffix (Rust Binary Parser is PRIMARY)
    FString OutputPath = OutputDir / TEXT("blueprint_metadata_commandlet.json");

    if (SaveJsonWithMetadata(OutputPath, RootJson, bIncrementalMode))
    {
        // Print statistics
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Processing complete:"));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Regular Blueprints - Processed: %d, Errors: %d"),
            ProcessedCount.Load(), ErrorCount.Load());
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] AnimBlueprints - Processed: %d, Errors: %d"),
            AnimBPProcessedCount.Load(), AnimBPErrorCount.Load());
        // Issue #108 Phase 1: AnimAsset processing temporarily removed (focus on Blueprint optimization)
        // UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Animation Assets - Processed: %d, Errors: %d"),
        //     AnimAssetProcessedCount.Load(), AnimAssetErrorCount.Load());
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] WidgetBlueprints - Processed: %d, Errors: %d"),
            WidgetBPProcessedCount.Load(), WidgetBPErrorCount.Load());
#if WITH_EDITOR
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] StateTrees - Processed: %d, Errors: %d"),
            StateTreeProcessedCount, StateTreeErrorCount);
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] BehaviorTrees - Processed: %d, Errors: %d"),
            BTProcessedCount, BTErrorCount);
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] InputActions - Processed: %d, Errors: %d"),
            InputActionProcessedCount, InputActionErrorCount);
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] InputMappings - Processed: %d, Errors: %d"),
            InputMappingProcessedCount, InputMappingErrorCount);
#endif

        // Issue #141 Phase 2: Loading optimization statistics
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
        if (bEnableProfiling)
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Loading Optimization (Issue #141 Phase 2):"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Total Blueprints Loaded: %d"), OptimizedLoadCount.Load());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Optimization: LOAD_DisableCompileOnLoad (skip compilation)"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Note: LOAD_DeferDependencyLoads incompatible with Blueprint (engine restriction)"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
        }

        // Issue #213 Phase 2: Data-Only Blueprint skip statistics
        if (bEnableProfiling && DataOnlySkippedCount.Load() > 0)
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Data-Only Blueprint Skip (Issue #213 Phase 2):"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Data-Only Blueprints Skipped: %d"), DataOnlySkippedCount.Load());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - LoadObject Skipped: %d BPs (IsDataOnly tag = True)"), DataOnlySkippedCount.Load());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Estimated Time Saved: ~%.2f seconds (avg 9.2ms per Data-Only BP)"),
                DataOnlySkippedCount.Load() * 9.2 / 1000.0);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Metadata: Extracted from FAssetData tags (parent_class, asset_path)"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
        }
        // Issue #213 Phase 4: Direct Child statistics
        const int32 DirectChildSkipped = DirectChildSkippedCount.Load();
        const int32 ActorDirect = ActorDirectCount.Load();
        const int32 PCGDirect = PCGDirectCount.Load();
        const int32 LandmassDirect = LandmassDirectCount.Load();

        if (bEnableProfiling && DirectChildSkipped > 0)
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Direct Child Skip (Issue #213 Phase 4):"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Total Direct Child BPs skipped: %d"), DirectChildSkipped);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("    Actor direct children:        %d"), ActorDirect);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("    PCG direct children:          %d"), PCGDirect);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("    Landmass direct children:     %d"), LandmassDirect);

            // Estimate time saved (110.7ms avg per BP)
            const double EstimatedTimeSaved = DirectChildSkipped * 110.7 / 1000.0;
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Estimated time saved:         %.2f seconds"), EstimatedTimeSaved);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Metadata: Minimal (no LoadObject for direct children)"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
        }

        // Issue #1327 Phase 2: High-Cost Skip statistics
        const int32 HighCostSkipped = HighCostSkippedCount.Load();
        if (bEnableProfiling && HighCostSkipped > 0)
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] High-Cost Blueprint Skip (Issue #1327 Phase 2):"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Total High-Cost BPs skipped: %d"), HighCostSkipped);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Skip List: PreCMCTick (4,347s = 72 min)"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Estimated time saved: ~4,347 seconds (from profiling data)"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
        }

        if (bEnableProfiling)
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Detailed Timing (Issue #141 Phase 2):"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Loading: %.2f seconds (%.1f ms avg per BP)"),
                TotalLoadingTime / 1000.0,
                OptimizedLoadCount.Load() > 0 ? TotalLoadingTime / OptimizedLoadCount.Load() : 0.0);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));

            // Issue #582: Log Property Type Coverage Statistics
            BlueprintExtractors::LogPropertyTypeCoverage();

            // Issue #1327 Phase 4: Pre-filter Profiling Statistics
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Pre-Filter Profiling (Issue #1327 Phase 4):"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - IsDataOnly Check:   %.3f sec (%d calls)"),
                AccumIsDataOnlyCheck, AssetsToProcess.Num());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Data-Only Path:     %.3f sec (%d BPs)"),
                AccumDataOnlyPath, DataOnlySkippedCount.Load());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - HighCost Check:     %.3f sec"),
                AccumHighCostCheck);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - HighCost Path:      %.3f sec (%d BPs)"),
                AccumHighCostPath, HighCostSkippedCount.Load());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - DirectChild Check:  %.3f sec"),
                AccumDirectChildCheck);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - DirectChild Path:   %.3f sec (%d BPs)"),
                AccumDirectChildPath, DirectChildSkippedCount.Load());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Total Pre-Filter:   %.3f sec"),
                AccumIsDataOnlyCheck + AccumDataOnlyPath + AccumHighCostCheck +
                AccumHighCostPath + AccumDirectChildCheck + AccumDirectChildPath);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
        }

        if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.JsonSave")); }

        // ====================================================================
        // Issue #261 Phase 1: Update timestamp after successful extraction
        // Issue #1132: Use helper function for DRY (Don't Repeat Yourself)
        // ====================================================================
        UpdateTimestampFile(TimestampPath);

        return 0; // Success
    }
    else
    {
        if (bEnableProfiling) { FCUCommandletHelpers::EndProfileSection(TEXT("BlueprintMetadata.JsonSave")); }

        UE_LOG(LogNarshaMCPMetadata, Error, TEXT("[NarshaMCP] Failed to save JSON"));
        return 1;
    }
}

// ============================================================================// Issue #108 Phase 1: Blueprint extraction functions moved to BlueprintExtractors namespace// ============================================================================// Functions migrated (378 lines)://   - ExtractBlueprintMetadata() → BlueprintExtractors::ExtractBlueprintMetadata()//   - ExtractFunctions() → BlueprintExtractors::ExtractFunctions()//   - ExtractCallRelationships() → BlueprintExtractors::ExtractCallRelationships()//   - ExtractVariables() → BlueprintExtractors::ExtractVariables()//   - ExtractGraphNodes() → BlueprintExtractors::ExtractGraphNodes()//   - ExtractNodePins() → BlueprintExtractors::ExtractNodePins()//// See: Source/NarshaMCP/Private/BlueprintMetadata/BlueprintExtractors.cpp (Lines 714-1094)// ============================================================================
// ============================================================================// Issue #108 Phase 1: Animation extraction functions moved to AnimationExtractors namespace// ============================================================================// Functions migrated (2 functions, ~118 lines)://   - ExtractAnimBlueprintMetadata() → AnimationExtractors::ExtractAnimBlueprintMetadata()//   - ExtractGameplayTagFromNotify() → AnimationExtractors::ExtractGameplayTagFromNotify()//// See: Source/NarshaMCP/Private/BlueprintMetadata/AnimationExtractors.cpp// ============================================================================

// Note (Issue #2368): FindOwningFunction() implementation removed (dead code)
// Active implementation is in BlueprintMetadataHelpers::FindOwningFunction()
// All callers use BlueprintMetadataHelpers::FindOwningFunction() via using directive

// ============================================================================
// Asset Change Tracking Helpers (Issue #1132)
// ============================================================================

bool UCUBlueprintMetadataCommandlet::UpdateTimestampFile(const FString& TimestampPath)
{
    // Get current UTC time in ISO8601 format
    FString TimestampStr = FDateTime::UtcNow().ToIso8601();

    // Ensure exactly one 'Z' suffix (defensive against ToIso8601 behavior changes)
    if (!TimestampStr.EndsWith(TEXT("Z")))
    {
        TimestampStr += TEXT("Z");
    }

    // Write timestamp to file
    if (FFileHelper::SaveStringToFile(TimestampStr, *TimestampPath))
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Updated timestamp: %s"), *TimestampStr);
        return true;
    }
    else
    {
        UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP] Failed to update timestamp file: %s"), *TimestampPath);
        return false;
    }
}

// ============================================================================
// StateTree Support (Issue #27 - v2.9.0+)
// ============================================================================

#if WITH_EDITOR
// ============================================================================
// Issue #108 Phase 1: StateTree extraction functions moved to AIExtractors namespace
// ============================================================================
// Functions migrated (2 functions, ~220 lines):
//   - ExtractStateTreeMetadata() → AIExtractors::ExtractStateTreeMetadata()
//   - ExtractStateTreeStateRecursive() → AIExtractors::ExtractStateTreeStateRecursive()
//
// See: Source/NarshaMCP/Private/BlueprintMetadata/AIExtractors.cpp (Lines 38-263)
// ============================================================================

// ============================================================================
// Issue #108 Phase 1: Input extraction functions moved to InputExtractors namespace
// ============================================================================
// Functions migrated (2 functions, ~138 lines):
//   - ExtractInputActionMetadata() → InputExtractors::ExtractInputActionMetadata()
//   - ExtractInputMappingContextMetadata() → InputExtractors::ExtractInputMappingContextMetadata()
//
// See: Source/NarshaMCP/Private/BlueprintMetadata/InputExtractors.cpp (Lines 20-163)
// ============================================================================

#endif // WITH_EDITOR

// ============================================================================
// Helper Functions (Refactoring v3.5.0)
// ============================================================================

// Note (Issue #327): IsRelevantGraphForExtraction() removed (dead code)
// Active implementation is in BlueprintMetadataHelpers.cpp:85 (blacklist approach)
// All callers use BlueprintMetadataHelpers::IsRelevantGraphForExtraction()

TSharedPtr<FJsonObject> UCUBlueprintMetadataCommandlet::CreateBasicAssetJson(const FString& AssetName, const FAssetData& AssetData)
{
    TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);

    Json->SetStringField(TEXT("name"), AssetName);
    Json->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
    Json->SetStringField(TEXT("package"), AssetData.PackageName.ToString());

    return Json;
}

// ============================================================================
// JSON Building Helpers (Phase 2 - v3.5.0)
// ============================================================================

TSharedPtr<FJsonObject> UCUBlueprintMetadataCommandlet::BuildFinalJsonObject(
    const TArray<TSharedPtr<FJsonValue>>& BlueprintsArray,
    const TArray<TSharedPtr<FJsonValue>>& AnimBPsArray,
    const TArray<TSharedPtr<FJsonValue>>& WidgetBPsArray,  // Issue #135 Phase 3
    const TArray<TSharedPtr<FJsonValue>>& AnimAssetsArray,
    const TArray<TSharedPtr<FJsonValue>>& StateTreesArray,
    const TArray<TSharedPtr<FJsonValue>>& BehaviorTreesArray,
    const TArray<TSharedPtr<FJsonValue>>& InputActionsArray,
    const TArray<TSharedPtr<FJsonValue>>& InputMappingsArray,
    const TArray<TSharedPtr<FJsonValue>>& InputActionBindingsArray,  // Issue #584 Phase 2A
    int32 ProcessedCount,
    int32 ErrorCount,
    int32 AnimBPProcessedCount,
    int32 AnimBPErrorCount,
    int32 WidgetBPProcessedCount,  // Issue #135 Phase 3
    int32 WidgetBPErrorCount,  // Issue #135 Phase 3
    int32 AnimAssetProcessedCount,
    int32 AnimAssetErrorCount,
    int32 StateTreeProcessedCount,
    int32 StateTreeErrorCount,
    int32 BTProcessedCount,
    int32 BTErrorCount,
    int32 InputActionProcessedCount,
    int32 InputActionErrorCount,
    int32 InputMappingProcessedCount,
    int32 InputMappingErrorCount
)
{
    TSharedPtr<FJsonObject> RootJson = MakeShareable(new FJsonObject);

    // Metadata
    RootJson->SetNumberField(TEXT("version"), 6);  // v6 with Input Action to Ability Bindings (Issue #584)
    RootJson->SetStringField(TEXT("generator"), TEXT("NarshaMCP::BlueprintMetadataCommandlet v6.0.0"));
    RootJson->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToString());

    // Blueprint counts
    RootJson->SetNumberField(TEXT("total_blueprints"), ProcessedCount);
    RootJson->SetNumberField(TEXT("total_anim_blueprints"), AnimBPProcessedCount);
    RootJson->SetNumberField(TEXT("total_anim_assets"), AnimAssetProcessedCount);
    RootJson->SetNumberField(TEXT("processed"), ProcessedCount);
    RootJson->SetNumberField(TEXT("errors"), ErrorCount);
    RootJson->SetNumberField(TEXT("anim_bp_processed"), AnimBPProcessedCount);
    RootJson->SetNumberField(TEXT("anim_bp_errors"), AnimBPErrorCount);
    RootJson->SetNumberField(TEXT("anim_asset_processed"), AnimAssetProcessedCount);
    RootJson->SetNumberField(TEXT("anim_asset_errors"), AnimAssetErrorCount);

    // WidgetBlueprint stats (Issue #135 Phase 3)
    RootJson->SetNumberField(TEXT("total_widgetbps"), WidgetBPProcessedCount);
    RootJson->SetNumberField(TEXT("widgetbp_processed"), WidgetBPProcessedCount);
    RootJson->SetNumberField(TEXT("widgetbp_errors"), WidgetBPErrorCount);

#if WITH_EDITOR
    // StateTree/BehaviorTree/Input counts
    RootJson->SetNumberField(TEXT("total_statetrees"), StateTreeProcessedCount);
    RootJson->SetNumberField(TEXT("statetree_processed"), StateTreeProcessedCount);
    RootJson->SetNumberField(TEXT("statetree_errors"), StateTreeErrorCount);
    RootJson->SetNumberField(TEXT("total_behaviortrees"), BTProcessedCount);
    RootJson->SetNumberField(TEXT("behaviortree_processed"), BTProcessedCount);
    RootJson->SetNumberField(TEXT("behaviortree_errors"), BTErrorCount);
    RootJson->SetNumberField(TEXT("total_inputactions"), InputActionProcessedCount);
    RootJson->SetNumberField(TEXT("inputaction_processed"), InputActionProcessedCount);
    RootJson->SetNumberField(TEXT("inputaction_errors"), InputActionErrorCount);
    RootJson->SetNumberField(TEXT("total_inputmappings"), InputMappingProcessedCount);
    RootJson->SetNumberField(TEXT("inputmapping_processed"), InputMappingProcessedCount);
    RootJson->SetNumberField(TEXT("inputmapping_errors"), InputMappingErrorCount);
    // Arrays

    RootJson->SetArrayField(TEXT("blueprints"), BlueprintsArray);
    RootJson->SetArrayField(TEXT("anim_blueprints"), AnimBPsArray);
    RootJson->SetArrayField(TEXT("widget_blueprints"), WidgetBPsArray);  // Issue #135 Phase 3
    RootJson->SetArrayField(TEXT("anim_assets"), AnimAssetsArray);

#if WITH_EDITOR
    RootJson->SetArrayField(TEXT("statetrees"), StateTreesArray);
    RootJson->SetArrayField(TEXT("behaviortrees"), BehaviorTreesArray);
    RootJson->SetArrayField(TEXT("input_actions"), InputActionsArray);
    RootJson->SetArrayField(TEXT("input_mappings"), InputMappingsArray);
    RootJson->SetArrayField(TEXT("input_ability_bindings"), InputActionBindingsArray);  // Issue #584 Phase 2A
    RootJson->SetNumberField(TEXT("total_input_bindings"), InputActionBindingsArray.Num());  // Issue #584
#endif
#endif // WITH_EDITOR (closes #if at line 794)

    return RootJson;
}

// Note (Issue #2368): EnrichNodeByType() implementation removed (dead code)
// Active implementation is in BlueprintMetadataHelpers::EnrichNodeByType()
// All callers use BlueprintMetadataHelpers::EnrichNodeByType() via using directive

// Issue #212: Save profiling results to CSV
void UCUBlueprintMetadataCommandlet::SaveProfilingResultsToCSV(const TArray<FCUBlueprintProfilingData>& ProfilingData, const FString& OutputDir)
{
    FString CSVPath = OutputDir / TEXT("blueprint_profiling.csv");

    // CSV header
    FString CSVContent = TEXT("BlueprintName,AssetPath,BlueprintType,ParentClass,NodeCount,FunctionCount,VariableCount,LoadTime,ExtractTime,SerializeTime,TotalTime\n");

    // Data rows
    for (const FCUBlueprintProfilingData& Data : ProfilingData)
    {
        // Escape commas in strings by wrapping in quotes
        FString SafeBlueprintName = Data.BlueprintName.Replace(TEXT(","), TEXT("_"));
        FString SafeAssetPath = Data.AssetPath.Replace(TEXT(","), TEXT("_"));
        FString SafeBlueprintType = Data.BlueprintType.Replace(TEXT(","), TEXT("_"));
        FString SafeParentClass = Data.ParentClass.Replace(TEXT(","), TEXT("_"));

        CSVContent += FString::Printf(
            TEXT("%s,%s,%s,%s,%d,%d,%d,%.6f,%.6f,%.6f,%.6f\n"),
            *SafeBlueprintName,
            *SafeAssetPath,
            *SafeBlueprintType,
            *SafeParentClass,
            Data.NodeCount,
            Data.FunctionCount,
            Data.VariableCount,
            Data.LoadTime,
            Data.ExtractTime,
            Data.SerializeTime,
            Data.TotalTime
        );
    }

    // Save CSV file
    if (FFileHelper::SaveStringToFile(CSVContent, *CSVPath))
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue #212] Profiling CSV saved: %s (%d blueprints)"),
               *CSVPath, ProfilingData.Num());
    }
    else
    {
        UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue #212] Failed to save profiling CSV: %s"), *CSVPath);
    }
}

// ============================================================================
// ============================================================================// Issue #108 Phase 1: ExtractCompositeNodeData moved to AIExtractors namespace// ============================================================================// Function migrated (~61 lines)://   - ExtractCompositeNodeData() → AIExtractors::ExtractCompositeNodeData()//// See: Source/NarshaMCP/Private/BlueprintMetadata/AIExtractors.cpp (Lines 306-367)// ============================================================================
