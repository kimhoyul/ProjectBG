// Copyright 2026 Next Stage Inc. All rights reserved.
//
// Issue #22752 — Persona / Animation Editor preview transport.
// Opens (or focuses) the animation asset editor, then drives the preview
// UAnimPreviewInstance via IPersonaToolkit → IPersonaPreviewScene →
// UDebugSkelMeshComponent. Mirrors the sequencer play/pause/stop/scrub axis.
//
// Issue #22855 — Persona preview viewport camera (set/get/focus). Uses the
// FEditorViewportClient bound to the same IPersonaPreviewScene (not the
// Level Editor viewport — see CUWebSocketEditorSubsystem::HandleControlCamera).

#include "CURemoteLibrary.h"
#include "CUAnimAuthoring_Shared.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationAsset.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimPreviewInstance.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IAnimationEditor.h"
#include "IPersonaToolkit.h"
#include "IPersonaPreviewScene.h"
// Issue #22834: AnimationEditorPreviewScene.h lives under Persona Private —
// not visible to plugin modules even when Persona is a private dep. This
// TU only needs IPersonaPreviewScene (already included above).
#include "PersonaModule.h"
#endif

using namespace NarshaAnimAuthoring;

namespace
{
#if WITH_EDITOR
	static UAnimationAsset* LoadAnimationAsset(const FString& AssetPath, FString& OutError)
	{
		if (AssetPath.IsEmpty())
		{
			OutError = TEXT("AssetPath is empty");
			return nullptr;
		}
		UObject* Obj = StaticLoadObject(UAnimationAsset::StaticClass(), nullptr, *AssetPath);
		UAnimationAsset* Anim = Cast<UAnimationAsset>(Obj);
		if (!Anim)
		{
			OutError = FString::Printf(TEXT("Not an animation asset (or not found): %s"), *AssetPath);
			return nullptr;
		}
		return Anim;
	}

	/** Open/focus the animation editor and return its Persona preview scene. */
	static TSharedPtr<IPersonaPreviewScene> ResolvePreviewScene(UAnimationAsset* Asset, FString& OutError)
	{
		if (!GEditor || !Asset)
		{
			OutError = TEXT("Editor not available");
			return nullptr;
		}

		UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!AES)
		{
			OutError = TEXT("AssetEditorSubsystem unavailable");
			return nullptr;
		}

		// Open / focus so Persona toolkit is live.
		AES->OpenEditorForAsset(Asset);

		IAssetEditorInstance* Inst = AES->FindEditorForAsset(Asset, /*bFocusIfOpen*/ true);
		if (!Inst)
		{
			OutError = TEXT("No open editor for asset after OpenEditorForAsset");
			return nullptr;
		}

		// IAnimationEditor is the Persona host for AnimSequence/AnimMontage/etc.
		// Issue #23061: the previous empty-if "guard" claimed crash prevention but
		// always fell through to GetPersonaToolkit(). Fail closed on unknown host
		// names so a wrong editor type cannot crash here.
		const FName EditorName = Inst->GetEditorName();
		const bool bKnownPersonaHost =
			EditorName == FName(TEXT("AnimationEditor"))
			|| EditorName == FName(TEXT("AnimationBlueprintEditor"))
			|| EditorName == FName(TEXT("SkeletalMeshEditor"));
		if (!bKnownPersonaHost)
		{
			OutError = FString::Printf(
				TEXT("Asset editor '%s' is not a known Persona host; refusing GetPersonaToolkit (Issue #23061)"),
				*EditorName.ToString());
			return nullptr;
		}

