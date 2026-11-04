# UE Editor Launcher - Complete Reference

Complete API reference for Unreal Editor launching.

---

## Environment Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `UECODEGEN_ENGINE_DIR` | Engine installation path | `C:/UE_5.7/Engine` |
| `UECODEGEN_PROJECT_DIR` | Project root path | `E:/LCP_B` |

**Location**: `.env` file at project root

---

## Launch Commands

### Windows

```bash
# Standard launch
start "" "{ENGINE_DIR}/Binaries/Win64/UnrealEditor.exe" "{PROJECT_DIR}/Project.uproject"

# With logging
start "" "{ENGINE_DIR}/Binaries/Win64/UnrealEditor.exe" "{PROJECT_DIR}/Project.uproject" -log

# Debug mode
start "" "{ENGINE_DIR}/Binaries/Win64/UnrealEditor.exe" "{PROJECT_DIR}/Project.uproject" -debug
```

### Common Launch Arguments

| Argument | Description |
|----------|-------------|
| `-log` | Enable logging |
| `-debug` | Debug mode |
| `-game` | Launch as game |
| `-server` | Launch as server |
| `-UNATTENDED` | Suppress modal dialogs (recovery, save prompts) |
| `-noremotecontrol` | Disable Remote Control |
| `-nullrhi` | No rendering (CI) |

### Unattended and Remote-Control Safety

- Treat `-UNATTENDED` as defense in depth for recovery and save prompts, not as the
  correctness mechanism for automation writes. Write tools must reject known modal
  branches before calling the Editor API that can open them.
- Automation tests can temporarily set `FApp::IsUnattended()` even when the Editor was
  launched without `-UNATTENDED`. Inspect the command line and, when launch-mode behavior
  matters, confirm it with a request made outside the Automation framework.
- Do not combine `-nullrhi` with a live Remote Control HTTP/WebSocket check unless that
  exact headless mode is known to initialize the required endpoint. Use a render-capable
  Editor for the canonical live probe.

---

## Remote Control Verification

```bash
# Check Remote Control is available
curl http://localhost:30010/remote/info
```

**Requirements**:
- Remote Control Web Interface Plugin enabled
- Editor fully loaded
- Port 30010 accessible
- For write operations, verify the WebSocket endpoint (normally 30011) as well as the
  HTTP health endpoint; the HTTP endpoint being reachable does not prove that Python
  execution over WebSocket is ready.

---

## Platform-Specific Notes

### Windows
- Use `start ""` for background launch
- Paths with spaces need quoting

### macOS
- Use `open` command
- App bundle path differs

### Linux
- Use `&` for background
- Check binary permissions

---

## Related Documentation

- [UE Editor Launch Guide](../../../docs/getting-started/UE_EDITOR_LAUNCH_GUIDE.md)
