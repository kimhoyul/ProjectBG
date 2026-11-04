// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5820: M6 — Cache persistence (serialize/deserialize to JSON file)

#include "CUAnnotationData.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPCache, Log, All);

FString FCUAnnotationCache::SerializeToJson() const
{
	FScopeLock Lock(&CacheLock);
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	for (const auto& Pair : Entries)
	{
		// Skip placeholder entries (not worth persisting)
		if (Pair.Value.AnnotationText == FCUAnnotationData::AnalyzingPlaceholderText)
		{
			continue;
		}
		JsonArray.Add(MakeShared<FJsonValueObject>(Pair.Value.ToJson()));
	}
	FString OutputString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonArray, Writer);
	return OutputString;
}

bool FCUAnnotationCache::DeserializeFromJson(const FString& JsonString)
{
	FScopeLock Lock(&CacheLock);
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonArray))
	{
		UE_LOG(LogNarshaMCPCache, Warning, TEXT("Failed to parse annotation cache JSON"));
		return false;
	}
	Entries.Empty();
	AccessOrder.Empty();
	for (const TSharedPtr<FJsonValue>& Value : JsonArray)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (Value->TryGetObject(ObjPtr) && ObjPtr && ObjPtr->IsValid())
		{
			FCUAnnotationData Data = FCUAnnotationData::FromJson(*ObjPtr);
			if (!Data.AnnotationId.IsEmpty())
			{
				Entries.Add(Data.AnnotationId, Data);
				AccessOrder.Add(Data.AnnotationId);
			}
		}
	}
	EvictIfNeeded();
	return true;
}

bool FCUAnnotationCache::SaveToFile(const FString& FilePath) const
{
	const FString Dir = FPaths::GetPath(FilePath);
	if (!FPaths::DirectoryExists(Dir))
	{
		IFileManager::Get().MakeDirectory(*Dir, true);
	}
	const FString Json = SerializeToJson();
	if (FFileHelper::SaveStringToFile(Json, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogNarshaMCPCache, Log, TEXT("Saved %d annotations to %s"), Num(), *FilePath);
		return true;
	}
	UE_LOG(LogNarshaMCPCache, Warning, TEXT("Failed to save annotation cache to %s"), *FilePath);
	return false;
}

bool FCUAnnotationCache::LoadFromFile(const FString& FilePath)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogNarshaMCPCache, Log, TEXT("No annotation cache file at %s"), *FilePath);
		return false;
	}
	if (DeserializeFromJson(JsonString))
	{
		UE_LOG(LogNarshaMCPCache, Log, TEXT("Loaded %d annotations from %s"), Num(), *FilePath);
		return true;
	}
	return false;
}