		IAnimationEditor* AnimEditor = static_cast<IAnimationEditor*>(Inst);
		TSharedRef<IPersonaToolkit> Toolkit = AnimEditor->GetPersonaToolkit();
		return Toolkit->GetPreviewScene();
	}

	/** Resolve Persona preview mesh for an open (or newly opened) animation editor. */
	static UDebugSkelMeshComponent* ResolvePreviewMesh(UAnimationAsset* Asset, FString& OutError)
	{
		TSharedPtr<IPersonaPreviewScene> PreviewScene = ResolvePreviewScene(Asset, OutError);
		if (!PreviewScene.IsValid())
		{
			return nullptr;
		}

		UDebugSkelMeshComponent* Mesh = PreviewScene->GetPreviewMeshComponent();
		if (!Mesh)
		{
			OutError = TEXT("Persona preview mesh component is null (open asset may lack skeleton/preview mesh)");
			return nullptr;
		}

		// Ensure preview instance targets this animation asset.
		Mesh->EnablePreview(true, Asset);

		return Mesh;
	}

	/**
	 * Find the FEditorViewportClient bound to this Persona preview scene.
	 * Scans GEditor->GetAllViewportClients() and matches GetPreviewScene().
	 * Does NOT use LevelEditor GetFirstActiveViewport (level camera path).
	 */
	static FEditorViewportClient* ResolvePersonaViewportClient(
		const TSharedRef<IPersonaPreviewScene>& PreviewScene,
		FString& OutError)
	{
		if (!GEditor)
		{
			OutError = TEXT("Editor not available");
			return nullptr;
		}

		// IPersonaPreviewScene : FAdvancedPreviewScene : FPreviewScene
		FPreviewScene* TargetScene = static_cast<FPreviewScene*>(&PreviewScene.Get());
		for (FEditorViewportClient* Client : GEditor->GetAllViewportClients())
		{
			if (Client && Client->GetPreviewScene() == TargetScene)
			{
				return Client;
			}
		}

		OutError = TEXT("No Persona viewport client bound to this preview scene (is the Animation Editor tab open?)");
		return nullptr;
	}

	static TSharedRef<FJsonObject> MakeCameraObject(
		const FString& AssetPath,
		const FString& Operation,
		const FEditorViewportClient& Client)
	{
		const FVector Loc = Client.GetViewLocation();
		const FRotator Rot = Client.GetViewRotation();
		const float FOV = Client.ViewFOV;

		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), true);
		Obj->SetStringField(TEXT("asset_path"), AssetPath);
		Obj->SetStringField(TEXT("operation"), Operation);

		TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
		Location->SetNumberField(TEXT("x"), Loc.X);
		Location->SetNumberField(TEXT("y"), Loc.Y);
		Location->SetNumberField(TEXT("z"), Loc.Z);
		Obj->SetObjectField(TEXT("location"), Location);

		TSharedRef<FJsonObject> Rotation = MakeShared<FJsonObject>();
		Rotation->SetNumberField(TEXT("pitch"), Rot.Pitch);
		Rotation->SetNumberField(TEXT("yaw"), Rot.Yaw);
		Rotation->SetNumberField(TEXT("roll"), Rot.Roll);
		Obj->SetObjectField(TEXT("rotation"), Rotation);

		Obj->SetNumberField(TEXT("fov"), FOV);
		return Obj;
	}

	static UAnimPreviewInstance* ResolvePreviewInstance(
		UDebugSkelMeshComponent* Mesh,
		UAnimationAsset* Asset,
		FString& OutError)
	{
		if (!Mesh)
		{
			OutError = TEXT("Preview mesh is null");
			return nullptr;
		}
		UAnimPreviewInstance* Preview = Cast<UAnimPreviewInstance>(Mesh->PreviewInstance);
		if (!Preview && Asset)
		{
			// Force create preview instance if missing.
			Mesh->EnablePreview(true, Asset);
			Preview = Cast<UAnimPreviewInstance>(Mesh->PreviewInstance);
		}
		if (!Preview)
		{
			OutError = TEXT("UAnimPreviewInstance unavailable on preview mesh");
			return nullptr;
		}
		return Preview;
	}

	static float GetPreviewLengthSec(UAnimPreviewInstance* Preview, UAnimationAsset* Asset)
	{
		if (Preview)
		{
			const float Len = Preview->GetLength();
			if (Len > 0.0f)
			{
				return Len;
			}
		}
		if (const UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(Asset))
		{
			return Seq->GetPlayLength();
		}
		return 0.0f;
	}

	static TSharedRef<FJsonObject> MakeStateObject(
		const FString& AssetPath,
		UAnimPreviewInstance* Preview,
		UAnimationAsset* Asset,
		const FString& Operation)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), true);
		Obj->SetStringField(TEXT("asset_path"), AssetPath);
		Obj->SetStringField(TEXT("operation"), Operation);

		const float Length = GetPreviewLengthSec(Preview, Asset);
		const float Time = Preview ? Preview->GetCurrentTime() : 0.0f;
		const bool bPlaying = Preview ? Preview->IsPlaying() : false;
		const bool bLoop = Preview ? Preview->IsLooping() : false;
		// GetSamplingFrameRate is on UAnimSequenceBase, not UAnimationAsset.
		// Issue #23061: advertise whether sample_rate is measured vs default fallback
		// so consumers can tell 30.0 default from a real sequence rate.
		float Rate = 30.0f;
		FString SampleRateSource = TEXT("default");
		if (const UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(Asset))
		{
			const float Measured = Seq->GetSamplingFrameRate().AsDecimal();
			if (Measured > 0.0f)
			{
				Rate = Measured;
				SampleRateSource = TEXT("measured");
			}
		}
		const float SafeRate = Rate > 0.0f ? Rate : 30.0f;
		const float Frame = Time * SafeRate;
		const float Percent = Length > 0.0f ? (Time / Length) * 100.0f : 0.0f;

		Obj->SetBoolField(TEXT("playing"), bPlaying);
		Obj->SetNumberField(TEXT("time"), Time);
		Obj->SetNumberField(TEXT("frame"), Frame);
		Obj->SetNumberField(TEXT("percent"), Percent);
		Obj->SetNumberField(TEXT("length"), Length);
		Obj->SetBoolField(TEXT("loop"), bLoop);
		Obj->SetNumberField(TEXT("sample_rate"), SafeRate);
		Obj->SetStringField(TEXT("sample_rate_source"), SampleRateSource);
		return Obj;
	}
