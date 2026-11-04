// Copyright 2026 Next Stage Inc. All rights reserved.
// CULicenseTokenGenerator.cpp — Fab license token generation (Issue #7948)
//
// Implementation of license token generation (Issue #25867 Option A: unsigned).
// See CULicenseTokenGenerator.h for architecture overview.

// UHT IWYU rule (Issue #10170): the paired header must be the FIRST include.
#include "CULicenseTokenGenerator.h"

#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// SHA256 — vendored header-only implementation (MIT, picosha2 by okdshin).
// Historical note: this code previously linked bcrypt.lib via Windows CNG
// (BCryptOpenAlgorithmProvider / etc.). That introduced a fragile single-
// consumer system-lib dependency where UBT's BuildRules cache — invalidated
// only by .Build.cs mtime — could drop the lib from the linker rsp on SVN
// workstations using use-commit-times (Issue #9622; pragma mitigation in
// PR #9623). Issue #9625 eliminated the dependency entirely via this embed;
// no system lib, no pragma, no rsp sensitivity. CRITICAL RULE 22 still
// applies to any FUTURE Windows system lib addition — it simply has no
// current invocations in this plugin.
#include <string>
#include "picosha2.h"  // Issue #25804: Source/ThirdParty/picosha2 via Build.cs PublicIncludePaths

// Phase 2: Uncomment when PluginWarden IDs are assigned
// #include "IPluginWardenModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPLicense, Log, All);

/** Get plugin version string from .uplugin descriptor at runtime. */
static FString GetNarshaMCPVersion()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NarshaMCP"));
	if (Plugin.IsValid())
	{
		return Plugin->GetDescriptor().VersionName;
	}
	return TEXT("0.0.0");
}

/** Read a string value from Windows registry. Returns false if not found.
 *  Windows-only: HKEY / HKEY_LOCAL_MACHINE symbols are undefined under Linux
 *  clang. Non-Windows callers MUST guard call sites with #if PLATFORM_WINDOWS
 *  too, since the function signature itself references HKEY. */
#if PLATFORM_WINDOWS
static bool ReadRegistryString(HKEY RootKey, const TCHAR* SubKey, const TCHAR* ValueName, FString& OutValue)
{
	HKEY hKey = nullptr;
	if (RegOpenKeyExW(RootKey, SubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		WCHAR Buffer[512] = {0};
		DWORD BufferSize = sizeof(Buffer);
		DWORD Type = 0;
		LSTATUS Status = RegQueryValueExW(hKey, ValueName, nullptr, &Type, reinterpret_cast<LPBYTE>(Buffer), &BufferSize);
		RegCloseKey(hKey);

		if (Status == ERROR_SUCCESS && (Type == REG_SZ || Type == REG_EXPAND_SZ))
		{
			OutValue = FString(Buffer);
			return true;
		}
	}
	return false;
}
#endif // PLATFORM_WINDOWS

/** Compute SHA256 hash of UTF-8 byte array, return lowercase hex string (64 chars). */
static FString ComputeSHA256Hex(const uint8* Data, int32 DataLen)
{
	std::string HexOut;
	picosha2::hash256_hex_string(Data, Data + DataLen, HexOut);
	return FString(UTF8_TO_TCHAR(HexOut.c_str()));
}

bool FCULicenseTokenGenerator::Generate(const FString& ProjectDir)
{
	// Step 1: Check entitlement (Phase 1: always passes)
	if (!CheckEntitlement())
	{
		UE_LOG(LogNarshaMCPLicense, Warning, TEXT("PluginWarden entitlement check failed — no license token generated"));
		return false;
	}

	// Step 2: Generate machine ID
	const FString MachineId = GetMachineId();
	if (MachineId.IsEmpty())
	{
		UE_LOG(LogNarshaMCPLicense, Error, TEXT("Failed to generate machine ID"));
		return false;
	}

	// Step 3: Compute timestamps
	const int64 IssuedAt = FDateTime::UtcNow().ToUnixTimestamp();
	const int64 ExpiresAt = IssuedAt + TokenValiditySecs;

	// Step 4: Get plugin version from .uplugin descriptor
	const FString PluginVersion = GetNarshaMCPVersion();

	// Step 5: Build full token JSON. Signature is empty on purpose (Issue #25867
	// Option A): a private key must not ship in source, and the server never
	// consumed the signature. Keep the field so v=1 tokens stay parse-compatible.
	TSharedPtr<FJsonObject> TokenObj = MakeShareable(new FJsonObject());
	TokenObj->SetNumberField(TEXT("v"), 1);
	TokenObj->SetNumberField(TEXT("issued_at"), static_cast<double>(IssuedAt));
	TokenObj->SetNumberField(TEXT("expires_at"), static_cast<double>(ExpiresAt));
	TokenObj->SetStringField(TEXT("machine_id"), MachineId);
	TokenObj->SetStringField(TEXT("plugin_version"), PluginVersion);
	TokenObj->SetStringField(TEXT("signature"), TEXT(""));

	FString TokenJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TokenJson);
	FJsonSerializer::Serialize(TokenObj.ToSharedRef(), Writer);

	// Step 6: Write token to disk
	const FString TokenDir = FPaths::Combine(ProjectDir, TEXT("Intermediate/NarshaMCP"));
	if (!WriteTokenAtomic(TokenDir, TokenJson))
	{
		UE_LOG(LogNarshaMCPLicense, Error, TEXT("Failed to write license token"));
		return false;
	}

	UE_LOG(LogNarshaMCPLicense, Log,
		TEXT("License token generated (v%s, machine=%s...%s, expires=%lld)"),
		*PluginVersion,
		*MachineId.Left(8),
		*MachineId.Right(8),
		ExpiresAt);
	return true;
}

