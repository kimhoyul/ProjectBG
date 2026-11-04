// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26157: Pure geometry helpers for Widget Designer screenshots.

#pragma once

#include "CoreMinimal.h"

/**
 * Thread-compatible reservation table for asynchronous Designer captures.
 * Callers provide their own lock so the same state transition can be exercised
 * by automation tests without constructing an Editor subsystem or WebSocket.
 */
class FCUWidgetDesignerCaptureGate
{
public:
	bool TryAcquire(
		const FString& RequestId,
		const void* AssetToken,
		const void* SocketToken)
	{
		if (RequestId.IsEmpty() || !AssetToken || Reservations.Contains(RequestId))
		{
			return false;
		}
		for (const TPair<FString, FReservation>& Pair : Reservations)
		{
			if (Pair.Value.AssetToken == AssetToken)
			{
				return false;
			}
		}
		Reservations.Add(RequestId, FReservation{AssetToken, SocketToken});
		return true;
	}

	bool Release(const FString& RequestId)
	{
		return Reservations.Remove(RequestId) > 0;
	}

	TArray<FString> ReleaseForSocket(const void* SocketToken)
	{
		TArray<FString> Released;
		for (const TPair<FString, FReservation>& Pair : Reservations)
		{
			if (Pair.Value.SocketToken == SocketToken)
			{
				Released.Add(Pair.Key);
			}
		}
		for (const FString& RequestId : Released)
		{
			Reservations.Remove(RequestId);
		}
		return Released;
	}

	TArray<FString> ReleaseAll()
	{
		TArray<FString> Released;
		Reservations.GenerateKeyArray(Released);
		Reservations.Empty();
		return Released;
	}

	int32 Num() const
	{
		return Reservations.Num();
	}

private:
	struct FReservation
	{
		const void* AssetToken = nullptr;
		const void* SocketToken = nullptr;
	};
	TMap<FString, FReservation> Reservations;
};

namespace CUWidgetDesignerCapture
{
	/**
	 * Restore the two mutable Designer settings and verify their read-back.
	 * The injected accessors keep the production transition identical to the
	 * disconnect/shutdown automation fixture without requiring a live editor.
	 */
	template <typename ModeType, typename SetOutlinesType, typename GetOutlinesType,
		typename SetModeType, typename GetModeType>
	inline void RestoreEditorState(
		bool bOutlinesChanged,
		bool bOriginalOutlines,
		bool bModeChanged,
		const ModeType& OriginalMode,
		SetOutlinesType&& SetOutlines,
		GetOutlinesType&& GetOutlines,
		SetModeType&& SetMode,
		GetModeType&& GetMode,
		bool& bOutOutlinesRestored,
		bool& bOutModeRestored)
	{
		bOutOutlinesRestored = !bOutlinesChanged;
		bOutModeRestored = !bModeChanged;
		if (bOutlinesChanged)
		{
			SetOutlines(bOriginalOutlines);
			bOutOutlinesRestored = GetOutlines() == bOriginalOutlines;
		}
		if (bModeChanged)
		{
			SetMode(OriginalMode);
			bOutModeRestored = GetMode() == OriginalMode;
		}
	}

	/** Convert widget bounds expressed in a container's local Slate units to pixels. */
	inline bool ComputePixelRect(
		const FVector2D& LocalMin,
		const FVector2D& LocalMax,
		const FVector2D& ContainerLocalSize,
		int32 ImageWidth,
		int32 ImageHeight,
		FIntRect& OutRect)
	{
		OutRect = FIntRect();
		if (!FMath::IsFinite(LocalMin.X) || !FMath::IsFinite(LocalMin.Y)
			|| !FMath::IsFinite(LocalMax.X) || !FMath::IsFinite(LocalMax.Y)
			|| !FMath::IsFinite(ContainerLocalSize.X) || !FMath::IsFinite(ContainerLocalSize.Y)
			|| ContainerLocalSize.X <= 0.0 || ContainerLocalSize.Y <= 0.0
			|| ImageWidth <= 0 || ImageHeight <= 0
			|| LocalMax.X <= LocalMin.X || LocalMax.Y <= LocalMin.Y)
		{
			return false;
		}

		const double PixelScaleX = static_cast<double>(ImageWidth) / ContainerLocalSize.X;
		const double PixelScaleY = static_cast<double>(ImageHeight) / ContainerLocalSize.Y;
		const int32 UnclampedLeft = FMath::FloorToInt(LocalMin.X * PixelScaleX);
		const int32 UnclampedTop = FMath::FloorToInt(LocalMin.Y * PixelScaleY);
		const int32 UnclampedRight = FMath::CeilToInt(LocalMax.X * PixelScaleX);
		const int32 UnclampedBottom = FMath::CeilToInt(LocalMax.Y * PixelScaleY);

		if (UnclampedRight <= 0 || UnclampedBottom <= 0
			|| UnclampedLeft >= ImageWidth || UnclampedTop >= ImageHeight)
		{
			return false;
		}

		const int32 Left = FMath::Clamp(UnclampedLeft, 0, ImageWidth - 1);
		const int32 Top = FMath::Clamp(UnclampedTop, 0, ImageHeight - 1);
		const int32 Right = FMath::Clamp(UnclampedRight, Left + 1, ImageWidth);
		const int32 Bottom = FMath::Clamp(UnclampedBottom, Top + 1, ImageHeight);
		OutRect = FIntRect(Left, Top, Right, Bottom);
		return OutRect.Width() > 0 && OutRect.Height() > 0;
	}