#endif // WITH_EDITOR
} // namespace

FString UCURemoteLibrary::AnimPreview_Play(const FString& AssetPath, bool bLoop)
{
#if !WITH_EDITOR
	return SerializeJsonResponse(MakeErrorObject(AssetPath, TEXT("Editor-only operation")));
#else
	FString Err;
	UAnimationAsset* Asset = LoadAnimationAsset(AssetPath, Err);
	if (!Asset)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}
	UDebugSkelMeshComponent* Mesh = ResolvePreviewMesh(Asset, Err);
	if (!Mesh)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err, TEXT("Ensure Persona can open this asset with a preview mesh/skeleton")));
	}
	UAnimPreviewInstance* Preview = ResolvePreviewInstance(Mesh, Asset, Err);
	if (!Preview)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	Preview->SetLooping(bLoop);
	Preview->SetPlaying(true);

	return SerializeJsonResponse(MakeStateObject(AssetPath, Preview, Asset, TEXT("preview_play")));
#endif
}

FString UCURemoteLibrary::AnimPreview_Pause(const FString& AssetPath)
{
#if !WITH_EDITOR
	return SerializeJsonResponse(MakeErrorObject(AssetPath, TEXT("Editor-only operation")));
#else
	FString Err;
	UAnimationAsset* Asset = LoadAnimationAsset(AssetPath, Err);
	if (!Asset)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}
	UDebugSkelMeshComponent* Mesh = ResolvePreviewMesh(Asset, Err);
	if (!Mesh)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}
	UAnimPreviewInstance* Preview = ResolvePreviewInstance(Mesh, Asset, Err);
	if (!Preview)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	Preview->SetPlaying(false);
	return SerializeJsonResponse(MakeStateObject(AssetPath, Preview, Asset, TEXT("preview_pause")));
#endif
}

FString UCURemoteLibrary::AnimPreview_Stop(const FString& AssetPath)
{
#if !WITH_EDITOR
	return SerializeJsonResponse(MakeErrorObject(AssetPath, TEXT("Editor-only operation")));
#else
	FString Err;
	UAnimationAsset* Asset = LoadAnimationAsset(AssetPath, Err);
	if (!Asset)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}
	UDebugSkelMeshComponent* Mesh = ResolvePreviewMesh(Asset, Err);
	if (!Mesh)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}
	UAnimPreviewInstance* Preview = ResolvePreviewInstance(Mesh, Asset, Err);
	if (!Preview)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	Preview->SetPlaying(false);
	Preview->SetPosition(0.0f, /*bFireNotifies*/ false);
	return SerializeJsonResponse(MakeStateObject(AssetPath, Preview, Asset, TEXT("preview_stop")));
#endif
}

