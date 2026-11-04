// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #28: Blueprint Modification Support - Phase 3

#include "CUBlueprintManipulator.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"  // Issue #32: Compiler validation
#include "Kismet2/BlueprintEditorUtils.h"  // Issue #2947: FlushCompilationQueue
#include "Logging/TokenizedMessage.h"     // Issue #32: Error message extraction
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "SecurityLimits.h"  // Issue #1403: Path validation
#include "CULog.h"

// Helper: Log error message to stderr
void FCUBlueprintManipulator::LogError(const FString& Message)
{
    FPlatformMisc::LowLevelOutputDebugStringf(TEXT("[ERROR] BlueprintManipulator: %s\n"), *Message);
}

// Helper: Log info message to stderr
void FCUBlueprintManipulator::LogInfo(const FString& Message)
{
    FPlatformMisc::LowLevelOutputDebugStringf(TEXT("[INFO] BlueprintManipulator: %s\n"), *Message);
}

// 1. Find node by GUID
UEdGraphNode* FCUBlueprintManipulator::FindNodeByGuid(UBlueprint* Blueprint, const FGuid& NodeGuid)
{
    if (!Blueprint)
    {
        LogError(TEXT("FindNodeByGuid: Blueprint is null"));
        return nullptr;
    }

    if (!NodeGuid.IsValid())
    {
        LogError(TEXT("FindNodeByGuid: NodeGuid is invalid"));
        return nullptr;
    }

    // Search all graphs in Blueprint
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph)
        {
            continue;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->NodeGuid == NodeGuid)
            {
                LogInfo(FString::Printf(TEXT("Found node: %s (GUID: %s)"), *Node->GetName(), *NodeGuid.ToString()));
                return Node;
            }
        }
    }

    LogError(FString::Printf(TEXT("Node not found with GUID: %s"), *NodeGuid.ToString()));
    return nullptr;
}

// 2. Find pin by ID
UEdGraphPin* FCUBlueprintManipulator::FindPinById(UBlueprint* Blueprint, const FGuid& PinId, UEdGraphNode*& OutNode)
{
    if (!Blueprint)
    {
        LogError(TEXT("FindPinById: Blueprint is null"));
        return nullptr;
    }

    if (!PinId.IsValid())
    {
        LogError(TEXT("FindPinById: PinId is invalid"));
        return nullptr;
    }

    // Search all graphs
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph)
        {
            continue;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }

            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin && Pin->PinId == PinId)
                {
                    OutNode = Node;
                    LogInfo(FString::Printf(TEXT("Found pin: %s (ID: %s) on node %s"),
                        *Pin->PinName.ToString(), *PinId.ToString(), *Node->GetName()));
                    return Pin;
                }
            }
        }
    }

    LogError(FString::Printf(TEXT("Pin not found with ID: %s"), *PinId.ToString()));
    return nullptr;
}

// 3. Set pin default value
bool FCUBlueprintManipulator::SetPinDefaultValue(UEdGraphPin* Pin, const FString& NewValue)
{
    if (!Pin)
    {
        LogError(TEXT("SetPinDefaultValue: Pin is null"));
        return false;
    }

    // Validate pin is an input pin
    if (Pin->Direction != EGPD_Input)
    {
        LogError(FString::Printf(TEXT("SetPinDefaultValue: Pin '%s' is not an input pin"), *Pin->PinName.ToString()));
        return false;
    }

    // Check if pin has connections (can't set default if connected)
    if (Pin->LinkedTo.Num() > 0)
    {
        LogError(FString::Printf(TEXT("SetPinDefaultValue: Pin '%s' has connections, cannot set default"), *Pin->PinName.ToString()));
        return false;
    }

    // Set default value
    FString OldValue = Pin->DefaultValue;
    Pin->DefaultValue = NewValue;

    LogInfo(FString::Printf(TEXT("Set pin default: %s = '%s' (was '%s')"),
        *Pin->PinName.ToString(), *NewValue, *OldValue));

    return true;
}

