// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CURemoteLibrary.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNarshaMCPRemote, Log, All);

/**
 * NarshaMCP Remote Control Library (Issue #118)
 *
 * Exposes NarshaMCP functions to Unreal Remote Control Plugin.
 * Enables Python MCP server to call C++ functions via HTTP REST API.
 *
 * Performance vs Commandlet:
 * - Response time: <1s (vs 5-20s)
 * - Memory overhead: 0 (vs +500MB)
 * - User disruption: None (vs window popup)
 *
 * Usage (Python):
 *   PUT http://localhost:30010/remote/object/call
 *   {
 *     "objectPath": "/Script/NarshaMCP.Default__CURemoteLibrary",
 *     "functionName": "GenerateBlueprintMetadata",
 *     "parameters": {
 *       "BlueprintPaths": ["/Game/Blueprints/BP_Test.BP_Test"],
 *       "OutputPath": "D:/MyProject/.narshamcp/metadata.json"
 *     }
 *   }
 */
UCLASS()
class NARSHAMCP_API UCURemoteLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Generate Blueprint metadata JSON via Remote Control (Issue #118).
	 *
	 * Reuses existing BlueprintExtractors::ExtractBlueprintData() logic.
	 * Much faster than Commandlet (50-200x speedup).
	 *
	 * @param BlueprintPaths Array of Blueprint asset paths (e.g., "/Game/Blueprints/BP_Test.BP_Test")
	 * @param OutputPath Absolute file path where to write JSON (e.g., "D:/MyProject/.narshamcp/metadata.json")
	 * @return True if successful, False if any error occurred
	 *
	 * Example (Python):
	 *   client = RemoteControlClient()
	 *   result = client.generate_blueprint_metadata(
	 *       blueprint_paths=["/Game/Blueprints/BP_Test.BP_Test"],
	 *       output_path="D:/MyProject/.narshamcp/metadata.json"
	 *   )
	 *   if result["ReturnValue"]:
	 *       print("Metadata generated successfully")
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static bool GenerateBlueprintMetadata(
		const TArray<FString>& BlueprintPaths,
		const FString& OutputPath
	);

	/**
	 * Health check endpoint for Remote Control (Issue #118).
	 *
	 * Returns plugin version string to verify connection.
	 *
	 * @return Plugin version string (e.g., "NarshaMCP v4.6.0")
	 *
	 * Example (Python):
	 *   client = RemoteControlClient()
	 *   version = client.ping()  # "NarshaMCP v4.6.0"
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString Ping();

	/**
	 * Get detailed plugin and Editor status (Issue #118).
	 *
	 * Returns JSON string with plugin info, Editor version, uptime, etc.
	 *
	 * @return JSON string with status info
	 *
	 * Example (Python):
	 *   client = RemoteControlClient()
	 *   status = client.get_status()  # Parses JSON automatically
	 *   print(status["plugin_version"])  # "4.6.0"
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString GetStatus();

	/**
	 * Describe a UObject's class + properties as JSON (Issue #16334).
	 *
	 * Reflection-based replacement for the Epic RC HTTP `PUT /remote/object/describe`
	 * endpoint. Because this UFUNCTION is invoked over the plugin's own per-project
	 * WebSocket subsystem (`call_narsha_rc` → WS on UE 5.8+), it runs in THIS
	 * editor's process and therefore describes the daemon's OWN project — never a
	 * foreign concurrent editor's, unlike the shared Epic RC HTTP endpoint.
	 *
	 * @param ObjectPath Full object path (e.g. "/Game/Foo.Foo" or "/Game/Foo.Foo:Comp")
	 * @return JSON string: {success, object_path, name, class, class_path, outer, properties{...}}
	 *         or {success:false, error} when the object can't be loaded.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString DescribeObject(const FString& ObjectPath);

	/**
	 * Search the Asset Registry as JSON (Issue #16334).
	 *
	 * Reflection-free replacement for the Epic RC HTTP `PUT /remote/search/assets`
	 * endpoint. Invoked over the per-project WebSocket subsystem (`call_narsha_rc`
	 * → WS on UE 5.8+), so the query runs in THIS editor's Asset Registry and never
	 * returns a foreign concurrent editor's assets (the shared-port READ leak).
	 *
	 * @param QueryJson JSON string: {class_names:[...], package_paths:[...], max_results:N}.
	 *        Empty/omitted fields are ignored (no class/path filter → all assets).
	 * @return JSON string: {success, count, total_matched, truncated, assets:[{name,class,package,path}]}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString SearchAssets(const FString& QueryJson);

	/**
	 * Set pin default value via Remote Control (Issue #124 Phase 1).
	 *
	 * Reuses FCUBlueprintManipulator::SetPinDefaultValue() logic.
	 * Much faster than Commandlet (50-200x speedup).
	 *
	 * @param BlueprintPath Blueprint asset path (e.g., "/Game/Blueprints/BP_Test.BP_Test")
	 * @param NodeGuid Node GUID string (e.g., "12345678-1234-1234-1234-123456789012")
	 * @param PinId Pin ID string (e.g., "87654321-4321-4321-4321-210987654321")
	 * @param NewValue New default value as string
	 * @return True if successful, False if any error occurred
	 *
	 * Example (Python):
	 *   client = RemoteControlClient()
	 *   result = client.set_pin_default_value(
	 *       blueprint_path="/Game/Blueprints/BP_Test.BP_Test",
	 *       node_guid="12345678-1234-1234-1234-123456789012",
	 *       pin_id="87654321-4321-4321-4321-210987654321",
	 *       new_value="42"
	 *   )
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static bool SetPinDefaultValue(
		const FString& BlueprintPath,
		const FString& NodeGuid,
		const FString& PinId,
		const FString& NewValue
	);

	/**
	 * Set pin default value by node GUID + pin name (Issue #8564).
	 * More reliable than SetBlueprintPinValue (uses GUID instead of name).
	 * Uses UEdGraphSchema_K2::TrySetDefaultValue for proper validation.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static bool SetPinDefaultValueByName(
		const FString& BlueprintPath,
		const FString& NodeGuid,
		const FString& PinName,
		const FString& NewValue
	);

	/**
	 * Add a case pin to a Switch node (SwitchOnInt/SwitchOnString) (Issue #7720).
	 * Returns JSON: {"success":true, "pin_name":"..."} or {"success":false, "error":"..."}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString AddSwitchCase(
		const FString& BlueprintPath,
		const FString& NodeGuid,
		const FString& CaseValue
	);

	/**
	 * Bulk configure switch cases after T3D import (Issue #7720).
	 * CasesJson: {"type":"int","count":3} or {"type":"string","values":["A","B","C"]}
	 * Returns JSON: {"success":true} or {"success":false, "error":"..."}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString ConfigureSwitchCases(
		const FString& BlueprintPath,
		const FString& NodeGuid,
		const FString& CasesJson
	);

	/**
	 * Connect two pins via Remote Control (Issue #124, #4828).
	 * Returns JSON with error details and available pins on failure.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString ConnectPins(
		const FString& BlueprintPath,
		const FString& SourcePinId,
		const FString& TargetPinId
	);

	/**
	 * Connect two pins by node GUID + pin name (Issue #4751 Phase 42).
	 *
	 * Unlike ConnectPins which requires pin GUIDs, this finds pins by name on each node.
	 * Used by build_graph to auto-wire fallback nodes created via CreateBlueprintNode.
	 *
	 * @param BlueprintPath Blueprint asset path
	 * @param SourceNodeGuid GUID of the source node
	 * @param SourcePinName Name of the output pin on source node (e.g., "then", "Completed")
	 * @param TargetNodeGuid GUID of the target node
	 * @param TargetPinName Name of the input pin on target node (e.g., "execute", "Condition")
	 * @return JSON string with success status and pin details
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString ConnectPinsByName(
		const FString& BlueprintPath,
		const FString& SourceNodeGuid,
		const FString& SourcePinName,
		const FString& TargetNodeGuid,
		const FString& TargetPinName
	);

	/**
	 * Disconnect pin from all connections via Remote Control (Issue #124 Phase 1).
	 *
	 * Reuses FCUBlueprintManipulator::DisconnectPin() logic.
	 *
	 * @param BlueprintPath Blueprint asset path
	 * @param PinId Pin ID string to disconnect
	 * @return Number of disconnected links (0 if no connections)
	 *
	 * Example (Python):
	 *   client = RemoteControlClient()
	 *   count = client.disconnect_pin(
	 *       blueprint_path="/Game/Blueprints/BP_Test.BP_Test",
	 *       pin_id="11111111-1111-1111-1111-111111111111"
	 *   )
	 *   print(f"Disconnected {count} links")
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static int32 DisconnectPin(
		const FString& BlueprintPath,
		const FString& PinId
	);

	/**
	 * Disconnect a specific link between two pins (Issue #4828).
	 * Unlike DisconnectPin which breaks ALL links, this breaks only the link
	 * between SourcePinId and TargetPinId.
	 * Returns JSON with error details on failure.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString DisconnectPins(
		const FString& BlueprintPath,
		const FString& SourcePinIdStr,
		const FString& TargetPinIdStr
	);

	// =========================================================
	// Sequencer Remote Control Functions (Issue #1529 Phase 3)
	// =========================================================

	// ---------------------------------------------------------
	// Track Operations
	// ---------------------------------------------------------

	/**
	 * Add a track to a Level Sequence (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path (e.g., "/Game/Cinematics/LS_Intro.LS_Intro")
	 * @param TrackType Track type class name (e.g., "MovieSceneSkeletalAnimationTrack")
	 * @param BindingId Optional binding GUID to attach track to (empty for master tracks)
	 * @param TrackName Optional display name for the track
	 * @return JSON string with track info (guid, name, type) or error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString AddSequencerTrack(
		const FString& SequencePath,
		const FString& TrackType,
		const FString& BindingId,
		const FString& TrackName
	);

	/**
	 * Remove a track from a Level Sequence (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param TrackGuid Track GUID to remove
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static bool RemoveSequencerTrack(
		const FString& SequencePath,
		const FString& TrackGuid
	);

	/**
	 * List all tracks in a Level Sequence (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param BindingId Optional binding GUID to filter tracks (empty for all)
	 * @return JSON array of track objects (guid, name, type, section_count)
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString ListSequencerTracks(
		const FString& SequencePath,
		const FString& BindingId
	);

	/**
	 * Get track properties (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param TrackGuid Track GUID
	 * @return JSON object with track properties
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString GetSequencerTrackProperties(
		const FString& SequencePath,
		const FString& TrackGuid
	);

	/**
	 * Set track properties (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param TrackGuid Track GUID
	 * @param PropertiesJson JSON object with properties to set
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static bool SetSequencerTrackProperties(
		const FString& SequencePath,
		const FString& TrackGuid,
		const FString& PropertiesJson
	);

	// ---------------------------------------------------------
	// Binding Operations
	// ---------------------------------------------------------

	/**
	 * Add a binding (possessable or spawnable) to a Level Sequence (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param ActorPath World actor path or Blueprint class path
	 * @param BindingType "possessable" or "spawnable"
	 * @param BindingName Display name for the binding
	 * @return JSON string with binding info (guid, name, type) or error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString AddSequencerBinding(
		const FString& SequencePath,
		const FString& ActorPath,
		const FString& BindingType,
		const FString& BindingName
	);

	/**
	 * Remove a binding from a Level Sequence (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param BindingGuid Binding GUID to remove
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static bool RemoveSequencerBinding(
		const FString& SequencePath,
		const FString& BindingGuid
	);

	/**
	 * List all bindings in a Level Sequence (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @return JSON array of binding objects (guid, name, type, track_count)
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString ListSequencerBindings(
		const FString& SequencePath
	);

	// ---------------------------------------------------------
	// Keyframe Operations
	// ---------------------------------------------------------

	/**
	 * Add a keyframe to a track section (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param TrackGuid Track GUID
	 * @param SectionIndex Section index (0-based)
	 * @param FrameNumber Frame number
	 * @param Value Keyframe value as JSON
	 * @param ChannelName Channel name (optional, for multi-channel tracks)
	 * @return JSON string with keyframe info or error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString AddSequencerKeyframe(
		const FString& SequencePath,
		const FString& TrackGuid,
		int32 SectionIndex,
		int32 FrameNumber,
		const FString& Value,
		const FString& ChannelName
	);

	/**
	 * Remove a keyframe from a track section (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param TrackGuid Track GUID
	 * @param SectionIndex Section index
	 * @param FrameNumber Frame number of keyframe to remove
	 * @param ChannelName Channel name (optional)
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static bool RemoveSequencerKeyframe(
		const FString& SequencePath,
		const FString& TrackGuid,
		int32 SectionIndex,
		int32 FrameNumber,
		const FString& ChannelName
	);

	/**
	 * Get keyframe value at a specific frame (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param TrackGuid Track GUID
	 * @param SectionIndex Section index
	 * @param FrameNumber Frame number
	 * @param ChannelName Channel name (optional)
	 * @return JSON string with keyframe value
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString GetSequencerKeyframeValue(
		const FString& SequencePath,
		const FString& TrackGuid,
		int32 SectionIndex,
		int32 FrameNumber,
		const FString& ChannelName
	);

	/**
	 * Set keyframe value at a specific frame (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param TrackGuid Track GUID
	 * @param SectionIndex Section index
	 * @param FrameNumber Frame number
	 * @param Value New value as JSON
	 * @param ChannelName Channel name (optional)
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static bool SetSequencerKeyframeValue(
		const FString& SequencePath,
		const FString& TrackGuid,
		int32 SectionIndex,
		int32 FrameNumber,
		const FString& Value,
		const FString& ChannelName
	);

	/**
	 * List all keyframes in a track section (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param TrackGuid Track GUID
	 * @param SectionIndex Section index
	 * @param ChannelName Channel name (optional, empty for all channels)
	 * @return JSON array of keyframe objects (frame, value, interpolation)
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString ListSequencerKeyframes(
		const FString& SequencePath,
		const FString& TrackGuid,
		int32 SectionIndex,
		const FString& ChannelName
	);

	/**
	 * Move a keyframe to a new frame position (Issue #1529).
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param TrackGuid Track GUID
	 * @param SectionIndex Section index
	 * @param SourceFrame Current frame number
	 * @param DestFrame Target frame number
	 * @param ChannelName Channel name (optional)
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static bool MoveSequencerKeyframe(
		const FString& SequencePath,
		const FString& TrackGuid,
		int32 SectionIndex,
		int32 SourceFrame,
		int32 DestFrame,
		const FString& ChannelName
	);

	// ---------------------------------------------------------
	// Sequence Operations (Issue #1531 Phase 4)
	// ---------------------------------------------------------

	/**
	 * Create a new Level Sequence asset (Issue #1531 Phase 4).
	 *
	 * Creates a new empty Level Sequence at the specified path.
	 * The sequence will have default settings that can be modified later.
	 *
	 * @param SequencePath Asset path for new sequence (e.g., "/Game/Cinematics/LS_NewScene")
	 * @param FrameRate Display frame rate (default: 30)
	 * @return JSON string with sequence info (path, frame_rate, success) or error
	 *
	 * Example (Python):
	 *   client = RemoteControlClient()
	 *   result = client.create_level_sequence(
	 *       sequence_path="/Game/Cinematics/LS_NewScene",
	 *       frame_rate=24
	 *   )
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString CreateLevelSequence(
		const FString& SequencePath,
		int32 FrameRate
	);

	/**
	 * Set the playback range of a Level Sequence (Issue #1531 Phase 4).
	 *
	 * Sets the start and end frames for sequence playback.
	 * Uses the sequence's display rate for frame conversion.
	 *
	 * @param SequencePath Level Sequence asset path
	 * @param StartFrame Start frame number (inclusive)
	 * @param EndFrame End frame number (exclusive)
	 * @return JSON string with updated range info or error
	 *
	 * Example (Python):
	 *   client = RemoteControlClient()
	 *   result = client.set_sequencer_playback_range(
	 *       sequence_path="/Game/Cinematics/LS_Intro",
	 *       start_frame=0,
	 *       end_frame=300  # 10 seconds at 30fps
	 *   )
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString SetSequencerPlaybackRange(
		const FString& SequencePath,
		int32 StartFrame,
		int32 EndFrame
	);

	/**
	 * Get the playback range of a Level Sequence (Issue #1531 Phase 4).
	 *
	 * Returns the current start and end frames of sequence playback.
	 *
	 * @param SequencePath Level Sequence asset path
	 * @return JSON string with range info (start_frame, end_frame, duration_seconds)
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Sequencer")
	static FString GetSequencerPlaybackRange(
		const FString& SequencePath
	);

	// =========================================================
	// Blueprint Node Creation POC (Issue #2947 Phase 0)
	// =========================================================

	/**
	 * POC: Create a Branch (IfThenElse) node in Blueprint (Issue #2947 Phase 0).
	 *
	 * This is a proof-of-concept to validate K2Node creation patterns before
	 * implementing full node creation support.
	 *
	 * @param BlueprintPath Blueprint asset path (e.g., "/Game/Blueprints/BP_Test.BP_Test")
	 * @param GraphName Graph name to add node to (e.g., "EventGraph")
	 * @param PosX Node X position
	 * @param PosY Node Y position
	 * @return JSON string with node info (node_guid, pins) or error
	 *
	 * Example (Python):
	 *   client = RemoteControlClient()
	 *   result = client.create_branch_node_poc(
	 *       blueprint_path="/Game/Blueprints/BP_Test.BP_Test",
	 *       graph_name="EventGraph",
	 *       pos_x=100,
	 *       pos_y=200
	 *   )
	 *   # result["node_guid"] = "12345678-..."
	 *   # result["pins"] = {"Condition": "pin-guid-1", "Then": "pin-guid-2", "Else": "pin-guid-3"}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString CreateBlueprintNode_POC(
		const FString& BlueprintPath,
		const FString& GraphName,
		int32 PosX,
		int32 PosY
	);

	// =========================================================
	// Blueprint Node Creation (Issue #2947 Phase 1)
	// =========================================================

	/**
	 * Create a Blueprint node of specified type (Issue #2947 Phase 1).
	 *
	 * Supported NodeTypes (7 types):
	 *   - Branch: UK2Node_IfThenElse
	 *   - CallFunction: UK2Node_CallFunction (requires FunctionPath in PropertiesJson)
	 *   - CustomEvent: UK2Node_CustomEvent (requires EventName in PropertiesJson)
	 *   - VariableGet/GetVar: UK2Node_VariableGet (requires VariableName in PropertiesJson)
	 *   - VariableSet/SetVar: UK2Node_VariableSet (requires VariableName in PropertiesJson)
	 *   - Cast: UK2Node_DynamicCast (requires TargetClass in PropertiesJson)
	 *   - MakeArray: UK2Node_MakeArray
	 *
	 * Note: ForLoop/ForEachLoop are MacroInstance nodes in UE5, not supported yet.
	 *
	 * @param BlueprintPath Blueprint asset path
	 * @param GraphName Target graph name (default: "EventGraph")
	 * @param NodeType Node type string (e.g., "Branch", "CallFunction")
	 * @param PosX Node X position
	 * @param PosY Node Y position
	 * @param PropertiesJson JSON with node-specific properties
	 * @return JSON with node_guid and pins map
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString CreateBlueprintNode(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& NodeType,
		int32 PosX,
		int32 PosY,
		const FString& PropertiesJson
	);

	/**
	 * Delete a Blueprint node by GUID (Issue #2947 Phase 1).
	 *
	 * @param BlueprintPath Blueprint asset path
	 * @param NodeGuid Node GUID to delete
	 * @return JSON with success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString DeleteBlueprintNode(
		const FString& BlueprintPath,
		const FString& NodeGuid
	);

	/**
	 * Create a new variable in Blueprint (Issue #2947 Phase 1).
	 *
	 * @param BlueprintPath Blueprint asset path
	 * @param VarName Variable name
	 * @param VarType Variable type (bool, int, float, FString, FVector, etc.)
	 * @param DefaultValue Default value as string
	 * @param FlagsJson JSON with flags (BlueprintReadWrite, EditAnywhere, etc.)
	 * @return JSON with success status and variable info
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString CreateBlueprintVariable(
		const FString& BlueprintPath,
		const FString& VarName,
		const FString& VarType,
		const FString& DefaultValue,
		const FString& FlagsJson
	);

	// ============================================================================
	// BehaviorTree Modification (Issue #4263)
	// ============================================================================

	/**
	 * Modify a BehaviorTree Task node properties.
	 *
	 * Supports modifying simple properties like AcceptableRadius, WaitTime, etc.
	 * Complex properties like BlackboardKey bindings are not supported.
	 *
	 * @param BTPath BehaviorTree asset path (e.g., "/Game/AI/BT_Enemy.BT_Enemy")
	 * @param TaskName Task node's DISPLAY name, i.e. UBTNode::GetNodeName() -- the
	 *        author-set name if there is one, else the class's static description
	 *        (e.g. "Find Best Position", "Wait - NoAction"). It must be UNIQUE in the
	 *        tree; a name matching more than one node is rejected with AI_ERR_603.
	 *        NOT the offline read path's synthesised "<Class>_<Index>" form
	 *        (shaped like "BTTask_MoveTo_<n>") -- that never resolves here, because the read path
	 *        answers from uasset_metadata_cache while this function resolves inside the
	 *        Editor. Issue #28276 shipped nine such values into the live catalog.
	 * @param PropertiesJson JSON object with property names and values
	 * @return JSON with success status and modified properties
	 *
	 * Example:
	 *   ModifyBehaviorTreeTask("/Game/AI/BT_Enemy", "Find Best Position",
	 *       "{\"AcceptableRadius\": 100.0, \"bStopOnOverlap\": true}")
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyBehaviorTreeTask(
		const FString& BTPath,
		const FString& TaskName,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Modify a BehaviorTree Decorator node properties.
	 *
	 * Supports modifying simple properties on decorators.
	 * Complex properties like BlackboardKey bindings are not supported.
	 *
	 * @param BTPath BehaviorTree asset path (e.g., "/Game/AI/BT_Enemy.BT_Enemy")
	 * @param DecoratorName Decorator node's DISPLAY name (e.g. "Have Ammo", "Is Enemy Find").
	 *        It must be UNIQUE in the tree (AI_ERR_603 otherwise) and is the Editor's
	 *        display name -- NOT the read path's synthesised "<Class>_<Index>" form.
	 *        See ModifyBehaviorTreeTask above (Issue #28276).
	 * @param PropertiesJson JSON object with property names and values
	 * @return JSON with success status and modified properties
	 *
	 * Example:
	 *   ModifyBehaviorTreeDecorator("/Game/AI/BT_Enemy", "BTDecorator_Cooldown_0",
	 *       "{\"CooldownTime\": 5.0}")
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyBehaviorTreeDecorator(
		const FString& BTPath,
		const FString& DecoratorName,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Modify a BehaviorTree Service node's properties (Issue #16572 gap2 — parity
	 * with ModifyBehaviorTreeTask/ModifyBehaviorTreeDecorator).
	 *
	 * Services attach to composites (UBTCompositeNode::Services) AND task leaves
	 * (UBTTaskNode::Services); the lookup DFS-searches both arrays by GetNodeName().
	 *
	 * @param BTPath BehaviorTree asset path (e.g., "/Game/AI/BT_Enemy.BT_Enemy")
	 * @param ServiceName Service node's DISPLAY name (e.g. "Reload Weapon", "BTS_CheckAmmo").
	 *        It must be UNIQUE in the tree (AI_ERR_603 otherwise) and is the Editor's
	 *        display name -- NOT the read path's synthesised "<Class>_<Index>" form.
	 *        See ModifyBehaviorTreeTask above (Issue #28276).
	 * @param PropertiesJson JSON object with property names and values
	 * @return JSON with success status and modified properties
	 *
	 * Example:
	 *   ModifyBehaviorTreeService("/Game/AI/BT_Enemy", "Reload Weapon",
	 *       "{\"Interval\": 0.25}")
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyBehaviorTreeService(
		const FString& BTPath,
		const FString& ServiceName,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	// ============================================================================
	// BehaviorTree node topology authoring (Issue #16912)
	// ============================================================================
	//
	// These author the RUNTIME tree (UBehaviorTree::RootNode->Children) directly.
	// The runtime BehaviorTreeManager::LoadTree recomputes ExecutionIndex /
	// memory offsets from the structure at PIE load (InitializeNodeHelper), so we
	// only build the node graph + save — no ExecutionIndex baking needed. They do
	// NOT touch the editor UBehaviorTreeGraph: the result PIE-runs immediately and
	// persists across editor reopen only while the asset has no BTGraph yet
	// (OnCreated->SpawnMissingNodes rescues RootNode into a graph); an asset that
	// already has a BTGraph wipes the edit on the next editor compile
	// (CreateBTFromGraph rebuilds RootNode from the graph). When BTGraph != null we
	// surface a "warning" field. Editor-graph node placement is the deferred
	// engine-wall sibling #16915.

	/**
	 * Add a composite node (Selector/Sequence/SimpleParallel) to a BehaviorTree.
	 *
	 * @param BTPath BehaviorTree asset path (e.g., "/Game/AI/BT_Enemy.BT_Enemy")
	 * @param ParentName Parent composite node name; EMPTY sets the new composite
	 *                   as RootNode (a fresh BT has RootNode==null)
	 * @param CompositeClass Concrete UBTCompositeNode subclass (e.g. "BTComposite_Selector")
	 * @param NodeName Optional display name for the new node
	 * @param PropertiesJson Optional JSON object of property name/value pairs
	 * @return JSON with success status, node_name, and BTGraph warning if any
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddBehaviorTreeComposite(
		const FString& BTPath,
		const FString& ParentName,
		const FString& CompositeClass,
		const FString& NodeName,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Add a task (leaf) node under a parent composite of a BehaviorTree.
	 *
	 * @param BTPath BehaviorTree asset path
	 * @param ParentName Parent composite node name (required)
	 * @param TaskClass Concrete UBTTaskNode subclass (e.g. "BTTask_MoveTo")
	 * @param NodeName Optional display name for the new node
	 * @param PropertiesJson Optional JSON object of property name/value pairs
	 * @return JSON with success status, node_name, and BTGraph warning if any
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddBehaviorTreeTask(
		const FString& BTPath,
		const FString& ParentName,
		const FString& TaskClass,
		const FString& NodeName,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Add a decorator onto a target node's child-link in a BehaviorTree.
	 *
	 * @param BTPath BehaviorTree asset path
	 * @param TargetNodeName Node whose parent child-link receives the decorator
	 * @param DecoratorClass Concrete UBTDecorator subclass (e.g. "BTDecorator_Blackboard")
	 * @param PropertiesJson Optional JSON object of property name/value pairs
	 * @return JSON with success status and BTGraph warning if any
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddBehaviorTreeDecorator(
		const FString& BTPath,
		const FString& TargetNodeName,
		const FString& DecoratorClass,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Add a service onto a target composite node in a BehaviorTree.
	 *
	 * @param BTPath BehaviorTree asset path
	 * @param TargetCompositeName Composite node the service attaches to
	 * @param ServiceClass Concrete UBTService subclass (e.g. "BTService_DefaultFocus")
	 * @param PropertiesJson Optional JSON object of property name/value pairs
	 * @return JSON with success status and BTGraph warning if any
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddBehaviorTreeService(
		const FString& BTPath,
		const FString& TargetCompositeName,
		const FString& ServiceClass,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Reparent an existing node under a different composite — BehaviorTree's form
	 * of "connect" (Issue #16912). Moves the node's whole child-link (preserving
	 * its decorators). Rejects reparenting the root, ambiguous names, a missing
	 * node/parent, and cycles (new parent inside the moved subtree).
	 *
	 * @param BTPath BehaviorTree asset path
	 * @param NodeName Non-root node to move (must be unique)
	 * @param NewParentName Destination composite node
	 * @return JSON with success status and BTGraph warning if any
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ReparentBehaviorTreeNode(
		const FString& BTPath,
		const FString& NodeName,
		const FString& NewParentName,
		bool bPreviewOnly = false
	);

	// ============================================================================
	// StateTree/BehaviorTree Creation & StateTree Modification (Issue #4263)
	// ============================================================================

	/**
	 * Create a new BehaviorTree asset.
	 *
	 * @param AssetName Name of the new asset (e.g., "BT_Enemy")
	 * @param PackagePath Package path (e.g., "/Game/AI")
	 * @return JSON with created asset path and success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString CreateBehaviorTree(
		const FString& AssetName,
		const FString& PackagePath,
		bool bPreviewOnly = false
	);

	/**
	 * Create a new StateTree asset with default schema.
	 *
	 * @param AssetName Name of the new asset (e.g., "ST_Enemy")
	 * @param PackagePath Package path (e.g., "/Game/AI")
	 * @return JSON with created asset path and success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString CreateStateTree(
		const FString& AssetName,
		const FString& PackagePath,
		bool bPreviewOnly = false
	);

	// ============================================================================
	// EnvironmentQuery (EQS) authoring (Issue #16572 gap4)
	// ============================================================================
	//
	// Author a real UEnvQuery headlessly. EQS classes (UEnvQuery::Options[] ->
	// Option.Generator / Option.Tests[]) live in AIModule (no editor module
	// needed). `create` seeds one empty Option; modify ops address it by
	// option_index (default 0).

	/**
	 * Create a new EnvQuery (EQS) asset with one empty Option.
	 *
	 * @param AssetName Name of the new asset (e.g., "EQS_FindCover")
	 * @param PackagePath Package path (e.g., "/Game/AI")
	 * @return JSON with created asset path, option_count, and success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString CreateEnvQuery(
		const FString& AssetName,
		const FString& PackagePath,
		bool bPreviewOnly = false
	);

	/**
	 * Append a new empty UEnvQueryOption to an existing EnvQuery (Issue #26099).
	 *
	 * NOTE: appends — NOT idempotent. Each call grows Options[] by one. The
	 * response `option_index` is the authoritative 0-based Options[] position
	 * of the new option (Num()-1 after Add). create still seeds index 0.
	 *
	 * @param EQSPath EnvQuery asset path (e.g., "/Game/AI/EQS_FindCover.EQS_FindCover")
	 * @return JSON with success, option_index, option_count
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddEnvQueryOption(
		const FString& EQSPath,
		bool bPreviewOnly = false
	);

	/**
	 * Set the generator on an EQS option (creates/replaces Option[OptionIndex].Generator).
	 *
	 * @param EQSPath EnvQuery asset path (e.g., "/Game/AI/EQS_FindCover.EQS_FindCover")
	 * @param OptionIndex Index into UEnvQuery::Options (create seeds index 0)
	 * @param GeneratorClass Concrete UEnvQueryGenerator subclass (e.g. "EnvQueryGenerator_ActorsOfClass")
	 * @param PropertiesJson Optional JSON object of property name/value pairs. Only
	 *        scalar props apply (bool/number/string/name); EQS struct-valued props
	 *        (FAIDataProviderValue, FEnv*Param — e.g. a generator's SearchRadius) are
	 *        NOT yet settable and are silently skipped. The response `modified_properties`
	 *        array lists what actually applied, so a no-op is observable.
	 * @return JSON with success status, option_index, generator_class, modified_properties
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyEQSGenerator(
		const FString& EQSPath,
		int32 OptionIndex,
		const FString& GeneratorClass,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Add a test to an EQS option (APPENDS to Option[OptionIndex].Tests).
	 *
	 * NOTE: appends — NOT idempotent. Repeated calls with the same TestClass add
	 * duplicate tests (an option legitimately carries multiple tests). The response
	 * `test_count` lets a caller detect growth before retrying.
	 *
	 * @param EQSPath EnvQuery asset path
	 * @param OptionIndex Index into UEnvQuery::Options (create seeds index 0)
	 * @param TestClass Concrete UEnvQueryTest subclass (e.g. "EnvQueryTest_Distance")
	 * @param PropertiesJson Optional JSON object of property name/value pairs. Scalar
	 *        props only (see ModifyEQSGenerator); struct props are silently skipped and
	 *        the applied set is returned in `modified_properties`.
	 * @return JSON with success status, option_index, test_class, test_count, modified_properties
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyEQSTest(
		const FString& EQSPath,
		int32 OptionIndex,
		const FString& TestClass,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Modify a StateTree state's properties.
	 *
	 * Supports modifying simple properties (Name, SelectionBehavior, etc.)
	 * via UProperty reflection.
	 *
	 * @param AssetName StateTree asset path (e.g., "/Game/AI/ST_Enemy.ST_Enemy")
	 * @param StateName Display name or UStateTreeState::ID GUID. Ambiguous names are rejected (Issue #26888).
	 * @param PropertiesJson JSON object with property names and values
	 * @return JSON with success status and modified properties
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyStateTreeState(
		const FString& AssetName,
		const FString& StateName,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Modify a StateTree transition's properties.
	 *
	 * Supports: Priority, bDelayTransition, DelayDuration, DelayRandomVariance,
	 * bTransitionEnabled.
	 *
	 * Issue #26804: the selector is resolved by AXIS PRECEDENCE, not by an
	 * unordered OR — transition GUID, then zero-based global index, then target
	 * state name. Ordering matters because a state merely NAMED "2" must not
	 * pre-empt numeric index 2. The name axis applies only when it identifies
	 * exactly ONE transition; several transitions may legitimately share a target
	 * state, so a collision is REFUSED with AI_ERR_603 and a candidate list
	 * ("candidates: [idx] Source -> Target (id=GUID), ...") rather than silently
	 * editing whichever the hierarchy walk reached first. Mirrors the state-side
	 * contract added for Issue #26888.
	 *
	 * @param AssetName StateTree asset path (e.g., "/Game/AI/ST_Enemy.ST_Enemy")
	 * @param TransitionIndex Selector: transition GUID, zero-based global index
	 *        (digits only), or target state name. A target state name that matches
	 *        more than one transition is rejected — pass the GUID or the index.
	 * @param PropertiesJson JSON object with property names and values
	 * @return JSON with success status and modified properties, or an AI_ERR_603
	 *         envelope listing the candidates when the name is ambiguous
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyStateTreeTransition(
		const FString& AssetName,
		const FString& TransitionIndex,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Add a new state to a StateTree asset (Issue #4263).
	 *
	 * @param AssetName StateTree asset path (e.g., "/Game/AI/ST_Enemy.ST_Enemy")
	 * @param StateName Name for the new state (must be unique within the tree)
	 * @param ParentStateName Parent display name or GUID (empty for root-level). Ambiguous names are rejected (Issue #26888).
	 * @param PropertiesJson JSON object with initial property values
	 * @return JSON with success status and new state info
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddStateTreeState(
		const FString& AssetName,
		const FString& StateName,
		const FString& ParentStateName,
		const FString& PropertiesJson,
		bool bPreviewOnly = false
	);

	/**
	 * Remove a state from a StateTree asset (Issue #4263).
	 *
	 * @param AssetName StateTree asset path (e.g., "/Game/AI/ST_Enemy.ST_Enemy")
	 * @param StateName Display name or UStateTreeState::ID GUID. Ambiguous names are rejected (Issue #26888).
	 * @return JSON with success status and removal info
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString RemoveStateTreeState(
		const FString& AssetName,
		const FString& StateName,
		bool bPreviewOnly = false
	);

	/**
	 * Add a parameter to a StateTree's root parameter bag (Issue #11472 Gap 3).
	 *
	 * Appends a new property to UStateTreeEditorData's RootParameterPropertyBag and
	 * optionally seeds it from a JSON default value. Works on UE 5.7 and 5.8.
	 *
	 * @param AssetName StateTree asset path (e.g., "/Game/AI/ST_Enemy.ST_Enemy")
	 * @param ParameterName Name for the new parameter (must be unique within the bag)
	 * @param ParameterTypeName Scalar type ("bool"/"int32"/"float"/"FName"/"FString"/"FVector")
	 *                          or a UScriptStruct path ("/Script/Module.MyStruct") for a struct parameter
	 * @param DefaultValueJson JSON scalar/object for the parameter's default value (may be empty)
	 * @return JSON with success status, asset_path, parameter_name, parameter_type
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddStateTreeParameter(
		const FString& AssetName,
		const FString& ParameterName,
		const FString& ParameterTypeName,
		const FString& DefaultValueJson,
		bool bPreviewOnly = false
	);

	/**
	 * Add a task node to a StateTree state (or to the global task list) (Issue #11472 Gap 3).
	 *
	 * Requires UE 5.8+ (uses FStateTreeEditorNode::InitializeAs runtime overload).
	 *
	 * @param AssetName StateTree asset path (e.g., "/Game/AI/ST_Enemy.ST_Enemy")
	 * @param StateName Target display name or GUID; empty -> append to EditorData->GlobalTasks. Ambiguous names are rejected (Issue #26888).
	 * @param TaskClassName UScriptStruct path of a FStateTreeTaskBase-derived task
	 *                      (e.g., "/Script/StateTreeModule.StateTreeDelayTask")
	 * @param InstanceDataJson JSON object for the task's instance data (may be empty)
	 * @return JSON with success status, asset_path, state_name, task_struct, task_id
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddStateTreeTask(
		const FString& AssetName,
		const FString& StateName,
		const FString& TaskClassName,
		const FString& InstanceDataJson,
		bool bPreviewOnly = false
	);

	/**
	 * Add a property binding to a StateTree's editor bindings (Issue #11472 Gap 3).
	 *
	 * If "completion_condition" is supplied a task-completion binding is created
	 * (UE 5.8+ only); otherwise a plain source->target property binding is added.
	 *
	 * @param AssetName StateTree asset path (e.g., "/Game/AI/ST_Enemy.ST_Enemy")
	 * @param BindingDescriptorJson JSON object:
	 *        { "source_task_id": "<guid>",
	 *          "target_path": "Foo.Bar"  (or "<guid>:Foo.Bar"),
	 *          "target_struct_id": "<guid>" (optional, overrides the prefix form),
	 *          "completion_condition": "Succeeded"|"Failed"|"Any" (optional, UE 5.8+ only) }
	 * @return JSON with success status, asset_path, source_task_id, target_path, binding_kind
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddStateTreeBinding(
		const FString& AssetName,
		const FString& BindingDescriptorJson,
		bool bPreviewOnly = false
	);

	/**
	 * Add a new transition to a StateTree source state (Issue #11397).
	 *
	 * Appends a FStateTreeTransition to SourceState->Transitions, targeting TargetStateName,
	 * with trigger/priority/delay sourced from TriggerJson. Works on UE 5.7 + 5.8.
	 *
	 * Wraps the mutation in FScopedTransaction so the action is undoable in the editor.
	 *
	 * @param AssetName StateTree asset path (e.g., "/Game/AI/ST_Enemy.ST_Enemy")
	 * @param SourceStateName Source display name or GUID. Ambiguous names are rejected (Issue #26888).
	 * @param TargetStateName Target display name or GUID (must exist unless empty; LinkType=GotoState). Ambiguous names are rejected.
	 * @param TriggerJson Optional JSON: { "Trigger": "OnStateCompleted"|"OnStateSucceeded"|"OnStateFailed"|"OnEvent"|"OnTick",
	 *                                     "Priority": "Low"|"Normal"|"Medium"|"High"|"Critical",
	 *                                     "bDelayTransition": bool, "DelayDuration": float,
	 *                                     "DelayRandomVariance": float, "bTransitionEnabled": bool,
	 *                                     "EventTag": "Tag.Path" (for OnEvent trigger) }
	 *                    Defaults: Trigger=OnStateCompleted, Priority=Normal, bTransitionEnabled=true.
	 * @return JSON with success status, asset_path, source_state, target_state, transition_id, applied_properties
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddStateTreeTransition(
		const FString& AssetName,
		const FString& SourceStateName,
		const FString& TargetStateName,
		const FString& TriggerJson,
		bool bPreviewOnly = false
	);

	/**
	 * Remove a transition from its owning StateTree state (Issue #27615).
	 *
	 * Selector is the same axis-precedence contract as ModifyStateTreeTransition
	 * (Issue #26804): transition GUID, then zero-based global index (digits only),
	 * then unique target-state name. A name that matches more than one transition
	 * is refused with AI_ERR_603 and a candidate list.
	 *
	 * Undo/Modify follows Issue #26803: FScopedTransaction then Modify() only the
	 * owning UStateTreeState (the Transitions TArray). UStateTree / EditorData
	 * are not Modify targets.
	 *
	 * @param AssetName StateTree asset path (e.g., "/Game/AI/ST_Enemy.ST_Enemy")
	 * @param TransitionIndex Selector: GUID, digits-only global index, or unique
	 *        target state name. Empty is rejected.
	 * @return JSON with success status, removed_transition_id, source_state, and
	 *         remaining_transitions (GUID strings), or an AI_ERR_603 envelope

	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString RemoveStateTreeTransition(
		const FString& AssetName,
		const FString& TransitionIndex,
		bool bPreviewOnly = false
	);

	/**
	 * Remove a task node by task GUID (Issue #26808 ST-06).
	 * StateName is optional (empty searches GlobalTasks then every state).
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString RemoveStateTreeTask(
		const FString& AssetName,
		const FString& TaskId,
		const FString& StateName,
		bool bPreviewOnly = false
	);

	/**
	 * Remove a root parameter from a StateTree parameter bag (Issue #26808 ST-06).
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString RemoveStateTreeParameter(
		const FString& AssetName,
		const FString& ParameterName,
		bool bPreviewOnly = false
	);

	/**
	 * Remove a property binding (Issue #26808 ST-06).
	 * BindingDescriptorJson: {"source_task_id":"<guid>","target_path":"optional","target_struct_id":"optional"}.
	 * If target_path is omitted, every binding from that source is removed.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString RemoveStateTreeBinding(
		const FString& AssetName,
		const FString& BindingDescriptorJson,
		bool bPreviewOnly = false
	);

	/**
	 * Set an existing root parameter's value (Issue #26808 ST-06).
	 * Does not change the parameter's type. DefaultValueJson uses the same
	 * scalar/struct encoding as AddStateTreeParameter.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyStateTreeParameter(
		const FString& AssetName,
		const FString& ParameterName,
		const FString& DefaultValueJson,
		bool bPreviewOnly = false
	);

	/**
	 * Move a state under a new parent (empty NewParentStateName = root SubTrees).
	 * Rejects cycles. SiblingIndex is optional (empty = append).
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ReparentStateTreeState(
		const FString& AssetName,
		const FString& StateName,
		const FString& NewParentStateName,
		const FString& SiblingIndex,
		bool bPreviewOnly = false
	);

	/**
	 * Reorder a state among its current siblings (Issue #26808 ST-06).
	 * SiblingIndex is 0-based and is clamped to the sibling array.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ReorderStateTreeState(
		const FString& AssetName,
		const FString& StateName,
		const FString& SiblingIndex,
		bool bPreviewOnly = false
	);

	/** Add an enter-condition (or a transition condition if TransitionIndex is set). UE 5.8+. */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddStateTreeCondition(
		const FString& AssetName,
		const FString& StateName,
		const FString& TransitionIndex,
		const FString& ConditionClassName,
		const FString& InstanceDataJson,
		bool bPreviewOnly = false,
		bool bTransitionIdOnly = false,
		bool bStateIdOnly = false
	);

	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyStateTreeCondition(
		const FString& AssetName,
		const FString& NodeId,
		const FString& InstanceDataJson,
		bool bPreviewOnly = false
	);

	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString RemoveStateTreeCondition(
		const FString& AssetName,
		const FString& NodeId,
		bool bPreviewOnly = false
	);

	/** Add a global evaluator. UE 5.8+. */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddStateTreeEvaluator(
		const FString& AssetName,
		const FString& EvaluatorClassName,
		const FString& InstanceDataJson,
		bool bPreviewOnly = false
	);

	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyStateTreeEvaluator(
		const FString& AssetName,
		const FString& NodeId,
		const FString& InstanceDataJson,
		bool bPreviewOnly = false
	);

	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString RemoveStateTreeEvaluator(
		const FString& AssetName,
		const FString& NodeId,
		bool bPreviewOnly = false
	);

	/** Add a state consideration. UE 5.8+. */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString AddStateTreeConsideration(
		const FString& AssetName,
		const FString& StateName,
		const FString& ConsiderationClassName,
		const FString& InstanceDataJson,
		bool bPreviewOnly = false
	);

	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString ModifyStateTreeConsideration(
		const FString& AssetName,
		const FString& NodeId,
		const FString& InstanceDataJson,
		bool bPreviewOnly = false
	);

	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AI")
	static FString RemoveStateTreeConsideration(
		const FString& AssetName,
		const FString& NodeId,
		bool bPreviewOnly = false
	);


	// =========================================================
	// PCG Graph User-Parameter Write API (Issue #18416)
	// =========================================================

	/**
	 * Add a user parameter to a UPCGGraph's parameter bag (Issue #18416).
	 *
	 * Appends a new property to UPCGGraph::UserParameters (FInstancedPropertyBag) via
	 * UpdateUserParametersStruct() and optionally seeds it from a JSON default value.
	 * Sibling of AddStateTreeParameter; shares StateTreeWriteHelpers::ResolveParameterType.
	 *
	 * @param AssetName PCG graph asset path (e.g., "/Game/PCG/PCG_Forest.PCG_Forest")
	 * @param ParameterName Name for the new parameter (must be unique within the bag)
	 * @param ParameterTypeName Scalar type ("bool"/"int32"/"float"/"FName"/"FString"/"FVector")
	 *                          or a UScriptStruct path ("/Script/Module.MyStruct" or a UDS path) for a struct parameter
	 * @param DefaultValueJson JSON scalar/object for the parameter's default value (may be empty)
	 * @return JSON with success status, asset_path, parameter_name, parameter_type, default_applied
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|PCG")
	static FString AddPCGGraphUserParameter(
		const FString& AssetName,
		const FString& ParameterName,
		const FString& ParameterTypeName,
		const FString& DefaultValueJson
	);

	/**
	 * List a UPCGGraph's user parameters (Issue #18416).
	 *
	 * Reads UPCGGraph::GetUserParametersStruct() and serializes each property to a
	 * {name, type, value} record. Editor-only (offline binary PropertyBag decode is deferred).
	 *
	 * @param AssetName PCG graph asset path (e.g., "/Game/PCG/PCG_Forest.PCG_Forest")
	 * @return JSON with success status, asset_path, parameter_count, parameters[]
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|PCG")
	static FString GetPCGGraphUserParameters(
		const FString& AssetName
	);

	/**
	 * Set the value of an existing UPCGGraph user parameter (Issue #18416).
	 *
	 * @param AssetName PCG graph asset path (e.g., "/Game/PCG/PCG_Forest.PCG_Forest")
	 * @param ParameterName Name of the existing parameter to set (must already exist in the bag)
	 * @param ValueJson JSON scalar/object for the new value
	 * @return JSON with success status, asset_path, parameter_name, value_applied
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|PCG")
	static FString SetPCGGraphUserParameter(
		const FString& AssetName,
		const FString& ParameterName,
		const FString& ValueJson
	);

	/**
	 * Remove a user parameter from a UPCGGraph (Issue #18416).
	 *
	 * @param AssetName PCG graph asset path (e.g., "/Game/PCG/PCG_Forest.PCG_Forest")
	 * @param ParameterName Name of the parameter to remove (must exist in the bag)
	 * @return JSON with success status, asset_path, parameter_name
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|PCG")
	static FString RemovePCGGraphUserParameter(
		const FString& AssetName,
		const FString& ParameterName
	);

	// =========================================================
	// AnimBP State Machine CRUD (Issue #6389)
	// =========================================================

	/**
	 * Add a state to an AnimBP State Machine (Issue #6389 Phase 1).
	 *
	 * Creates a new UAnimStateNode in the specified state machine's editor graph.
	 * Optionally associates an animation asset (sequence player) with the state.
	 *
	 * @param BlueprintPath AnimBlueprint asset path (e.g., "/Game/Characters/ABP_Mannequin.ABP_Mannequin")
	 * @param StateMachineName Name of the state machine (e.g., "Locomotion")
	 * @param StateName Name for the new state
	 * @param AnimationAsset Optional animation sequence path to play in this state
	 * @param PositionX Node X position in graph editor
	 * @param PositionY Node Y position in graph editor
	 * @return JSON with success, state_name, node_guid, all_states
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AnimBP")
	static FString AddAnimState(
		const FString& BlueprintPath,
		const FString& StateMachineName,
		const FString& StateName,
		const FString& AnimationAsset,
		float PositionX,
		float PositionY
	);

	/**
	 * Remove a state from an AnimBP State Machine (Issue #6389 Phase 1).
	 *
	 * Removes the UAnimStateNode and all connected transitions.
	 *
	 * @param BlueprintPath AnimBlueprint asset path
	 * @param StateMachineName Name of the state machine
	 * @param StateName Name of the state to remove
	 * @return JSON with success, removed_state, removed_transitions_count
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AnimBP")
	static FString RemoveAnimState(
		const FString& BlueprintPath,
		const FString& StateMachineName,
		const FString& StateName
	);

	/**
	 * Set the entry (default) state of an AnimBP State Machine (Issue #6389 Phase 1).
	 *
	 * Updates the entry node's connection to point to the specified state.
	 *
	 * @param BlueprintPath AnimBlueprint asset path
	 * @param StateMachineName Name of the state machine
	 * @param StateName Name of the state to set as entry
	 * @return JSON with success, entry_state, previous_entry_state
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AnimBP")
	static FString SetAnimEntryState(
		const FString& BlueprintPath,
		const FString& StateMachineName,
		const FString& StateName
	);

	/**
	 * Add a transition between two states in an AnimBP State Machine (Issue #6389 Phase 2).
	 *
	 * Creates a UAnimStateTransitionNode connecting source and destination states.
	 *
	 * @param BlueprintPath AnimBlueprint asset path
	 * @param StateMachineName Name of the state machine
	 * @param FromState Source state name
	 * @param ToState Destination state name
	 * @param Duration Crossfade duration in seconds (default: 0.2)
	 * @param BlendMode Blend mode string: "Standard", "Inertialization" (default: "Standard")
	 * @return JSON with success, from_state, to_state, transition_info
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AnimBP")
	static FString AddAnimTransition(
		const FString& BlueprintPath,
		const FString& StateMachineName,
		const FString& FromState,
		const FString& ToState,
		float Duration,
		const FString& BlendMode
	);

	/**
	 * Set transition rule properties (Issue #6389 Phase 2).
	 *
	 * Modifies an existing transition's blend settings without compiling it.
	 *
	 * @param BlueprintPath AnimBlueprint asset path
	 * @param StateMachineName Name of the state machine
	 * @param FromState Source state name
	 * @param ToState Destination state name
	 * @param PropertiesJson JSON with: CrossfadeDuration, PriorityOrder, BlendMode,
	 * CustomBlendCurve (asset path; empty clears), Bidirectional,
	 * bAutomaticRuleBasedOnSequencePlayerInState, AutomaticRuleTriggerTime, and
	 * BlendProfile ({"path":"...", "mode":"TimeFactor|WeightFactor"}; empty path clears)
	 * @param TransitionExportName Optional exact transition UObject name; wins over the state pair
	 * @param bPreviewOnly Validate and resolve without modifying the asset
	 * @return JSON with success, transition_export_name, modified/would_modify properties, compiled=false
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|AnimBP")
	static FString SetAnimTransitionRule(
		const FString& BlueprintPath,
		const FString& StateMachineName,
		const FString& FromState,
		const FString& ToState,
		const FString& PropertiesJson,
		const FString& TransitionExportName = TEXT(""),
		bool bPreviewOnly = false
	);

	// =========================================================
	// Material Remote Control Functions (Issue #4346)
	// =========================================================

	/**
	 * Get HLSL shader code for a Material (Issue #4346 P0).
	 *
	 * Extracts the generated HLSL shader code from a compiled Material.
	 * Requires the Material to be compiled.
	 *
	 * @param MaterialPath Material asset path (e.g., "/Game/Materials/M_Character")
	 * @return JSON string with HLSL code or error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString GetMaterialHLSL(const FString& MaterialPath);

	/**
	 * Get the live material node graph for the "feature catalog" (Issue #16324, Epic #16323).
	 *
	 * Reads the loaded UMaterial's expressions directly to fill the data gaps that binary
	 * .uasset metadata cannot: live node names (GetName), DefaultValue / ParameterName per
	 * node, and name-based edges (from_node/to_node are node names, not GUIDs).
	 *
	 * @param MaterialPath Material asset path (e.g., "/Game/Materials/M_Character")
	 * @return JSON string: { success, material_name, material_path, node_count,
	 *         connection_count, nodes[{name,type,parameter_name?,default_value?,...}],
	 *         connections[{from_node,from_pin,to_node,to_pin}] } or error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString GetMaterialGraph(const FString& MaterialPath);

	/**
	 * Set a parameter value on a MaterialInstance (Issue #4346 P1).
	 *
	 * Supports scalar, vector, and texture parameters.
	 *
	 * @param MaterialPath MaterialInstance asset path
	 * @param ParameterName Name of the parameter to set
	 * @param ParameterValue Value as JSON string
	 * @param ParameterType Type hint: "scalar", "vector", or "texture"
	 * @return JSON with success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString SetMaterialParameter(
		const FString& MaterialPath,
		const FString& ParameterName,
		const FString& ParameterValue,
		const FString& ParameterType
	);

	/**
	 * Create a new MaterialInstanceConstant (Issue #4346 P1).
	 *
	 * @param InstanceName Name for the new instance
	 * @param ParentMaterial Parent Material asset path
	 * @param PackagePath Package path to create the instance in
	 * @return JSON with created instance info or error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString CreateMaterialInstance(
		const FString& InstanceName,
		const FString& ParentMaterial,
		const FString& PackagePath
	);

	/**
	 * Create a new Material asset (Issue #4346 P2, #4425).
	 *
	 * @param MaterialName Name for the new Material
	 * @param PackagePath Package path to create the Material in
	 * @param ShadingModel Shading model (DefaultLit, Unlit, Subsurface, ClearCoat, etc.). Default: "DefaultLit"
	 * @param BlendMode Blend mode (Opaque, Masked, Translucent, Additive, Modulate). Default: "Opaque"
	 * @return JSON with created Material info or error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString CreateMaterial(
		const FString& MaterialName,
		const FString& PackagePath,
		const FString& ShadingModel = TEXT("DefaultLit"),
		const FString& BlendMode = TEXT("Opaque")
	);

	/**
	 * Add a MaterialExpression node to a Material (Issue #4346 P2).
	 *
	 * @param MaterialPath Material OR MaterialFunction asset path. Issue #17066: when the path
	 *               resolves to a UMaterialFunction, the node is inserted into the MF's internal
	 *               graph (previously rejected with "Material not found").
	 * @param NodeType Expression class name (e.g., "MaterialExpressionTextureSample"). For Lerp,
	 *               pass "LinearInterpolate" (the engine class is UMaterialExpressionLinearInterpolate).
	 * @param NodeName Optional name for the node
	 * @param LocationJson JSON with X/Y position
	 * @return JSON with created node info (incl. owner_type) or error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString AddMaterialNode(
		const FString& MaterialPath,
		const FString& NodeType,
		const FString& NodeName,
		const FString& LocationJson
	);

	/**
	 * Modify MaterialExpression node properties (Issue #4346 P3).
	 *
	 * @param MaterialPath Material OR MaterialFunction asset path (Issue #17066). For an MF the
	 *               response also carries referencing_asset_count (warn-only blast-radius — modify
	 *               is non-structural and is never refused; there is no force gate on modify).
	 * @param NodeName Name of the node to modify
	 * @param PropertiesJson JSON with properties to set
	 * @return JSON with success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString ModifyMaterialNode(
		const FString& MaterialPath,
		const FString& NodeName,
		const FString& PropertiesJson
	);

	/**
	 * Set a Material-level property (Issue #8159).
	 *
	 * Supports: bUseMaterialAttributes, MaterialDomain, BlendMode,
	 *           ShadingModel, bTwoSided, OpacityMaskClipValue, bAllowNegativeEmissiveColor
	 *
	 * @param MaterialPath Material asset path
	 * @param PropertyName Property to set
	 * @param PropertyValue Value as string (booleans: "true"/"false", enums: name string, floats: numeric string)
	 * @return JSON with success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString SetMaterialProperty(
		const FString& MaterialPath,
		const FString& PropertyName,
		const FString& PropertyValue
	);

	/**
	 * Connect two MaterialExpression nodes (Issue #4346 P3).
	 *
	 * @param MaterialPath Material OR MaterialFunction asset path (Issue #17066).
	 * @param SourceNode Source node name
	 * @param TargetNode Target node name, "Material" for the material main node, or — for a
	 *               MaterialFunction (which has no main node) — empty/"output" to target the MF's
	 *               single FunctionOutput node (Issue #17066; refused if there are 0 or >1 outputs).
	 * @param SourcePin Source output pin name
	 * @param TargetPin Target input pin name
	 * @return JSON with success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString ConnectMaterialNodes(
		const FString& MaterialPath,
		const FString& SourceNode,
		const FString& TargetNode,
		const FString& SourcePin,
		const FString& TargetPin
	);

	/**
	 * Disconnect MaterialExpression nodes (Issue #4346 P3).
	 *
	 * @param MaterialPath Material asset path
	 * @param SourceNode Source node name
	 * @param TargetNode Target node name
	 * @return JSON with success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString DisconnectMaterialNodes(
		const FString& MaterialPath,
		const FString& SourceNode,
		const FString& TargetNode
	);

	/**
	 * Delete a MaterialExpression node (Issue #4346 P3).
	 *
	 * @param MaterialPath Material OR MaterialFunction asset path (Issue #17066).
	 * @param NodeName Name of the node to delete (exact match preferred, Issue #14979)
	 * @param bForce When false (default), refuse to delete a node that still has inbound
	 *               connections. When true, sever those inbound connections first and
	 *               recompile once via FMaterialUpdateContext (Issue #14979).
	 *               Issue #17066: for a UMaterialFunction the gate is widened to cross-asset
	 *               scope — without force, delete is refused when the node has inbound
	 *               connections OR the MF is referenced by other assets; with force the MF path
	 *               severs inbound + RemoveExpression only (FMaterialUpdateContext is Material-only
	 *               and not applied to MFs — PostEditChange drives the consumer recompile).
	 * @return JSON with success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString DeleteMaterialNode(
		const FString& MaterialPath,
		const FString& NodeName,
		bool bForce = false
	);

	/**
	 * Replace a texture reference in a Material (Issue #4346 P3).
	 *
	 * @param MaterialPath Material asset path
	 * @param OldTexture Path of texture to replace
	 * @param NewTexture Path of replacement texture
	 * @return JSON with success status and count of replacements
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString ReplaceMaterialTexture(
		const FString& MaterialPath,
		const FString& OldTexture,
		const FString& NewTexture
	);

	// =========================================================
	// Material Function Support (Issue #7921 — ADK Phase 4)
	// =========================================================

	/**
	 * Create a new Material Function asset.
	 *
	 * Uses UMaterialFunctionFactoryNew to create an empty UMaterialFunction asset
	 * at the specified path. Use AddFunctionInputPin / AddFunctionOutputPin to
	 * populate input/output pins after creation.
	 *
	 * @param MFName Material Function asset name (e.g., "MF_BlendByHeight")
	 * @param PackagePath Output package path (default: "/Game/MaterialFunctions")
	 * @return JSON with success status, mf_name, mf_path
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString CreateMaterialFunction(
		const FString& MFName,
		const FString& PackagePath
	);

	/**
	 * Add a FunctionInput pin to a Material Function.
	 *
	 * @param MFPath Material Function asset path (e.g., /Game/MaterialFunctions/MF_MyFunc)
	 * @param PinName Name for the input (e.g., "InColor")
	 * @param InputType Input type enum name (e.g., "FunctionInput_Vector3", "FunctionInput_Scalar"). Default: FunctionInput_Vector3
	 * @param LocationJson JSON with X/Y position
	 * @return JSON with success status, node_name, input_type
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString AddFunctionInputPin(
		const FString& MFPath,
		const FString& PinName,
		const FString& InputType,
		const FString& LocationJson
	);

	/**
	 * Add a FunctionOutput pin to a Material Function.
	 *
	 * @param MFPath Material Function asset path
	 * @param PinName Name for the output (e.g., "OutColor")
	 * @param LocationJson JSON with X/Y position
	 * @return JSON with success status, node_name
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString AddFunctionOutputPin(
		const FString& MFPath,
		const FString& PinName,
		const FString& LocationJson
	);

	/**
	 * Add a MaterialFunctionCall node to a Material that references a Material Function.
	 *
	 * The call node automatically synchronizes input/output pins with the referenced
	 * Material Function via SetMaterialFunction().
	 *
	 * @param MaterialPath Target Material asset path
	 * @param MFAssetPath Material Function asset path to reference
	 * @param LocationJson JSON with X/Y position
	 * @return JSON with success status, node_name, function_path, input_count, output_count
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Material")
	static FString AddMaterialFunctionCall(
		const FString& MaterialPath,
		const FString& MFAssetPath,
		const FString& LocationJson
	);

	// =========================================================
	// Enhanced Input Remote Control Functions (Issue #4262)
	// =========================================================

	/**
	 * Create a new InputAction asset (Issue #4262).
	 *
	 * Creates a UInputAction at /Game/Input/Actions/<ActionName>.
	 *
	 * @param ActionName Name for the new InputAction (e.g., "IA_Attack")
	 * @param ValueType Value type: "Digital", "Axis1D", "Axis2D", "Axis3D"
	 * @param Triggers Comma-separated trigger names (e.g., "Hold,Tap"). Optional. Supported: Pressed, Released, Down, Hold, HoldAndRelease, Tap
	 * @param Modifiers Comma-separated modifier names (e.g., "Negate,SwizzleAxis"). Optional.
	 * @return JSON string with asset path, value_type, success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Gameplay")
	static FString CreateInputAction(
		const FString& ActionName,
		const FString& ValueType,
		const FString& Triggers = TEXT(""),
		const FString& Modifiers = TEXT("")
	);

	/**
	 * Add a key mapping to an InputMappingContext (Issue #4262).
	 *
	 * Finds the specified InputAction and InputMappingContext,
	 * then adds a new key mapping entry.
	 *
	 * @param InputAction InputAction asset name or path
	 * @param Key Physical key name (e.g., "LeftMouseButton", "SpaceBar")
	 * @param MappingContext InputMappingContext asset name or path
	 * @param Modifiers Comma-separated modifier names (e.g., "Negate,SwizzleAxis"). Optional. Supported: Negate, SwizzleAxis, DeadZone, Scalar, FOVScaling, ToWorldSpace, Smooth, ResponseCurve
	 * @param Triggers Comma-separated trigger names (e.g., "Pressed,Hold"). Optional. Supported: Pressed, Released, Down, Hold, HoldAndRelease, Tap
	 * @return JSON string with success status, mapping details
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Gameplay")
	static FString CreateInputMapping(
		const FString& InputAction,
		const FString& Key,
		const FString& MappingContext,
		const FString& Modifiers = TEXT(""),
		const FString& Triggers = TEXT("")
	);

	// =========================================================
	// Blueprint T3D Import (Issue #4741 - Blueprint Fast Generation)
	// =========================================================

	/**
	 * Import Blueprint nodes from T3D text format (Issue #4741).
	 *
	 * Uses FEdGraphUtilities::ImportNodesFromText() to bulk-import nodes
	 * from Unreal's native T3D clipboard format. This enables importing
	 * entire node graphs in a single call, supporting ALL node types.
	 *
	 * The T3D format is the same text format used by Blueprint Editor
	 * copy/paste operations and BlueprintUE.com.
	 *
	 * @param BlueprintPath Blueprint asset path (e.g., "/Game/Blueprints/BP_Test.BP_Test")
	 * @param GraphName Target graph name (default: "EventGraph")
	 * @param T3DText T3D text containing node definitions (Begin Object ... End Object)
	 * @return JSON string with imported node info (count, node_guids) or error
	 *
	 * Example T3D text:
	 *   Begin Object Class=/Script/BlueprintGraph.K2Node_CallFunction Name="K2Node_CallFunction_0"
	 *     FunctionReference=(MemberParent=Class'"/Script/Engine.KismetSystemLibrary"',MemberName="PrintString")
	 *     NodePosX=256
	 *     NodePosY=0
	 *   End Object
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString ImportBlueprintNodesFromText(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& T3DText
	);

	/**
	 * Export Blueprint nodes to T3D text format (Issue #4741).
	 *
	 * Uses FEdGraphUtilities::ExportNodesToText() to export all nodes
	 * from a Blueprint graph to T3D clipboard text format.
	 *
	 * @param BlueprintPath Blueprint asset path
	 * @param GraphName Target graph name (default: "EventGraph")
	 * @return JSON string with t3d_text field containing the exported text, or error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString ExportBlueprintNodesToText(
		const FString& BlueprintPath,
		const FString& GraphName
	);

	/**
	 * Create a new Blueprint asset at the given package path (Issue #12696).
	 *
	 * Idempotent — if the asset already exists, returns success with
	 * `already_exists=true` and `created=false`. On creation, uses
	 * `FKismetEditorUtilities::CreateBlueprint` for proper BP initialization
	 * (skeleton class, generated class) and saves the package to disk via
	 * `UPackage::SavePackage`, matching the project-wide save pattern used
	 * by `CreateLevelSequence` at CURemoteLibrary.cpp:2150 (function start;
	 * the mirrored MarkPackageDirty + AssetCreated + SavePackage tail is at
	 * lines 2225-2240).
	 *
	 * Primary use: editor-e2e test harness auto-creating the
	 * `/Game/Test/BP_E2ETest` fixture that the C4-C7 T3D import/export
	 * tests target. `home-pc-aifarm02` runner's `C:/Lyra` project is not
	 * git-tracked, so the fixture must be authored at runtime.
	 *
	 * @param PackagePath Long package path of the parent folder (e.g. "/Game/Test")
	 * @param AssetName   Asset name without extension (e.g. "BP_E2ETest")
	 * @param ParentClassName Parent C++ class short name (e.g. "Actor"). Falls
	 *                        back to AActor when the name cannot be resolved.
	 * @return JSON with {success, created, already_exists, saved_to_disk, path,
	 *         parent_class} on success, or {success=false, error, ...} on failure.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString CreateBlueprintAsset(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& ParentClassName
	);

	/**
	 * Read-only, dialog-free preflight for Blueprint asset creation (Issue #28199).
	 *
	 * Mirrors the public, non-mutating checks that guard AssetTools creation and adds an
	 * authoritative loaded-package/object probe. It deliberately does not call
	 * UAssetToolsImpl::CanCreateAsset (private), CreatePackage, StaticLoadObject, or
	 * HandleFullyLoadingPackages because those paths can mutate state or show a modal.
	 * Source-control status is queried by filename, but no revert, checkout, or other
	 * source-control write operation is executed.
	 *
	 * @param PackagePath Long package folder (for example, "/Game/Test").
	 * @param AssetName Asset name without an extension.
	 * @param ParentClass Resolved parent class. May be null only for the dedicated
	 *                    BlueprintInterfaceFactory path.
	 * @param bUsesDedicatedInterfaceFactory True only for Blueprint Interface creation.
	 * @return Structured JSON with success/error and data.can_create/reason_code/runtime state.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString PreflightBlueprintAssetCreate(
		const FString& PackagePath,
		const FString& AssetName,
		UClass* ParentClass,
		bool bUsesDedicatedInterfaceFactory
	);

	/**
	 * Get live Blueprint graph data (nodes, pins, connections) from Editor memory (Issue #4828).
	 * Returns full node/pin/connection data for dynamically-created Blueprints.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString GetBlueprintGraphData(
		const FString& BlueprintPath,
		const FString& GraphName
	);

	/**
	 * Enumerate every graph in a Blueprint (Issue #28275).
	 *
	 * GetBlueprintGraphData needs a GraphName, but nothing told the caller which
	 * names were valid — omitting it silently fell back to "EventGraph", which on
	 * an AnimBP reads as "this Blueprint has only an EventGraph". This op is the
	 * missing directory.
	 *
	 * Walks UBlueprint::GetAllGraphs, which recurses SubGraphs via
	 * UEdGraph::GetAllChildrenGraphs — so AnimGraph, its State Machine graphs, and
	 * the per-state / per-transition graphs beneath them are all included. Each
	 * entry carries the UE class name so callers can filter by kind.
	 *
	 * Returns JSON: {"success":true, "blueprint_path":"...", "graph_count":N,
	 *   "graphs":[{"name","graph_class","node_count","outer_graph"}],
	 *   "truncated":bool, "returned_count":N}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString GetBlueprintGraphs(
		const FString& BlueprintPath
	);

	/**
	 * Bulk-update node positions in a Blueprint graph (Issue #6388).
	 * Accepts a JSON array of {node_guid, pos_x, pos_y} entries.
	 * Returns JSON: {"success":true/false, "updated_count":N, "not_found":[], "total_requested":N}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString SetNodePositions(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& PositionsJson
	);

	/**
	 * Add a function graph to a Blueprint (Issue #7703).
	 * Works with regular Blueprints AND Blueprint Interfaces (BPI_*).
	 * Uses FBlueprintEditorUtils::AddFunctionGraph.
	 * Returns JSON: {"success":true/false, "error":"...", "function_name":"...", "blueprint_type":"..."}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString AddBlueprintFunction(
		const FString& BlueprintPath,
		const FString& FunctionName,
		bool bIsPure
	);

	/**
	 * Add a component to a Blueprint (Issue #7704).
	 * Uses SubobjectDataSubsystem to add default subobject.
	 * Returns JSON: {"success":true/false, "error":"...", "component_name":"...", "component_class":"..."}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString AddBlueprintComponent(
		const FString& BlueprintPath,
		const FString& ComponentClassName,
		const FString& ComponentName
	);

	/**
	 * Set a pin's default value on a Blueprint node (Issue #7704).
	 * Finds a node by name in the Blueprint's graphs and sets the named pin's DefaultValue.
	 * Uses UEdGraphSchema_K2::TrySetDefaultValue.
	 * Returns JSON: {"success":true/false, "error":"...", "node_name":"...", "pin_name":"...", "value":"..."}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString SetBlueprintPinValue(
		const FString& BlueprintPath,
		const FString& NodeName,
		const FString& PinName,
		const FString& PinValue
	);

	/**
	 * Rename a Blueprint member variable via FBlueprintEditorUtils (Issue #4828).
	 * Returns JSON: {"success":true/false, "error":"...", "old_name":"...", "new_name":"..."}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString RenameBlueprintVariable(
		const FString& BlueprintPath,
		const FString& OldName,
		const FString& NewName
	);

	/**
	 * Rename a Blueprint function graph via FBlueprintEditorUtils::RenameGraph (Issue #14803).
	 * Renames the function graph + its entry/result node member references, then fixes up
	 * UK2Node_CallFunction call sites within the same Blueprint and recompiles.
	 * Returns JSON: {"success":true/false, "error":"...", "old_name":"...", "new_name":"...",
	 *                "blueprint_path":"...", "callsites_fixed":N}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString RenameBlueprintFunction(
		const FString& BlueprintPath,
		const FString& OldName,
		const FString& NewName
	);

	/**
	 * Introduce a Blueprint member variable from a node's output pin (Issue #14803).
	 * Reads the source node's first non-exec output pin type (or NodeName="Node.Pin" to
	 * disambiguate), creates a member variable of that exact FEdGraphPinType, and — when
	 * bCreateSetNode is true — spawns a UK2Node_VariableSet wired to the source output pin.
	 * Mirrors the editor's "Promote to Variable" (FBlueprintEditor::DoPromoteToVariable2f).
	 * Returns JSON: {"success":true/false, "error":"...", "variable_name":"...",
	 *                "variable_type":"...", "variable_guid":"...", "source_node":"...", "wired":true/false}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString IntroduceVariableFromNode(
		const FString& BlueprintPath,
		const FString& NodeName,
		const FString& VariableName,
		bool bCreateSetNode
	);

	/**
	 * Set default value of a Blueprint variable (Issue #4828).
	 * Uses FProperty::ImportText to parse string value into correct type.
	 * Returns JSON: {"success":true/false, "error":"...", "variable":"...", "value":"..."}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString SetBlueprintVariableDefault(
		const FString& BlueprintPath,
		const FString& VariableName,
		const FString& DefaultValue
	);

	/**
	 * Set default value of a Blueprint SCS component property (Issue #9060).
	 * Supports dot-path notation: "ComponentName.PropertyName" or nested
	 * "ComponentName.Struct.Field" (e.g. "Mesh1.RelativeLocation",
	 * "StaticMesh.BodyInstance.bGenerateOverlapEvents").
	 *
	 * Resolution order:
	 *   1. Walk Blueprint->SimpleConstructionScript->GetAllNodes() for own SCS component
	 *   2. Walk parent BP chain via Blueprint->ParentClass->ClassGeneratedBy for inherited SCS
	 *      (in this case target is the child CDO's component instance = archetype override)
	 *   3. CDO fallback: if first segment is not an SCS component name, treat the entire
	 *      path as a CDO property path
	 *
	 * Persists via MarkPackageDirty(). Does NOT auto-compile — caller invokes compile_blueprint.
	 * Returns JSON: {"success":true/false, "error":"...", "component":"...", "property":"...",
	 *                "value":"...", "previous_value":"...", "property_type":"...", "source":"own_scs|parent_scs|cdo"}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString SetComponentDefault(
		const FString& BlueprintPath,
		const FString& PropertyPath,
		const FString& DefaultValue
	);

	/**
	 * Append a modifier to a GameplayEffect's CDO (Issue #16552).
	 *
	 * Runs in C++ because FGameplayModifierInfo's fields (ModifierOp, Attribute, ModifierMagnitude)
	 * are all EditDefaultsOnly (CPF_DisableEditOnInstance) and therefore cannot be set from UE Python
	 * on a struct instance (set_editor_property raises "cannot be edited on instances"; direct attr /
	 * kwarg-ctor are unsupported). `Op` is the stable integer EGameplayModOp value
	 * (AddBase=0, MultiplyAdditive=1, DivideAdditive=2, Override=3); EGameplayModOp is a namespace enum
	 * not exposed to Python as `unreal.EGameplayModOp`. Attribute ("SetName.AttrName" or "AttrName") is
	 * best-effort and non-fatal.
	 *
	 * Returns JSON: {"success":true/false, "error":"...", "effect_asset":"...", "op":N,
	 *                "magnitude":F, "attribute":"...", "modifier_count":N, "saved":true/false}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Gameplay")
	static FString AddGameplayEffectModifier(
		const FString& EffectAsset,
		const FString& Attribute,
		int32 Op,
		float Magnitude
	);

	/**
	 * Write gameplay-tag containers onto a UGameplayAbility Blueprint's CDO (Issue #17689 step 2).
	 *
	 * Runs in C++ because UE 5.8 Python cannot build an FGameplayTag from a string
	 * (PyConversion has no str->struct ImportText fallback; PyWrapperStruct treats a str as a
	 * sequence; FGameplayTag::RequestGameplayTag / SetAssetTags are not UFUNCTIONs). Each tag string
	 * is resolved via FGameplayTag::RequestGameplayTag(name, ErrorIfNotFound=false); tags absent from
	 * the project registry are surfaced in `unresolved_tags` and NEVER applied as empty tags. The
	 * `AbilityTags` (AssetTags) and `ActivationOwnedTags` FGameplayTagContainer properties are set on
	 * the CDO via reflection — the same effect as Python set_editor_property, which the Python tier
	 * cannot construct the value for.
	 *
	 * Returns JSON: {"success":bool, "error":"...", "ability_asset":"...",
	 *   "ability_tags_applied":N, "activation_owned_tags_applied":N,
	 *   "unresolved_tags":[...], "saved":bool}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Gameplay")
	static FString SetGameplayAbilityCdoTags(
		const FString& AbilityAsset,
		const TArray<FString>& AbilityTags,
		const TArray<FString>& ActivationOwnedTags
	);

	/**
	 * Write the single trigger FGameplayTag onto a GameplayCueNotify Blueprint's CDO (Issue #17689 step 2).
	 *
	 * Same C++-only rationale as SetGameplayAbilityCdoTags. Sets the `GameplayCueTag` (single
	 * FGameplayTag) property and mirrors `GameplayCueName` (FName, AssetRegistrySearchable) via
	 * reflection. A tag absent from the registry yields success:false — the single thing this op
	 * must do could not be done (honest failure, never a silent empty tag).
	 *
	 * Returns JSON: {"success":bool, "error":"...", "cue_asset":"...", "gameplay_cue_tag":"...", "saved":bool}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Gameplay")
	static FString SetGameplayCueNotifyCdoTag(
		const FString& CueAsset,
		const FString& GameplayCueTag
	);

	/**
	 * Get default value of a Blueprint SCS component property (Issue #9060).
	 * Live Editor read — mirrors SetComponentDefault resolution logic.
	 * Used as fallback when offline metadata doesn't contain nested struct paths.
	 * Returns JSON: {"success":true/false, "error":"...", "component":"...", "property":"...",
	 *                "value":"...", "property_type":"...", "source":"own_scs|parent_scs|cdo"}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString GetComponentDefault(
		const FString& BlueprintPath,
		const FString& PropertyPath
	);

	/**
	 * Set a node-level UPROPERTY on a K2/EdGraph node in a Blueprint graph (Issue #7718).
	 *
	 * Replaces the old `set_node_property` stub (which returned not_implemented). A slash-prefixed
	 * package path is loaded exactly and never redirects; only a bare name is resolved through
	 * Asset Registry when it has exactly one all-mount match. Ambiguous names fail before mutation and
	 * return every matching package path. Finds the node by
	 * NodeGuid (preferred) or a name-contains fallback across all the Blueprint's graphs, then sets
	 * the named node property via reflection (FindPropertyByName + ImportText_Direct) — e.g.
	 * `NodeComment`, `bCommentBubbleVisible`, `NodePosX/Y`, or a K2Node flag like `bIsPureFunc`.
	 * Pin/struct-affecting changes are followed by ReconstructNode(). Distinct from `set_pin_value`
	 * (pin defaults) and `set_variable_default` (CDO variables).
	 *
	 * Returns JSON: {"success":true/false, "node":"...", "node_guid":"...", "property":"...",
	 *                "value":"...", "previous_value":"...", "property_type":"...", "error":"...",
	 *                "available_nodes":"...", "available_properties":"...",
	 *                "resolved_blueprint_path":"...", "candidate_paths":["..."]}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString SetBlueprintNodeProperty(
		const FString& BlueprintPath,
		const FString& NodeIdentifier,
		const FString& PropertyName,
		const FString& PropertyValue
	);

	/**
	 * Set any UObject property by full path + property name + ExportText value (Issue #8564).
	 * Universal property setter using FProperty::ImportText for type-safe reverse serialization.
	 * Works on any loaded UObject including nested SM Blueprint nodes, level actors, components.
	 * Array properties (Issue #28284): trimmed-empty / "()" ExportText is rejected unless
	 * bAllowClear is true. A successful ImportText that applies fewer top-level elements
	 * than the ExportText item count is success:false; previous value is restored.
	 * Array JSON extras: requested_count, applied_count. Scalar/struct JSON is unchanged.
	 * Returns JSON: {"success":true, "previous_value":"...", "new_value":"...", "property_type":"..."}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString SetObjectPropertyByPath(
		const FString& ObjectPath,
		const FString& PropertyName,
		const FString& ExportTextValue,
		bool bAllowClear = false
	);

	/**
	 * Create an Instanced subobject and append it to a UObject's array property (Issue #28227).
	 *
	 * The engine's own adders for this shape are C++ templates -- UGameplayEffect::AddComponent
	 * and FindOrAddComponent (GameplayEffect.h:2207/:2215) are not UFUNCTIONs, so no reflection
	 * or Python caller can reach them. ImportText is not a substitute either: it resolves
	 * references and silently drops the ones it cannot resolve (Issue #28227 Step 0, and see
	 * Issue #28284). This reproduces the engine's creation idiom instead:
	 * `NewObject<T>(Owner, Class, NAME_None, Owner->GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional)`.
	 *
	 * Type-agnostic by construction: it takes the element type from the array property itself,
	 * so all 11 GameplayEffectComponents types -- and any non-GAS class with an Instanced object
	 * array -- travel the same code path. Rejects non-array, non-object-array, non-Instanced,
	 * wrong-base, abstract and deprecated targets, and any class whose UCLASS(Within=X)
	 * the owner does not satisfy -- the engine only ENSUREs on that one
	 * (UObjectGlobals.cpp:3321) and constructs the object regardless, so left unguarded it
	 * returns success:true for a subobject the engine itself considers illegally outered.
	 *
	 * Returns JSON: {"success":true, "subobject":"...", "index":N, "count_before":N,
	 *                "count_after":N, "child_flags":N, "owner_masked_flags":N,
	 *                "rf_propagate_to_subobjects":N, "rf_transactional":N}
	 * `child_flags` is read back AFTER construction; GetMaskedFlags intersects with the owner's
	 * flags, so callers must assert the relation rather than hardcode RF_Public.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|RemoteControl")
	static FString AddInstancedSubobjectByPath(
		const FString& ObjectPath,
		const FString& ArrayPropertyName,
		const FString& SubobjectClassPath
	);

	/**
	 * Compile a Blueprint via HTTP Remote Control (Issue #4751).
	 * Uses FKismetEditorUtilities::CompileBlueprint. Replaces WS compile_blueprints for reliability.
	 * Returns JSON: {"success":true/false, "error_count":N, "errors":[], "compiled_path":"...", "status":"UpToDate|Error"}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString CompileBlueprint(
		const FString& BlueprintPath
	);

	/**
	 * Get all variables from a Blueprint for live search (Issue #4751).
	 * Returns JSON: {"success":true, "variables":[{name, type, guid, default_value, ...}], "variable_count":N}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString GetBlueprintVariables(
		const FString& BlueprintPath
	);

	// =========================================================
	// Blueprint Breakpoint Remote Control Functions (Issue #4758)
	// =========================================================

	/**
	 * Set a breakpoint on a Blueprint graph node (Issue #4758).
	 *
	 * Creates a breakpoint on the specified node. If a breakpoint already exists,
	 * it will be enabled.
	 *
	 * @param BlueprintPath Blueprint asset path (e.g., "/Game/Blueprints/BP_Test.BP_Test")
	 * @param NodeGuidStr Node GUID string (e.g., "12345678-1234-1234-1234-123456789012")
	 * @return JSON with success status, node info, and breakpoint state
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString SetBreakpoint(
		const FString& BlueprintPath,
		const FString& NodeGuidStr
	);

	/**
	 * Clear (remove) a breakpoint from a Blueprint graph node (Issue #4758).
	 *
	 * @param BlueprintPath Blueprint asset path
	 * @param NodeGuidStr Node GUID string
	 * @return JSON with success status
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString ClearBreakpoint(
		const FString& BlueprintPath,
		const FString& NodeGuidStr
	);

	/**
	 * Enable or disable a breakpoint on a Blueprint graph node (Issue #4758).
	 *
	 * @param BlueprintPath Blueprint asset path
	 * @param NodeGuidStr Node GUID string
	 * @param bEnabled True to enable, False to disable
	 * @return JSON with success status and new enabled state
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString EnableBreakpoint(
		const FString& BlueprintPath,
		const FString& NodeGuidStr,
		bool bEnabled
	);

	/**
	 * List all breakpoints in a Blueprint (Issue #4758).
	 *
	 * @param BlueprintPath Blueprint asset path
	 * @return JSON array of breakpoint objects (node_guid, node_name, node_class, enabled)
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString ListBreakpoints(
		const FString& BlueprintPath
	);

	// =========================================================
	// Variable Write Remote Control Functions (Issue #4848)
	// =========================================================

	/**
	 * Set a GAS (Gameplay Ability System) attribute value on an actor (Issue #4848).
	 *
	 * Finds the actor in PIE world (fallback: editor world), locates its
	 * UAbilitySystemComponent, and calls SetNumericAttributeBase().
	 *
	 * @param ActorPath Actor path, name, or label
	 * @param AttributeName GAS attribute name (e.g., "Health", "Mana", "MaxHealth")
	 * @param NewValue New numeric value for the attribute
	 * @return JSON: {success, old_value, new_value, attribute_name}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString SetGASAttribute(
		const FString& ActorPath,
		const FString& AttributeName,
		float NewValue
	);

	// =========================================================
	// Log Stream Remote Control Functions (Issue #4759)
	// =========================================================

	/**
	 * Start capturing UE log output (Issue #4759).
	 *
	 * Registers a custom FOutputDevice to capture log messages.
	 * Only one capture session can be active at a time.
	 *
	 * @param Categories Comma-separated log category filter (empty = all). E.g., "LogTemp,LogBlueprintUserMessages"
	 * @param MinSeverity Minimum severity: "Log", "Warning", "Error". Default: "Log"
	 * @return JSON with success status and session info
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString StartLogCapture(
		const FString& Categories,
		const FString& MinSeverity
	);

	/**
	 * Get captured log entries since a given index (Issue #4759).
	 *
	 * @param SinceIndex Return entries after this index (0 for all)
	 * @return JSON with log entries array and next_index
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString GetCapturedLogs(int32 SinceIndex);

	/**
	 * Stop log capture and clean up (Issue #4759).
	 *
	 * @return JSON with success status and total entries captured
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString StopLogCapture();

	// =========================================================
	// PIE & Debug State Functions (Issue #4757, #4758)
	// =========================================================

	/**
	 * Get current PIE (Play In Editor) state (Issue #4757).
	 *
	 * @return JSON with is_pie_running and is_simulating flags
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString GetPIEState();

	/**
	 * Get current Blueprint debug state — best effort (Issue #4758).
	 *
	 * Returns PIE status and whether execution is paused at a breakpoint.
	 * When paused, attempts to extract current Blueprint/node info from debug stack.
	 *
	 * @return JSON with is_pie_running, is_paused, and optional breakpoint context
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString GetDebugState();

	// =========================================================
	// Memory Measurement Functions (Issue #4829)
	// =========================================================

	/**
	 * Get per-class memory statistics using GetResourceSizeEx (Issue #4829).
	 *
	 * Iterates all actors in the active world (PIE or Editor), calls
	 * GetResourceSizeEx on each to measure actual byte usage.
	 *
	 * @param MaxClasses Maximum number of top classes to return (default 20)
	 * @return JSON with actor_count, total_exclusive_bytes, class_distribution, top_classes
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString GetMemoryStats(int32 MaxClasses);

	/**
	 * Get class-specific instance memory using GetResourceSizeEx (Issue #4829).
	 *
	 * Filters actors by class name, measures per-instance memory.
	 *
	 * @param ClassName Target class name (exact match, then inheritance fallback)
	 * @param MaxInstances Maximum instances to return details for (default 50)
	 * @return JSON with class_name, instance_count, total_exclusive_bytes, instances with exclusive_bytes
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString GetClassMemoryUsage(const FString& ClassName, int32 MaxInstances);

	/**
	 * Get compiler results from the Blueprint message log (Issue #5291 Phase 1A).
	 *
	 * Accesses FCompilerResultsLog data from the live Editor to retrieve
	 * tokenized compilation messages with severity, tokens, and annotated nodes.
	 *
	 * @param BlueprintPath Asset path of the Blueprint to query (e.g., "/Game/Blueprints/BP_Player.BP_Player").
	 *                      If empty, returns the global compiler log ("BlueprintLog").
	 * @return JSON string with compiler results: messages array with severity, tokens, annotated_nodes, counts.
	 *
	 * Example (Python via RC):
	 *   import unreal
	 *   result = unreal.CURemoteLibrary.get_compiler_results("/Game/Blueprints/BP_Player.BP_Player")
	 *   # Returns: {"success": true, "messages": [...], "num_errors": 2, "num_warnings": 1}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Errors")
	static FString GetCompilerResults(const FString& BlueprintPath);

	// =========================================================
	// FMessageLog Health Report Functions (Issue #5296)
	// =========================================================

	/**
	 * Query FMessageLog categories and messages via Remote Control (Issue #5296).
	 *
	 * When Category is empty: enumerates all known FMessageLog categories with message counts.
	 * When Category is provided: returns messages in that category with optional token decomposition.
	 *
	 * @param Category FMessageLog category name (e.g., "MapCheck", "DataValidation"). Empty = enumerate all.
	 * @param SeverityFilter Filter by severity: "Error", "Warning", "Info", "All".
	 * @param MaxMessages Maximum messages to return per category (capped at 500).
	 * @param IncludeTokens If true, decompose each message into typed tokens (Text, Fix, Asset, etc.).
	 * @return JSON string with categories summary or messages array.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Diagnostics")
	static FString GetMessageLog(
		const FString& Category,
		const FString& SeverityFilter,
		int64 MaxMessages,
		bool IncludeTokens
	);

	/**
	 * Execute a Fix token from an FTokenizedMessage (Issue #5296).
	 *
	 * Identifies the Fix token at the specified indices and either executes it or previews it (DryRun).
	 *
	 * @param Category FMessageLog category name (e.g., "MapCheck").
	 * @param MessageIndex 0-based message index within the category.
	 * @param FixTokenIndex 0-based fix token index within the message.
	 * @param DryRun If true, return fix description without executing.
	 * @return JSON string with fix result or dry-run description.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Diagnostics")
	static FString ExecuteMessageFix(
		const FString& Category,
		int64 MessageIndex,
		int64 FixTokenIndex,
		bool DryRun
	);

	// =========================================================
	// Blueprint Runtime Debug Functions (Issue #5293)
	// =========================================================

	/**
	 * Get runtime values of Blueprint watched pins (Issue #5293).
	 *
	 * Iterates FBlueprintWatchedPin entries for the specified Blueprint,
	 * resolves each via FKismetDebugUtilities::GetWatchText() which uses
	 * FPropertyInstanceInfo to format hierarchical property values.
	 *
	 * @param BlueprintPath Blueprint asset path (e.g., "/Game/Blueprints/BP_Player.BP_Player")
	 * @param MaxDepth Maximum property recursion depth (default 3, max 10)
	 * @return JSON: {success, watches: [{pin_name, value, type, node_name, node_guid}]}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString GetWatchedValues(
		const FString& BlueprintPath,
		int32 MaxDepth = 3
	);

	/**
	 * Get Blueprint execution trace buffer (Issue #5293).
	 *
	 * Reads FKismetTraceSample ring buffer from KismetDebugUtilities.
	 * Each sample: {Context(UObject), Function(UFunction), Offset, ObservationTime}.
	 * Returns most recent N trace entries.
	 *
	 * @param MaxEntries Maximum entries to return (default 50, max 500)
	 * @param BlueprintFilter Optional Blueprint path to filter trace entries
	 * @return JSON: {success, traces: [{object_name, function_name, blueprint_path, offset, timestamp_ms}]}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString GetExecutionTrace(
		int32 MaxEntries = 50,
		const FString& BlueprintFilter = TEXT("")
	);

	/**
	 * Control PIE execution stepping (Issue #5293).
	 *
	 * Wraps DebuggerCommands.h actions: Resume, Pause, StepInto, StepOver,
	 * StepOut, SingleFrameAdvance.
	 *
	 * @param Action Step action: "resume", "pause", "step_into", "step_over", "step_out", "frame_advance"
	 * @return JSON: {success, action, new_state}
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Debug")
	static FString StepExecution(const FString& Action);

	// ── Issue #7573: Blueprint function parameter manipulation (ECABridge parity) ──
	// K2Node_EditablePinBase::CreateUserDefinedPin/RemoveUserDefinedPinByName are not UFUNCTION,
	// so Python/RC cannot call them directly. These wrappers expose them.

	/**
	 * Add a typed parameter to a Blueprint function (Issue #7573).
	 *
	 * Wraps K2Node_EditablePinBase::CreateUserDefinedPin() which is not a UFUNCTION.
	 *
	 * @param BlueprintPath Asset path (e.g., "/Game/Blueprints/BP_Test")
	 * @param FunctionName Function graph name (e.g., "CalculateDamage")
	 * @param ParameterName New parameter name (e.g., "DamageMultiplier")
	 * @param PinCategory Pin type category: "bool", "int", "real", "string", "name", "text", "struct", "object"
	 * @param PinSubCategoryObject For struct/object types: full path (e.g., "/Script/CoreUObject.Vector"), empty for primitives
	 * @param bIsOutput True = output parameter (FunctionResult node), False = input (FunctionEntry node)
	 * @return Result string with SUCCESS marker or error description
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString AddBlueprintFunctionParameter(
		const FString& BlueprintPath,
		const FString& FunctionName,
		const FString& ParameterName,
		const FString& PinCategory,
		const FString& PinSubCategoryObject,
		bool bIsOutput
	);

	/**
	 * Remove a parameter from a Blueprint function by name (Issue #7573).
	 *
	 * Wraps K2Node_EditablePinBase::RemoveUserDefinedPinByName() which is not a UFUNCTION.
	 *
	 * @param BlueprintPath Asset path (e.g., "/Game/Blueprints/BP_Test")
	 * @param FunctionName Function graph name (e.g., "CalculateDamage")
	 * @param ParameterName Parameter to remove
	 * @return Result string with SUCCESS marker or error description
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString RemoveBlueprintFunctionParameter(
		const FString& BlueprintPath,
		const FString& FunctionName,
		const FString& ParameterName
	);

	/**
	 * Add a Blueprint Interface to a Blueprint's ImplementedInterfaces list.
	 * Issue #7719: ImplementedInterfaces is protected in Python — requires C++ bridge.
	 * Issue #21116: bPreviewOnly mirrors RemoveBlueprintInterface — run the shared
	 * #20521 resolver + already-implemented check with no mutation, returning a
	 * PREVIEW marker so dry_run previews predict real failures (not-found, //, etc.).
	 *
	 * @param BlueprintPath Full game path (e.g., "/Game/Blueprints/BP_Player")
	 * @param InterfaceName Interface asset path or short name (e.g., "BPI_Interactable")
	 * @param bPreviewOnly true = no mutation; resolver validation + PREVIEW marker
	 * @return Result string with [add_interface] SUCCESS/PREVIEW/error marker
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString AddBlueprintInterface(
		const FString& BlueprintPath,
		const FString& InterfaceName,
		bool bPreviewOnly = false
	);

	/**
	 * Remove a Blueprint Interface from a Blueprint's ImplementedInterfaces list.
	 * Issue #20522: symmetric CRUD to AddBlueprintInterface — shares the same
	 * interface resolver (#20521: /Script/ full path, /Game/ BPI asset, short name,
	 * double-slash rejection, CLASS_Interface validation).
	 *
	 * @param BlueprintPath Full game path (e.g., "/Game/Blueprints/BP_Player")
	 * @param InterfaceName Interface asset path or short name (e.g., "BPI_Interactable")
	 * @param bPreserveFunctions false (default, engine default) = the interface's
	 *        implemented function graphs AND its event nodes are deleted;
	 *        true = kept as normal functions / custom events (pin links preserved).
	 * @param bPreviewOnly true = no mutation; enumerate both deletion sets
	 *        (function graphs + interface event nodes) in a PREVIEW marker string.
	 * @return Result string with [remove_interface] SUCCESS/PREVIEW/error marker
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static FString RemoveBlueprintInterface(
		const FString& BlueprintPath,
		const FString& InterfaceName,
		bool bPreserveFunctions = false,
		bool bPreviewOnly = false
	);

	/**
	 * Enumerate a Blueprint's implemented interface class names (read-only).
	 * Issue #20522: UE 5.8 Python cannot read UBlueprint::ImplementedInterfaces —
	 * bp.get_editor_property('implemented_interfaces') raises "Failed to find
	 * property", so the Python-side enumeration silently returned [] in a live
	 * editor. Like the add/remove writes (#7719), the read needs the C++ bridge.
	 *
	 * @param BlueprintPath Full game path (e.g., "/Game/Blueprints/BP_Player")
	 * @return Generated-class names of implemented interfaces (e.g., "BPI_Interactable_C");
	 *         empty when the Blueprint is missing or implements none.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Blueprint")
	static TArray<FString> GetBlueprintImplementedInterfaces(
		const FString& BlueprintPath
	);

	/**
	 * Extract memory CSV from .utrace file (Issue #7879).
	 *
	 * Delegates to UCUInsightsMemoryCSVCommandlet::RunExtractionStatic.
	 * Exposed here because UCommandlet subclasses are not available in Python bindings.
	 *
	 * @param OutputDir   Directory for CSV output files (MemoryAllocs.csv, MemoryPools.csv, LLMCounters.csv)
	 * @param TraceFile   Path to .utrace file
	 * @param TimeRangeStart Optional start time in seconds (0.0 = from start)
	 * @param TimeRangeEnd   Optional end time in seconds (0.0 = to end)
	 * @param PoolFilter     Optional comma-separated pool filter (empty = all)
	 * @param MaxAllocations Maximum allocations to export (0 = unlimited)
	 * @return 0 on success, non-zero on error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Insights")
	static int32 ExtractMemoryCSV(
		const FString& OutputDir,
		const FString& TraceFile,
		double TimeRangeStart = 0.0,
		double TimeRangeEnd = 0.0,
		const FString& PoolFilter = TEXT(""),
		int32 MaxAllocations = 0
	);

	// =========================================================
	// AnimNotify Manipulation (Issue #8564 Phase K-2)
	// =========================================================

	/**
	 * Move an AnimNotify to a new trigger time.
	 *
	 * Works with AnimSequence, AnimMontage, and other UAnimSequenceBase subclasses.
	 *
	 * @param AssetPath AnimMontage or AnimSequence path (e.g., "/Game/Anim/AM_Dash.AM_Dash")
	 * @param NotifyIndex Zero-based index in the Notifies array
	 * @param NewTriggerTime New trigger time in seconds
	 * @return JSON with success, old_time, new_time
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Animation")
	static FString MoveAnimNotify(
		const FString& AssetPath,
		int32 NotifyIndex,
		float NewTriggerTime
	);

	/**
	 * Add an AnimNotify at a specified time.
	 *
	 * Creates a new FAnimNotifyEvent in the asset's Notifies array.
	 * Supports both instant notifies (Duration=0) and notify states (Duration>0).
	 *
	 * @param AssetPath AnimMontage or AnimSequence path
	 * @param NotifyClassName Notify class name (e.g., "AnimNotify_PlaySound", "AnimNotifyState_TimedNiagaraEffect")
	 * @param TriggerTime Trigger time in seconds
	 * @param Duration Duration in seconds (0 for instant notifies)
	 * @return JSON with success, notify_index, notify_name, trigger_time
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Animation")
	static FString AddAnimNotify(
		const FString& AssetPath,
		const FString& NotifyClassName,
		float TriggerTime,
		float Duration = 0.0f
	);

	/**
	 * Remove an AnimNotify by index.
	 *
	 * @param AssetPath AnimMontage or AnimSequence path
	 * @param NotifyIndex Zero-based index in the Notifies array
	 * @return JSON with success, removed_notify_name, remaining_count
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Animation")
	static FString RemoveAnimNotify(
		const FString& AssetPath,
		int32 NotifyIndex
	);

	/**
	 * List all AnimNotifies with timing info.
	 *
	 * Returns structured JSON with each notify's index, class, trigger time,
	 * duration, track index, and notify name.
	 *
	 * @param AssetPath AnimMontage or AnimSequence path
	 * @return JSON array of notify objects
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Animation")
	static FString ListAnimNotifies(
		const FString& AssetPath
	);

	//========================================================================
	// UMG Widget Blueprint authoring (Issue #9344)
	//========================================================================
	// Five authoring UFUNCTIONs surfaced through `ue_manage_widget` via
	// `call_narsha_rc` from `MCP/native/mcp_server/src/tools/widget_authoring_ops.rs`.
	//
	// Each returns a JSON string: `{success: bool, widget_path?: string, error?: string, ...}`.
	// The Rust side parses this via `NarshaMcpServer::parse_json_return_value`.
	//
	// MarkDirty=true (default) calls `MarkPackageDirty` + `PostEditChange` so
	// user-visible "unsaved changes" indicator reflects the mutation. Users must
	// still save the asset via File > Save to persist to disk.

	/**
	 * Set a TextBlock's `Text` property inside a Widget Blueprint (Issue #9344).
	 *
	 * @param WidgetBlueprintName  Widget Blueprint name or full path (e.g., "W_MainMenu" or "/Game/UI/W_MainMenu.W_MainMenu").
	 * @param TargetWidget         Widget instance name inside the WBP tree (e.g., "TitleLabel").
	 * @param Text                 New text content (plain FString — localisation integration is follow-up).
	 * @param MarkDirty            If true, marks the asset dirty so the user gets a save prompt (default true).
	 * @return JSON: `{"success": true, "widget_path": "..."}` or `{"success": false, "error": "..."}`.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Widget")
	static FString SetWidgetText(
		const FString& WidgetBlueprintName,
		const FString& TargetWidget,
		const FString& Text,
		bool MarkDirty = true
	);

	/**
	 * Set an Image widget's brush texture (and optional tint) (Issue #9344).
	 *
	 * @param WidgetBlueprintName  Widget Blueprint name or full path.
	 * @param TargetWidget         Widget instance name (must be an Image-derived widget).
	 * @param BrushAssetPath       Asset path to the new texture or material (e.g., "/Game/Textures/T_Background.T_Background").
	 * @param TintColorJson        Optional tint color as JSON string (`{"r":1,"g":1,"b":1,"a":1}`) or empty for no override.
	 * @param MarkDirty            If true, marks the asset dirty (default true).
	 * @return JSON: `{"success": true, "widget_path": "...", "previous_brush_path": "..."}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Widget")
	static FString SetWidgetImageBrush(
		const FString& WidgetBlueprintName,
		const FString& TargetWidget,
		const FString& BrushAssetPath,
		const FString& TintColorJson,
		bool MarkDirty = true
	);

	/**
	 * Set a layout field on a widget's parent slot (Issue #9344).
	 *
	 * Supported `PropertyName` values (handler enforces):
	 *  - `anchors`         CanvasPanelSlot: `{"min_x":0,"min_y":0,"max_x":1,"max_y":1}`
	 *  - `offsets`|`padding`  `{"left":0,"top":0,"right":0,"bottom":0}`
	 *  - `alignment`       `{"x":0.5,"y":0.5}`
	 *  - `z_order`         integer
	 *  - `auto_size`       bool (CanvasPanelSlot)
	 *  - `size_rule`       "Auto" | "Fill" (HorizontalBoxSlot / VerticalBoxSlot)
	 *  - `fill_value`      float (for Fill size rule)
	 *
	 * @param WidgetBlueprintName  Widget Blueprint name or full path.
	 * @param TargetWidget         Widget whose parent slot is being modified.
	 * @param PropertyName         One of the supported names above.
	 * @param PropertyValueJson    JSON-encoded value matching the property shape.
	 * @param MarkDirty            If true, marks the asset dirty (default true).
	 * @return JSON: `{"success": true, "widget_path": "...", "slot_class": "CanvasPanelSlot", ...}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Widget")
	static FString SetWidgetSlotProperty(
		const FString& WidgetBlueprintName,
		const FString& TargetWidget,
		const FString& PropertyName,
		const FString& PropertyValueJson,
		bool MarkDirty = true
	);

	/**
	 * Insert a new child widget under a parent panel (Issue #9344).
	 *
	 * `ChildClass` must be a UMG widget class name (e.g. "TextBlock", "Button", "Image",
	 * "CanvasPanel"). The handler resolves the class via `UWidgetBlueprintLibrary` /
	 * `StaticLoadClass`. `ChildName` is the FName assigned to the new instance — it
	 * must be unique inside the WBP tree (the handler returns an error on collision).
	 *
	 * @param WidgetBlueprintName  Widget Blueprint name or full path.
	 * @param ParentWidget         Existing panel widget (CanvasPanel, HorizontalBox, etc.) in the WBP.
	 * @param ChildClass           UMG widget class name.
	 * @param ChildName            FName for the new widget instance (must be unique).
	 * @param InsertIndex          Zero-based insertion index (-1 appends at end, default -1).
	 * @param SlotPropertiesJson   Optional JSON forwarded to `SetWidgetSlotProperty` after attach.
	 * @param MarkDirty            If true, marks the asset dirty (default true).
	 * @return JSON: `{"success": true, "widget_path": "...", "child_widget": "...", "child_class": "..."}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Widget")
	static FString AddWidgetToParent(
		const FString& WidgetBlueprintName,
		const FString& ParentWidget,
		const FString& ChildClass,
		const FString& ChildName,
		int32 InsertIndex,
		const FString& SlotPropertiesJson,
		bool MarkDirty = true
	);

	/**
	 * Remove a widget from its parent panel (Issue #9344).
	 *
	 * The root widget of a Widget Blueprint cannot be removed — the handler
	 * returns `{"success": false, "error": "Cannot remove root widget"}` in that
	 * case. Widgets referenced by NamedSlot content are detached but the
	 * NamedSlot entry itself is preserved (caller should explicitly null it).
	 *
	 * @param WidgetBlueprintName  Widget Blueprint name or full path.
	 * @param TargetWidget         Widget instance name to remove.
	 * @param MarkDirty            If true, marks the asset dirty (default true).
	 * @return JSON: `{"success": true, "widget_path": "...", "removed_widget": "...", "former_parent": "..."}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Widget")
	static FString RemoveWidget(
		const FString& WidgetBlueprintName,
		const FString& TargetWidget,
		bool MarkDirty = true
	);

	/**
	 * Reparent an existing widget under a new panel parent (Issue #20002).
	 *
	 * Detaches `TargetWidget` from its current panel (via `UPanelWidget::RemoveChild`)
	 * and attaches it to `NewParent` (`AddChild`). Does NOT use the non-existent
	 * `WidgetBlueprintLibrary.reparent_widget` Python API that the legacy
	 * `ue_manage_blueprint(modify_widget_hierarchy)` path relied on.
	 *
	 * Root widgets cannot be reparented (same guard as RemoveWidget). Reparenting
	 * onto self or a descendant is rejected to avoid cycles.
	 *
	 * @param WidgetBlueprintName  Widget Blueprint name or full path.
	 * @param TargetWidget         Widget instance name to move.
	 * @param NewParent            Destination panel widget name.
	 * @param InsertIndex          Zero-based index under NewParent (-1 appends).
	 * @param MarkDirty            If true, marks the asset dirty (default true).
	 * @return JSON success payload with former/new parent + child_index, or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Widget")
	static FString ReparentWidget(
		const FString& WidgetBlueprintName,
		const FString& TargetWidget,
		const FString& NewParent,
		int32 InsertIndex,
		bool MarkDirty = true
	);

	/**
	 * Reorder a widget among its current panel siblings (Issue #20002).
	 *
	 * Uses `UPanelWidget::ShiftChild` to move `TargetWidget` to `InsertIndex`
	 * within its current parent. Does not change parent.
	 *
	 * @param WidgetBlueprintName  Widget Blueprint name or full path.
	 * @param TargetWidget         Widget instance name to reorder.
	 * @param InsertIndex          Zero-based sibling index (clamped to valid range).
	 * @param MarkDirty            If true, marks the asset dirty (default true).
	 * @return JSON success payload with parent + previous/new index, or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Widget")
	static FString ReorderWidget(
		const FString& WidgetBlueprintName,
		const FString& TargetWidget,
		int32 InsertIndex,
		bool MarkDirty = true
	);

	/**
	 * Set an arbitrary reflected UWidget property inside a Widget Blueprint (Issue #19999).
	 *
	 * The generic write counterpart to the ~15 designer properties the offline binary
	 * parser reads (Visibility, RenderOpacity, bIsEnabled, ToolTipText, Clipping,
	 * FlowDirectionPreference, RenderTransformPivot, ColorAndOpacity, Font, ...) — closes the
	 * readable-but-not-writable asymmetry left by the type-specific `SetWidgetText` /
	 * `SetWidgetImageBrush`. Distinct from Blueprint `set_variable_default` (which edits a
	 * member-variable CDO); this edits a widget INSTANCE property inside the WBP tree.
	 *
	 * Resolution: `PropertyName` is first mapped through a friendly-alias table (the
	 * read-side snake_case names — `render_opacity`, `is_enabled`, `tool_tip`, `color`, ...)
	 * to the real UE property name, then looked up via `FindPropertyByName` on the target
	 * widget's own class. An exact UE property name (e.g. `RenderOpacity`) is accepted
	 * verbatim. The value is applied via `FProperty::ImportText_Direct` (the same reflection
	 * path as `SetGraphNodeProperty`), except `FTextProperty` which is set directly from the
	 * string (UE's FText import format is not JSON-friendly). Struct-valued properties
	 * (`ColorAndOpacity`, `Font`) take a full struct object using UE field names
	 * (e.g. `{"R":1,"G":0,"B":0,"A":1}`); single nested sub-field addressing is a follow-up.
	 *
	 * Issue #26342: if the resolved property has a companion `bOverride_<Name>` bitfield
	 * (or a simple `editcondition` identifier marked `InlineEditConditionToggle`), that
	 * flag is turned on in the same mutation. Writing the flag itself is not auto-reenabled.
	 * A false→true flag flip counts as a modification even when the value was already stored.
	 *
	 * @param WidgetBlueprintName  Widget Blueprint name or full path.
	 * @param TargetWidget         Widget instance name inside the WBP tree.
	 * @param PropertyName         Friendly alias (render_opacity/is_enabled/tool_tip/...) or an exact UE property name.
	 * @param PropertyValueJson    JSON-encoded value (scalar/bool/enum-string/struct-object) matching the property shape.
	 * @param MarkDirty            If true, marks the asset dirty so the user gets a save prompt (default true).
	 * @return JSON: `{"success": true, "widget_path": "...", "resolved_property": "...", "previous_value": "...", "applied_value": "..."}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Widget")
	static FString SetWidgetProperty(
		const FString& WidgetBlueprintName,
		const FString& TargetWidget,
		const FString& PropertyName,
		const FString& PropertyValueJson,
		bool MarkDirty = true
	);

	/**
	 * Create a `UWidgetAnimation` inside a Widget Blueprint, optionally with property
	 * tracks and keyframes (Issue #26388).
	 *
	 * Closes a total authoring gap: `UWidgetAnimation` had NO write path anywhere —
	 * `ue_manage_widget` exposed only `get_animations` / `get_animation_tracks`,
	 * `ue_sequencer_*` is LevelSequence-only, and Python reflection is blocked because
	 * `UWidgetBlueprint::Animations` carries a bare `UPROPERTY()` with no Blueprint/Editor
	 * visibility specifier. C++ can reach it (WidgetBlueprint.h:229, under the `public:`
	 * block at L222), which is why this lives here rather than in `execute_python`.
	 *
	 * Creation mirrors the engine's own path in
	 * `UMGEditor/Private/TabFactory/AnimationTabSummoner.cpp:612-660` + `:284-295`:
	 * NewObject<UWidgetAnimation> → SetDisplayLabel + Rename → NewObject<UMovieScene>
	 * child → SetDisplayRate → SetPlaybackRange → Animations.Add → OnVariableAdded.
	 * The `OnVariableAdded` call is NOT optional: skipping it trips the UE 5.8
	 * `WidgetBlueprintCompiler` ensure that `AddWidgetToParent` already documents
	 * (see the comment at CURemoteLibrary.cpp:20879-20881).
	 *
	 * Bindings use `UMovieScene::AddPossessable(Name, Class)` — NOT
	 * `BindPossessableObject`, which is `CastChecked<UUserWidget>(Context)` and so needs a
	 * live preview widget. `AddPossessable` creates the `FMovieScenePossessable` and its
	 * `FMovieSceneBinding` together, leaving only the `FWidgetAnimationBinding` to push;
	 * that keeps the two GUIDs the offline parser joins on in agreement
	 * (`dispatch_helpers/widget/animation_bindings.rs:11-31`).
	 *
	 * `TracksJson` is a JSON array; an empty string or `[]` creates a bare animation.
	 * Each element: `{"target_widget": "...", "property_path": "RenderTransform",
	 * "keyframes": [{"time": 0.0, "value": 0.0, "interp": "Linear",
	 * "component": "Translation.X"}, ...]}`.
	 * `property_path` is resolved through an EXPLICIT class map (`RenderOpacity` →
	 * `/Script/MovieSceneTracks.MovieSceneFloatTrack`, `RenderTransform` →
	 * `/Script/UMG.MovieScene2DTransformTrack`) and an unknown path is rejected rather
	 * than silently coerced to Float — the fail-closed contract Issue #19351 established
	 * for the sibling sequencer surface. String assembly against
	 * `/Script/MovieSceneTracks.` / `/Script/MovieScene.` (what `AddSequencerTrack` does
	 * at CURemoteLibrary.cpp:1669-1673) CANNOT find the 2D transform track: it lives in
	 * `/Script/UMG`.
	 *
	 * For `RenderTransform`, `component` selects `Scale.X`, `Scale.Y`, `Scale.XY`,
	 * `Translation.X`, `Translation.Y`, or `Translation.XY`. Omission fails closed
	 * unless the containing track sets `legacy_default_scale_component=true`, which
	 * preserves the #26388 uniform `Scale.XY` behavior. Components are invalid for
	 * `RenderOpacity`, which writes its single implicit float channel. Unknown or
	 * mistyped components are rejected before the Widget Blueprint is mutated.
	 *
	 * @param WidgetBlueprintName  Widget Blueprint name or full `/Game/...` path.
	 * @param AnimationName        New animation's name; must not collide with an existing one.
	 * @param DurationSeconds      Playback range length in seconds (must be > 0).
	 * @param FrameRate            Display rate numerator in fps (default 60; tick resolution stays the MovieScene default, 60000 in UE 5.8).
	 * @param TracksJson           JSON array of track specs (see above), or empty for none.
	 * @param MarkDirty            If true, marks dirty and saves the package (default true).
	 * @return JSON: `{"success": true, "widget_path": "...", "animation_name": "...", "duration_seconds": N, "tracks_created": N, "keyframes_created": N, "marked_dirty": bool, "persisted": bool}` or an error payload.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Widget")
	static FString CreateWidgetAnimation(
		const FString& WidgetBlueprintName,
		const FString& AnimationName,
		float DurationSeconds,
		int32 FrameRate,
		const FString& TracksJson,
		bool MarkDirty = true
	);

	// ==========================================================================
	// MetaSound editor mutations (Issue #8833)
	//
	// Each function resolves a MetaSound asset (UMetaSoundPatch / UMetaSoundSource),
	// fetches a UMetaSoundBuilderBase via UMetaSoundEditorSubsystem::FindOrBeginBuilding,
	// then calls the existing engine BlueprintCallable mutation API. The call is
	// wrapped in FScopedTransaction + Outer->Modify() so OnObjectTransacted fires
	// for NarshaMCP's transaction-replay capture (the MetaSound document otherwise
	// uses custom serialization that bypasses the standard property delta path —
	// see docs/analysis/METASOUND_API_8833.md § 3 for rationale).
	//
	// Editor-only by construction — UMetaSoundEditorSubsystem is WITH_EDITOR.
	// ==========================================================================

	/**
	 * List nodes in a MetaSound graph (Issue #8833).
	 *
	 * @param MetaSoundAssetPath  MetaSound asset path or name (e.g., "/Game/Audio/MS_Footstep" or "MS_Footstep").
	 * @param FilterType          Optional substring filter on node class name (empty = all nodes).
	 * @return JSON: `{"success": true, "nodes": [{"node_id":"...","class_name":"...","node_type":"..."}], "node_count": N}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|MetaSound")
	static FString ListMetaSoundNodes(
		const FString& MetaSoundAssetPath,
		const FString& FilterType
	);

	/**
	 * List edges (connections) in a MetaSound graph (Issue #16561).
	 *
	 * Editor-side EDGE oracle for the audio GT parity harness. Walks the active build
	 * page's `FMetasoundFrontendGraph::Edges` and resolves each GUID endpoint to node
	 * names + pin names (via the node interface vertices), matching the offline parser's
	 * `{from_node, from_pin, to_node, to_pin}` edge shape.
	 *
	 * @param MetaSoundAssetPath  MetaSound asset path or name (e.g., "/Game/Audio/MS_Footstep" or "MS_Footstep").
	 * @return JSON: `{"success": true, "edges": [{"from_node":"...","from_pin":"...","to_node":"...","to_pin":"..."}], "edge_count": N}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|MetaSound")
	static FString ListMetaSoundEdges(
		const FString& MetaSoundAssetPath
	);

	/**
	 * Author a CustomAttenuationCurve on a SoundAttenuation asset (Issue #16561).
	 *
	 * Sets the inline FRichCurve keys (which UE Python cannot — EditorCurveData is a
	 * bare UPROPERTY) and switches DistanceAlgorithm to Custom. The caller saves the
	 * asset. Used to author a GT-by-construction curve fixture so the audio GT
	 * curve-parity surface has real (non-empty) key data.
	 *
	 * @param AttenuationAssetPath  Full object path (e.g. "/Game/Audio/AttenuationPresets/ATT_GTCurveSynth").
	 * @param KeysJson              `[{"time":..,"value":..,"interp":"Linear|Cubic|Constant"}]`.
	 * @return JSON: `{"success": true, "keys_set": N}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|MetaSound")
	static FString SetAttenuationCustomCurve(
		const FString& AttenuationAssetPath,
		const FString& KeysJson
	);

	/**
	 * Read a SoundAttenuation asset's CustomAttenuationCurve inline FRichCurve keys
	 * (Issue #16561). UE Python cannot (EditorCurveData is a bare UPROPERTY). Returns
	 * the editor-truth curve key rows for the audio GT curve-VALUE oracle.
	 *
	 * @param AttenuationAssetPath  Full object path.
	 * @return JSON: `{"success": true, "key_count": N, "keys": [{"time":..,"value":..,"interp":".."}]}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|MetaSound")
	static FString GetAttenuationCustomCurveKeys(
		const FString& AttenuationAssetPath
	);

	/**
	 * Add a node to a MetaSound graph by class name (Issue #8833).
	 *
	 * Wraps `UMetaSoundBuilderBase::AddNodeByClassName`. Optionally sets the visual
	 * location via `UMetaSoundEditorSubsystem::SetNodeLocation` if `PositionJson` is
	 * non-empty (`{"x":100,"y":200}`).
	 *
	 * @param MetaSoundAssetPath  MetaSound asset path or name.
	 * @param NodeClass           Fully-qualified class name (e.g., "Float.Add", or namespaced "Audio:Mixer").
	 * @param NodeName            Display name for the new node (used for echo in response).
	 * @param PositionJson        Optional `{"x":...,"y":...}` for visual layout. Empty string means default location.
	 * @param MarkDirty           If true, marks the asset dirty (default true).
	 * @return JSON: `{"success": true, "node_id": "<guid>", "node_class": "..."}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|MetaSound")
	static FString AddMetaSoundNode(
		const FString& MetaSoundAssetPath,
		const FString& NodeClass,
		const FString& NodeName,
		const FString& PositionJson,
		bool MarkDirty = true
	);

	/**
	 * Remove a node from a MetaSound graph by node GUID (Issue #8833).
	 *
	 * Wraps `UMetaSoundBuilderBase::RemoveNode` with `bRemoveUnusedDependencies=true`.
	 *
	 * @param MetaSoundAssetPath  MetaSound asset path or name.
	 * @param NodeGuid            Node GUID (string form, as returned by `ListMetaSoundNodes` / `AddMetaSoundNode`).
	 * @param MarkDirty           If true, marks the asset dirty (default true).
	 * @return JSON: `{"success": true, "removed_node_id": "..."}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|MetaSound")
	static FString RemoveMetaSoundNode(
		const FString& MetaSoundAssetPath,
		const FString& NodeGuid,
		bool MarkDirty = true
	);

	/**
	 * Connect two nodes in a MetaSound graph by pin name (Issue #8833).
	 *
	 * Wraps `UMetaSoundBuilderBase::ConnectNodes`.
	 *
	 * @param MetaSoundAssetPath  MetaSound asset path or name.
	 * @param FromNodeGuid        Source node GUID.
	 * @param FromPin             Source pin name (must be an output on the source node).
	 * @param ToNodeGuid          Target node GUID.
	 * @param ToPin               Target pin name (must be an input on the target node).
	 * @param MarkDirty           If true, marks the asset dirty (default true).
	 * @return JSON: `{"success": true, "from_node": "...", "from_pin": "...", "to_node": "...", "to_pin": "..."}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|MetaSound")
	static FString ConnectMetaSoundNodes(
		const FString& MetaSoundAssetPath,
		const FString& FromNodeGuid,
		const FString& FromPin,
		const FString& ToNodeGuid,
		const FString& ToPin,
		bool MarkDirty = true
	);

	/**
	 * Set a parameter (input default literal) on a MetaSound node (Issue #8833).
	 *
	 * Wraps `UMetaSoundBuilderBase::SetNodeInputDefault`. The literal is parsed from
	 * `ParameterValueJson` according to the input pin's data type (Float / Int32 /
	 * Bool / String / FName / Object / etc.).
	 *
	 * @param MetaSoundAssetPath  MetaSound asset path or name.
	 * @param NodeGuid            Target node GUID.
	 * @param ParameterName       Input pin name (e.g. "Frequency", "Gain").
	 * @param ParameterValueJson  JSON-encoded value (e.g. `100.5`, `true`, `"text"`).
	 * @param MarkDirty           If true, marks the asset dirty (default true).
	 * @return JSON: `{"success": true, "node_id": "...", "parameter": "...", "previous_value": "...", "new_value": "..."}` or error.
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|MetaSound")
	static FString SetMetaSoundParameter(
		const FString& MetaSoundAssetPath,
		const FString& NodeGuid,
		const FString& ParameterName,
		const FString& ParameterValueJson,
		bool MarkDirty = true
	);

	/**
	 * Set the WebSocket per-frame dispatch budget at runtime (Issue #9768).
	 *
	 * Mutates `n.WebSocket.MaxPerFrame` via `IConsoleVariable::Set(Clamped, ECVF_SetByCode)`.
	 * This is a runtime-tuning op, not a widget op — it lives on this UFUNCTION library
	 * because the HTTP RC transport is the only reliable mutation path for the CVar
	 * (the Editor's `execute_console_command` silently no-ops in the GameThread CVar
	 * instance; see Issue #9768 and PR #9763 stress harness for empirical evidence).
	 *
	 * Value is clamped to [kMinPerFrame, kMaxPerFrameHardCap] = [1, 64].
	 * If a higher-priority CVar source (e.g. `-Cvar=` commandline) holds the value,
	 * the post-Set readback (`applied_value`) will differ from `clamped_value`,
	 * surfaced as `priority_locked: true` in the response so the caller can detect+warn.
	 *
	 * @param Value  Requested budget. Clamped to [1, 64]. Common values: 4 (default), 8, 16, 32, 64.
	 * @return JSON: `{"success": true, "op": "set_websocket_budget", "requested_value": N,
	 *                "applied_value": N, "previous_value": N, "clamped": bool,
	 *                "priority_locked": bool, "min": 1, "max": 64}` or error.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Performance")
	static FString SetWebSocketBudget(int32 Value);

	// ========================================================================
	// Issue #9489 Phase 2 — ue_manage_animation authoring ops (15 UFUNCTIONs)
	// ========================================================================
	//
	// Routed from Rust handlers in `MCP/native/mcp_server/src/tools/animation/`
	// via the existing `call_narsha_rc` HTTP-PUT helper (objectPath
	// `/Script/NarshaMCP.Default__CURemoteLibrary`). Each function:
	// - Wraps mutation in FScopedTransaction so Editor Ctrl+Z works
	// - Marks the package dirty and saves via UEditorAssetLibrary::SaveLoadedAsset
	// - Returns a JSON-encoded FString that the Rust side parses via
	//   `parse_json_return_value` and lifts whitelisted fields from
	//   (sample_index / section_index / notify_index / modified / saved / etc.)
	//
	// Return JSON shape (success):
	//   {"success": true, "asset_path": "...", "<op_field>": ..., "modified": true, "saved": true}
	// Return JSON shape (failure):
	//   {"success": false, "error": "...", "hint": "..."}
	//
	// Implementation lives in `Private/CUAnimAuthoring.cpp` to keep
	// CURemoteLibrary.cpp from exceeding sane file-size limits — C++ allows
	// splitting a class's method definitions across multiple TUs.

	// ---- BlendSpace authoring (7) ----

	/**
	 * Place an AnimSequence at a grid position on a BlendSpace asset.
	 *
	 * UE 5.8 RC has a known TArray<float> marshalling defect (1-2 element
	 * input gets mangled to N+1 with leading zero), so the user-facing axis
	 * parameter is `FVector` instead — BlendSpace internally uses FVector
	 * (X, Y unused for BS1D; X+Y for BS/AO; Z always 0). Callers send
	 * `{"X": 42.0, "Y": 0.0, "Z": 0.0}`.
	 *
	 * @param AssetPath  /Game/... package path
	 * @param AnimRef    /Game/... AnimSequence ref
	 * @param SampleValue X = primary axis, Y = secondary (0 for BS1D/AO1D), Z = unused
	 * @return JSON with sample_index of the new sample
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimBS_AddSample(
		const FString& AssetPath,
		const FString& AnimRef,
		const FVector& SampleValue
	);

	/**
	 * Remove a sample by index OR by exact axis coordinate.
	 * @param bUseIndex true → use Index; false → match SampleValue exactly
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimBS_RemoveSample(
		const FString& AssetPath,
		int32 Index,
		const FVector& SampleValue,
		bool bUseIndex
	);

	/** Move a sample to new grid coordinates (by index). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimBS_MoveSample(
		const FString& AssetPath,
		int32 Index,
		const FVector& NewSampleValue
	);

	/** Swap the AnimReference on an existing sample (by index). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimBS_SetSampleAnimation(
		const FString& AssetPath,
		int32 Index,
		const FString& NewAnimRef
	);

	/** Set FBlendParameter.Min/Max for an axis (0 or 1). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimBS_SetAxisRange(
		const FString& AssetPath,
		int32 AxisIndex,
		float MinVal,
		float MaxVal
	);

	/** Change BlendSpace interpolation: Linear/Cubic/Step/LinearStep/CubicStep. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimBS_SetInterpolation(
		const FString& AssetPath,
		const FString& Kind
	);

	/**
	 * Generic UPROPERTY setter (whitelisted keys: wrap_input /
	 * notify_trigger_mode / sample_rate).
	 * JsonValue is a JSON-serialized value (bool / number / string).
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimBS_SetParameter(
		const FString& AssetPath,
		const FString& Key,
		const FString& JsonValue
	);

	// ---- AnimMontage authoring (8) ----

	/** Add a new FCompositeSection at StartTime. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimAM_AddSection(
		const FString& AssetPath,
		const FString& Name,
		float StartTime
	);

	/** Remove a section by name. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimAM_RemoveSection(
		const FString& AssetPath,
		const FString& Name
	);

	/**
	 * Set NextSection and/or LinkingRule on a section.
	 *
	 * NextSection semantics (Issue #9489 C-2 fix):
	 * - bUseNextSection=false → leave existing NextSectionName UNTOUCHED
	 * - bUseNextSection=true + NextSection="" → CLEAR the link (NAME_None)
	 * - bUseNextSection=true + NextSection="<name>" → set to <name>
	 *
	 * Previously, callers updating only LinkingRule would accidentally clear
	 * NextSectionName because empty string was ambiguous.
	 *
	 * LinkingRule: "Absolute" / "Relative" / "Proportional" (EAnimLinkMethod).
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimAM_SetSectionLink(
		const FString& AssetPath,
		const FString& Section,
		const FString& NextSection,
		bool bUseNextSection,
		const FString& LinkingRule
	);

	/** Move a section to a new StartTime. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimAM_MoveSection(
		const FString& AssetPath,
		const FString& Name,
		float NewStartTime
	);

	/**
	 * Insert an AnimNotify (point notify) at TriggerTime on Track.
	 * NotifyClass is a fully-qualified class path
	 * (e.g. `/Game/Notifies/AN_Footstep.AN_Footstep_C`).
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimAM_AddNotify(
		const FString& AssetPath,
		const FString& Track,
		float TriggerTime,
		const FString& NotifyClass
	);

	/** Insert an AnimNotifyState with Duration. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimAM_AddNotifyState(
		const FString& AssetPath,
		const FString& Track,
		float TriggerTime,
		float Duration,
		const FString& NotifyClass
	);

	/**
	 * Remove a notify by index OR (Track, TriggerTime). Ambiguous (Track,
	 * TriggerTime) matches return success=false + matches[] for the caller
	 * to disambiguate via index.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimAM_RemoveNotify(
		const FString& AssetPath,
		int32 Index,
		const FString& Track,
		float TriggerTime,
		bool bUseIndex
	);

	/**
	 * Update BlendIn / BlendOut FAlphaBlend on a montage.
	 * Option is an EAlphaBlendOption enum name; empty string preserves
	 * the existing option.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimAM_SetBlendInOut(
		const FString& AssetPath,
		float BlendIn,
		float BlendOut,
		const FString& Option
	);

	// ---- Asset creation (2) — Issue #16573 ----

	/**
	 * Create a new AnimMontage from a source AnimSequence, embedding the
	 * sequence in the auto-generated DefaultSlot / "Default" section
	 * (UAnimMontageFactory). TargetSkeleton is auto-derived from the source
	 * sequence — callers supply no skeleton. Editor required.
	 *
	 * @param SourceAnimSequencePath /Game/... AnimSequence to embed
	 * @param DestinationPath        /Game/... package path for the new montage
	 *                               (must not already exist)
	 * @return JSON {success, asset_path, source_anim, slot, section, modified, saved}
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimAM_CreateMontage(
		const FString& SourceAnimSequencePath,
		const FString& DestinationPath
	);

	/**
	 * Create a new 1D (AxisCount=1 → UBlendSpace1D) or 2D (AxisCount=2 →
	 * UBlendSpace) BlendSpace bound to SkeletonPath, with per-axis
	 * name/min/max/grid applied at creation. Editor required.
	 *
	 * Axis params are flattened scalars (not a TArray) to dodge the UE 5.8 RC
	 * TArray<float> marshalling defect (see AnimBS_AddSample). The Y* params are
	 * ignored when AxisCount==1.
	 *
	 * @param DestinationPath /Game/... package path (must not already exist)
	 * @param SkeletonPath    /Game/... USkeleton the BlendSpace targets
	 * @param AxisCount       1 (BlendSpace1D) or 2 (BlendSpace2D)
	 * @return JSON {success, asset_path, axis_count, blend_type, modified, saved}
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimBS_CreateBlendSpace(
		const FString& DestinationPath,
		const FString& SkeletonPath,
		int32 AxisCount,
		const FString& XAxisName,
		float XAxisMin,
		float XAxisMax,
		int32 XGridDivisions,
		const FString& YAxisName,
		float YAxisMin,
		float YAxisMax,
		int32 YGridDivisions
	);

	// ------------------------------------------------------------------
	// Issue #14795 — AnimSequence notify authoring + notify-track creation +
	// notify payload property. Implemented in CUAnimAuthoring_Notify.cpp.
	// All three operate on UAnimSequenceBase (covers AnimSequence AND
	// AnimMontage), so the Rust `add_notify` op can asset-kind auto-branch
	// (AM_* montage → AnimAM_AddNotify strict-track; sequence/other →
	// AnimSeq_AddNotify track-optional). Refutes WONTFIX #11155/#11189
	// (campaign #14709 E04 notify-event / E05 notify-track evidence).
	// ------------------------------------------------------------------

	/**
	 * Add an AnimNotify (point) — or an AnimNotifyState when Duration>0 and
	 * NotifyClass is a UAnimNotifyState subclass — to a UAnimSequenceBase
	 * (AnimSequence or AnimMontage). Track is optional: "" → default lane 0;
	 * a named track must already exist (create via Anim_AddNotifyTrack).
	 * NotifyClass is a fully-qualified class path. Returns the post-refresh
	 * (serialized-order) notify_index so offline reparse can verify it.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimSeq_AddNotify(
		const FString& AssetPath,
		float TriggerTime,
		const FString& NotifyClass,
		const FString& Track,
		float Duration
	);

	/**
	 * Append a new named FAnimNotifyTrack (notify lane) to a UAnimSequenceBase.
	 * 33/51 Lyra montages ship with zero notify tracks, so this is a
	 * prerequisite for placing notifies on a named track. Track names must be
	 * unique; a duplicate returns success=false.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString Anim_AddNotifyTrack(
		const FString& AssetPath,
		const FString& TrackName
	);

	/**
	 * Set a property (ExportText format) on the notify instance object of the
	 * FAnimNotifyEvent at NotifyIndex (its UAnimNotify or UAnimNotifyState).
	 * Mirrors set_editor_property: FProperty + ImportText_Direct.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString Anim_SetNotifyProperty(
		const FString& AssetPath,
		int32 NotifyIndex,
		const FString& PropertyName,
		const FString& Value
	);

	// ------------------------------------------------------------------
	// Issue #14852 — AnimSequence sync-marker authoring. Implemented in
	// CUAnimAuthoring_SyncMarker.cpp, wrapping UAnimationBlueprintLibrary's
	// AddAnimationSyncMarker + RemoveAnimationSyncMarkers{ByName,ByTrack,All}.
	// Sync markers are UAnimSequence-typed (AuthoredSyncMarkers); reads are
	// served offline by the Rust get_sync_markers / is_valid_sync_marker_name
	// ops (Rule 20). Campaign #14709 E03 evidence.
	// ------------------------------------------------------------------

	/**
	 * Add an FAnimSyncMarker to a UAnimSequence's AuthoredSyncMarkers. The
	 * marker attaches to an existing notify track (NotifyTrack must be non-empty
	 * and already present — create one via Anim_AddNotifyTrack). Returns the
	 * post-add serialized marker_index so offline reparse can verify it.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimSeq_AddSyncMarker(
		const FString& AssetPath,
		const FString& MarkerName,
		float Time,
		const FString& NotifyTrack
	);

	/**
	 * Remove sync markers from a UAnimSequence. Mode dispatch: "name" removes by
	 * MarkerName, "track" removes every marker on NotifyTrack, "all" clears every
	 * marker. Returns removed_count (snapshotted before the void RemoveAll call).
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimSeq_RemoveSyncMarker(
		const FString& AssetPath,
		const FString& MarkerName,
		const FString& NotifyTrack,
		const FString& Mode
	);

	// ------------------------------------------------------------------
	// Issue #27875 — destructive Animation Modifier execution.
	// Implemented in CUAnimAuthoring_Modifier.cpp. These are deliberately
	// editor-only: modifier execution rewrites and saves the source .uasset.
	// This does not reopen the offline raw/compressed editing boundary from
	// #11155/#11189.
	// ------------------------------------------------------------------

	/**
	 * Apply every Animation Modifier already configured on an AnimSequence.
	 * Warning/error dialogs are suppressed for unattended Remote Control use.
	 * Returns the saved file's SHA-1 before/after plus modifier counts/classes.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimSeq_ApplyAnimationModifiers(
		const FString& AssetPath
	);

	/**
	 * Revert every configured Animation Modifier in reverse order. The entire
	 * set is preflighted with CanRevert before any mutation is allowed.
	 * Returns the saved file's SHA-1 before/after plus modifier counts/classes.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation")
	static FString AnimSeq_RevertAnimationModifiers(
		const FString& AssetPath
	);

	// ------------------------------------------------------------------
	// Issue #22752 — Persona / Animation Editor preview transport control.
	// Mirrors ue_sequencer_playback's play/pause/stop/scrub/get_state axis for
	// AnimSequence / AnimMontage open in the Persona toolkit preview viewport.
	// Implemented in CUAnimAuthoring_Preview.cpp (Persona + AnimationEditor).
	// ------------------------------------------------------------------

	/** Open (or focus) the animation asset editor and start preview playback. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation|Preview")
	static FString AnimPreview_Play(
		const FString& AssetPath,
		bool bLoop = true
	);

	/** Pause the active Persona preview for AssetPath (keeps current time). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation|Preview")
	static FString AnimPreview_Pause(
		const FString& AssetPath
	);

	/** Stop preview and reset scrubber to t=0. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation|Preview")
	static FString AnimPreview_Stop(
		const FString& AssetPath
	);

	/**
	 * Scrub the preview. Priority: TimeSec (>=0) > Frame (>=0) > Percent [0,100].
	 * Pass negative / unused axes as -1 (Time/Frame) or -1 (Percent).
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation|Preview")
	static FString AnimPreview_Scrub(
		const FString& AssetPath,
		float TimeSec = -1.0f,
		float Frame = -1.0f,
		float Percent = -1.0f
	);

	/** Read preview transport state: playing, time, frame, percent, length, loop. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation|Preview")
	static FString AnimPreview_GetState(
		const FString& AssetPath
	);

	// ------------------------------------------------------------------
	// Issue #22855 — Persona preview viewport camera control.
	// Sibling to #22752 transport; drives FEditorViewportClient on the
	// IPersonaPreviewScene (NOT the level viewport).
	// ------------------------------------------------------------------

	/**
	 * Set Persona preview camera. Bool flags mark which axes to apply
	 * (partial update supported). Location = world cm; Rotation = pitch/yaw/roll deg; FOV = degrees.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation|Preview")
	static FString AnimPreview_SetCamera(
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
		float FOV
	);

	/** Read Persona preview camera: location, rotation, fov. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation|Preview")
	static FString AnimPreview_GetCamera(
		const FString& AssetPath
	);

	/**
	 * Frame the Persona preview camera on the preview mesh bounds
	 * (IPersonaPreviewScene::FocusViews + FocusViewportOnBox).
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="NarshaMCP|Animation|Preview")
	static FString AnimPreview_Focus(
		const FString& AssetPath
	);
};