bool FCULicenseTokenGenerator::CheckEntitlement()
{
	// Phase 1: PluginWarden bypass — always returns true
	// Token is always generated; Firebase online check is the real guard.
	//
	// Phase 2: Uncomment the following when Epic assigns real ItemID/OfferID:
	//
	// if (FModuleManager::Get().IsModuleLoaded(TEXT("PluginWarden")))
	// {
	//     IPluginWardenModule& PluginWarden = IPluginWardenModule::Get();
	//     PluginWarden.CheckEntitlement(
	//         FText::FromString(TEXT("NarshaMCP")),
	//         FabItemId, FabOfferId, TEXT(""),
	//         FEntitlementResolved::CreateLambda([](bool bIsEntitled) { })
	//     );
	// }

	UE_LOG(LogNarshaMCPLicense, Verbose, TEXT("PluginWarden: Phase 1 bypass (always entitled)"));
	return true;
}

FString FCULicenseTokenGenerator::GetMachineId()
{
	// Must match Rust implementation: SHA256(cpu_brand + "\n" + hostname + "\n" + machine_guid)

	// 1. CPU brand from registry (Windows) or /proc/cpuinfo (Linux/Mac fallback)
	FString CpuBrand;
#if PLATFORM_WINDOWS
	if (!ReadRegistryString(
		HKEY_LOCAL_MACHINE,
		TEXT("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
		TEXT("ProcessorNameString"),
		CpuBrand))
	{
		CpuBrand = TEXT("unknown-cpu");
	}
#else
	// TODO(#9681 Phase 2): machine-id parity with the Rust generator on
	// non-Windows requires reading /proc/cpuinfo (Linux) or `sysctl` (macOS)
	// to get the actual CPU brand string. Until then, ALL non-Windows
	// machines collapse to the same "unknown-cpu" + "unknown-guid" tuple,
	// meaning per-machine token uniqueness is LOST on Linux/macOS. Acceptable
	// for #10089's enabler scope (token system is Phase 2 RSA-signed only),
	// but a dedicated cross-platform machine-id implementation is required
	// before token-based licensing goes live on these platforms.
	CpuBrand = TEXT("unknown-cpu");
#endif
	CpuBrand.TrimStartAndEndInline();

	// 2. Hostname — must match Rust's GetComputerNameExW(ComputerNamePhysicalDnsHostname)
	FString Hostname;
#if PLATFORM_WINDOWS
	WCHAR HostBuf[256] = {0};
	DWORD HostSize = 256;
	if (GetComputerNameExW(ComputerNamePhysicalDnsHostname, HostBuf, &HostSize))
	{
		Hostname = FString(HostSize, HostBuf);
	}
#endif
	if (Hostname.IsEmpty())
	{
		Hostname = FPlatformProcess::ComputerName();
	}
	if (Hostname.IsEmpty())
	{
		Hostname = TEXT("unknown-host");
	}

	// 3. Machine GUID from registry (Windows) — Linux/Mac use a stable opaque
	//    placeholder until Phase 2 ADR (#9681) lands cross-platform machine_id.
	FString MachineGuid;
#if PLATFORM_WINDOWS
	if (!ReadRegistryString(
		HKEY_LOCAL_MACHINE,
		TEXT("SOFTWARE\\Microsoft\\Cryptography"),
		TEXT("MachineGuid"),
		MachineGuid))
	{
		MachineGuid = TEXT("unknown-guid");
	}
#else
	MachineGuid = TEXT("unknown-guid");
#endif

	// 4. Compute SHA256 — must match Rust's SHA256(format!("{}\n{}\n{}", cpu, host, guid).as_bytes())
	const FString Input = FString::Printf(TEXT("%s\n%s\n%s"), *CpuBrand, *Hostname, *MachineGuid);
	FTCHARToUTF8 Utf8Input(*Input);

	return ComputeSHA256Hex(
		reinterpret_cast<const uint8*>(Utf8Input.Get()),
		Utf8Input.Length());
}

bool FCULicenseTokenGenerator::WriteTokenAtomic(const FString& TokenDir, const FString& TokenJson)
{
	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*TokenDir))
	{
		FileManager.MakeDirectory(*TokenDir, true);
	}

	// Atomic write: .tmp → rename (prevents race with exe reading)
	const FString TmpPath = FPaths::Combine(TokenDir, TEXT(".license_token.tmp"));
	const FString FinalPath = FPaths::Combine(TokenDir, TEXT(".license_token"));

	if (!FFileHelper::SaveStringToFile(TokenJson, *TmpPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogNarshaMCPLicense, Error, TEXT("Failed to write temp token: %s"), *TmpPath);
		return false;
	}

	// Remove existing token if present
	if (FileManager.FileExists(*FinalPath))
	{
		FileManager.Delete(*FinalPath);
	}

	// Rename .tmp → final
	if (!FileManager.Move(*FinalPath, *TmpPath))
	{
		UE_LOG(LogNarshaMCPLicense, Error, TEXT("Failed to stage token: %s → %s"), *TmpPath, *FinalPath);
		FileManager.Delete(*TmpPath);
		return false;
	}

	return true;
}