FString UCURemoteLibrary::AnimPreview_Scrub(
	const FString& AssetPath,
	float TimeSec,
	float Frame,
	float Percent)
{
#if !WITH_EDITOR
	return SerializeJsonResponse(MakeErrorObject(AssetPath, TEXT("Editor-only operation")));
#else
	FString Err;
	UAnimationAsset* Asset = LoadAnimationAsset(AssetPath, Err);
	if (!Asset)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}
	UDebugSkelMeshComponent* Mesh = ResolvePreviewMesh(Asset, Err);
	if (!Mesh)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}
	UAnimPreviewInstance* Preview = ResolvePreviewInstance(Mesh, Asset, Err);
	if (!Preview)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	const float Length = GetPreviewLengthSec(Preview, Asset);
	// GetSamplingFrameRate is on UAnimSequenceBase, not UAnimationAsset (see :215).
	float Rate = 30.0f;
	if (const UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(Asset))
	{
		Rate = Seq->GetSamplingFrameRate().AsDecimal();
	}
	const float SafeRate = Rate > 0.0f ? Rate : 30.0f;

	float Target = 0.0f;
	if (TimeSec >= 0.0f)
	{
		Target = TimeSec;
	}
	else if (Frame >= 0.0f)
	{
		Target = Frame / SafeRate;
	}
	else if (Percent >= 0.0f)
	{
		Target = (FMath::Clamp(Percent, 0.0f, 100.0f) / 100.0f) * Length;
	}
	else
	{
		return SerializeJsonResponse(MakeErrorObject(
			AssetPath,
			TEXT("Provide TimeSec (>=0), Frame (>=0), or Percent ([0,100]); unused axes as -1")));
	}

	if (Length > 0.0f)
	{
		Target = FMath::Clamp(Target, 0.0f, Length);
	}
	else
	{
		Target = FMath::Max(0.0f, Target);
	}

	// Scrub while paused so state is stable for capture / notify inspection.
	Preview->SetPlaying(false);
	Preview->SetPosition(Target, /*bFireNotifies*/ false);

	TSharedRef<FJsonObject> Obj = MakeStateObject(AssetPath, Preview, Asset, TEXT("preview_scrub"));
	Obj->SetNumberField(TEXT("scrub_time"), Target);
	return SerializeJsonResponse(Obj);
#endif
}

FString UCURemoteLibrary::AnimPreview_GetState(const FString& AssetPath)
{
#if !WITH_EDITOR
	return SerializeJsonResponse(MakeErrorObject(AssetPath, TEXT("Editor-only operation")));
#else
	FString Err;
	UAnimationAsset* Asset = LoadAnimationAsset(AssetPath, Err);
	if (!Asset)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}
	UDebugSkelMeshComponent* Mesh = ResolvePreviewMesh(Asset, Err);
	if (!Mesh)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}
	UAnimPreviewInstance* Preview = ResolvePreviewInstance(Mesh, Asset, Err);
	if (!Preview)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	return SerializeJsonResponse(MakeStateObject(AssetPath, Preview, Asset, TEXT("preview_get_state")));
#endif
}

// ------------------------------------------------------------------
// Issue #22855 — Persona preview camera
// ------------------------------------------------------------------

FString UCURemoteLibrary::AnimPreview_SetCamera(
	const FString& AssetPath,
	bool bSetLocation,
	float LocX,
	float LocY,
	float LocZ,
	bool bSetRotation,
	float Pitch,
	float Yaw,
	float Roll,
	bool bSetFOV,
	float FOV)
{
#if !WITH_EDITOR
	return SerializeJsonResponse(MakeErrorObject(AssetPath, TEXT("Editor-only operation")));
#else
	if (!bSetLocation && !bSetRotation && !bSetFOV)
	{
		return SerializeJsonResponse(MakeErrorObject(
			AssetPath,
			TEXT("Provide at least one of location, rotation, or fov"),
			TEXT("Pass bSetLocation / bSetRotation / bSetFOV true for the axes to update")));
	}

	FString Err;
	UAnimationAsset* Asset = LoadAnimationAsset(AssetPath, Err);
	if (!Asset)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	TSharedPtr<IPersonaPreviewScene> PreviewScene = ResolvePreviewScene(Asset, Err);
	if (!PreviewScene.IsValid())
	{
		return SerializeJsonResponse(MakeErrorObject(
			AssetPath,
			Err,
			TEXT("Ensure Persona can open this asset (AnimSequence / AnimMontage / etc.)")));
	}

	// Keep mesh preview targeting this asset (same as transport path).
	if (UDebugSkelMeshComponent* Mesh = PreviewScene->GetPreviewMeshComponent())
	{
		Mesh->EnablePreview(true, Asset);
	}

	FEditorViewportClient* Client = ResolvePersonaViewportClient(PreviewScene.ToSharedRef(), Err);
	if (!Client)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	if (bSetLocation)
	{
		Client->SetViewLocation(FVector(LocX, LocY, LocZ));
	}
	if (bSetRotation)
	{
		Client->SetViewRotation(FRotator(Pitch, Yaw, Roll));
	}
	if (bSetFOV)
	{
		// Clamp FOV to a sane editor range (matches typical FEditorViewportClient bounds).
		Client->ViewFOV = FMath::Clamp(FOV, 5.0f, 170.0f);
	}

	Client->Invalidate();
	PreviewScene->InvalidateViews();

	return SerializeJsonResponse(MakeCameraObject(AssetPath, TEXT("preview_set_camera"), *Client));
#endif
}

