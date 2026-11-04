// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #16915 (Epic #16906 Tier 3): Domain C++ authoring for the reflection-blocked
// BehaviorTree EDITOR GRAPH (UBehaviorTree::BTGraph + UBTGraphNode_*), which Python
// reflection / set_editor_property / Remote Control cannot reach. Mirrors the camera_rig
// #16577 spike pattern.

#pragma once

#include "CoreMinimal.h"

/**
 * A decorator/service sub-node spec (Issue #16915 Phase B — arbitrary topology).
 * `ClassName` is a native class short/full name (e.g. "BTDecorator_Blackboard") or a class object
 * path (e.g. "/Game/AI/BTD_Foo.BTD_Foo_C" for a Blueprint node). `Properties` are applied to the
 * spawned NodeInstance via reflection (FProperty::ImportText) — best effort, unknown keys skipped.
 */
struct FCUBTSubNodeSpec
{
	FString ClassName;
	TMap<FString, FString> Properties;
};

/** A behavior-tree node spec: a composite (with ordered children) or a leaf task. */
struct FCUBTNodeSpec
{
	FString Id;          // unique within the spec; referenced by parent `Children`/`RootChildId`
	FString Kind;        // "composite" | "task"
	FString ClassName;   // e.g. "BTComposite_Selector", "BTTask_Wait", or a /Game/... _C path
	int32 X = 0;
	int32 Y = 0;
	TArray<FString> Children; // ordered child ids (composites only)
	TMap<FString, FString> Properties;
	TArray<FCUBTSubNodeSpec> Decorators;
	TArray<FCUBTSubNodeSpec> Services;
};

/** A blackboard key to create (Issue #16915 Phase B.2). Type ∈ Bool/Int/Float/Vector/Rotator/
 *  Object/Class/Name/String/Enum (the common UBlackboardKeyType_* set). */
struct FCUBTBlackboardKeySpec
{
	FString Name;
	FString Type;
};

/** A full behavior-tree spec parsed from the tool's `tree` JSON. */
struct FCUBehaviorTreeSpec
{
	FString RootChildId;          // id of the node wired under the graph Root
	TArray<FCUBTNodeSpec> Nodes;
	// Blackboard (Phase B.2): `blackboard` is a path (load existing) OR {path, keys:[{name,type}]}
	// (create + save a new UBlackboardData). FBlackboardKeySelector node properties bind against it.
	FString BlackboardPath;
	TArray<FCUBTBlackboardKeySpec> BlackboardKeys; // non-empty → create at BlackboardPath
	bool bHasBlackboard = false;
	FString ParseError;           // non-empty if parsing/validation failed
	bool IsValid() const { return ParseError.IsEmpty() && !Nodes.IsEmpty() && !RootChildId.IsEmpty(); }
};

/**
 * Inputs for FCUBehaviorTreeAuthor::Execute.
 *
 * Plain C++ (non-USTRUCT) on purpose: an intra-module helper contract shared by the
 * commandlet (headless) and a future Remote Control UFUNCTION (live editor), neither of
 * which needs reflection on the parameter bag itself. Same shape as FCUCameraRigAuthorParams.
 */
struct FCUBehaviorTreeAuthorParams
{
	/** Long package name for the authored Behavior Tree, e.g. "/Game/NarshaMCP/Spike/BT_Authored".
	 *  An existing asset at this path is rejected unless bOverwrite is true (Issue #26809). */
	FString BTPackagePath;

	/** When false (default), Execute refuses if the package already exists on disk.
	 *  When true, the existing asset is replaced and a warning is logged. Issue #26809. */
	bool bOverwrite = false;

	/** Issue #16915 Phase B: arbitrary-topology spec as JSON. When non-empty it drives the whole
	 *  tree; when empty, the legacy fixed Selector→Wait default below is authored (back-compat). */
	FString SpecJson;

