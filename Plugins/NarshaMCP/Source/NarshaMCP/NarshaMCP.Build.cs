// Copyright 2026 Next Stage Inc. All rights reserved.

using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;
using System;
using EpicGames.Core;

public class NarshaMCP : ModuleRules
{
	public NarshaMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		// =========================================================
		// AUTO-INSTALLATION: First Build Detection (v4.0.0)
		// =========================================================
		// This runs ONCE on first build to automatically:
		// 1. Add PostBuildSteps to Target.cs
		// 2. Create .mcp.json
		// 3. Create SubAgent
		// 4. Update CLAUDE.md
		//
		// User experience: Extract plugin → Build → Done!
		// No manual script execution required.
		// =========================================================

		string PluginDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "../.."));
		string ProjectDir = Target.ProjectFile?.Directory.FullName;

		if (!string.IsNullOrEmpty(ProjectDir))
		{
			RunAutoSetupIfNeeded(Target, ProjectDir, PluginDir);
		}

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Issue #25804: Fab 4.3.7.3.d — vendored third-party headers live under
		// Source/ThirdParty/<lib>/ (sibling of the module), not Module/Private/ThirdParty.
		// 4.3.6.2.e: ModuleDirectory-relative include path (not a bare relative literal).
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty", "picosha2"));

		// Issue #1327: NarshaMCP-specific debug macro for performance optimization
		// Debug logs (Issue #520) are wrapped with #if UE_CODEGEN_BUILD_DEBUG
		// This prevents ~10-15s overhead from ToString() comparisons in release builds
		if (Target.Configuration == UnrealTargetConfiguration.Debug ||
		    Target.Configuration == UnrealTargetConfiguration.DebugGame)
		{
			PublicDefinitions.Add("UE_CODEGEN_BUILD_DEBUG=1");
		}
		else
		{
			PublicDefinitions.Add("UE_CODEGEN_BUILD_DEBUG=0");
		}

		// Issue #809: Force recompile on source changes
		// This prevents stale binaries when BlueprintExtractors.cpp is modified
		// Without this, UBT incremental build doesn't detect plugin .cpp changes
		// Trade-off: +1-5s incremental builds (typical) for guaranteed fresh compilation
		bUsePrecompiled = false;
		bPrecompile = false;

		// Issue #1696: Enable Python script execution via WebSocket
		// Required for ue_manage_blueprint Create/Modify operations
		// This enables the execute_python WebSocket handler
		PublicDefinitions.Add("WITH_PYTHON=1");

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Projects", // For IPluginManager
				"EditorSubsystem", // For UEditorSubsystem base class (Issue #1632 - must be public for UECODEGEN_API classes)
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"RHI",               // Issue #224: GMaxRHIFeatureLevel for Material compilation check (UE 5.6)
				"RenderCore",        // For FShaderMapContent (Material metadata extraction - Issue #518)
				"AssetRegistry",     // For FAssetData (LNK2019 fix)
				"AssetTools",        // Issue #19714: IAssetTools::RenameAssets/DuplicateAsset for UDS asset-lifecycle
				                     // ops (rename_struct/duplicate_struct in CUStructAuthor). Engine module
				                     // (Engine/Source/Developer/AssetTools); not a plugin → no .uplugin/Rule #18
				                     // change (same precedent as AIGraph below).
				"SourceControl",     // Issue #28199: read-only deleted-package state preflight before AssetTools
				"UnrealEd",          // For Blueprint editor classes
				"UMG",               // For UWidgetBlueprint (Issue #158 fix) and UWidget classes (Issue #135, #155)
				"UMGEditor",         // For UMG editor classes (Issue #135)
				"BlueprintGraph",    // For UK2Node classes
				"KismetCompiler",    // For Kismet compilation
				"Kismet",            // For FBlueprintEditor::JumpToNode (focus_blueprint_node)
				"Json",              // For JSON serialization
				"JsonUtilities",     // For FJsonSerializer
				"HTTP",              // Issue #5985: For FGenericPlatformHttp::UrlDecode (HandleAnnotateLink)
				"GameplayTags",      // For FGameplayTagContainer
				"AIModule",          // For UBehaviorTree, UBTNode classes (Issue #27 Phase 2)
				"BehaviorTreeEditor", // For UBehaviorTreeFactory (Issue #1699 - StateTree/BT creation)
				"AIGraph",           // Issue #16915: UAIGraphNode::GetInputPin/GetOutputPin + FGraphNodeClassData ctor
				                     // (AIGRAPH_API) for BehaviorTree editor-graph authoring (CUBehaviorTreeAuthor).
				                     // Core editor module (sibling of BehaviorTreeEditor); not a plugin → no .uplugin/Rule #18 change.
				"InputCore",         // For FKey::ToString() (Issue #20 Phase 7 - v3.4.0)
				"LevelSequence",     // For ULevelSequence (Issue #1529 - Sequencer Remote Control)
				"MovieScene",        // For UMovieScene, UMovieSceneTrack (Issue #1529 - Sequencer)
				"MovieSceneTracks",  // Issue #25940: typed Sequencer channel readback
				// Sockets + Networking removed (Issue #5657)
				"ImageWrapper",      // For PNG screenshot encoding (Issue #1632)
				"Slate",             // For FSlateApplication (Issue #1632)
				"SlateCore",         // For SWindow (Issue #1632)
				"DeveloperSettings", // For UDeveloperSettings (CUToolMaturitySettings, CUChatPanelSettings, CUAnnotationSettings)
				"TraceServices",     // Issue #3907: For Memory CSV export via IAllocationsProvider (CUInsightsMemoryCSVCommandlet)
				"TraceAnalysis",     // Issue #3907: For IAnalysisSession (CUInsightsMemoryCSVCommandlet)
				"Cbor",              // Issue #3907: For FCborReader in CUInsightsMemoryCSVCommandlet metadata path.
				                     //   Issue #77 (2026-05-08) note: CUMetadataExpand moved to NarshaMCPDevTools sub-module
				                     //   but Cbor stays here because CUInsightsMemoryCSVCommandlet (user-facing backend
				                     //   for `ue_analyze_insights`) still consumes it. Stage 2 WS1 (#23 + #67) will retire
				                     //   the commandlet once the native Rust parser ships, at which point this dep
				                     //   becomes removable along with TraceServices/TraceAnalysis.
				// RemoteControl: RUNTIME_ONLY via NarshaMCP.uplugin (Issue #28204 AC6). No C++
				// #include; leftover Build.cs link dropped so Rule 18 reverse scan stays honest.
				// HTTP API is the Rust daemon path (#3954). WebSocketNetworking stays — the
				// RemoteControl.uplugin lists it too, but we still consume IWebSocketServer
				// directly (PrivateIncludePathModuleNames).
				"MessageLog",        // Issue #5293: For FMessageLogModule (Blueprint debug watched values)
				// Issue #25249: FSlateNotificationManager lives in Runtime/Slate (already depended via "Slate")
				"ContentBrowser",    // Issue #5478 Phase 2: SyncBrowserToAssets for asset hyperlinks
				"WorkspaceMenuStructure", // Issue #5478: For WorkspaceMenu dock tab placement (Developer Tools category)
				"ToolMenus",         // Issue #5647: UToolMenus for BP graph context menu (AI annotation)
				"GraphEditor",       // Issue #5647: FGraphEditorModule for BP graph overlay access
				"LevelEditor",       // Issue #7911: For FLevelEditorModule::GetFirstActiveLevelViewport (auto_capture_analyze)
			"AnimGraph",         // Issue #6389: UAnimGraphNode_StateMachine, UAnimStateNode (AnimBP State Machine CRUD)
				"AnimationBlueprintLibrary", // Issue #14852: UAnimationBlueprintLibrary::AddAnimationSyncMarker / RemoveAnimationSyncMarkers* (sync-marker authoring; WITH_EDITOR)
				"AnimationModifiers", // Issue #27875: editor-only apply/revert Animation Modifier APIs.
				                      // Core Editor module (Engine/Source/Editor), not a plugin -> no .uplugin / Rule #18 change.
				// Issue #22752: Persona toolkit + Animation Editor for montage/sequence
				// preview transport (play/pause/stop/scrub/get_state). Editor-only modules
				// (siblings of BehaviorTreeEditor / LevelSequenceEditor) — not plugins →
				// no .uplugin / Rule #18 change.
				"Persona",
				"AnimationEditor",
				"AdvancedPreviewScene",
				"ApplicationCore",   // Issue #5657: For FPlatformApplicationMisc::ClipboardCopy (SCUChatMarkdownBlock)
				"PropertyEditor",    // For SDetailsView / IDetailsView (AI Chat Detail Panel — SCUChatDetailPanel)
				// Issue #7948: PluginWarden for Fab per-seat license verification
				// Phase 1: Module loaded but not called (placeholder IDs)
				// Phase 2: Real entitlement check with Epic-assigned IDs
				// "PluginWarden",    // Uncomment in Phase 2 when IDs are assigned
			}
		);

		// Issue #27909: AnimMixer-family modules are linked ONLY when the consuming project
		// opts into the plugins itself. NarshaMCP used to declare them Enabled:true in its
		// .uplugin, which force-enabled them (and their non-optional closure: UAF,
		// UAFAnimGraph, UAFMirroring, Workspace, UAFControlRig) in every consumer project.
		// Measured: BuildPlugins 367 -> 365 for a single flag flip, mounts 367 -> 0.
		// Engine condition first, matching NarshaMCPDevTools.Build.cs. The AnimMixer
		// surface is 5.8+ only (the layer model arrived there), so linking these modules
		// on 5.7 would pull in dependencies whose consumers are all compiled out.
		bool bEngine58Plus = Target.Version.MajorVersion > 5 ||
			(Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8);
		bool bWithAnimMixer = bEngine58Plus && NarshaAnimMixerLink.ShouldLink(Target);
		if (bWithAnimMixer)
		{
			// Private, deliberately: promoting these to Public re-exposes three engine
			// modules to every module that depends on NarshaMCP.
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"MovieSceneAnimMixer",             // Issue #11398
				"HierarchyTableRuntime",           // Issue #25920
				"HierarchyTableAnimationRuntime",  // Issue #25920
			});
		}

		// Consumed by CURemoteSequencerAnimMixerInternals.h to compile the AnimMixer
		// surface out entirely when the modules are not linked.
		PublicDefinitions.Add("NARSHA_WITH_ANIM_MIXER=" + (bWithAnimMixer ? "1" : "0"));

		// Issue #28215 Phase A: GameplayStateTree is opt-in. Our only use of it is resolving
		// UStateTreeComponentSchema as the default StateTree schema, so a project that never
		// authors StateTree assets through us must not have the plugin force-enabled. Same
		// probe contract as AnimMixer: the CONSUMING project's .uproject decides, fail-closed.
		bool bWithGameplayStateTree = NarshaGameplayStateTreeLink.ShouldLink(Target);
		if (bWithGameplayStateTree)
		{
			PrivateDependencyModuleNames.Add("GameplayStateTreeModule");
		}

		// Consumed by the two UStateTreeComponentSchema call sites.
		PublicDefinitions.Add("WITH_NARSHAMCP_GAMEPLAYSTATETREE=" + (bWithGameplayStateTree ? "1" : "0"));

		// Issue #28215 Phase A: GameplayAbilities is opt-in. It is the single largest
		// marginal cost in the descriptor (itself + DataRegistry[BETA]); a project that
		// does not use the Gameplay Ability System should not receive either.
		bool bWithGameplayAbilities = NarshaGameplayAbilitiesLink.ShouldLink(Target);
		if (bWithGameplayAbilities)
		{
			PrivateDependencyModuleNames.Add("GameplayAbilities");
		}

		// Consumed by the GAS call sites across the commandlet, extractors and the two
		// remote/editor surfaces.
		PublicDefinitions.Add("WITH_NARSHAMCP_GAMEPLAYABILITIES=" + (bWithGameplayAbilities ? "1" : "0"));

		// Issue #28215 Phase B: all non-transport feature plugins are consumer-controlled.
		// Keep plugin-name literals in the helper below the ModuleRules class: Rule 18 scans
		// this class for module links, and several plugin names equal their module names.
		bool bWithStateTree = NarshaIssue28215PluginLink.StateTree(Target);
		if (bWithStateTree)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"StateTreeModule",
				"StateTreeEditorModule",
				"PropertyBindingUtils",
			});
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_STATETREE=" + (bWithStateTree ? "1" : "0"));
		PublicDefinitions.Add("WITH_NARSHAMCP_PROPERTYBINDINGUTILS=" + (bWithStateTree ? "1" : "0"));

		bool bWithEnhancedInput = NarshaIssue28215PluginLink.EnhancedInput(Target);
		if (bWithEnhancedInput)
		{
			PrivateDependencyModuleNames.Add("EnhancedInput");
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_ENHANCEDINPUT=" + (bWithEnhancedInput ? "1" : "0"));

		bool bWithPCG = NarshaIssue28215PluginLink.PCG(Target);
		if (bWithPCG)
		{
			PrivateDependencyModuleNames.Add("PCG");
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_PCG=" + (bWithPCG ? "1" : "0"));

		bool bWithNiagara = NarshaIssue28215PluginLink.Niagara(Target);
		if (bWithNiagara)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "Niagara", "NiagaraEditor" });
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_NIAGARA=" + (bWithNiagara ? "1" : "0"));

		bool bWithGameplayCameras = NarshaIssue28215PluginLink.GameplayCameras(Target);
		if (bWithGameplayCameras)
		{
			PrivateDependencyModuleNames.Add("GameplayCameras");
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_GAMEPLAYCAMERAS=" + (bWithGameplayCameras ? "1" : "0"));

		bool bWithMetasound = NarshaIssue28215PluginLink.Metasound(Target);
		if (bWithMetasound)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"MetasoundEngine",
				"MetasoundFrontend",
				"MetasoundEditor",
			});
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_METASOUND=" + (bWithMetasound ? "1" : "0"));

		bool bWithLevelSequenceEditor = NarshaIssue28215PluginLink.LevelSequenceEditor(Target);
		if (bWithLevelSequenceEditor)
		{
			PrivateDependencyModuleNames.Add("LevelSequenceEditor");
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_LEVELSEQUENCEEDITOR=" + (bWithLevelSequenceEditor ? "1" : "0"));

		bool bWithSequencerScripting = NarshaIssue28215PluginLink.SequencerScripting(Target);
		if (bWithSequencerScripting)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"SequencerScripting",
				"SequencerScriptingEditor",
			});
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_SEQUENCERSCRIPTING=" + (bWithSequencerScripting ? "1" : "0"));

		bool bWithRigVM = NarshaIssue28215PluginLink.RigVM(Target);
		if (bWithRigVM)
		{
			PrivateDependencyModuleNames.Add("RigVM");
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_RIGVM=" + (bWithRigVM ? "1" : "0"));

		bool bWithControlRig = NarshaIssue28215PluginLink.ControlRig(Target);
		if (bWithControlRig)
		{
			PrivateDependencyModuleNames.Add("ControlRig");
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_CONTROLRIG=" + (bWithControlRig ? "1" : "0"));

		bool bWithPythonScriptPlugin = NarshaIssue28215PluginLink.PythonScriptPlugin(Target);
		if (bWithPythonScriptPlugin)
		{
			PrivateDependencyModuleNames.Add("PythonScriptPlugin");
			PrivateIncludePathModuleNames.Add("PythonScriptPlugin");
		}
		PublicDefinitions.Add("WITH_NARSHAMCP_PYTHONSCRIPTPLUGIN=" + (bWithPythonScriptPlugin ? "1" : "0"));

		// Issue #9229: LiveCoding is Win64-only (UE 5.7
		// Engine/Source/Developer/LiveCoding/LiveCoding.Build.cs gates the module on
		// UnrealTargetPlatform.Win64; Live++ — the underlying patch-and-continue
		// tech — is Windows-exclusive). Provide WITH_NARSHAMCP_LIVE_CODING define so
		// consumer C++ in CUChatPanelSubsystem compiles cleanly on Mac/Linux.
		// Mirrors the existing Win64 RuntimeDependency guard pattern below at the
		// pdb_native*.pyd block, and the PublicDefinitions ternary pattern above
		// at UE_CODEGEN_BUILD_DEBUG.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("LiveCoding");
			PublicDefinitions.Add("WITH_NARSHAMCP_LIVE_CODING=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_NARSHAMCP_LIVE_CODING=0");
		}

		// Issue #25867 Option A: do not add OpenSSL (or bcrypt.lib) for license
		// token signing. Client-side RSA is gone; a private key must not ship in
		// source. Do NOT re-add "bcrypt.lib" — reopens the #9622/#9625 UBT mtime
		// regression bug class. HTTP→SSL still pulls OpenSSL transitively.

		// Issue #16334: re-add "Sockets" (removed in #5657) for StartAuto()'s
		// free-port probe. UE's WebSocketNetworking Server->Init() binds with
		// SO_REUSEADDR, so it does NOT fail on an occupied port; we need a plain
		// FSocket Bind() probe to detect occupancy and advance the port scan.
		PrivateDependencyModuleNames.Add("Sockets");

		// Optional dependencies (Issue #138 Fix: WebSocket crash in commandlet)
		// WebSocket modules are NOT available in commandlet mode (UnrealEditor-Cmd.exe)
		// They will be loaded at runtime using FModuleManager::LoadModule() with null check
		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"WebSockets",        // For WebSocket bidirectional communication (Issue #124 Phase 3)
				"WebSocketNetworking", // For IWebSocketServer (server-side WebSocket, Issue #124 Phase 3)
			}
		);

		// Runtime Dependencies for packaging
		// These files will be included when packaging the plugin

		// 1. MCP Server (Rust binary + assets)
		RuntimeDependencies.Add(Path.Combine(PluginDir, "MCP/..."), StagedFileType.NonUFS);

		// 2. Config files
		RuntimeDependencies.Add(Path.Combine(PluginDir, "Config/..."), StagedFileType.NonUFS);

		// 4. Agent templates
		RuntimeDependencies.Add(Path.Combine(PluginDir, "Agents/..."), StagedFileType.NonUFS);

		// 5. MCP configuration templates (v4.10.0 - UV-based .mcp.json)
		RuntimeDependencies.Add(Path.Combine(PluginDir, "templates/..."), StagedFileType.NonUFS);

		// 6. Setup scripts (setup-project.bat, add_postbuild_step.py)
		RuntimeDependencies.Add(Path.Combine(PluginDir, "scripts/..."), StagedFileType.NonUFS);

		// 6. Platform-specific native modules
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			RuntimeDependencies.Add(
				Path.Combine(PluginDir, "Binaries/Win64/pdb_native*.pyd"),
				StagedFileType.NonUFS
			);
		}

		// Note: PostBuildSteps removed - ModuleRules doesn't support it
		// Use .uplugin PostBuildSteps or TargetRules instead
	}

	/// <summary>
	/// Issue #5385: Runs auto-setup on first build via narshamcp.exe setup (Rust-only, no Python)
	/// Issue #6953: Flag/log stored per-project (not in PluginDir) to support engine plugins
	///             shared across multiple projects (Fab marketplace installs to Engine/Plugins/Fab/).
	/// </summary>
	private void RunAutoSetupIfNeeded(ReadOnlyTargetRules Target, string ProjectDir, string PluginDir)
	{
		// Issue #6953: Store flag/log in project's Intermediate directory (per-project isolation).
		// Previous location (PluginDir) caused engine plugins to skip setup for 2nd+ projects.
		string NarshaIntermediateDir = Path.Combine(ProjectDir, "Intermediate", "NarshaMCP");
		Directory.CreateDirectory(NarshaIntermediateDir);
		string SetupFlagFile = Path.Combine(NarshaIntermediateDir, "setup_done");
		string LogFile = Path.Combine(NarshaIntermediateDir, "auto_setup_build.log");

		// Log that Build.cs is being executed
		File.AppendAllText(LogFile, $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] Build.cs executed (v7.0.0 Rust-only)\n");
		File.AppendAllText(LogFile, $"  ProjectDir: {ProjectDir}\n");
		File.AppendAllText(LogFile, $"  PluginDir: {PluginDir}\n");
		File.AppendAllText(LogFile, $"  EngineDirectory: {EngineDirectory}\n");
		File.AppendAllText(LogFile, $"  SetupFlagFile: {SetupFlagFile}\n");
		File.AppendAllText(LogFile, $"  Flag exists: {File.Exists(SetupFlagFile)}\n");

		if (File.Exists(SetupFlagFile))
		{
			File.AppendAllText(LogFile, "  -> Setup already done, skipping\n");
			return;
		}

		// Issue #6953: Check legacy flag location for backward compatibility.
		// Existing users upgrading from pre-#6953 have the flag in PluginDir.
		// Migrate it to the new per-project location to avoid unnecessary re-setup.
		string LegacyFlagFile = Path.Combine(PluginDir, ".narshamcp_setup_done");
		if (File.Exists(LegacyFlagFile))
		{
			try
			{
				File.WriteAllText(SetupFlagFile, File.ReadAllText(LegacyFlagFile));
				File.AppendAllText(LogFile, "  -> Migrated legacy setup flag from PluginDir to Intermediate/\n");
			}
			catch (Exception)
			{
				// Legacy flag in read-only engine dir — create new flag instead
				try
				{
					File.WriteAllText(SetupFlagFile, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"));
					File.AppendAllText(LogFile, "  -> Legacy flag found but unreadable, created new flag\n");
				}
				catch (Exception innerEx)
				{
					// Intermediate dir also unwritable — log to console and proceed to setup
					Console.WriteLine("[NarshaMCP] WARNING: Cannot write setup flag: " + innerEx.Message);
				}
			}
			return;
		}

		File.AppendAllText(LogFile, "  -> First build detected, running auto setup...\n");
		Console.WriteLine("[NarshaMCP] ========================================");
		Console.WriteLine("[NarshaMCP] First build detected - running auto setup...");
		Console.WriteLine("[NarshaMCP] ========================================");

		// Find narshamcp binary (Issue #9230: platform-aware suffix)
		string NarshaBinaryName = GetNarshaMcpBinaryName(Target);
		string NarshaBinary = FindNarshaMcpBinary(PluginDir, Target);
		File.AppendAllText(LogFile, $"  NarshaBinary: {NarshaBinary ?? "NULL"}\n");

		if (string.IsNullOrEmpty(NarshaBinary))
		{
			// Issue #11108: surface every search location so the user can diagnose
			// release-ZIP corruption, AV quarantine, or local-build mis-staging without
			// having to read source.
			string[] searchedPaths = GetNarshaMcpBinarySearchPaths(PluginDir, NarshaBinaryName);
			File.AppendAllText(LogFile, $"  ERROR: {NarshaBinaryName} not found!\n");
			Console.WriteLine($"[NarshaMCP] ERROR: {NarshaBinaryName} not found!");
			Console.WriteLine("[NarshaMCP] Searched in (priority order):");
			for (int i = 0; i < searchedPaths.Length; i++)
			{
				File.AppendAllText(LogFile, $"    [{i + 1}] {searchedPaths[i]}\n");
				Console.WriteLine($"[NarshaMCP]   {i + 1}. {searchedPaths[i]}");
			}
			Console.WriteLine("[NarshaMCP] Likely causes:");
			Console.WriteLine("[NarshaMCP]   - Release ZIP partially extracted (re-extract and retry)");
			Console.WriteLine("[NarshaMCP]   - Antivirus quarantined narshamcp.exe (whitelist plugin dir)");
			Console.WriteLine("[NarshaMCP]   - Local build skipped (run scripts\\build\\build-rust.bat release)");
			return;
		}

		// Resolve engine path for Rust CLI:
		// ModuleRules.EngineDirectory returns "C:\UE_5.7\Engine" (WITH /Engine suffix)
		// Rust CLI expects "C:\UE_5.7" (WITHOUT /Engine suffix)
		string EngineRoot = Path.GetDirectoryName(EngineDirectory);
		File.AppendAllText(LogFile, $"  EngineRoot (stripped): {EngineRoot}\n");

		// Issue: SVN-checkout / external-engine layouts can cause UBT to hand us
		// relative path strings (e.g. "../../../Engine") in ProjectDir / EngineDirectory.
		// If those leak into `narshamcp setup`, .mcp.json ends up with relative
		// --project-path / --engine-path args that break daemon spawn under Claude Code's CWD.
		// Promote to absolute paths at the Build.cs boundary so the downstream Rust
		// chain (ConfigResolver → create_mcp_json) never sees a relative path.
		string ProjectDirAbs = Path.GetFullPath(ProjectDir);
		string PluginDirAbs = Path.GetFullPath(PluginDir);
		string EngineRootAbs = string.IsNullOrEmpty(EngineRoot) ? EngineRoot : Path.GetFullPath(EngineRoot);
		File.AppendAllText(LogFile, $"  ProjectDirAbs: {ProjectDirAbs}\n");
		File.AppendAllText(LogFile, $"  PluginDirAbs: {PluginDirAbs}\n");
		File.AppendAllText(LogFile, $"  EngineRootAbs: {EngineRootAbs}\n");

		try
		{
			// Build arguments: narshamcp.exe setup --project-path ... --plugin-dir ... --engine-path ... --skip-claude-registration --scope project
			// Issue #13347: pass --scope project explicitly (defense-in-depth; CLI
			// default is now also `project` but the explicit flag documents the
			// build-time intent and survives a hypothetical future default flip).
			string arguments = string.Format(
				"setup --project-path \"{0}\" --plugin-dir \"{1}\" --engine-path \"{2}\" --skip-claude-registration --scope project",
				ProjectDirAbs, PluginDirAbs, EngineRootAbs);

			File.AppendAllText(LogFile, $"  Command: {NarshaBinary} {arguments}\n");

			var processInfo = new System.Diagnostics.ProcessStartInfo
			{
				FileName = NarshaBinary,
				Arguments = arguments,
				UseShellExecute = false,
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				CreateNoWindow = true
			};

			var process = System.Diagnostics.Process.Start(processInfo);

			// Read stderr on a separate thread to avoid .NET pipe deadlock.
			// ReadToEnd() on stdout blocks until EOF; if the child fills the stderr
			// pipe buffer (4 KB on Windows), both sides stall forever.
			string errors = null;
			var stderrThread = new System.Threading.Thread(() => {
				errors = process.StandardError.ReadToEnd();
			});
			stderrThread.Start();

			string output = process.StandardOutput.ReadToEnd();
			stderrThread.Join(TimeSpan.FromSeconds(60));

			// Timeout: narshamcp setup should complete within 60 seconds (much faster than Python).
			if (!process.WaitForExit(60_000))
			{
				File.AppendAllText(LogFile, "  TIMEOUT: narshamcp setup exceeded 60 seconds, killing\n");
				Console.WriteLine("[NarshaMCP] WARNING: Auto setup timed out (> 60s), skipping.");
				try { process.Kill(); } catch { }
				return;
			}

			File.AppendAllText(LogFile, $"  Process exit code: {process.ExitCode}\n");
			File.AppendAllText(LogFile, $"  Stdout: {output}\n");
			File.AppendAllText(LogFile, $"  Stderr: {errors}\n");

			if (!string.IsNullOrEmpty(output))
			{
				Console.WriteLine(output);
			}

			if (process.ExitCode == 0)
			{
				// Success - create flag file
				File.WriteAllText(SetupFlagFile, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"));
				File.AppendAllText(LogFile, "  SUCCESS: Auto setup complete!\n");
				Console.WriteLine("[NarshaMCP] ========================================");
				Console.WriteLine("[NarshaMCP] Auto setup complete!");
				Console.WriteLine("[NarshaMCP] Next steps:");
				Console.WriteLine("[NarshaMCP]   1. Restart VS Code / Claude Code");
				Console.WriteLine("[NarshaMCP]   2. Run: /mcp");
				Console.WriteLine("[NarshaMCP]   3. Verify 'narshamcp' is active");
				Console.WriteLine("[NarshaMCP] ========================================");
			}
			else
			{
				File.AppendAllText(LogFile, $"  ERROR: Auto setup failed with exit code {process.ExitCode}\n");
				Console.WriteLine("[NarshaMCP] ERROR: Auto setup failed with exit code " + process.ExitCode);
				if (!string.IsNullOrEmpty(errors))
				{
					Console.WriteLine("[NarshaMCP] Error output:");
					Console.WriteLine(errors);
				}
			}
		}
		catch (Exception ex)
		{
			File.AppendAllText(LogFile, $"  EXCEPTION: {ex.Message}\n");
			File.AppendAllText(LogFile, $"  Stack trace: {ex.StackTrace}\n");
			Console.WriteLine("[NarshaMCP] ERROR: Failed to run auto setup: " + ex.Message);
		}
	}

	/// <summary>
	/// Returns the platform-aware narshamcp binary file name (Issue #9230).
	/// "narshamcp.exe" on Win64, "narshamcp" on Mac/Linux.
	/// </summary>
	private static string GetNarshaMcpBinaryName(ReadOnlyTargetRules Target)
	{
		string suffix = (Target.Platform == UnrealTargetPlatform.Win64) ? ".exe" : "";
		return "narshamcp" + suffix;
	}

	/// <summary>
	/// Returns the ordered list of locations FindNarshaMcpBinary() searches, so
	/// the diagnostic message can surface them all when the binary is missing.
	/// Order MUST match FindNarshaMcpBinary() iteration order.
	/// Issue #11108: kept as a single source of truth to avoid Build.cs/diag drift.
	/// </summary>
	private static string[] GetNarshaMcpBinarySearchPaths(string PluginDir, string binaryName)
	{
		// Rule 7B (CLAUDE.md): build-rust.bat default profile is thin-lto, NOT release.
		// Both build-rust.bat AND raw `cargo build` may emit either profile depending
		// on developer flags, so we check both for each output root. Order matches
		// the reliability of each location — Fab/release ZIP first, then dev paths.
		return new string[]
		{
			// 1. Source/ThirdParty/bin/ — Fab marketplace layout (rule 4.3.6.1.e); the
			//    canonical release ZIP location after _apply_fab_post_processing()
			//    in scripts/build/package_release.py moves the binary here.
			Path.Combine(PluginDir, "Source", "ThirdParty", "bin", binaryName),
			// 2. MCP/bin/ — legacy release layout (pre-Fab), kept for backward compat
			//    with older v0.9.x ZIPs that did not Fab-relocate.
			Path.Combine(PluginDir, "MCP", "bin", binaryName),
			// 3-4. MCP/native/build/<profile>/ — build-rust.bat canonical output
			//      (Rule 7B). thin-lto is the DEFAULT profile, so check it first
			//      before release. Avoids requiring manual copy into MCP/bin/.
			Path.Combine(PluginDir, "MCP", "native", "build", "thin-lto", binaryName),
			Path.Combine(PluginDir, "MCP", "native", "build", "release", binaryName),
			// 5-6. MCP/native/target/<profile>/ — Cargo workspace target (raw
			//      `cargo build [--release]`). Same profile precedence as build/.
			Path.Combine(PluginDir, "MCP", "native", "target", "thin-lto", binaryName),
			Path.Combine(PluginDir, "MCP", "native", "target", "release", binaryName),
		};
	}

	/// <summary>
	/// Finds narshamcp binary in the plugin directory (Issue #5385).
	/// Issue #9230: platform-aware suffix passed via Target.
	/// Issue #11108: multi-path fallback so Fab/legacy/local-build layouts all work,
	/// including v0.9.13 ZIPs where Build.cs auto-patching silently no-op'd.
	/// </summary>
	private static string FindNarshaMcpBinary(string PluginDir, ReadOnlyTargetRules Target)
	{
		string binaryName = GetNarshaMcpBinaryName(Target);
		foreach (string candidate in GetNarshaMcpBinarySearchPaths(PluginDir, binaryName))
		{
			if (File.Exists(candidate))
			{
				return candidate;
			}
		}
		return null;
	}
}

/// <summary>
/// Decides whether the AnimMixer-family engine modules may be linked (Issue #27909).
///
/// A second top-level type in a Build.cs is compiled into the same rules assembly and is
/// therefore visible to sibling module rules (NarshaMCPDevTools uses it too). Only
/// *.Build.cs / *.Target.cs are compiled into that assembly, so this cannot live in a
/// plain .cs file next to it.
///
/// Fail-closed: anything unexpected answers false, which compiles the AnimMixer surface
/// out rather than emitting a link against modules that may not be enabled.
/// </summary>
public static class NarshaPluginLink
{
	static Dictionary<string, PluginSet> ReadAvailablePluginSets(
		ReadOnlyTargetRules Target,
		ProjectDescriptor Project)
	{
		// Use UBT's own discovery and collision policy rather than reproducing it here.
		// It applies Engine < Project < External < Mod precedence, highest Version within
		// a type, and platform-extension merging before exposing ChoiceVersion.
		List<PluginInfo> Available = UnrealBuildTool.Plugins.ReadAvailablePlugins(
			UnrealBuildBase.Unreal.EngineDirectory,
			Target.ProjectFile.Directory,
			Project.AdditionalPluginDirectories);
		return UnrealBuildTool.Plugins.ToFilteredDictionary(Available);
	}

	static bool ListHas(IReadOnlyList<string> List, string Name)
	{
		if (List == null)
		{
			return false;
		}
		for (int Index = 0; Index < List.Count; ++Index)
		{
			if (string.Equals(List[Index], Name, StringComparison.OrdinalIgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	public static bool ShouldLink(ReadOnlyTargetRules Target, IReadOnlyList<string> RequiredPlugins)
	{
		try
		{
			// An explicit -DisablePlugin wins over everything else.
			foreach (string Required in RequiredPlugins)
			{
				if (ListHas(Target.DisablePlugins, Required))
				{
					return false;
				}
			}

			// -EnablePlugin on the command line counts as an opt-in.
			bool bAllEnabledOnCommandLine = true;
			foreach (string Required in RequiredPlugins)
			{
				if (!ListHas(Target.EnablePlugins, Required))
				{
					bAllEnabledOnCommandLine = false;
					break;
				}
			}
			if (bAllEnabledOnCommandLine)
			{
				return true;
			}

			// Otherwise the consuming project's own descriptor is the authority. Note this
			// reads the .uproject directly rather than Target.EnabledPlugins/BuildPlugins:
			// those are reference-derived and would still contain what NarshaMCP itself
			// dragged in, which is the behaviour this issue removes.
			if (Target.ProjectFile == null)
			{
				return false;
			}

			ProjectDescriptor Project = ProjectDescriptor.FromFile(Target.ProjectFile);
			if (Project == null)
			{
				return false;
			}

			Dictionary<string, PluginSet> AvailablePluginSets = null;
			foreach (string Required in RequiredPlugins)
			{
				bool bFound = false;
				if (Project.Plugins != null)
				{
					foreach (PluginReferenceDescriptor Reference in Project.Plugins)
					{
						if (string.Equals(Reference.Name, Required, StringComparison.OrdinalIgnoreCase))
						{
							if (!Reference.bEnabled)
							{
								return false;
							}
							bFound = true;
							// Keep scanning duplicate direct references: a later explicit
							// disable must win regardless of descriptor order.
						}
					}
				}
				if (!bFound)
				{
					if (AvailablePluginSets == null)
					{
						AvailablePluginSets = ReadAvailablePluginSets(Target, Project);
					}
					PluginSet AvailableSet;
					if (!AvailablePluginSets.TryGetValue(Required, out AvailableSet)
						|| AvailableSet.ChoiceVersion == null
						|| !AvailableSet.ChoiceVersion.IsEnabledByDefault(
							Target.bAllowEnginePluginsEnabledByDefault))
					{
						return false;
					}
				}
			}
			return true;
		}
		catch (Exception)
		{
			// Fail-closed: an unreadable or malformed descriptor must not produce a link
			// against modules the project may not have enabled.
			return false;
		}
	}
}

/// <summary>
/// AnimMixer-family opt-in (Issue #27909). Thin named wrapper over NarshaPluginLink
/// so the original call sites keep working unchanged.
/// </summary>
public static class NarshaAnimMixerLink
{
	static readonly string[] RequiredPlugins =
		{ "MovieSceneAnimMixer", "HierarchyTable", "HierarchyTableAnimation" };

	public static bool ShouldLink(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, RequiredPlugins);
	}
}

/// <summary>
/// GameplayStateTree opt-in (Issue #28215 Phase A). Same contract as the AnimMixer
/// wrapper: the consuming project's own descriptor decides.
/// </summary>
public static class NarshaGameplayStateTreeLink
{
	static readonly string[] RequiredPlugins = { "GameplayStateTree" };

	public static bool ShouldLink(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, RequiredPlugins);
	}
}

/// <summary>
/// GameplayAbilities opt-in (Issue #28215 Phase A). Same contract as the AnimMixer
/// and GameplayStateTree wrappers.
/// </summary>
public static class NarshaGameplayAbilitiesLink
{
	static readonly string[] RequiredPlugins = { "GameplayAbilities" };

	public static bool ShouldLink(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, RequiredPlugins);
	}
}

/// <summary>
/// Phase B named probes (Issue #28215). Plugin-name literals deliberately live outside
/// the ModuleRules class so Rule 18 cannot confuse a probe argument with a module link.
/// </summary>
public static class NarshaIssue28215PluginLink
{
	static readonly string[] StateTreePlugins = { "StateTree", "PropertyBindingUtils" };
	static readonly string[] EnhancedInputPlugins = { "EnhancedInput" };
	static readonly string[] PCGPlugins = { "PCG" };
	static readonly string[] NiagaraPlugins = { "Niagara" };
	static readonly string[] GameplayCamerasPlugins = { "GameplayCameras" };
	static readonly string[] MetasoundPlugins = { "Metasound" };
	static readonly string[] LevelSequenceEditorPlugins = { "LevelSequenceEditor" };
	static readonly string[] SequencerScriptingPlugins = { "SequencerScripting" };
	static readonly string[] RigVMPlugins = { "RigVM" };
	static readonly string[] ControlRigPlugins = { "ControlRig" };
	static readonly string[] PythonScriptPluginPlugins = { "PythonScriptPlugin" };

	public static bool StateTree(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, StateTreePlugins);
	}

	public static bool EnhancedInput(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, EnhancedInputPlugins);
	}

	public static bool PCG(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, PCGPlugins);
	}

	public static bool Niagara(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, NiagaraPlugins);
	}

	public static bool GameplayCameras(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, GameplayCamerasPlugins);
	}

	public static bool Metasound(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, MetasoundPlugins);
	}

	public static bool LevelSequenceEditor(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, LevelSequenceEditorPlugins);
	}

	public static bool SequencerScripting(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, SequencerScriptingPlugins);
	}

	public static bool RigVM(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, RigVMPlugins);
	}

	public static bool ControlRig(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, ControlRigPlugins);
	}

	public static bool PythonScriptPlugin(ReadOnlyTargetRules Target)
	{
		return NarshaPluginLink.ShouldLink(Target, PythonScriptPluginPlugins);
	}
}
