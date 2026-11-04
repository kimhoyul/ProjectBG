// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandletFramework/CUCommandletBase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "CUAIMetadataCommandlet.generated.h"

// Forward declarations
struct FAssetData;
class IAssetRegistry;

// Version constant (AUTO-UPDATED by scripts/update_docs_chain.py)
#define AIMETADATA_COMMANDLET_VERSION TEXT("4.9.0")

/**
 * Commandlet for extracting AI (StateTree + BehaviorTree) metadata
 *
 * Usage:
 *   UnrealEditor-Cmd.exe YourProject.uproject -run=AIMetadata -OutputDir="Path/To/Output"
 *
 * Output:
 *   ai_metadata.json.gz - JSON file containing all AI asset metadata
 *
 * Features (Issue #848 - v4.9.0):
 *   - Extracts StateTree assets with complete structure
 *   - Extracts BehaviorTree assets with complete structure
 *   - Independent from Blueprint metadata (vs BlueprintMetadataCommandlet)
 *   - Compressed output (gzip) for efficient storage
 *   - 100% accurate (reuses AIExtractors.cpp from Issue #27)
 *
 * Architecture:
 *   - Reuses existing AIExtractors::ExtractStateTreeMetadata()
 *   - Reuses existing AIExtractors::ExtractBehaviorTreeMetadata()
 *   - Follows CommandletBase pattern (like the other metadata commandlets)
 *   - Integrates with CUBatch as "AIMetadata" task
 *
 * Related Issues:
 *   - Issue #27: StateTree/BehaviorTree extraction (original implementation)
 *   - Issue #844: ue_analyze_ai Ground Truth tests (consumer)
 *   - Issue #848: Independent AI metadata commandlet (this file)
 */
UCLASS()
class NARSHAMCP_API UCUAIMetadataCommandlet : public UCUCommandletBase
{
	GENERATED_BODY()

public:
	UCUAIMetadataCommandlet(const FObjectInitializer& ObjectInitializer);

protected:
	/**
	 * Run extraction logic - Overrides CommandletBase::RunExtraction()
	 */
	virtual int32 RunExtraction(const FString& OutputDir, IAssetRegistry& Registry) override;

public:
	/**
	 * Static extraction method (for batch commandlet execution)
	 *
	 * @param OutputDir - Output directory for JSON file
	 * @param AssetRegistry - Initialized AssetRegistry instance (shared in batch mode)
	 * @return 0 on success, non-zero on error
	 * @since v4.9.0 (Issue #848 - Independent AI Metadata Commandlet)
	 */
	static int32 RunAIExtraction(const FString& OutputDir, IAssetRegistry& AssetRegistry);
};
