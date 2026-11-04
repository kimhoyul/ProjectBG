// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #9230: Cross-platform path / shell helpers for Mac/Linux UE plugin support.

#include "CUPlatformPaths.h"
#include "CUPlatformPathsInternal.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

namespace NarshaMCP::Platform
{
	const TCHAR* GetExeExtension()
	{
#if PLATFORM_WINDOWS
		return TEXT(".exe");
#else
		return TEXT("");
#endif
	}

	const TCHAR* GetEditorBinariesSubdir()
	{
#if PLATFORM_WINDOWS
		return TEXT("Binaries/Win64");
#elif PLATFORM_MAC
		return TEXT("Binaries/Mac");
#elif PLATFORM_LINUX
		return TEXT("Binaries/Linux");
#else
		// Defensive default — keeps the plugin compiling on any future platform
		// added to .uplugin without crashing. Callers should treat an unknown
		// platform as "binary not found" via FPaths::FileExists.
		return TEXT("Binaries/Unknown");
#endif
	}

	FString GetNarshaMCPBinaryName()
	{
		return FString(TEXT("narshamcp")) + GetExeExtension();
	}

	FString GetEditorCmdBinaryName()
	{
		return FString(TEXT("UnrealEditor-Cmd")) + GetExeExtension();
	}

	bool CreateInheritableStdinPipe(void*& OutChildRead, void*& OutParentWrite)
	{
		// Issue #29321. The child inherits the READ end of a stdin pipe, so the
		// WRITE end is the one that must be parent-local. The default
		// (bWritePipeLocal=false) clears HANDLE_FLAG_INHERIT on the READ end
		// instead, which is right for capturing stdout and wrong for feeding stdin.
		return FPlatformProcess::CreatePipe(OutChildRead, OutParentWrite, /*bWritePipeLocal=*/ true);
	}

	namespace Private
	{
		FString ResolveClaudeCliPath(
			const FClaudeCliSearchContext& Context,
			TFunctionRef<bool(const FString&)> FileExists)
		{
			TArray<FString> Candidates;
#if PLATFORM_WINDOWS
			Candidates.Reserve(6);
			Candidates.Add(Context.NativeOnPath);
			if (!Context.UserProfile.IsEmpty())
			{
				Candidates.Add(FPaths::Combine(
					Context.UserProfile, TEXT(".local"), TEXT("bin"), TEXT("claude.exe")));
			}
			if (!Context.LocalAppData.IsEmpty())
			{
				Candidates.Add(FPaths::Combine(
					Context.LocalAppData, TEXT("Microsoft"), TEXT("WinGet"), TEXT("Links"), TEXT("claude.exe")));
				Candidates.Add(FPaths::Combine(
					Context.LocalAppData, TEXT("Programs"), TEXT("claude-code"), TEXT("claude.exe")));
			}
			Candidates.Add(Context.NpmOnPath);
			if (!Context.AppData.IsEmpty())
			{
				Candidates.Add(FPaths::Combine(Context.AppData, TEXT("npm"), TEXT("claude.cmd")));
			}
#else
			Candidates.Reserve(2);
			Candidates.Add(Context.NativeOnPath);
			if (!Context.Home.IsEmpty())
			{
				Candidates.Add(FPaths::Combine(
					Context.Home, TEXT(".local"), TEXT("bin"), TEXT("claude")));
			}
#endif

			for (const FString& Candidate : Candidates)
			{
				if (!Candidate.IsEmpty() && FileExists(Candidate))
				{
					return Candidate;
				}
			}
			return FString();
		}
	}

	namespace
	{
		// Returns the absolute path of the platform shell executable used by
		// RunShellCommand and FindOnPath. Win64 = %COMSPEC% (cmd.exe fallback),
		// Mac/Linux = /bin/sh.
		FString ResolveShellExecutable()
		{
#if PLATFORM_WINDOWS
			FString ComSpec = FPlatformMisc::GetEnvironmentVariable(TEXT("COMSPEC"));
			if (ComSpec.IsEmpty())
			{
				ComSpec = TEXT("C:\\Windows\\System32\\cmd.exe");
			}
			return ComSpec;
#else
			return TEXT("/bin/sh");
#endif
		}

