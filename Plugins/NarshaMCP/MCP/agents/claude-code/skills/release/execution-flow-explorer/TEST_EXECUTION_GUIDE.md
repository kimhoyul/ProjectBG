# Test Execution Guide - Execution Flow Explorer

**Purpose**: Step-by-step guide for running E2E tests

**Issue**: #3033
**Test File**: `tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py`

---

## Prerequisites

### 1. Environment Setup

**Required**:
```bash
# Set project path
export UECODEGEN_PROJECT_DIR="E:/last-contact-prod"

# Optional: Override test function
export TEST_FUNCTION_NAME="ACharacter::TakeDamage"
export TEST_INPUT_FUNCTION="APlayerController::ProcessInput"
```

### 2. MCP Server Status

**Verify server is running**:
```bash
# In Claude Code or via MCP client
ue_check_health with project_root="E:/last-contact-prod"

# Expected output:
# - status: "healthy"
# - pdb_index.status: "ready"
# - metadata.status: "ready"
```

**If server not running**:
```bash
# Restart MCP server
# (Project-specific instructions)
```

---

## Running Tests

### Option 1: All E2E Tests (Recommended)

```bash
# Run all 10 tests
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py -v -s

# Expected output:
# ✅ test_skill_activation PASSED
# ✅ test_bidirectional_trace_workflow PASSED
# ✅ test_parallel_tool_usage PASSED
# ✅ test_mermaid_diagram_output PASSED
# ✅ test_depth_control PASSED
# ✅ test_cross_domain_detection PASSED
# ✅ test_parallel_execution_performance PASSED
# ✅ test_error_handling_function_not_found PASSED
# ✅ test_caller_only_mode PASSED
# ✅ test_full_workflow_integration PASSED
#
# ========== 10 passed in ~60s ==========
```

### Option 2: Single Test

```bash
# Test specific functionality
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_bidirectional_trace_workflow -v -s

# Test performance
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_parallel_execution_performance -v -s

# Test error handling
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_error_handling_function_not_found -v -s
```

### Option 3: With Coverage

```bash
# Run with coverage report
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py \
    --cov=.claude/skills/execution-flow-explorer \
    --cov-report=html \
    -v -s

# View coverage report
# open htmlcov/index.html
```

### Option 4: Specific Test Categories

```bash
# Core functionality tests (1-4)
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py \
    -k "activation or workflow or tool_usage or mermaid" \
    -v -s

# Skill-specific tests (5-7)
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py \
    -k "depth_control or cross_domain or performance" \
    -v -s

# Error handling tests (8-9)
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py \
    -k "error_handling or caller_only" \
    -v -s
```

---

## Test Scenarios

### Test 1: Skill Activation (30s)

**Purpose**: Verify trigger keywords activate skill

**Command**:
```bash
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_skill_activation -v -s
```

**Expected Output**:
```text
✅ Skill activated with keyword: '호출 관계 보여줘'
📞 Tools called: ['ue_analyze_symbols', 'ue_trace_execution']
```

**Success Criteria**:
- ✅ At least 1 trigger keyword activates skill
- ✅ Both MCP tools called after activation

---

### Test 2: Bidirectional Trace Workflow (60s)

**Purpose**: Verify caller + callee tracing works

**Command**:
```bash
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_bidirectional_trace_workflow -v -s
```

**Expected Output**:
```text
✅ Bidirectional trace workflow passed
📊 Function analyzed: ACharacter::TakeDamage
🔧 Tools called: ['ue_analyze_symbols', 'ue_trace_execution']
```

**Success Criteria**:
- ✅ Both ue_analyze_symbols and ue_trace_execution called
- ✅ Function name correctly parsed from user query

---

### Test 3: Parallel Tool Usage (60s)

**Purpose**: Verify parallel execution of MCP tools

**Command**:
```bash
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_parallel_tool_usage -v -s
```

**Expected Output**:
```text
✅ Both MCP tools called: ['ue_analyze_symbols', 'ue_trace_execution']
⚡ Parallel execution verified
```

