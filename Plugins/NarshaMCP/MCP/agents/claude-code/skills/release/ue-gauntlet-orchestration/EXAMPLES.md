# Gauntlet Orchestration — Code Templates & CI/CD Examples

Soak/Perf C++ templates, multiplayer additions, GitHub Actions, and Jenkinsfile examples.
Parent: [SKILL.md](SKILL.md)

---

## 1. Soak Mode C++ Templates

### Header (.h)

Write to `{PROJECT_DIR}/Source/{MODULE_NAME}/Tests/{CONTROLLER_CLASS_NAME}.h`:

```cpp
#pragma once
#include "CoreMinimal.h"
#include "GauntletTestController.h"
#include "{CONTROLLER_CLASS_NAME}.generated.h"

UCLASS()
class {MODULE_NAME}_API {CONTROLLER_CLASS} : public UGauntletTestController
{
    GENERATED_BODY()

public:
    virtual void OnInit() override;
    virtual void OnTick(float TimeDelta) override;

private:
    double TestStartTime = 0.0;
    double LastMemoryCheck = 0.0;
    uint64 MaxAllowedMemory = 4ULL * 1024 * 1024 * 1024; // 4GB
    float MinAcceptableFPS = 20.0f;
    int32 LowFPSCount = 0;
};
```

### Implementation (.cpp)

Write to `{PROJECT_DIR}/Source/{MODULE_NAME}/Tests/{CONTROLLER_CLASS_NAME}.cpp`:

```cpp
#include "{CONTROLLER_CLASS_NAME}.h"
#include "GenericPlatform/GenericPlatformMemory.h"

void {CONTROLLER_CLASS}::OnInit()
{
    SetMaxDuration(static_cast<float>({DURATION}));
    TestStartTime = FPlatformTime::Seconds();
}

void {CONTROLLER_CLASS}::OnTick(float TimeDelta)
{
    const double Elapsed = FPlatformTime::Seconds() - TestStartTime;

    // Periodic memory check (every 5 min)
    if (Elapsed - LastMemoryCheck > 300.0)
    {
        FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
        UE_LOG(LogTemp, Log, TEXT("[Soak] %.0fs: UsedPhysical=%.1fMB"),
            Elapsed, Stats.UsedPhysical / (1024.0 * 1024.0));

        if (Stats.UsedPhysical > MaxAllowedMemory)
        {
            UE_LOG(LogTemp, Error, TEXT("Memory leak: exceeded max allowed memory"));
            EndTest(1);
        }
        LastMemoryCheck = Elapsed;
    }

    // Periodic FPS check
    if (FApp::GetDeltaTime() > 0.0f)
    {
        float CurrentFPS = 1.0f / FApp::GetDeltaTime();
        if (CurrentFPS < MinAcceptableFPS)
        {
            LowFPSCount++;
            if (LowFPSCount > 100)
            {
                UE_LOG(LogTemp, Error, TEXT("Sustained low FPS: 100+ consecutive frames below threshold"));
                EndTest(1);
            }
        }
        else
        {
            LowFPSCount = 0; // Reset on good frame
        }
    }
}
```

---

## 2. Perf Mode C++ Templates

### Header (.h)

Write to `{PROJECT_DIR}/Source/{MODULE_NAME}/Tests/{CONTROLLER_CLASS_NAME}.h`:

```cpp
#pragma once
#include "CoreMinimal.h"
#include "GauntletTestController.h"
#include "{CONTROLLER_CLASS_NAME}.generated.h"

UCLASS()
class {MODULE_NAME}_API {CONTROLLER_CLASS} : public UGauntletTestController
{
    GENERATED_BODY()

public:
    virtual void OnInit() override;
    virtual void OnTick(float TimeDelta) override;

private:
    double TestStartTime = 0.0;
    double SpinUpTime = 3.0;
    double ProfilingTime = 30.0;
    bool bProfilingStarted = false;
};
```

### Implementation (.cpp)

Write to `{PROJECT_DIR}/Source/{MODULE_NAME}/Tests/{CONTROLLER_CLASS_NAME}.cpp`:

```cpp
#include "{CONTROLLER_CLASS_NAME}.h"
#include "Engine/Engine.h"

void {CONTROLLER_CLASS}::OnInit()
{
    SpinUpTime = 3.0;   // Warmup before profiling
    ProfilingTime = 30.0; // Capture duration
    TestStartTime = FPlatformTime::Seconds();
}

void {CONTROLLER_CLASS}::OnTick(float TimeDelta)
{
    const double Elapsed = FPlatformTime::Seconds() - TestStartTime;

    if (!bProfilingStarted && Elapsed > SpinUpTime)
    {
        GEngine->Exec(GetWorld(), TEXT("CsvProfile START"));
        bProfilingStarted = true;
        UE_LOG(LogTemp, Log, TEXT("[Perf] CSV profiling started"));
    }
    else if (bProfilingStarted && Elapsed > SpinUpTime + ProfilingTime)
    {
        GEngine->Exec(GetWorld(), TEXT("CsvProfile STOP"));
        UE_LOG(LogTemp, Log, TEXT("[Perf] CSV profiling stopped"));
        UE_LOG(LogTemp, Log, TEXT("Performance profiling complete"));
        EndTest(0);
    }
}
```