// 4. Connect two pins via the K2 schema (Issue #15283 / #15336)
bool FCUBlueprintManipulator::ConnectPins(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin,
    TArray<UEdGraphNode*>* OutAutoInsertedNodes /*= nullptr*/,
    bool* OutReplacedExistingLink /*= nullptr*/)
{
    if (!SourcePin || !TargetPin)
    {
        LogError(TEXT("ConnectPins: One or both pins are null"));
        return false;
    }

    // Validate directions
    if (SourcePin->Direction != EGPD_Output)
    {
        LogError(FString::Printf(TEXT("ConnectPins: Source pin '%s' is not an output pin"), *SourcePin->PinName.ToString()));
        return false;
    }

    if (TargetPin->Direction != EGPD_Input)
    {
        LogError(FString::Printf(TEXT("ConnectPins: Target pin '%s' is not an input pin"), *TargetPin->PinName.ToString()));
        return false;
    }

    // Resolve the owning graph(s) + the K2 schema. The schema both validates the
    // connection (honest failure on CONNECT_RESPONSE_DISALLOW — Issue #15336) and
    // auto-inserts conversion nodes for convertible pin pairs (Issue #15283).
    UEdGraphNode* SourceNode = SourcePin->GetOwningNodeUnchecked();
    UEdGraphNode* TargetNode = TargetPin->GetOwningNodeUnchecked();
    UEdGraph* SourceGraph = SourceNode ? SourceNode->GetGraph() : nullptr;
    UEdGraph* TargetGraph = TargetNode ? TargetNode->GetGraph() : nullptr;
    if (!SourceGraph)
    {
        LogError(TEXT("ConnectPins: Source pin has no owning graph"));
        return false;
    }

    const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(SourceGraph->GetSchema());
    if (!K2Schema)
    {
        LogError(TEXT("ConnectPins: Graph is not a K2 (Blueprint) graph"));
        return false;
    }

    // Check if already connected (nothing to do, nothing auto-inserted)
    if (SourcePin->LinkedTo.Contains(TargetPin))
    {
        LogInfo(TEXT("ConnectPins: Pins are already connected"));
        return true;
    }

    // Pre-check for an engine-authoritative diagnostic on disallow. Replaces the
    // legacy hand-rolled ArePinsCompatible check (which was weaker than the schema
    // and produced false successes — Issue #15336).
    const FPinConnectionResponse Response = K2Schema->CanCreateConnection(SourcePin, TargetPin);
    if (Response.Response == CONNECT_RESPONSE_DISALLOW)
    {
        LogError(FString::Printf(TEXT("ConnectPins: schema disallows connection (%s -> %s): %s"),
            *SourcePin->PinType.PinCategory.ToString(),
            *TargetPin->PinType.PinCategory.ToString(),
            *Response.Message.ToString()));
        return false;
    }

    // Whether a pre-existing link is broken to make this one is finalized AFTER the
    // connect (see below). A BREAK_OTHERS verdict means the schema will silently break a
    // pre-existing link before making this one (single-input pins) — unlike the legacy
    // additive MakeLinkTo. But the outer verdict alone UNDER-reports: when the verdict is
    // MAKE_WITH_CONVERSION_NODE, the auto-inserted conversion node recursively breaks the
    // target's prior link inside AutowireConversionNode (TryCreateConnection(ConvOut,
    // TargetIn) -> BREAK_OTHERS_B), which never surfaces in this outer Response. So we
    // also snapshot the target pin's links here and observe the break post-connect
    // (Issue #15283, same rationale as conversion-node reporting).
    const bool bVerdictBreaksLink =
        Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_A ||
        Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_B ||
        Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_AB;
    const TSet<UEdGraphPin*> TargetLinksBefore(TargetPin->LinkedTo);

    // Snapshot nodes by pointer identity BEFORE connecting so we can detect any
    // conversion (cast) node the schema auto-inserts (Issue #15283). Diff by
    // pointer, not NodeGuid — freshly spawned nodes get new GUIDs. The engine
    // spawns the conversion node into the INPUT pin's owning graph
    // (CreateAutomaticConversionNodeAndConnections), which can differ from the
    // source pin's graph for cross-graph connects, so scan both graphs.
    TArray<UEdGraph*, TInlineAllocator<2>> ScanGraphs;
    ScanGraphs.Add(SourceGraph);
    if (TargetGraph && TargetGraph != SourceGraph)
    {
        ScanGraphs.Add(TargetGraph);
    }

    TSet<UEdGraphNode*> NodesBefore;
    for (UEdGraph* G : ScanGraphs)
    {
        NodesBefore.Append(G->Nodes);
    }

    if (!K2Schema->TryCreateConnection(SourcePin, TargetPin))
    {
        LogError(FString::Printf(TEXT("ConnectPins: TryCreateConnection failed (%s -> %s)"),
            *SourcePin->PinType.PinCategory.ToString(),
            *TargetPin->PinType.PinCategory.ToString()));
        return false;
    }

    // Report any nodes the schema added (auto-inserted conversion nodes). May be
    // empty (direct connection) or >1 (chained cast). Count unconditionally so the
    // log below is accurate even when the caller opts out of the node list.
    int32 AutoInsertedCount = 0;
    for (UEdGraph* G : ScanGraphs)
    {
        for (UEdGraphNode* Node : G->Nodes)
        {
            if (Node && !NodesBefore.Contains(Node))
            {
                if (OutAutoInsertedNodes)
                {
                    OutAutoInsertedNodes->Add(Node);
                }
                ++AutoInsertedCount;
            }
        }
    }

    // Issue #15283: finalize replaced_existing_link by combining the outer BREAK_OTHERS
    // verdict with an observation — a prior link on the (single-input) target pin that is
    // now gone. The observation catches the recursive break an auto-inserted conversion
    // node performs (which the outer MAKE_WITH_CONVERSION_NODE verdict does not encode),
    // so the flag is accurate even in the conversion-onto-already-linked-pin case.
    if (OutReplacedExistingLink)
    {
        bool bReplaced = bVerdictBreaksLink;
        if (!bReplaced)
        {
            for (UEdGraphPin* PrevLink : TargetLinksBefore)
            {
                if (!TargetPin->LinkedTo.Contains(PrevLink))
                {
                    bReplaced = true;
                    break;
                }
            }
        }
        *OutReplacedExistingLink = bReplaced;
    }

    FString InsertNote;
    if (AutoInsertedCount > 0)
    {
        InsertNote = FString::Printf(TEXT(" (auto-inserted %d conversion node%s)"),
            AutoInsertedCount, AutoInsertedCount == 1 ? TEXT("") : TEXT("s"));
    }
    LogInfo(FString::Printf(TEXT("Connected pins: %s -> %s%s"),
        *SourcePin->PinName.ToString(), *TargetPin->PinName.ToString(), *InsertNote));

    return true;
}

