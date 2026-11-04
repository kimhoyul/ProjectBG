// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUCommandletBase.generated.h"

/**
 * Abstract base class for NarshaMCP metadata extraction commandlets.
 *
 * Implements Template Method design pattern with automatic profiling:
 * - Main() handles common initialization (parameters, directories, AssetRegistry)
 * - RunExtraction() is overridden by child classes for specific extraction logic
 * - Automatic profiling tracks AssetRegistry init time and extraction time
 * - Uses CommandletHelpers for consistency across all commandlets
 *
 * Benefits:
 * - Eliminates code duplication (7 Main() functions → 1 base implementation)
 * - Consistent error handling and logging
 * - Built-in performance profiling
 * - Simplified batch commandlet integration
 *
 * Usage:
 *   class UMyCommandlet : public UCUCommandletBase {
 *       virtual int32 RunExtraction(const FString& OutputDir, IAssetRegistry& Registry) override {
 *           // Your extraction logic here
 *           return 0;
 *       }
 *   };
 *
 * @since Issue #289 Phase 4-8 - CommandletBase Framework
 */
UCLASS(Abstract)
class NARSHAMCP_API UCUCommandletBase : public UCommandlet
{
	GENERATED_BODY()

public:
	UCUCommandletBase(const FObjectInitializer& ObjectInitializer);

	/**
	 * Main entry point (Template Method pattern)
	 *
	 * Implements common commandlet workflow:
	 * 1. Parse parameters (OutputDir)
	 * 2. Ensure output directory exists
	 * 3. Initialize AssetRegistry (with profiling)
	 * 4. Call RunExtraction() (with profiling)
	 * 5. Log results and profiling summary
	 *
	 * Child classes should NOT override this - override RunExtraction() instead.
	 *
	 * @param Params - Commandlet parameters (e.g., "OutputDir=D:/Custom/Path")
	 * @return 0 on success, non-zero on error
	 */
	virtual int32 Main(const FString& Params) override;

protected:
	/**
	 * Control whether to wait for AssetRegistry full scan (Hook for Issue #289 Phase 10)
	 *
	 * Override this to opt-out of AssetRegistry full scan wait time (30-50 seconds).
	 * Most commandlets should wait (true), but some like ValidateDesignPatterns
	 * only need runtime metadata and can skip the wait.
	 *
	 * @return true to wait for full scan (default), false to skip wait
	 *
	 * @note ValidateDesignPatternsCommandlet overrides this to return false
	 * @since Issue #289 Phase 10 (Performance regression fix)
	 */
	virtual bool ShouldWaitForAssetRegistry() const { return true; }

	/**
	 * Perform extraction work (Template Method)
	 *
	 * Child classes MUST override this to implement their specific extraction logic.
	 * Called automatically by Main() after common initialization.
	 *
	 * @param OutputDir - Output directory for generated files (guaranteed to exist)
	 * @param Registry - Initialized AssetRegistry reference (ready to use)
	 * @return 0 on success, non-zero on error count
	 */
	virtual int32 RunExtraction(const FString& OutputDir, class IAssetRegistry& Registry);
};