FString UCURemoteLibrary::AnimPreview_GetCamera(const FString& AssetPath)
{
#if !WITH_EDITOR
	return SerializeJsonResponse(MakeErrorObject(AssetPath, TEXT("Editor-only operation")));
#else
	FString Err;
	UAnimationAsset* Asset = LoadAnimationAsset(AssetPath, Err);
	if (!Asset)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	TSharedPtr<IPersonaPreviewScene> PreviewScene = ResolvePreviewScene(Asset, Err);
	if (!PreviewScene.IsValid())
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	FEditorViewportClient* Client = ResolvePersonaViewportClient(PreviewScene.ToSharedRef(), Err);
	if (!Client)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	return SerializeJsonResponse(MakeCameraObject(AssetPath, TEXT("preview_get_camera"), *Client));
#endif
}

FString UCURemoteLibrary::AnimPreview_Focus(const FString& AssetPath)
{
#if !WITH_EDITOR
	return SerializeJsonResponse(MakeErrorObject(AssetPath, TEXT("Editor-only operation")));
#else
	FString Err;
	UAnimationAsset* Asset = LoadAnimationAsset(AssetPath, Err);
	if (!Asset)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath, Err));
	}

	TSharedPtr<IPersonaPreviewScene> PreviewScene = ResolvePreviewScene(Asset, Err);
	if (!PreviewScene.IsValid())
	{
		return SerializeJsonResponse(MakeErrorObject(
			AssetPath,
			Err,
			TEXT("Ensure Persona can open this asset with a preview mesh/skeleton")));
	}

	UDebugSkelMeshComponent* Mesh = PreviewScene->GetPreviewMeshComponent();
	if (Mesh)
	{
		Mesh->EnablePreview(true, Asset);
	}

	// Primary: Persona multi-view focus (registered OnFocusViews delegates).
	PreviewScene->FocusViews();

	// Also frame the active viewport client on mesh bounds so a single-view
	// editor still orbits the asset even if FocusViews is a no-op.
	FEditorViewportClient* Client = ResolvePersonaViewportClient(PreviewScene.ToSharedRef(), Err);
	if (Client && Mesh)
	{
		const FBoxSphereBounds Bounds = Mesh->Bounds;
		if (Bounds.SphereRadius > KINDA_SMALL_NUMBER)
		{
			Client->FocusViewportOnBox(Bounds.GetBox(), /*bInstant*/ true);
		}
		Client->Invalidate();
	}
	else if (!Client)
	{
		// FocusViews may still have fired via registered viewport delegates.
		// Return success with a warning rather than hard-failing.
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), true);
		Obj->SetStringField(TEXT("asset_path"), AssetPath);
		Obj->SetStringField(TEXT("operation"), TEXT("preview_focus"));
		Obj->SetBoolField(TEXT("focused"), true);
		Obj->SetStringField(TEXT("warning"), Err);
		return SerializeJsonResponse(Obj);
	}

	if (Client)
	{
		TSharedRef<FJsonObject> Obj = MakeCameraObject(AssetPath, TEXT("preview_focus"), *Client);
		Obj->SetBoolField(TEXT("focused"), true);
		return SerializeJsonResponse(Obj);
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetStringField(TEXT("operation"), TEXT("preview_focus"));
	Obj->SetBoolField(TEXT("focused"), true);
	return SerializeJsonResponse(Obj);
#endif
}
