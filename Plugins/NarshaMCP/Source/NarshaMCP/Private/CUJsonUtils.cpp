// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #14142: definitions for the shared serialize + JSON response builders declared in
// Public/CUJsonUtils.h. Consolidates the TJsonWriterFactory<> + FJsonSerializer::Serialize idiom and
// the {"success": false, "error": <msg>} block that were hand-rolled across CURemoteLibrary.cpp.
// Kept in a .cpp (not the public header) so the Serialization/Dom includes — which depend on the
// module's PRIVATE Json/JsonUtilities modules — stay out of the public surface.

#include "CUJsonUtils.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace CUJsonUtils
{
	FString SerializeToString(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	FString SerializeToString(const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid())
		{
			return FString();
		}
		return SerializeToString(Obj.ToSharedRef());
	}

	FString MakeErrorResponse(const FString& Message)
	{
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetBoolField(TEXT("success"), false);
		Error->SetStringField(TEXT("error"), Message);
		return SerializeToString(Error);
	}

	FString MakeSuccessResponse(const TSharedPtr<FJsonObject>& DataObject)
	{
		// Match the legacy CreateSuccessJson contract: mutate the supplied object (set "success": true)
		// and serialize it; synthesize an empty object when none was provided.
		TSharedRef<FJsonObject> Result = DataObject.IsValid() ? DataObject.ToSharedRef() : MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		return SerializeToString(Result);
	}
}