	/** When false (default), a property that fails to apply ABORTS the whole author before anything
	 *  is written to disk (Issue #26812). When true, the author proceeds and reports the failures in
	 *  FailedProperties -- the pre-#26812 behaviour, minus the silence. */
	bool bAllowPartialProperties = false;

	/** Legacy default-tree positions (used only when SpecJson is empty). */
	int32 CompositePosX = 0;
	int32 CompositePosY = 200;
	int32 TaskPosX = 0;
	int32 TaskPosY = 400;
};

/** Result of an authoring run; serialized to JSON by the commandlet for machine verification. */
struct FCUBehaviorTreeAuthorResult
{
	bool bSuccess = false;
	/** Number of UBTGraphNode_* nodes in the authored BTGraph (expected 3: Root + Composite + Task). */
	int32 NumGraphNodes = 0;
	/** Class name of the runtime RootNode after graph→tree sync (expected "BTComposite_Selector"). */
	FString RuntimeRootClass;
	/** true if UBehaviorTree::BTGraph is non-null after authoring (the reflection-blocked editor graph). */
	bool bHasEditorGraph = false;
	/** On-disk path of the saved .uasset (empty on failure). */
	FString SavedFilePath;
	/** Whether the editor graph was rebuilt into a valid runtime tree. */
	bool bCompiled = false;
	/** Compile/rebuild diagnostics. Empty when compilation succeeds. */
	TArray<FString> CompileErrors;
	/** Whether every requested package save completed successfully. */
	bool bSaved = false;
	/** Whether all saved packages could be loaded through an isolated diff package. */
	bool bReloadVerified = false;
	/** Whether the isolated reload matched the canonical editor/runtime/Blackboard signature. */
	bool bRuntimeGraphVerified = false;
	/** Whether at least one package committed before a later save failed. */
	bool bPartialCommit = false;
	/** Whether an on-disk rollback was attempted after a post-compile failure. */
	bool bRollbackAttempted = false;
	/** Whether every file snapshot was restored (or newly created file removed). */
	bool bRollbackSucceeded = false;
	/** Absolute filenames successfully saved before verification completed or failed. */
	TArray<FString> CommittedFiles;
	/** Errors encountered while restoring on-disk snapshots. */
	TArray<FString> RollbackErrors;
	/** Number of keys on the bound blackboard (-1 = no blackboard authored). Phase B.2. */
	int32 BlackboardKeyCount = -1;
	/** Pre-replace graph node count of an existing BT package (-1 = no existing package). Issue #26809. */
	int32 ExistingGraphNodeCount = -1;
	/** True when Execute replaced an existing package (`overwrite=true`). Issue #26809. */
	bool bOverwrote = false;
	/** Issue #26812. Property keys whose text the engine ACCEPTED. Note "accepted", not "fully
	 *  applied": the engine can accept a value while dropping part of it (e.g. surplus elements of a
	 *  fixed-size array) -- such a key appears here AND in Warnings. */
	TArray<FString> ModifiedProperties;
	/** Issue #26812. Property keys that could NOT be applied -- an unknown name, or an ImportText
	 *  rejection. The value of such a property must be treated as UNTRUSTWORTHY rather than
	 *  unchanged: a failed struct import can still have written part of the value
	 *  (measured: "(X=1,Y=2" leaves an FVector at (1,2,0) and returns nullptr).
	 *  Covers top-level scalars: a rejected import, and text that does not parse strictly as the
	 *  property's numeric type (both float and integer -- the engine accepts a PARTIAL numeric parse
	 *  and stores whatever Atof/Atoi made of the whole string).
	 *  KNOWN LIMITATION -- this does NOT cover a failure INSIDE a struct (e.g. "(DefaultValue=abc)")
	 *  or an unresolvable object reference: the engine returns non-null, writes nothing to the import
	 *  error device, and silently coerces the value. Those are pinned by the Property.* tests and
	 *  tracked in a follow-up issue.
	 *  PR3 ADDED a class: an FBlackboardKeySelector whose key is MISSING from the authored
	 *  blackboard, or EXISTS with a type the node's AllowedTypes filter forbids. Both are cases the
	 *  engine silently rewrites in FBlackboardKeySelector::PreSave on save, so reporting them as
	 *  applied was the same silent partial success. Validated via NarshaBlackboardKeyGuard (shared
	 *  with the runtime Remote Control path, Issue #16913). */
	TArray<FString> FailedProperties;
	/** Issue #26812. Non-fatal diagnostics -- an engine warning that accompanied an accepted value,
	 *  or (PR3) a key selector on a spec that authored NO blackboard: UBTNode::PreSave early-returns
	 *  without a blackboard, so nothing is rewritten and the saved asset matches the request. A key
	 *  that is merely wrong is a FailedProperty, not a warning. */
	TArray<FString> Warnings;
	/** True when Patch mutated an existing editor graph in place (Issue #26814). Never set by Execute. */
	bool bPatched = false;
	/** True when Patch ran with dry_run (in-memory apply, no SavePackage). Issue #26814. */
	bool bDryRun = false;
	/** Applied patch op names in order (Issue #26814). */
	TArray<FString> AppliedOps;
	/** Semantic diff counters (Issue #26814). */
	int32 NodesAdded = 0;
	int32 NodesRemoved = 0;
	int32 NodesMoved = 0;
	int32 ClassChanges = 0;
	int32 DecoratorsAdded = 0;
	int32 DecoratorsRemoved = 0;
	int32 ServicesAdded = 0;
	int32 ServicesRemoved = 0;
	int32 BbKeysRenamed = 0;
	/** Human-readable failure reason (empty on success). */
	FString Error;
};