		// Builds the shell-specific argument string for a one-shot command.
		// Win64: /c <CommandLine>           (cmd.exe leaves the rest as-is)
		// Mac/Linux: -c '<CommandLine>'     (single-quoted to suppress sh's
		//                                    expansion of $VAR / `cmd` / \)
		//
		// Single-quoting matches the FindOnPath escape policy below — both
		// helpers must agree, otherwise CommandLine substrings containing $
		// or backticks get re-evaluated only on POSIX and we get a silent
		// platform divergence (Issue #9230 review M-1).
		FString BuildShellArgs(const FString& CommandLine)
		{
#if PLATFORM_WINDOWS
			return FString::Printf(TEXT("/c %s"), *CommandLine);
#else
			// Canonical sh escape: '\'' closes the literal, inserts a literal
			// apostrophe, then reopens. Survives any embedded single quote
			// in CommandLine without re-expansion.
			FString Escaped = CommandLine.Replace(TEXT("'"), TEXT("'\\''"));
			return FString::Printf(TEXT("-c '%s'"), *Escaped);
#endif
		}
	}

	bool RunShellCommand(
		const FString& CommandLine,
		int32& OutReturnCode,
		FString& OutStdOut,
		FString& OutStdErr)
	{
		const FString Shell = ResolveShellExecutable();
		const FString Args = BuildShellArgs(CommandLine);

		OutReturnCode = -1;
		OutStdOut.Reset();
		OutStdErr.Reset();

		const bool bOk = FPlatformProcess::ExecProcess(
			*Shell,
			*Args,
			&OutReturnCode,
			&OutStdOut,
			&OutStdErr);

		return bOk;
	}

	FString FindOnPath(const FString& BinaryName)
	{
		if (BinaryName.IsEmpty())
		{
			return FString();
		}

#if PLATFORM_WINDOWS
		// `where` does not require quoting and binary names with metacharacters
		// fail closed (cmd.exe rejects them). Defensive: bare quotes around the
		// argument confuse `where`'s parser, so we don't add them here. Reject
		// shell-meaningful chars early so a hostile/garbage BinaryName cannot
		// reach cmd.exe's tokenizer (defence-in-depth; S-1 review note).
		const TCHAR* DisallowedChars = TEXT("&|<>^\"'");
		for (TCHAR Ch : BinaryName)
		{
			if (FCString::Strchr(DisallowedChars, Ch) != nullptr)
			{
				return FString();
			}
		}
		const FString Cmd = FString::Printf(TEXT("where %s"), *BinaryName);
#else
		// POSIX shell single-quotes are safe except when BinaryName itself
		// contains a single quote — that closes the quoted region. Apply the
		// canonical sh escape `'\''` so embedded apostrophes are quoted intact.
		FString Escaped = BinaryName.Replace(TEXT("'"), TEXT("'\\''"));
		const FString Cmd = FString::Printf(TEXT("command -v '%s'"), *Escaped);
#endif

		int32 ReturnCode = -1;
		FString StdOut, StdErr;
		if (!RunShellCommand(Cmd, ReturnCode, StdOut, StdErr))
		{
			return FString();
		}

		if (ReturnCode != 0 || StdOut.IsEmpty())
		{
			return FString();
		}

		// `where` may return multiple lines; `command -v` returns one. Take
		// the first non-empty line and validate it points to a real file.
		FString FirstLine;
		int32 NewlineIdx = INDEX_NONE;
		if (StdOut.FindChar('\n', NewlineIdx))
		{
			FirstLine = StdOut.Left(NewlineIdx);
		}
		else
		{
			FirstLine = StdOut;
		}
		FirstLine = FirstLine.TrimStartAndEnd();

		if (FirstLine.IsEmpty() || !FPaths::FileExists(FirstLine))
		{
			return FString();
		}
		return FirstLine;
	}

	FString FindClaudeCliPath()
	{
		Private::FClaudeCliSearchContext Context;
#if PLATFORM_WINDOWS
		Context.NativeOnPath = FindOnPath(TEXT("claude.exe"));
		Context.NpmOnPath = FindOnPath(TEXT("claude.cmd"));
		Context.UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
		Context.LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		Context.AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
#else
		Context.NativeOnPath = FindOnPath(TEXT("claude"));
		Context.Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
#endif

		const auto FileExists = [](const FString& Candidate)
		{
			return FPaths::FileExists(Candidate);
		};
		return Private::ResolveClaudeCliPath(Context, FileExists);
	}
}