---

## 3. Multiplayer Addition (MULTIPLAYER > 0)

Add this comment to OnInit() when multiplayer mode is enabled:

```cpp
// Multiplayer: This controller runs on both server and client.
// Use GetWorld()->GetNetMode() to differentiate behavior:
//   NM_DedicatedServer / NM_ListenServer / NM_Client
```

---

## 4. GitHub Actions Template (CI_TARGET == github)

Write to `{PROJECT_DIR}/.github/workflows/gauntlet-tests.yml`:

```yaml
name: Gauntlet Tests
on:
  push:
    branches: [main]
  pull_request:
  schedule:
    - cron: '0 2 * * *'  # Nightly at 2 AM

# IMPORTANT: Set UE_ROOT on your self-hosted runner, or define it here:
# env:
#   UE_ROOT: "C:/Program Files/Epic Games/UE_5.7"

jobs:
  {TEST_MODE}-test:
    runs-on: [self-hosted, UE5, Windows]
    timeout-minutes: {TIMEOUT_MINUTES}

    steps:
    - uses: actions/checkout@v4

    - name: Build & Cook
      shell: pwsh
      run: |
        & "$env:UE_ROOT/Build/BatchFiles/RunUAT.bat" BuildCookRun `
          -project="$pwd/{UPROJECT_FILENAME}" `
          -platform={PLATFORM} -clientconfig=Development `
          -build -cook -stage -pak

    - name: Run {TEST_MODE} Test
      shell: pwsh
      run: |
        # Find game exe in staged build
        $GameExe = Get-ChildItem "$pwd/Saved/StagedBuilds/{PLATFORM_DIR}" -Recurse -Filter "*.exe" |
          Where-Object { $_.Directory.Name -eq "{PLATFORM}" -and $_.Name -notmatch "CrashReport|EpicWebHelper" } |
          Select-Object -First 1
        if (-not $GameExe) { Write-Error "Game exe not found"; exit 1 }
        Write-Host "Running: $($GameExe.FullName)"
        & $GameExe.FullName `
          -gauntlet={CONTROLLER_CLASS_NAME} `
          -unattended -nosplash -nosound `
          -windowed -ResX=640 -ResY=480 `
          -log{EXTRA_PARAMS}

    - name: Collect Logs
      if: always()
      shell: pwsh
      run: |
        # Collect game logs for analysis (Option A does not produce JUnit XML)
        New-Item -ItemType Directory -Force -Path TestResults | Out-Null
        $Logs = Get-ChildItem "$pwd/Saved" -Recurse -Filter "*.log" -ErrorAction SilentlyContinue
        if ($Logs) { Copy-Item $Logs.FullName -Destination TestResults/ }

    - name: Upload Artifacts
      if: always()
      uses: actions/upload-artifact@v4
      with:
        name: {TEST_MODE}-test-results
        path: TestResults/
```

---

## 5. Jenkinsfile Template (CI_TARGET == jenkins)

Write to `{PROJECT_DIR}/Jenkinsfile.gauntlet`:

```groovy
pipeline {
    agent { label 'UE5-Windows' }
    options { timeout(time: {TIMEOUT_MINUTES}, unit: 'MINUTES') }

    stages {
        stage('Cook') {
            steps {
                bat """
                    RunUAT.bat BuildCookRun ^
                        -project="%WORKSPACE%\\{UPROJECT_FILENAME}" ^
                        -platform={PLATFORM} -clientconfig=Development ^
                        -build -cook -stage -pak
                """
            }
        }
        stage('Test') {
            steps {
                bat """
                    @rem Find game exe in staged build (excludes CrashReport/EpicWebHelper)
                    setlocal enabledelayedexpansion
                    for /R "%WORKSPACE%\\Saved\\StagedBuilds\\{PLATFORM_DIR}" %%G in (*.exe) do (
                        echo %%~nxG | findstr /V /I "CrashReport EpicWebHelper" >nul && (
                            echo Running: %%G
                            "%%G" ^
                                -gauntlet={CONTROLLER_CLASS_NAME} ^
                                -unattended -nosplash -nosound ^
                                -windowed -ResX=640 -ResY=480 ^
                                -log
                            goto :done
                        )
                    )
                    :done
                """
            }
            post {
                always {
                    bat 'if not exist TestResults mkdir TestResults && xcopy /Y /S Saved\\Logs\\*.log TestResults\\ 2>nul || echo No logs found'
                    archiveArtifacts artifacts: 'TestResults/**'
                }
            }
        }
    }
}
```

---

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `Unable to find type` error at runtime | Used `-test=MyControllerName` instead of `-gauntlet=MyControllerName` | Use `-test=DefaultTest` for C# TestNode and `-gauntlet=<ControllerName>` for C++ controller (no `U` prefix) |
| Build.cs `Gauntlet` module not found | Gauntlet plugin not enabled in `.uproject` or engine version too old | Verify `.uproject` has `{"Name": "Gauntlet", "Enabled": true}` in Plugins array |
| Exit code 3 (timeout) | Test duration exceeds `-timeout` value or `SetMaxDuration()` limit | Increase timeout in C++ `SetMaxDuration()` and RunUAT `-timeout=` parameter |

---

*Version: 2.3.0 | Extracted from SKILL.md for size optimization*