// 5. Disconnect pin
int32 FCUBlueprintManipulator::DisconnectPin(UEdGraphPin* Pin)
{
    if (!Pin)
    {
        LogError(TEXT("DisconnectPin: Pin is null"));
        return 0;
    }

    int32 DisconnectedCount = Pin->LinkedTo.Num();

    if (DisconnectedCount == 0)
    {
        LogInfo(FString::Printf(TEXT("DisconnectPin: Pin '%s' has no connections"), *Pin->PinName.ToString()));
        return 0;
    }

    Pin->BreakAllPinLinks();

    LogInfo(FString::Printf(TEXT("Disconnected pin '%s' from %d links"), *Pin->PinName.ToString(), DisconnectedCount));

    return DisconnectedCount;
}

// 6. Save Blueprint with compiler validation (Issue #32, #2947)
bool FCUBlueprintManipulator::SaveBlueprint(UBlueprint* Blueprint, bool bRecompile)
{
    if (!Blueprint)
    {
        LogError(TEXT("SaveBlueprint: Blueprint is null"));
        return false;
    }

    // Issue #2947: Flush any pending compilations before starting new one
    // This fixes sequential call failures where Blueprint is still compiling
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);

    // Recompile if requested with detailed error logging (Issue #32)
    if (bRecompile)
    {
        LogInfo(TEXT("Recompiling Blueprint..."));

        // Issue #2947: Mark as structurally modified to ensure fresh compile
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

        // Create compiler results log to capture compilation details
        FCompilerResultsLog ResultsLog;
        ResultsLog.bSilentMode = false;
        ResultsLog.bLogInfoOnly = false;

        // Compile Blueprint with results log
        FKismetEditorUtilities::CompileBlueprint(
            Blueprint,
            EBlueprintCompileOptions::SkipGarbageCollection,  // Issue #2947: Skip GC during sequential calls
            &ResultsLog
        );

        // Check for compilation errors (Issue #32)
        int32 NumErrors = ResultsLog.NumErrors;
        int32 NumWarnings = ResultsLog.NumWarnings;

        if (NumErrors > 0)
        {
            // Log compilation failure with error count
            UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("✗ Compilation failed: %d errors, %d warnings"), NumErrors, NumWarnings);

            // Extract and log detailed error messages
            for (const TSharedRef<FTokenizedMessage>& Message : ResultsLog.Messages)
            {
                if (Message->GetSeverity() == EMessageSeverity::Error)
                {
                    FString ErrorText = Message->ToText().ToString();
                    UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("   - %s"), *ErrorText);
                }
            }

            return false;  // Compilation failed - exit code 1
        }

        // Log successful compilation with warnings
        if (NumWarnings > 0)
        {
            UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("✓ Compilation successful (%d warnings)"), NumWarnings);

            // Log warning messages (first 5 only)
            int32 WarningCount = 0;
            for (const TSharedRef<FTokenizedMessage>& Message : ResultsLog.Messages)
            {
                if (Message->GetSeverity() == EMessageSeverity::Warning && WarningCount < 5)
                {
                    FString WarningText = Message->ToText().ToString();
                    UE_LOG(LogNarshaMCPBlueprint, Warning, TEXT("   - %s"), *WarningText);
                    WarningCount++;
                }
            }
        }
        else
        {
            UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("✓ Compilation successful (no warnings)"));
        }
    }

    // Get package
    UPackage* Package = Blueprint->GetOutermost();
    if (!Package)
    {
        LogError(TEXT("SaveBlueprint: Failed to get package"));
        return false;
    }

    // Issue #2947: Ensure package is fully loaded before saving
    if (!Package->IsFullyLoaded())
    {
        Package->FullyLoad();
        UE_LOG(LogNarshaMCPBlueprint, Log, TEXT("  Package fully loaded: %s"), *Package->GetName());
    }

    // Mark package as dirty
    Package->MarkPackageDirty();

    // Save package
    FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    UE_LOG(LogNarshaMCPBlueprint, Log, TEXT("  Saving to: %s"), *PackageFileName);

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;  // Issue #2947: Standard save flags

    if (UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs))
    {
        UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("✓ Blueprint saved successfully"));
        return true;
    }
    else
    {
        // Issue #2947: Enhanced error logging
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("✗ Failed to save Blueprint: %s"), *PackageFileName);
        return false;
    }
}

