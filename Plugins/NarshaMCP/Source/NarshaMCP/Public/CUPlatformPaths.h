// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #9230: Cross-platform path / shell helpers for Mac/Linux UE plugin support.
//
// Consolidates the Win64-coupled dispatch sites that PR #10234 (Issue #9229) deferred
// to this issue. All platform-specific decisions live in CUPlatformPaths.cpp behind
// #if PLATFORM_WINDOWS / PLATFORM_MAC / PLATFORM_LINUX guards so the rest of the plugin
// can stay platform-neutral.

#pragma once

#include "CoreMinimal.h"

namespace NarshaMCP::Platform
{
	/** Suffix for executable artifacts. ".exe" on Win64, "" on Mac/Linux. */
	NARSHAMCP_API const TCHAR* GetExeExtension();

	/** Engine Binaries subdirectory: "Binaries/Win64" / "Binaries/Mac" / "Binaries/Linux". */
	NARSHAMCP_API const TCHAR* GetEditorBinariesSubdir();

	/** "narshamcp.exe" on Win64, "narshamcp" on Mac/Linux. */
	NARSHAMCP_API FString GetNarshaMCPBinaryName();

	/** "UnrealEditor-Cmd.exe" on Win64, "UnrealEditor-Cmd" on Mac/Linux. */
	NARSHAMCP_API FString GetEditorCmdBinaryName();

	/**
	 * Locate Claude Code across the supported native and npm install layouts.
	 * Native installations are preferred; returns an empty string when no
	 * candidate exists.
	 */
	NARSHAMCP_API FString FindClaudeCliPath();

	/**
	 * Run a one-shot shell command.
	 * - Win64: %COMSPEC% (or C:\Windows\System32\cmd.exe fallback) /c <CommandLine>
	 * - Mac/Linux: /bin/sh -c <CommandLine>
	 * Returns false if the shell could not be invoked. ReturnCode/StdOut/StdErr are
	 * populated only on successful invocation.
	 */
	NARSHAMCP_API bool RunShellCommand(
		const FString& CommandLine,
		int32& OutReturnCode,
		FString& OutStdOut,
		FString& OutStdErr);

	/**
	 * Locate a binary on PATH.
	 * - Win64: cmd.exe /c where <BinaryName>
	 * - Mac/Linux: /bin/sh -c "command -v <BinaryName>"
	 * Returns the first absolute path that exists on disk, or an empty FString
	 * if not found. Trailing whitespace and CR/LF are trimmed.
	 */
	NARSHAMCP_API FString FindOnPath(const FString& BinaryName);

	/**
	 * Create a pipe for feeding a child process's STDIN (Issue #29321).
	 *
	 * `FPlatformProcess::CreatePipe(Read, Write)` defaults to `bWritePipeLocal=false`,
	 * which clears HANDLE_FLAG_INHERIT on the *read* end. That default is correct for
	 * capturing a child's STDOUT (parent reads, child writes), but it is exactly wrong
	 * for STDIN: the read end is the handle the child must inherit. Passing a
	 * default-created read end as `CreateProc`'s `PipeReadChild` makes the engine log
	 * `LogWindows: Error: PipeReadChild passed to CreateProc is not inheritable.` and
	 * then silently substitute `nullptr`, so the child launches with no stdin at all.
	 *
	 * Engine precedent for this exact case: `FInteractiveProcess` creates its stdin pipe
	 * by calling `CreatePipe` with `bWritePipeLocal = true`
	 * (Runtime/Core/Private/Misc/InteractiveProcess.cpp).
	 *
	 * @param OutChildRead   read end — inheritable; pass to CreateProc as PipeReadChild.
	 * @param OutParentWrite write end — parent-local; write the child's stdin through it.
	 * @return false if the pipe could not be created (both handles are then unusable).
	 */
	NARSHAMCP_API bool CreateInheritableStdinPipe(void*& OutChildRead, void*& OutParentWrite);
}