/**
 * Inputs for FCUBehaviorTreeAuthor::Patch (Issue #26814).
 *
 * `PatchJson` is either a JSON array of ops or `{ "ops": [ ... ] }`. Presence of ops
 * selects load-in-place graph surgery — it is NOT `overwrite=true` (which evicts the graph).
 */
struct FCUBehaviorTreePatchParams
{
	FString BTPackagePath;
	FString PatchJson;
	bool bDryRun = false;
};

/**
 * Result of a Verify run: loads an existing BT .uasset FROM DISK (fresh, like the editor would),
 * reports its structure, and re-runs the graph→tree sync to prove recompile-survival. Used to
 * validate that an authored asset is a genuine, loadable, durable Behavior Tree — not just that
 * the right strings appear in the binary.
 */
struct FCUBehaviorTreeAuthorVerifyResult
{
	bool bLoaded = false;
	/** true if the loaded UBehaviorTree has a non-null BTGraph (editor graph present on disk). */
	bool bHasEditorGraph = false;
	/** Number of UBTGraphNode_* nodes in the loaded BTGraph. */
	int32 NumGraphNodes = 0;
	/** Per-node "Class @ (X,Y) inPins/outPins linked" summaries from the loaded graph. */
	TArray<FString> GraphNodeSummaries;
	/** Runtime RootNode class as loaded from disk (before any rebuild). */
	FString RootNodeClass;
	/** Runtime RootNode child count as loaded from disk. */
	int32 RootChildCount = 0;
	/** Class of the first runtime child (expected the Wait task). */
	FString FirstChildClass;
	/** Runtime RootNode class AFTER re-running UpdateAsset (simulates an editor recompile). */
	FString RootNodeClassAfterRebuild;
	/** Runtime child count after the simulated recompile. */
	int32 RootChildCountAfterRebuild = 0;
	/** true iff the runtime root class + child count are unchanged by the rebuild (no wipe). */
	bool bSurvivesRecompile = false;
	/** Number of keys on the loaded BT's blackboard (-1 = none). Phase B.2. */
	int32 BlackboardKeyCount = -1;
	FString Error;
};