// 7. Load Blueprint
UBlueprint* FCUBlueprintManipulator::LoadBlueprint(const FString& BlueprintPath)
{
    if (BlueprintPath.IsEmpty())
    {
        LogError(TEXT("LoadBlueprint: BlueprintPath is empty"));
        return nullptr;
    }

    // Issue #7806: Use UE mount point system for dynamic path validation
    // Replaces hardcoded /Game/, /Engine/, /Script/ whitelist to support
    // plugin mount points like /ShooterCore/ in Lyra (consistent with #7118 read-path fix)
    FText PathReason;
    if (!FPackageName::IsValidLongPackageName(BlueprintPath, false, &PathReason))
    {
        LogError(FString::Printf(TEXT("LoadBlueprint: Invalid asset path: %s (%s)"), *BlueprintPath, *PathReason.ToString()));
        return nullptr;
    }

    // Issue #1403: Path traversal check using SecurityLimits helpers
    if (SecurityLimits::ContainsPathTraversal(BlueprintPath))
    {
        LogError(FString::Printf(TEXT("LoadBlueprint: Path traversal detected: %s"), *BlueprintPath));
        return nullptr;
    }

    if (SecurityLimits::ContainsEncodedPathTraversal(BlueprintPath))
    {
        LogError(FString::Printf(TEXT("LoadBlueprint: URL-encoded path traversal detected: %s"), *BlueprintPath));
        return nullptr;
    }

    // Load Blueprint asset
    UObject* LoadedObject = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath);
    UBlueprint* Blueprint = Cast<UBlueprint>(LoadedObject);

    if (Blueprint)
    {
        LogInfo(FString::Printf(TEXT("Loaded Blueprint: %s"), *BlueprintPath));
    }
    else
    {
        LogError(FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
    }

    return Blueprint;
}

// Note: the legacy hand-rolled ArePinsCompatible helper was removed in Issue
// #15283 / #15336. Pin compatibility + conversion-node insertion are now handled
// authoritatively by UEdGraphSchema_K2::CanCreateConnection / TryCreateConnection
// inside ConnectPins above.