**Success Criteria**:
- ✅ Both tools called (order doesn't matter)
- ✅ No missing tools

---

### Test 4: Mermaid Diagram Output (60s)

**Purpose**: Verify Mermaid visualization generation

**Command**:
```bash
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_mermaid_diagram_output -v -s
```

**Expected Output**:
```text
✅ Mermaid diagram generated
📊 Text summary included
```

**Success Criteria**:
- ✅ Output contains Mermaid diagram (`graph TB`)
- ✅ Output contains text summary (statistics, hotspots)

---

### Test 5: Depth Control (120s)

**Purpose**: Verify depth parameter affects results

**Command**:
```bash
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_depth_control -v -s
```

**Expected Output**:
```text
✅ Depth control test passed
📊 depth=1 executed successfully
📊 depth=2 executed successfully
```

**Success Criteria**:
- ✅ Both depth=1 and depth=2 complete successfully
- ✅ depth=2 should have >= nodes than depth=1 (if measurable)

---

### Test 6: Cross-Domain Detection (60s)

**Purpose**: Verify C++ ↔ Blueprint link detection

**Command**:
```bash
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_cross_domain_detection -v -s
```

**Expected Output**:
```text
✅ Cross-domain detection test passed
🔗 Function analyzed: APlayerController::ProcessInput
📊 Tools called: ['ue_analyze_symbols', 'ue_trace_execution']
```

**Success Criteria**:
- ✅ Input handler function analyzed successfully
- ✅ Both tools called (likely to find Blueprint callers)

---

### Test 7: Performance Benchmarking (60s)

**Purpose**: Verify execution time < 3s

**Command**:
```bash
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_parallel_execution_performance -v -s
```

**Expected Output**:
```text
✅ Performance test passed
⚡ Execution time: 1.24s (expected < 3s)
🚀 Parallel execution confirmed
```

**Success Criteria**:
- ✅ Execution time < 3.0 seconds for depth=2
- ✅ Workflow completes successfully

---

### Test 8: Error Handling (60s)

**Purpose**: Graceful handling of non-existent function

**Command**:
```bash
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_error_handling_function_not_found -v -s
```

**Expected Output**:
```text
✅ Error handling test completed
📊 Workflow handled non-existent function gracefully
```

**Success Criteria**:
- ✅ No crashes or hangs
- ✅ Error message provided (or graceful failure)

---

### Test 9: Caller-Only Mode (60s)

**Purpose**: Single-direction tracing (callers only)

**Command**:
```bash
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_caller_only_mode -v -s
```

**Expected Output**:
```text
✅ Caller-only mode test passed
📊 Only caller tracing executed
```

**Success Criteria**:
- ✅ Only ue_analyze_symbols called (no ue_trace_execution)
- ✅ Workflow completes successfully

---

### Test 10: Full Workflow Integration (120s)

**Purpose**: End-to-end integration test

**Command**:
```bash
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_full_workflow_integration -v -s
```

**Expected Output**:
```text
✅ Full workflow integration test passed
⏱️ Total time: 1.45s
🎯 Function: ACharacter::TakeDamage
🔧 All tools called correctly
📊 Bidirectional trace completed successfully
```

**Success Criteria**:
- ✅ All workflow stages pass
- ✅ Correct tools called
- ✅ Total time reasonable (< 3s)

---

## Interpreting Results

### Success Indicators

**All Tests Passing**:
```text
===== 10 passed in 60.45s =====
```
- ✅ Skill ready for production
- ✅ All features working correctly
- ✅ Performance within expected range

**Some Tests Passing**:
```text
===== 8 passed, 2 failed in 65.23s =====
```
- 🟡 Review failed tests
- 🟡 Check if failures are environment-specific
- 🟡 May need project-specific adjustments

### Common Failures

**1. MCP Server Not Connected**:
```text
ERROR: Not connected
```
**Solution**: Start MCP server, verify with `ue_check_health`

**2. Function Not Found**:
```text
AssertionError: Stage 1 failed. No callers found for function.
```
**Solution**: Update `TEST_FUNCTION_NAME` to existing function in your project

**3. Timeout**:
```text
ERROR: timeout after 60s
```
**Solution**: Increase `StandardTimeouts.WORKFLOW_STAGE` in config.py

**4. Tool Not Called**:
```text
AssertionError: Missing tools: ['ue_trace_execution']
```
**Solution**: Check if skill correctly orchestrates both tools

---

## Debugging Tips

### Enable Verbose Output

```bash
# Maximum verbosity
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py -vvv -s --tb=long

# Show print statements
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py -s

# Show warnings
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py -v -W all
```

### Inspect Test Output

```bash
# Save output to file
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py -v -s > test_output.txt 2>&1

# Inspect specific test
pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py::test_bidirectional_trace_workflow -v -s --pdb
```

### Check MCP Server Logs

```bash
# If tests fail, check server logs for errors
# Location: (project-specific)
tail -f /path/to/mcp-server.log
```

---

## CI/CD Integration

### GitHub Actions

Add to `.github/workflows/ci-e2e.yml`:

```yaml
- name: Run Execution Flow Explorer E2E Tests
  run: |
    pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py \
      -v \
      --junit-xml=test-results/execution-flow-explorer-e2e.xml \
      --cov=.claude/skills/execution-flow-explorer \
      --cov-report=xml
  env:
    UECODEGEN_PROJECT_DIR: ${{ secrets.TEST_PROJECT_PATH }}
```

### Local CI Simulation

```bash
# Run in CI-like environment
docker run -it \
  -e UECODEGEN_PROJECT_DIR="/test-project" \
  -v $(pwd):/workspace \
  python:3.11 \
  bash -c "cd /workspace && pytest tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py -v"
```

---

## Success Metrics

**After all tests pass**:

- ✅ **Functional**: All 10 test scenarios passing
- ✅ **Performance**: Execution < 3s for depth=2
- ✅ **Reliability**: No flaky tests (run 3 times, all pass)
- ✅ **Coverage**: Core features tested (activation, workflow, tools, output)

**Ready for**:
- Merge to main branch
- Production deployment
- User acceptance testing

---

**Last Updated**: 2026-01-01
**Test File**: tests/e2e/skills/test_execution_flow_explorer_skill_e2e.py
**Total Tests**: 10
**Expected Duration**: ~60-120s