/**
 * Authors a minimal BehaviorTree (Root → Selector → Wait) including its EDITOR GRAPH from C++.
 *
 * Why this exists (Issue #16915 / Epic #16906): UBehaviorTree::BTGraph and the visual node
 * classes UBTGraphNode_* live in the editor-only BehaviorTreeEditor module and are not surfaced
 * to UE reflection. No reflection-based automation path — set_editor_property,
 * ObjectTools.set_properties, execute_python, or Remote Control — can place a positioned visual
 * node in a Behavior Tree's graph; only the runtime tree (UBehaviorTree::RootNode->Children) is
 * reachable, and a runtime-only edit is wiped on the next editor compile when CreateBTFromGraph
 * rebuilds RootNode from the (stale) graph. Only domain C++ that builds the editor graph directly
 * makes BT authoring durable.
 *
 * The reflection-bypassing logic lives here (not in the commandlet) so the same call can later
 * back a live-editor UCURemoteLibrary UFUNCTION without duplication (mirrors FCUCameraRigAuthor).
 */
class NARSHAMCP_API FCUBehaviorTreeAuthor
{
public:
	/**
	 * Create a UBehaviorTree, build its editor graph (UBehaviorTreeGraph with a positioned
	 * Root → Selector → Wait node chain), sync the runtime tree from the graph via UpdateAsset,
	 * and save the package to disk.
	 *
	 * @param In   Authoring parameters (package path + node positions).
	 * @param Out  Populated with graph-node count, runtime root class, saved path, or error.
	 * @return true on success (editor graph built, runtime root synced, package saved).
	 */
	static bool Execute(const FCUBehaviorTreeAuthorParams& In, FCUBehaviorTreeAuthorResult& Out);

	/**
	 * Issue #26814: load an existing BehaviorTree's editor graph and apply incremental ops
	 * (add/remove/move/reorder/replace_class/decorator/service CRUD/BB key rename/set_properties/
	 * set_position) without EvictNamedAssetForOverwrite or CreateNewGraph. Always UpdateAsset;
	 * SavePackage unless bDryRun. Spec ids are stamped onto UBTNode::NodeName on create and patch-add.
	 */
	static bool Patch(const FCUBehaviorTreePatchParams& In, FCUBehaviorTreeAuthorResult& Out);

	/**
	 * Load an existing BehaviorTree .uasset from disk and validate it round-trips: report the
	 * editor-graph node structure + runtime tree, then re-run UpdateAsset (the editor's own
	 * recompile path) and confirm the runtime RootNode is unchanged (durability proof). Works on
	 * any BT (authored or hand-made), so it can be pointed at real project assets too.
	 *
	 * @param BTPackagePath  Long package name of the BT to load (e.g. "/Game/.../BT_Authored").
	 * @param Out            Populated with structure + recompile-survival verdict, or error.
	 * @return true if the asset loaded and the runtime root survived the simulated recompile.
	 */
	static bool Verify(const FString& BTPackagePath, FCUBehaviorTreeAuthorVerifyResult& Out);

	/**
	 * Issue #21226: author a BT whose child slot carries a Composite Decorator with non-empty
	 * FBTCompositeChild.DecoratorOps — the only editor path that persists AND/OR/NOT streams.
	 *
	 * Topology: Root → Selector → Wait, with Composite Decorator logic
	 * `(KeyA AND KeyB) OR NOT KeyC` (three UBTDecorator_Blackboard tests) on the Wait slot.
	 * Creates companion blackboard `/Game/.../BB_*` next to the BT when absent.
	 *
	 * @param BTPackagePath  Long package name, e.g. "/Game/NarshaMCP/AI/BT_NarshaMCP_DecoratorOps_Test".
	 * @param Out            Same result bag as Execute (saved path / graph counts / error).
	 * @return true on success (Composite Decorator compiled into runtime DecoratorOps + package saved).
	 */
	static bool AuthorDecoratorOpsFixture(const FString& BTPackagePath, FCUBehaviorTreeAuthorResult& Out);
};
