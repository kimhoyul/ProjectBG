// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

// Shared internals for the AnimMixer RC surface (Issue #27002 Phase 5d).
//
// Phases 1-5c live in CURemoteSequencerAnimMixerLibrary.cpp, which had grown to
// ~3.4k lines by the time Phase 5d arrived. Rather than push it further, the
// bake op ships in its own translation unit and the pieces both files need live
// here. Nothing behavioural moved: these are the same helpers the original
// anonymous namespace declared, promoted to a shared header verbatim.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// The Sequencer Animation Mixer "layer" model on UMovieSceneAnimationMixerTrack
// (InsertLayer / GetLayer / AddNewAnimationOnRow / UMovieSceneAnimationMixerLayer /
// UMovieSceneLayerWeightDecoration) was introduced in UE 5.8. In 5.7 the plugin
// ships a layer-less track, so on 5.7 every AnimMixer function compiles but
// returns a graceful "requires UE 5.8+" decline. There is intentionally no
// engine-version router on the Rust side -- this macro is the only version branch.
#define NARSHAMCP_ANIMMIXER_ENGINE_SUPPORTED (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))

// Issue #27909: the AnimMixer engine modules are linked only when the CONSUMING project
// opts into MovieSceneAnimMixer / HierarchyTable / HierarchyTableAnimation itself.
// NarshaMCP.Build.cs defines this to 1/0 (NarshaAnimMixerLink.ShouldLink). The fallback
// keeps this header self-contained for any TU compiled without that define.
#ifndef NARSHA_WITH_ANIM_MIXER
// NarshaMCP.Build.cs always supplies this through PublicDefinitions, so reaching this
// fallback means the TU was compiled outside UBT's normal path. Fail CLOSED. Defaulting
// to 1 would assume the AnimMixer modules ARE linked exactly when we cannot know it, which
// is the same presume-the-opt-in mistake this file exists to stop -- and it is the
// direction that turns into a link error rather than a decline.
#define NARSHA_WITH_ANIM_MIXER 0
#endif

// Both conditions must hold: a new-enough engine AND the modules actually linked.
#define NARSHAMCP_ANIMMIXER_SUPPORTED (NARSHAMCP_ANIMMIXER_ENGINE_SUPPORTED && NARSHA_WITH_ANIM_MIXER)

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#include "LevelSequence.h"
#include "UObject/SoftObjectPath.h"
#endif

namespace NarshaAnimMixerInternal
{
	inline FString SerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	inline FString MakeErrorJson(const FString& Message)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), Message);
		return SerializeJson(Obj);
	}

	inline FString MakeErrorJson(const FString& Message, const TCHAR* ErrorCode)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), Message);
		if (ErrorCode && ErrorCode[0] != TEXT('\0'))
		{
			Obj->SetStringField(TEXT("error_code"), ErrorCode);
		}
		return SerializeJson(Obj);
	}

	inline FString MakeUnsupported(const TCHAR* FunctionName)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
#if !NARSHAMCP_ANIMMIXER_ENGINE_SUPPORTED
		// Engine-first: this string is the pre-existing 5.7 contract and is kept
		// byte-identical. A project on an older engine gets the same answer it always did,
		// regardless of whether it opted into the plugins.
		Obj->SetStringField(
			TEXT("error"),
			FString::Printf(
				TEXT("%s requires UE 5.8+ (Sequencer AnimMixer layer model). Engine is %d.%d."),
				FunctionName, ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION));
#elif !NARSHA_WITH_ANIM_MIXER
		// Issue #27909: engine is fine, the project simply has not opted in. Actionable,
		// and distinguishable from the version decline by its error_code.
		Obj->SetStringField(
			TEXT("error"),
			FString::Printf(
				TEXT("%s requires the AnimMixer plugins, which this project has not enabled."),
				FunctionName));
		Obj->SetStringField(TEXT("error_code"), TEXT("anim_mixer_plugin_unavailable"));
		Obj->SetStringField(
			TEXT("hint"),
			TEXT("Enable MovieSceneAnimMixer, HierarchyTable, and HierarchyTableAnimation in "
				 "your .uproject, then rebuild. Control-rig layer ops (layer_index >= 0) "
				 "additionally require MovieSceneMixedControlRig."));
#else
		// Unreachable: MakeUnsupported is only called from the !SUPPORTED branches. Kept so
		// the function still has a body when both conditions hold (an empty body would not
		// compile).
		Obj->SetStringField(TEXT("error"), FString::Printf(TEXT("%s is unavailable."), FunctionName));
#endif
		Obj->SetStringField(TEXT("engine_version"),
			FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION));
		return SerializeJson(Obj);
	}

	// Pure availability decision kept here so optional-plugin failure states can
	// be regression-tested without unloading an engine module from a live editor.
	// Issue #27909: whether the AnimMixer surface is compiled in at all. Pure so the
	// unavailable path is regression-testable without unlinking an engine module from a
	// live editor -- the same reason MixedControlRigAvailabilityErrorCode below is pure.
	//
	// The two are different questions and both can fail independently:
	//   this one          -- are MovieSceneAnimMixer / HierarchyTable* linked?
	//   MixedControlRig.. -- is the optional control-rig plugin loaded at runtime?
	inline const TCHAR* AnimMixerAvailabilityErrorCode(bool bAnimMixerLinked)
	{
		return bAnimMixerLinked ? nullptr : TEXT("anim_mixer_plugin_unavailable");
	}

	// Compile-time answer for the current build, expressed through the pure function so
	// callers and tests agree by construction.
	inline const TCHAR* AnimMixerAvailabilityErrorCodeForThisBuild()
	{
		return AnimMixerAvailabilityErrorCode(NARSHA_WITH_ANIM_MIXER != 0);
	}

	inline const TCHAR* MixedControlRigAvailabilityErrorCode(
		bool bPluginFound, bool bPluginEnabled, bool bModuleExists, bool bModuleLoaded)
	{
		return bPluginFound && bPluginEnabled && bModuleExists && bModuleLoaded
			? nullptr
			: TEXT("mixed_control_rig_unavailable");
	}

#if NARSHAMCP_ANIMMIXER_SUPPORTED
	inline ULevelSequence* LoadLevelSequence(const FString& SequencePath)
	{
		if (SequencePath.IsEmpty())
		{
			return nullptr;
		}
		// Accept both "/Game/Foo/LS" and "/Game/Foo/LS.LS".
		if (ULevelSequence* Direct = LoadObject<ULevelSequence>(nullptr, *SequencePath))
		{
			return Direct;
		}
		const FSoftObjectPath SoftPath(SequencePath);
		return Cast<ULevelSequence>(SoftPath.TryLoad());
	}
#endif // NARSHAMCP_ANIMMIXER_SUPPORTED
}
