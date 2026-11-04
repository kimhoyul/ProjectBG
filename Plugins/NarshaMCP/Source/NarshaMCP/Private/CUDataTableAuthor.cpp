// Copyright 2026 Next Stage Inc. All rights reserved. Issue #20524
#include "CUDataTableAuthor.h"
#include "CULog.h"
#include "Engine/DataTable.h"
// Issue #22834: public path is StructUtils/, not Engine/ (UE 5.7+ / 5.8).
#include "StructUtils/UserDefinedStruct.h"
#include "DataTableEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
bool LoadDataTableAuthorSpec(const FString& SpecJson, TSharedPtr<FJsonObject>& OutSpec, FString& OutError){
 OutSpec.Reset(); if (SpecJson.IsEmpty()){ OutSpec=MakeShared<FJsonObject>(); return true; }
 const TSharedRef<TJsonReader<>> Reader=TJsonReaderFactory<>::Create(SpecJson);
 if (!FJsonSerializer::Deserialize(Reader, OutSpec) || !OutSpec.IsValid()){ OutError=TEXT("bad SpecJson"); return false; } return true; }
void FillRows(UDataTable* Table, FCUDataTableAuthorResult& Out){
 Out.Rows.Reset(); if(!Table) return;
 if (const UScriptStruct* RS = Table->GetRowStruct()) { Out.RowStructPath = RS->GetPathName(); }
 for (const FName& N : Table->GetRowNames()) { FCUDataTableRowInfo R; R.Name=N.ToString(); Out.Rows.Add(R); }
 Out.RowCount = Out.Rows.Num();
}
bool SaveTable(UDataTable* Table, FCUDataTableAuthorResult& Out){
 if(!Table){ Out.Error=TEXT("null table"); return false; }
 UPackage* Package=Table->GetOutermost(); Package->MarkPackageDirty();
 FString PackageFilename;
 if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension())) { Out.Error=TEXT("path convert failed"); return false; }
 FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
 if (!UPackage::SavePackage(Package, Table, *PackageFilename, SaveArgs)) { Out.Error=TEXT("SavePackage failed"); return false; }
 Out.SavedFilePath = PackageFilename; return true;
}
} // namespace

bool FCUDataTableAuthor::Execute(const FCUDataTableAuthorParams& In, FCUDataTableAuthorResult& Out)
{
 Out = FCUDataTableAuthorResult{};
 const FString Op = In.Operation.TrimStartAndEnd().ToLower();
 const FString PackagePath = In.TablePackagePath.TrimStartAndEnd();
 if (PackagePath.IsEmpty()) { Out.Error=TEXT("TablePackagePath empty"); return false; }
 TSharedPtr<FJsonObject> Spec; if (!LoadDataTableAuthorSpec(In.SpecJson, Spec, Out.Error)) return false;

 if (Op == TEXT("create_datatable"))
 {
  const FString RowStructPath = Spec->GetStringField(TEXT("row_struct_path")).TrimStartAndEnd();
  if (RowStructPath.IsEmpty()) { Out.Error=TEXT("create_datatable requires row_struct_path"); return false; }
  UScriptStruct* RowStruct = LoadObject<UScriptStruct>(nullptr, *RowStructPath);
  if (!RowStruct) { Out.Error=FString::Printf(TEXT("failed to load row struct %s"), *RowStructPath); return false; }
  FString PackageName=PackagePath; FString AssetName=FPackageName::GetLongPackageAssetName(PackageName);
  UPackage* Package=CreatePackage(*PackageName); if(!Package){ Out.Error=TEXT("CreatePackage failed"); return false; }
  Package->FullyLoad();
  UDataTable* Table = NewObject<UDataTable>(Package, FName(*AssetName), RF_Public|RF_Standalone|RF_Transactional);
  if (!Table) { Out.Error=TEXT("NewObject UDataTable failed"); return false; }
  Table->RowStruct = RowStruct;
  FAssetRegistryModule::AssetCreated(Table);
  FillRows(Table, Out);
  if (!SaveTable(Table, Out)) return false;
  Out.bSuccess=true; return true;
 }

 UDataTable* Table = LoadObject<UDataTable>(nullptr, *PackagePath);
 if (!Table) { Out.Error=FString::Printf(TEXT("failed to load DataTable %s"), *PackagePath); return false; }

 if (Op == TEXT("get_datatable")) { FillRows(Table, Out); Out.bSuccess=true; return true; }

 if (Op == TEXT("add_row"))
 {
  FString RowName = Spec->GetStringField(TEXT("row_name")).TrimStartAndEnd();
  if (RowName.IsEmpty()) { Out.Error=TEXT("add_row requires row_name"); return false; }
  // Empty row with default-constructed struct data.
  // UDataTable has no row-struct size helper; size lives on the row UScriptStruct.
  const UScriptStruct* RowStruct = Table->GetRowStruct();
  if (!RowStruct)
  {
   Out.Error = TEXT("add_row: DataTable has no row struct");
   return false;
  }
  // Issue #23061: refuse non-FTableRowBase row structs before reinterpret_cast.
  // UE asset authoring usually enforces this, but the cast is otherwise unchecked.
  if (!RowStruct->IsChildOf(FTableRowBase::StaticStruct()))
  {
   Out.Error = FString::Printf(
    TEXT("add_row: row struct '%s' is not derived from FTableRowBase"),
    *RowStruct->GetName());
   return false;
  }
  uint8* RowData = static_cast<uint8*>(FMemory::Malloc(RowStruct->GetStructureSize()));
  RowStruct->InitializeStruct(RowData);
  Table->AddRow(FName(*RowName), *reinterpret_cast<FTableRowBase*>(RowData));
  RowStruct->DestroyStruct(RowData);
  FMemory::Free(RowData);
  FillRows(Table, Out);
  if (!SaveTable(Table, Out)) return false;
  Out.bSuccess=true; return true;
 }

 if (Op == TEXT("remove_row"))
 {
  FString RowName = Spec->GetStringField(TEXT("row_name")).TrimStartAndEnd();
  if (RowName.IsEmpty()) { Out.Error=TEXT("remove_row requires row_name"); return false; }
  Table->RemoveRow(FName(*RowName));
  FillRows(Table, Out);
  if (!SaveTable(Table, Out)) return false;
  Out.bSuccess=true; return true;
 }

 Out.Error=FString::Printf(TEXT("unknown operation: %s"), *In.Operation);
 return false;
}