	/** Output dimensions used by the shared screenshot encoder's base64 cap. */
	inline FIntPoint CalculateCappedOutputSize(
		int32 Width,
		int32 Height,
		const FString& ReturnMode,
		int32 MaxDimension = 1024)
	{
		if (Width <= 0 || Height <= 0 || MaxDimension <= 0
			|| !ReturnMode.Equals(TEXT("base64"), ESearchCase::IgnoreCase)
			|| (Width <= MaxDimension && Height <= MaxDimension))
		{
			return FIntPoint(Width, Height);
		}

		const double Scale = FMath::Min(
			static_cast<double>(MaxDimension) / Width,
			static_cast<double>(MaxDimension) / Height);
		return FIntPoint(
			FMath::Max(1, FMath::RoundToInt(Width * Scale)),
			FMath::Max(1, FMath::RoundToInt(Height * Scale)));
	}

	/** Scale a pixel rect into the encoder's final image coordinate space. */
	inline FIntRect ScalePixelRect(
		const FIntRect& Rect,
		int32 SourceWidth,
		int32 SourceHeight,
		int32 OutputWidth,
		int32 OutputHeight)
	{
		if (Rect.IsEmpty() || SourceWidth <= 0 || SourceHeight <= 0
			|| OutputWidth <= 0 || OutputHeight <= 0)
		{
			return FIntRect();
		}

		const double ScaleX = static_cast<double>(OutputWidth) / SourceWidth;
		const double ScaleY = static_cast<double>(OutputHeight) / SourceHeight;
		const int32 Left = FMath::Clamp(FMath::FloorToInt(Rect.Min.X * ScaleX), 0, OutputWidth - 1);
		const int32 Top = FMath::Clamp(FMath::FloorToInt(Rect.Min.Y * ScaleY), 0, OutputHeight - 1);
		const int32 Right = FMath::Clamp(FMath::CeilToInt(Rect.Max.X * ScaleX), Left + 1, OutputWidth);
		const int32 Bottom = FMath::Clamp(FMath::CeilToInt(Rect.Max.Y * ScaleY), Top + 1, OutputHeight);
		return FIntRect(Left, Top, Right, Bottom);
	}

	/** Copy a validated pixel rect from a row-major color buffer. */
	inline bool CropPixels(
		const TArray<FColor>& Source,
		int32 SourceWidth,
		int32 SourceHeight,
		const FIntRect& Rect,
		TArray<FColor>& OutPixels)
	{
		if (SourceWidth <= 0 || SourceHeight <= 0
			|| Source.Num() != SourceWidth * SourceHeight
			|| Rect.IsEmpty()
			|| Rect.Min.X < 0 || Rect.Min.Y < 0
			|| Rect.Max.X > SourceWidth || Rect.Max.Y > SourceHeight)
		{
			OutPixels.Reset();
			return false;
		}

		OutPixels.SetNumUninitialized(Rect.Width() * Rect.Height());
		for (int32 Y = 0; Y < Rect.Height(); ++Y)
		{
			const FColor* SourceRow = Source.GetData() + (Rect.Min.Y + Y) * SourceWidth + Rect.Min.X;
			FColor* DestinationRow = OutPixels.GetData() + Y * Rect.Width();
			FMemory::Memcpy(DestinationRow, SourceRow, Rect.Width() * sizeof(FColor));
		}
		return true;
	}
}
