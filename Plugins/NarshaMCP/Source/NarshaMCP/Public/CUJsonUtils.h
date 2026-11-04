// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #3913: Shared JSON utility functions for NarshaMCP WebSocket responses
// Issue #4794: Extracted from CUWebSocketEditorSubsystem.cpp to fix Unity Build duplicate symbol

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace CUJsonUtils
{
	// Issue #14142: shared serialize + JSON response builders. Consolidates the
	// TJsonWriterFactory<> + FJsonSerializer::Serialize idiom (144x in CURemoteLibrary.cpp) and the
	// {"success": false, "error": <msg>} block (79x) that were hand-rolled across the WebSocket /
	// Remote Control library. Declared here; DEFINED in Private/CUJsonUtils.cpp so the heavy
	// Serialization/Dom includes stay out of this public header and respect the module's private
	// Json/JsonUtilities dependency boundary. EscapeJsonString below stays header-only (FORCEINLINE).

	/** Condensed-writer serialize of a JSON object to an FString (the canonical idiom). */
	NARSHAMCP_API FString SerializeToString(const TSharedRef<FJsonObject>& Obj);

	/** Convenience overload; returns an empty string for an invalid pointer. */
	NARSHAMCP_API FString SerializeToString(const TSharedPtr<FJsonObject>& Obj);

	/** Build + serialize a {"success": false, "error": Message} error response. */
	NARSHAMCP_API FString MakeErrorResponse(const FString& Message);

	/** Set "success": true on DataObject (created if null) and serialize it. */
	NARSHAMCP_API FString MakeSuccessResponse(const TSharedPtr<FJsonObject>& DataObject);


	/**
	 * Proper JSON string escaping for WebSocket responses (Issue #3913)
	 *
	 * ReplaceCharWithEscapedChar() doesn't escape backslashes properly for JSON.
	 * This function handles all JSON escape sequences including control characters.
	 *
	 * @param Input - Raw string to escape
	 * @return JSON-safe escaped string
	 */
	FORCEINLINE FString EscapeJsonString(const FString& Input)
	{
		FString Result;
		Result.Reserve(Input.Len() * 2);  // Worst case: every char needs escaping

		for (TCHAR Char : Input)
		{
			switch (Char)
			{
				case TEXT('\\'):  Result += TEXT("\\\\"); break;
				case TEXT('"'):   Result += TEXT("\\\""); break;
				case TEXT('\n'):  Result += TEXT("\\n");  break;
				case TEXT('\r'):  Result += TEXT("\\r");  break;
				case TEXT('\t'):  Result += TEXT("\\t");  break;
				case TEXT('\b'):  Result += TEXT("\\b");  break;
				case TEXT('\f'):  Result += TEXT("\\f");  break;
				default:
					// Control characters (0x00-0x1F) as \uXXXX
					if (Char < 0x20)
					{
						Result += FString::Printf(TEXT("\\u%04x"), (int32)Char);
					}
					else
					{
						Result += Char;
					}
					break;
			}
		}

		return Result;
	}
}
