# UE Automation Test Builder — Reference

UE5 Automation Test API 전체 레퍼런스. 5가지 테스트 타입, assertion 목록, flag 조합, 코드 템플릿.

---

## Table of Contents

1. [Test Types & Templates](#1-test-types--full-code-templates)
2. [Assertion API](#2-assertion-api)
3. [EAutomationTestFlags](#3-eautomationtestflags)
4. [Latent Commands](#4-latent-commands)
5. [BDD Spec Pattern](#5-bdd-spec-pattern-begin_define_spec)
6. [AFunctionalTest Lifecycle](#6-afunctionaltest-lifecycle)
7. [Screenshot Comparison](#7-screenshot-comparison)
8. [Console Commands](#8-console-commands--cli)
9. [Performance Testing](#9-performance-testing)
10. [Test Quality Scoring Rubric](#10-test-quality-5-criteria-scoring-rubric)
11. [MCP Tool Orchestration](#11-mcp-tool-orchestration-reference)
12. [Build.cs Dependencies](#12-buildcs-module-dependencies)

---

## 1. Test Types & Full Code Templates

### Type 1: Simple Automation Test

가장 기본적인 테스트. 단일 함수 호출 → 결과 검증.

```cpp
// File: Source/MyGame/Tests/MyCharacter_Health_Test.cpp

#include "Misc/AutomationTest.h"
#include "MyCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMyCharacter_TakeDamage_ReducesHP,                          // Class name
    "MyGame.Characters.MyCharacter.TakeDamage.ReducesHP",       // Test path (dot-separated)
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::ProductFilter)

bool FMyCharacter_TakeDamage_ReducesHP::RunTest(const FString& Parameters)
{
    // Arrange
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AMyCharacter* Character = World->SpawnActor<AMyCharacter>();
    float InitialHP = Character->GetHealth();

    // Act
    Character->TakeDamage(25.0f, FDamageEvent(), nullptr, nullptr);

    // Assert
    TestEqual(TEXT("HP should decrease by damage amount"),
              Character->GetHealth(), InitialHP - 25.0f);
    TestTrue(TEXT("Character should still be alive"),
             Character->IsAlive());

    // Cleanup
    World->DestroyWorld(false);
    return true;
}
```

### Type 2: Complex Automation Test + Latent Commands

멀티프레임 테스트. `ADD_LATENT_AUTOMATION_COMMAND`로 프레임을 넘겨서 비동기 동작을 테스트.

```cpp
// File: Source/MyGame/Tests/MyCharacter_Movement_Test.cpp

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "MyCharacter.h"

// --- Latent Command 정의 ---
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
    FWaitForCharacterLanded,
    AMyCharacter*, Character)

bool FWaitForCharacterLanded::Update()
{
    // Return true = 완료, false = 다음 프레임에 다시 호출
    if (!Character || !Character->IsValidLowLevel())
    {
        return true; // 캐릭터 무효 → 중단
    }
    return Character->GetCharacterMovement()->IsMovingOnGround();
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FVerifyCharacterPosition,
    FAutomationTestBase*, Test,
    AMyCharacter*, Character)

bool FVerifyCharacterPosition::Update()
{
    Test->TestTrue(TEXT("Character should have moved forward"),
                   Character->GetActorLocation().X > 0.0f);
    return true;
}

// --- 테스트 정의 ---
IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FMyCharacter_Jump_LandsOnGround,
    "MyGame.Characters.MyCharacter.Jump.LandsOnGround",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::ProductFilter)

void FMyCharacter_Jump_LandsOnGround::GetTests(
    TArray<FString>& OutBeautifiedNames,
    TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("Default Gravity"));
    OutTestCommands.Add(TEXT("1.0"));

    OutBeautifiedNames.Add(TEXT("Low Gravity"));
    OutTestCommands.Add(TEXT("0.5"));
}

bool FMyCharacter_Jump_LandsOnGround::RunTest(const FString& Parameters)
{
    float GravityScale = FCString::Atof(*Parameters);

    // 비동기 테스트: Latent Command 체인
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Tests/MAP_TestArena")));
    ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0f)); // 1초 대기
    // ... 캐릭터 스폰 후 점프 명령 ...
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCharacterLanded(Character));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyCharacterPosition(this, Character));

    return true;
}
```

### Type 3: BDD Spec (권장)

`BEGIN_DEFINE_SPEC`/`END_DEFINE_SPEC` 패턴. `Describe`/`It`/`BeforeEach`/`AfterEach`로 구조화.

```cpp
// File: Source/MyGame/Tests/MyCharacter_TakeDamage.spec.cpp

#include "Misc/AutomationTest.h"
#include "MyCharacter.h"

BEGIN_DEFINE_SPEC(FMyCharacter_TakeDamage_Spec,
    "MyGame.Characters.MyCharacter.TakeDamage",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::ProductFilter)

    UWorld* TestWorld;
    AMyCharacter* Character;
    float InitialHP;

END_DEFINE_SPEC(FMyCharacter_TakeDamage_Spec)

void FMyCharacter_TakeDamage_Spec::Define()
{
    BeforeEach([this]()
    {
        TestWorld = FAutomationEditorCommonUtils::CreateNewMap();
        Character = TestWorld->SpawnActor<AMyCharacter>();
        InitialHP = Character->GetHealth();
    });

    AfterEach([this]()
    {
        if (TestWorld)
        {
            TestWorld->DestroyWorld(false);
            TestWorld = nullptr;
        }
    });

    Describe("Normal Damage", [this]()
    {
        It("Should reduce HP by damage amount", [this]()
        {
            Character->TakeDamage(25.0f, FDamageEvent(), nullptr, nullptr);
            TestEqual(TEXT("HP reduced"), Character->GetHealth(), InitialHP - 25.0f);
        });

        It("Should not reduce HP below zero", [this]()
        {
            Character->TakeDamage(InitialHP + 100.0f, FDamageEvent(), nullptr, nullptr);
            TestTrue(TEXT("HP >= 0"), Character->GetHealth() >= 0.0f);
        });

        It("Should trigger death when HP reaches zero", [this]()
        {
            Character->TakeDamage(InitialHP, FDamageEvent(), nullptr, nullptr);
            TestTrue(TEXT("Character is dead"), !Character->IsAlive());
        });
    });

    Describe("Edge Cases", [this]()
    {
        It("Should handle zero damage", [this]()
        {
            Character->TakeDamage(0.0f, FDamageEvent(), nullptr, nullptr);
            TestEqual(TEXT("HP unchanged"), Character->GetHealth(), InitialHP);
        });

        It("Should handle negative damage", [this]()
        {
            Character->TakeDamage(-10.0f, FDamageEvent(), nullptr, nullptr);
            // 구현에 따라: 무시 또는 힐
            TestTrue(TEXT("HP not above max"),
                     Character->GetHealth() <= Character->GetMaxHealth());
        });
    });

    Describe("With Armor", [this]()
    {
        BeforeEach([this]()
        {
            Character->SetArmor(10.0f);
        });

        It("Should reduce damage by armor value", [this]()
        {
            Character->TakeDamage(25.0f, FDamageEvent(), nullptr, nullptr);
            TestEqual(TEXT("Damage reduced by armor"),
                      Character->GetHealth(), InitialHP - 15.0f);
        });
    });
}
```

### Type 4: Functional Test Actor

레벨에 배치하는 테스트 액터. PIE에서 실행되며 실제 게임 환경에서 테스트.

```cpp
// File: Source/MyGame/Tests/AFT_DamageTest.h
#pragma once

#include "FunctionalTest.h"
#include "AFT_DamageTest.generated.h"

UCLASS()
class MYGAME_API AAFT_DamageTest : public AFunctionalTest
{
    GENERATED_BODY()

public:
    AAFT_DamageTest();

protected:
    virtual void PrepareTest() override;
    virtual bool IsReady_Implementation() override;
    virtual void StartTest() override;

    UFUNCTION()
    void OnTestCharacterDamaged(float DamageAmount, float NewHP);

private:
    UPROPERTY()
    AMyCharacter* TestCharacter;

    UPROPERTY(EditAnywhere, Category = "Test Config")
    float DamageToApply = 25.0f;

    UPROPERTY(EditAnywhere, Category = "Test Config")
    float ExpectedHPAfterDamage = 75.0f;

    bool bCharacterReady = false;
};

// File: Source/MyGame/Tests/AFT_DamageTest.cpp
#include "AFT_DamageTest.h"

AAFT_DamageTest::AAFT_DamageTest()
{
    // 테스트 메타데이터
    Author = TEXT("QA Team");
    Description = TEXT("Verifies TakeDamage reduces HP correctly");
}

void AAFT_DamageTest::PrepareTest()
{
    Super::PrepareTest();

    // 테스트 환경 설정
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    TestCharacter = GetWorld()->SpawnActor<AMyCharacter>(
        AMyCharacter::StaticClass(),
        GetActorLocation() + FVector(200.f, 0.f, 100.f),
        FRotator::ZeroRotator,
        SpawnParams);

    if (TestCharacter)
    {
        TestCharacter->OnDamaged.AddDynamic(this, &AAFT_DamageTest::OnTestCharacterDamaged);
        bCharacterReady = true;
    }
}

bool AAFT_DamageTest::IsReady_Implementation()
{
    return bCharacterReady && TestCharacter != nullptr;
}

void AAFT_DamageTest::StartTest()
{
    Super::StartTest();

    if (!TestCharacter)
    {
        FinishTest(EFunctionalTestResult::Failed, TEXT("TestCharacter is null"));
        return;
    }

    // Act
    TestCharacter->TakeDamage(DamageToApply, FDamageEvent(), nullptr, nullptr);

    // Assert
    float ActualHP = TestCharacter->GetHealth();
    if (FMath::IsNearlyEqual(ActualHP, ExpectedHPAfterDamage, 0.01f))
    {
        FinishTest(EFunctionalTestResult::Succeeded,
                   FString::Printf(TEXT("HP correctly reduced to %.1f"), ActualHP));
    }
    else
    {
        FinishTest(EFunctionalTestResult::Failed,
                   FString::Printf(TEXT("Expected HP %.1f but got %.1f"),
                                   ExpectedHPAfterDamage, ActualHP));
    }
}

void AAFT_DamageTest::OnTestCharacterDamaged(float DamageAmount, float NewHP)
{
    LogMessage(FString::Printf(TEXT("Damage applied: %.1f, New HP: %.1f"),
                                DamageAmount, NewHP));
}
```

### Type 5: Editor Utility Test

Blueprint에서도 사용 가능한 테스트. `ExpectTrue`/`ExpectFalse` assertion.

```cpp
// File: Source/MyGame/Tests/UET_InventoryTest.h
#pragma once

#include "EditorUtilityTest.h"
#include "UET_InventoryTest.generated.h"

UCLASS(Blueprintable)
class MYGAME_API UUET_InventoryTest : public UEditorUtilityTest
{
    GENERATED_BODY()

public:
    UUET_InventoryTest();

protected:
    virtual void PrepareTest() override;
    virtual void StartTest() override;

    UPROPERTY(EditAnywhere, Category = "Test Config")
    TSubclassOf<UInventoryComponent> InventoryClass;
};

// File: Source/MyGame/Tests/UET_InventoryTest.cpp
#include "UET_InventoryTest.h"

UUET_InventoryTest::UUET_InventoryTest()
{
    Description = TEXT("Inventory system validation");
    TimeLimit = 10.0f;
}

void UUET_InventoryTest::PrepareTest()
{
    FinishPrepareTest(); // 준비 완료
}

void UUET_InventoryTest::StartTest()
{
    UInventoryComponent* Inventory = NewObject<UInventoryComponent>();

    // Test: Add item
    bool bAdded = Inventory->AddItem(TEXT("Sword"), 1);
    ExpectTrue(bAdded, TEXT("Should add item successfully"));
    ExpectTrue(Inventory->HasItem(TEXT("Sword")), TEXT("Should contain added item"));

    // Test: Stack items
    Inventory->AddItem(TEXT("Sword"), 2);
    ExpectTrue(Inventory->GetItemCount(TEXT("Sword")) == 3,
               TEXT("Should stack items"));

    // Test: Remove item
    bool bRemoved = Inventory->RemoveItem(TEXT("Sword"), 1);
    ExpectTrue(bRemoved, TEXT("Should remove item"));
    ExpectTrue(Inventory->GetItemCount(TEXT("Sword")) == 2,
               TEXT("Should decrease count"));

    FinishTest();
}
```

---

## 2. Assertion API

### Core Assertions (FAutomationTestBase)

모든 테스트 타입에서 사용 가능한 기본 assertion 메서드.

| Method | Signature | Description |
|--------|-----------|-------------|
| `TestTrue` | `TestTrue(const FString& What, bool Value)` | Value가 true인지 검증 |
| `TestFalse` | `TestFalse(const FString& What, bool Value)` | Value가 false인지 검증 |
| `TestEqual` | `TestEqual(const FString& What, T Actual, T Expected)` | 값이 동일한지 검증 |
| `TestNotEqual` | `TestNotEqual(const FString& What, T LHS, T RHS)` | 값이 다른지 검증 |
| `TestNull` | `TestNull(const FString& What, const void* Pointer)` | 포인터가 null인지 검증 |
| `TestNotNull` | `TestNotNull(const FString& What, const void* Pointer)` | 포인터가 non-null인지 검증 |
| `TestSame` | `TestSame(const FString& What, const T& A, const T& B)` | 같은 객체를 참조하는지 검증 |
| `TestInRange` | `TestInRange(const FString& What, T Value, T Min, T Max)` | 값이 범위 내인지 검증 |
| `AddError` | `AddError(const FString& Message)` | 에러 추가 (테스트 실패) |
| `AddWarning` | `AddWarning(const FString& Message)` | 경고 추가 (테스트 통과) |
| `AddInfo` | `AddInfo(const FString& Message)` | 정보 로그 (테스트 통과) |

### Float 비교 주의사항

```cpp
// BAD: float 정확 동치는 flaky test의 원인
TestEqual(TEXT("HP"), Character->GetHealth(), 75.0f);

// GOOD: 부동소수점 오차 허용
TestTrue(TEXT("HP approximately 75"),
         FMath::IsNearlyEqual(Character->GetHealth(), 75.0f, 0.01f));
```

### Enhanced Assertions (AutomationUtils Plugin)

Fluent assertion API. `ASSERT_THAT` 매크로로 체이닝.

```cpp
// Fluent assertion (requires AutomationUtils module)
ASSERT_THAT(Character->GetHealth()).Equals(75.0f, TEXT("HP after damage"));
ASSERT_THAT(Character->GetHealth()).Not().Equals(0.0f, TEXT("Not dead"));
ASSERT_THAT(Character).IsNotNull(TEXT("Character exists"));

// Float-specific
ASSERT_THAT_FLOAT(Distance).IsCloseTo(100.0f, 0.1f, TEXT("Near target"));
ASSERT_THAT_FLOAT(Speed).IsGreaterThan(0.0f, TEXT("Moving"));
ASSERT_THAT_FLOAT(HP).IsInRange(0.0f, MaxHP, TEXT("Valid HP range"));

// Collection
ASSERT_THAT_ARRAY(Inventory->GetItems()).Contains(Sword, TEXT("Has sword"));
ASSERT_THAT_ARRAY(Inventory->GetItems()).HasCount(3, TEXT("3 items"));
ASSERT_THAT_ARRAY(Inventory->GetItems()).IsEmpty(TEXT("Empty inventory"));
```

### EditorUtilityTest Assertions

```cpp
// UEditorUtilityTest에서 사용 가능
ExpectTrue(bool Condition, const FString& ErrorMessage);
ExpectFalse(bool Condition, const FString& ErrorMessage);
AddError(const FString& Message);
AddWarning(const FString& Message);
AddInfo(const FString& Message);
```

### AFunctionalTest Assertions

```cpp
// AFunctionalTest에서 사용 가능
AssertTrue(bool Condition, FString Message, UObject* ContextObject = nullptr);
AssertFalse(bool Condition, FString Message, UObject* ContextObject = nullptr);
AssertIsValid(UObject* Object, FString Message);
AssertEqual_Float(float Actual, float Expected, FString What, float Tolerance = 0.0001f);
AssertEqual_Int(int32 Actual, int32 Expected, FString What);
AssertEqual_String(FString Actual, FString Expected, FString What);
AssertEqual_Vector(FVector Actual, FVector Expected, FString What, float Tolerance = 0.0001f);
AssertEqual_Rotator(FRotator Actual, FRotator Expected, FString What, float Tolerance = 0.0001f);
AssertEqual_Transform(FTransform Actual, FTransform Expected, FString What, float Tolerance = 0.0001f);

LogMessage(FString Message);

FinishTest(EFunctionalTestResult TestResult, const FString& Message);
```

---

## 3. EAutomationTestFlags

### Flag Categories

#### Context Flags (테스트 실행 환경)

| Flag | Value | Description |
|------|-------|-------------|
| `EditorContext` | 0x00000001 | Editor 내에서 실행 (가장 일반적) |
| `ClientContext` | 0x00000002 | 클라이언트 모드에서 실행 |
| `ServerContext` | 0x00000004 | 서버 모드에서 실행 |
| `CommandletContext` | 0x00000008 | 커맨드렛에서 실행 |
| `ApplicationContextMask` | 0x0000000F | 모든 컨텍스트 |

#### Priority Flags (테스트 중요도)

| Flag | Value | Description |
|------|-------|-------------|
| `CriticalPriority` | 0x00000100 | 최고 우선순위 (핵심 기능) |
| `HighPriority` | 0x00000200 | 높은 우선순위 |
| `MediumPriority` | 0x00000400 | 중간 우선순위 |
| `LowPriority` | 0x00000800 | 낮은 우선순위 |

#### Filter Flags (테스트 분류)

| Flag | Value | Description | Use Case |
|------|-------|-------------|----------|
| `SmokeFilter` | 0x00010000 | 빠른 스모크 테스트 | CI에서 항상 실행 |
| `EngineFilter` | 0x00020000 | 엔진 레벨 테스트 | 엔진 수정 시 |
| `ProductFilter` | 0x00040000 | 게임/제품 테스트 | **대부분의 게임 테스트** |
| `PerfFilter` | 0x00080000 | 성능 테스트 | 성능 벤치마크 |
| `StressFilter` | 0x00100000 | 스트레스 테스트 | 부하/안정성 |
| `NegativeFilter` | 0x00200000 | 네거티브 테스트 | 에러 조건 검증 |

#### Feature Flags (추가 요구사항)

| Flag | Value | Description |
|------|-------|-------------|
| `NonNullRHI` | 0x01000000 | GPU 필요 (렌더링 테스트) |
| `RequiresUser` | 0x02000000 | 사용자 입력 필요 |
| `Disabled` | 0x10000000 | 비활성화 |

### 일반적인 Flag 조합

```cpp
// Unit Test (Editor, 제품 필터)
EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter

// Smoke Test (빠른 검증, CI용)
// Issue #27969: SmokeFilter runs inside FEngineLoop::PreInitPostStartupScreen
// (FAutomationTestFramework::RunSmokeTests), before UTypedElementRegistry
// registers 'Components'. Do not attach this filter to tests that construct
// actors, worlds, or subsystems — NewObject<A...> / SpawnActor will assert
// and the editor never reaches a window.
EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter

// Performance Test
EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter

// Stress Test (부하 테스트)
EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter

// Negative Test (에러 경로)
EAutomationTestFlags::EditorContext | EAutomationTestFlags::NegativeFilter | EAutomationTestFlags::ProductFilter

// Headless (CI, 렌더링 없음)
EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter

// Client/Server (네트워킹 테스트)
EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::ProductFilter

// Critical (핵심 기능, 최우선)
EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::CriticalPriority | EAutomationTestFlags::SmokeFilter
```

---

## 4. Latent Commands

멀티프레임 테스트를 위한 비동기 명령 체인.

### 매크로 정의

```cpp
// 파라미터 없음
DEFINE_LATENT_AUTOMATION_COMMAND(FMyCommand)
bool FMyCommand::Update()
{
    // return true = 완료, false = 다음 프레임 재시도
    return true;
}

// 1 파라미터
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FMyCommandWithParam, FString, Data)
bool FMyCommandWithParam::Update()
{
    UE_LOG(LogTemp, Log, TEXT("Data: %s"), *Data);
    return true;
}

// 2 파라미터
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FMyCommandWith2Params,
    ACharacter*, Character,
    float, ExpectedHP)
bool FMyCommandWith2Params::Update()
{
    return FMath::IsNearlyEqual(Character->GetHealth(), ExpectedHP, 0.01f);
}
```

### 내장 Latent Commands

```cpp
// 시간 대기
ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(2.0f)); // 2초

// 맵 로드
ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/TestArena")));

// 콘솔 명령 실행
ADD_LATENT_AUTOMATION_COMMAND(FExecStringLatentCommand(TEXT("stat fps")));

// 조건 대기 (타임아웃 포함)
ADD_LATENT_AUTOMATION_COMMAND(FWaitUntilCommand(
    [Character]() { return Character->IsMovingOnGround(); },
    10.0f)); // 10초 타임아웃
```

### 체이닝 패턴

Latent Command들은 순차적으로 실행됩니다. 앞 명령의 `Update()`가 `true`를 반환해야 다음 명령 시작.

```cpp
bool FMyAsyncTest::RunTest(const FString& Parameters)
{
    // 1. 맵 로드 (완료될 때까지 대기)
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Tests/Arena")));

    // 2. 1초 대기 (맵 안정화)
    ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(1.0f));

    // 3. 캐릭터 스폰 및 설정
    ADD_LATENT_AUTOMATION_COMMAND(FSpawnTestCharacter(this));

    // 4. 액션 실행
    ADD_LATENT_AUTOMATION_COMMAND(FApplyDamageCommand(this, 25.0f));

    // 5. 프레임 대기 (데미지 처리)
    ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.1f));

    // 6. 결과 검증
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyHealthCommand(this, 75.0f));

    return true; // Latent 모드 시작
}
```

### FScreenshotRequest (PIE 게임 뷰포트 캡처)

`capture_viewport_analyze`(에디터 뷰포트)와 달리, `FScreenshotRequest`는 **PIE 게임 뷰포트**를 캡처합니다.
Complex+Latent 테스트에서 Latent Command 내부에서 사용:

```cpp
#include "UnrealClient.h"

// PIE 실행 중 게임 화면 캡처 (비동기, 다음 프레임에 저장)
FScreenshotRequest::RequestScreenshot(
    TEXT("TestName_01_Description"),  // 파일명 (Saved/Screenshots/ 하위)
    false,  // bShowUI
    false   // bAddUniqueSuffix
);
```

### Multi-Frame Input Simulation (Hold Button)

1프레임 Press→Release는 시각적으로 보이지 않음. 지속 시간 동안 입력을 유지하는 패턴:

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHoldFireCommand, FAutomationTestBase*, Test);
bool FHoldFireCommand::Update()
{
    // ... ASC, FireTag 유효성 검사 ...

    if (PhaseStartTime == 0.0)
    {
        PhaseStartTime = FPlatformTime::Seconds();
        ASC->AbilityInputTagPressed(FireTag);  // 누르기 시작
    }

    ASC->ProcessAbilityInput(0.016f, false);  // 매 틱 입력 처리 유지

    const double Elapsed = FPlatformTime::Seconds() - PhaseStartTime;

    if (Elapsed >= 0.5 && !bScreenshotTaken)
    {
        FScreenshotRequest::RequestScreenshot(TEXT("WeaponFired"), false, false);
        bScreenshotTaken = true;
    }

    if (Elapsed >= 2.0)
    {
        ASC->AbilityInputTagReleased(FireTag);  // 릴리즈
        return true;  // 완료
    }
    return false;  // 계속
}
```

**핵심**: `AbilityInputTagPressed` → 매 틱 `ProcessAbilityInput` → 시간 후 `AbilityInputTagReleased`

---

## 5. BDD Spec Pattern (BEGIN_DEFINE_SPEC)

### 구조

```cpp
BEGIN_DEFINE_SPEC(FClassName, "TestPath.Dot.Separated", Flags)
    // 멤버 변수 (모든 테스트에서 공유)
    UWorld* TestWorld;
    AMyActor* TestActor;
END_DEFINE_SPEC(FClassName)

void FClassName::Define()
{
    // BeforeEach: 각 It 실행 전 호출
    BeforeEach([this]() { /* setup */ });

    // AfterEach: 각 It 실행 후 호출
    AfterEach([this]() { /* teardown */ });

    // Describe: 논리적 그룹
    Describe("Group Name", [this]()
    {
        // 중첩 BeforeEach 가능
        BeforeEach([this]() { /* group-specific setup */ });

        // It: 개별 테스트 케이스
        It("should do something", [this]()
        {
            // assertions
            TestTrue(TEXT("condition"), true);
        });

        // 중첩 Describe 가능
        Describe("Sub Group", [this]()
        {
            It("should handle edge case", [this]()
            {
                // ...
            });
        });
    });
}
```

### BeforeEach/AfterEach 실행 순서

```text
[Top-level BeforeEach]
  [Describe-level BeforeEach]
    [It: test execution]
  [Describe-level AfterEach]
[Top-level AfterEach]
```

### Latent Commands와 함께 사용

```cpp
void FMySpec::Define()
{
    LatentBeforeEach([this](const FDoneDelegate& Done)
    {
        // 비동기 setup
        AsyncLoadLevel(TEXT("/Game/Tests/Arena"), [Done]()
        {
            Done.Execute(); // setup 완료 신호
        });
    });

    LatentIt("should handle async operation", [this](const FDoneDelegate& Done)
    {
        // 비동기 테스트
        StartAsyncOperation([this, Done]()
        {
            TestTrue(TEXT("Operation succeeded"), true);
            Done.Execute(); // 테스트 완료 신호
        });
    });
}
```

---

## 6. AFunctionalTest Lifecycle

### 실행 흐름

```text
PrepareTest()          ← 환경 설정 (액터 스폰, 레벨 구성)
       │
       ↓
IsReady_Implementation()  ← 준비 완료 확인 (매 프레임 체크)
       │                     false → 다음 프레임 재시도
       ↓ (true)              타임아웃 → 실패
StartTest()            ← 테스트 로직 실행
       │
       ↓
FinishTest(Result, Msg) ← 결과 보고
       │
       ↓
Cleanup                ← 자동 정리
```

### Key Overrideable Methods

```cpp
// 환경 설정 (필수)
virtual void PrepareTest() override;

// 준비 상태 확인 (선택)
virtual bool IsReady_Implementation() override;

// 테스트 실행 (필수)
virtual void StartTest() override;

// 테스트 완료 후 정리 (선택)
virtual void OnTestFinished(EFunctionalTestResult TestResult) override;
```

### EFunctionalTestResult

```cpp
enum class EFunctionalTestResult : uint8
{
    Default,     // 기본 (사용하지 않음)
    Invalid,     // 무효
    Error,       // 에러 발생
    Running,     // 실행 중
    Failed,      // 실패
    Succeeded    // 성공
};
```

### Properties

```cpp
// Blueprint에서 편집 가능한 속성
UPROPERTY(EditAnywhere, Category = "FunctionalTest")
FString Author;                        // 테스트 작성자

UPROPERTY(EditAnywhere, Category = "FunctionalTest")
FString Description;                   // 테스트 설명

UPROPERTY(EditAnywhere, Category = "FunctionalTest")
float TimeLimit = 60.0f;              // 타임아웃 (초)

UPROPERTY(EditAnywhere, Category = "FunctionalTest")
float PreparationTimeLimit = 60.0f;   // PrepareTest 타임아웃

UPROPERTY(EditAnywhere, Category = "FunctionalTest")
TArray<FName> Tags;                    // 테스트 태그 (필터용)
```

---

## 7. Screenshot Comparison

시각적 회귀 테스트를 위한 스크린샷 비교.

### Comparison Algorithms

| Algorithm | Speed | Accuracy | Use Case |
|-----------|-------|----------|----------|
| `PixelDiff` | Fast | High | Pixel-perfect UI 검증 |
| `Perceptual` | Medium | Medium | 시각적으로 유사한지 (사람 눈 기준) |
| `SSIM` | Slow | Very High | 구조적 유사도 (과학적 비교) |
| `Histogram` | Fast | Low | 색상 분포 비교 |

### Config

```cpp
FScreenshotComparisonConfig Config;
Config.Algorithm = EScreenshotComparisonAlgorithm::PixelDiff;
Config.Tolerance = 0.02f;           // 2% 오차 허용
Config.bGenerateDiffImage = true;    // 차이 이미지 생성
Config.DiffHighlightColor = FLinearColor::Red;
Config.bIgnoreUI = false;           // UI 포함
Config.RegionOfInterest = FBox2D(FVector2D(0, 0), FVector2D(1920, 1080));
```

### Usage Pattern

```cpp
// In test
UScreenshotComparisonSubsystem* ScreenshotSystem =
    GEngine->GetEngineSubsystem<UScreenshotComparisonSubsystem>();

// 1. Baseline 생성 (최초 1회)
ScreenshotSystem->CreateBaseline(TEXT("DamageVFX_Test"));

// 2. 이후 테스트에서 비교
FScreenshotComparisonResult Result;
bool bPassed = ScreenshotSystem->CaptureAndCompare(
    TEXT("DamageVFX_Test"), Config, Result);

// 3. 결과 분석
if (!bPassed)
{
    AddError(FString::Printf(TEXT("Visual diff: %.1f%% different"),
                              Result.DifferencePercentage));
}
```

---

## 8. Console Commands & CLI

### Editor Console Commands

```text
Automation RunTests <TestName>          # 특정 테스트 실행
Automation RunTests <Prefix>            # 프리픽스 매칭 (하위 모두 실행)
Automation List                         # 등록된 테스트 목록
Automation RunFilter Smoke              # 필터별 실행 (Smoke/Product/Perf/Stress)
Automation SetMinimumPriority High      # 최소 우선순위 설정
```

### 커맨드라인 (Headless, CI용)

```bash
# 기본 실행 (NullRHI = 렌더링 없음)
UnrealEditor-Cmd.exe MyGame.uproject \
  -ExecCmds="Automation RunTests MyGame;Quit" \
  -NullRHI -Unattended -NoSplash

# 결과 JSON 내보내기
UnrealEditor-Cmd.exe MyGame.uproject \
  -ExecCmds="Automation RunTests MyGame;Quit" \
  -ReportExportPath="TestResults/" \
  -NullRHI -Unattended

# 특정 필터만 실행
UnrealEditor-Cmd.exe MyGame.uproject \
  -ExecCmds="Automation RunFilter Smoke;Quit" \
  -NullRHI

# Gauntlet 실행 (RunUAT.bat)
RunUAT.bat RunUnreal \
  -project=MyGame.uproject \
  -test=SmokeTestController \
  -build=Editor \
  -platform=Win64
```

### MCP를 통한 실행

```python
# Console Command
ue_editor_automation(operation="execute_console_command",
    command="Automation RunTests MyGame.Characters.TakeDamage")

# Python으로 직접 제어
ue_editor_automation(operation="execute_python", code="""
import unreal
# 테스트 실행
unreal.AutomationLibrary.run_automation_tests("MyGame.Characters")
""")

# 에러 로그 수집 (WS 경로만 사용)
ue_editor_automation(operation="get_error_log",
    max_entries=200, severity_filter="all")
```

### FPS Throttle Workaround

> **v1.1.0+**: `run_automation_tests`는 BenchmarkMode + DefaultInteractiveFramerate를 자동 설정/복원.
> 아래 수동 설정은 `execute_console_command` 경로 사용 시에만 필요.

에디터가 백그라운드일 때 CPU 쓰로틀 (3fps)이 발동하면
`FWaitForInteractiveFrameRate`가 600초까지 대기합니다:

```ini
; DefaultEditorPerProjectUserSettings.ini
[/Script/UnrealEd.EditorPerProjectUserSettings]
bThrottleCPUWhenNotForeground=False
```

또는 콘솔 커맨드:
```text
t.MaxFPS 0
Slate.bAllowThrottling 0
```

**최선책**: 테스트 실행 중 에디터 창을 **포그라운드**로 유지

---

## 9. Performance Testing

### FPerformanceMetrics

```cpp
struct FPerformanceMetrics
{
    float AverageFPS;
    float MinFPS;
    float MaxFPS;
    float P99FPS;
    float AverageFrameTimeMs;
    float P99FrameTimeMs;
    int32 HitchCount;         // 프레임 > 33ms
    float AverageMemoryMB;
    float PeakMemoryMB;
    int32 SampleCount;
};
```

### 스코프 기반 성능 측정

```cpp
FPerformanceMetrics Metrics;

for (int32 i = 0; i < 100; ++i)
{
    {
        FPerformanceMeasurement Measurement(Metrics);
        // 이 스코프 내 코드 자동 측정
        MyFunction();
    } // 소멸자에서 측정 기록
}

// 결과 분석
TestTrue(TEXT("Average < 16ms"),
         Metrics.TotalTimeMs / Metrics.Iterations < 16.0);
```

### Soak Test (장기 안정성)

```cpp
// 4시간 안정성 테스트
struct FSoakTestSnapshot
{
    float TimestampSeconds;
    float MemoryUsageMB;
    float AverageFPS;
    int32 ErrorCount;
    float GPUMemoryMB;
};

// 기준:
// - 메모리 누수: < 10 MB/hour
// - FPS 저하: < 5%/hour
// - 에러 증가: 0 new errors
```

### Report Formats

| Format | Extension | Use Case |
|--------|-----------|----------|
| JUnit XML | `.xml` | CI/CD (Jenkins, GitHub Actions) |
| HTML | `.html` | 사람이 읽는 리포트 |
| JSON | `.json` | 머신 파싱 / MCP 결과 분석 |
| CSV | `.csv` | 스프레드시트 / 데이터 분석 |

---

## 10. Test Quality 5-Criteria Scoring Rubric

SKILL.md Phase 5에서 사용하는 테스트 품질 평가 기준.

### Criterion 1: Test Coverage (30%)

| Score | Description |
|-------|-------------|
| 5 | 모든 public 함수 + 분기 + edge case 커버. boundary value, null, overflow 포함 |
| 4 | 대부분 함수/분기 커버. 주요 edge case 포함 |
| 3 | 핵심 함수만 커버. edge case 일부 누락 |
| 2 | Happy path만 테스트. edge case 대부분 누락 |
| 1 | 1-2개 함수만 단순 테스트 |

### Criterion 2: Assertion Quality (25%)

| Score | Description |
|-------|-------------|
| 5 | 다양한 assertion 타입 (TestEqual, TestTrue, TestNull, TestInRange). 의미 있는 메시지. float 오차 처리 |
| 4 | 3종 이상 assertion. 대부분 의미 있는 메시지 |
| 3 | TestTrue/TestEqual 위주. 메시지 양호 |
| 2 | TestTrue만 사용. 메시지 불충분 |
| 1 | assertion 부족 또는 무의미한 검증 |

### Criterion 3: Test Isolation (20%)

| Score | Description |
|-------|-------------|
| 5 | 완전한 setup/teardown. 외부 의존성 없음. 테스트 간 상태 공유 없음 |
| 4 | BeforeEach/AfterEach 사용. 대부분 격리 |
| 3 | 기본 cleanup 있음. 일부 상태 누출 가능 |
| 2 | cleanup 불완전. 테스트 순서 의존성 |
| 1 | 격리 없음. 글로벌 상태 직접 수정 |

### Criterion 4: Readability (15%)

| Score | Description |
|-------|-------------|
| 5 | Arrange-Act-Assert 패턴. 명확한 테스트 이름 (Method_Condition_Expected). BDD Describe/It 구조 |
| 4 | AAA 패턴 대부분 준수. 이름 명확 |
| 3 | 구조 있음. 이름 보통 |
| 2 | 구조 불명확. 이름이 테스트 내용 미반영 |
| 1 | 읽기 어려움. 의도 파악 불가 |

### Criterion 5: Robustness (10%)

| Score | Description |
|-------|-------------|
| 5 | Flaky 방지 (float 오차 처리, 타이밍 독립, 환경 독립). deterministic |
| 4 | 대부분 robust. 타이밍 의존성 최소 |
| 3 | 일부 flaky 가능성. 기본 오차 처리 |
| 2 | 타이밍/환경 의존적. flaky 발생 가능 |
| 1 | 매 실행 결과 다름. 근본적으로 불안정 |

### Score Calculation

```text
Final Score = (Coverage × 0.30) + (Assertion × 0.25) + (Isolation × 0.20)
            + (Readability × 0.15) + (Robustness × 0.10)
```

### Score Interpretation

| Range | Action | Description |
|-------|--------|-------------|
| **4.0 - 5.0** | **PASS** | 품질 충분. 완료 처리 |
| **3.0 - 3.9** | **IMPROVE** | 구체적 개선 제안 → Phase 2로 돌아가 수정 |
| **< 3.0** | **REWRITE** | 근본 문제. Phase 1로 돌아가 재분석 |

---

## 11. MCP Tool Orchestration Reference

### Phase별 MCP 도구 매핑

| Phase | Tool | Operation | Purpose |
|-------|------|-----------|---------|
| **1. 분석** | `ue_analyze_symbols` | `search_symbols` | 클래스/함수 시그니처 |
| **1. 분석** | `ue_analyze_symbols` | `find_callers` | 함수 호출자 추적 |
| **1. 분석** | `ue_search_assets` | (query) | 관련 에셋 탐색 |
| **1. 분석** | `ue_manage_blueprint` | `get_structure` | BP 구조 분석 |
| **2. 생성** | `ue_generate_code` | `derive_class` | AFunctionalTest 서브클래스 |
| **2. 환경** | `ue_editor_*` | `load_level` | 테스트 레벨 로드 |
| **2. 환경** | `ue_editor_*` | `spawn_actor` | 테스트 액터 배치 |
| **2. 환경** | `ue_batch_editor_operations` | (batch) | 다수 액터 일괄 배치 |
| **3. 빌드** | `ue_editor_*` | `execute_python` | LiveCoding.compile() |
| **3. 빌드** | `ue_editor_*` | `compile_project` | C++ 빌드 (구현 시) |
| **3. 실행** | `ue_editor_*` | `run_automation_tests` | **테스트 실행 + 폴링 + 결과 파싱 (권장)** |
| **3. 실행** | `ue_editor_*` | `list_automation_tests` | 등록된 테스트 목록 조회 |
| **3. 실행** | `ue_editor_*` | `execute_console_command` | Automation RunTests (수동 fallback) |
| **3. 실행** | `ue_editor_*` | `play_in_editor` | Functional Test PIE |
| **3. 중지** | `ue_editor_*` | `stop_play` | PIE 종료 |
| **4. 결과** | `ue_editor_*` | `get_error_log` | 로그 수집 (수동 파싱 시) |
| **4. 결과** | `ue_editor_*` | `execute_python` | 결과 파일 파싱 |
| **4. 시각** | `ue_editor_*` | `capture_viewport_analyze` | 시각적 검증 |
| **수정** | `ue_fix_errors` | `smart` | 컴파일 에러 자동 수정 |

### 도구별 제약 사항

| Tool/Operation | Protocol | Timeout | Notes |
|----------------|----------|---------|-------|
| `execute_console_command` | WS (Python wrap) | 5s | Python으로 `unreal.SystemLibrary.execute_console_command()` 호출 |
| `execute_python` | WS | 30s + buffer | 긴 작업은 timeout 주의 |
| `get_error_log` (WS) | WS | default | 정상 동작 |
| `get_error_log` (RC) | RC | - | **스텁 — 사용 불가** |
| `compile_blueprints` | WS | 60s | BP 컴파일만 (C++ 아님) |
| `play_in_editor` | RC HTTP | 20s | PIE 시작 |
| `stop_play` | RC HTTP | 10s | PIE 종료 |
| `spawn_actor` | WS (Python) | default | Native class → BP fallback |
| `run_automation_tests` | WS (Python+Poll) | 120s (max 600s) | trigger→poll→parse 파이프라인 |
| `list_automation_tests` | WS (Python) | 30s | AutomationController 쿼리 |

### run_automation_tests 오퍼레이션

3-Step Pipeline (trigger → poll → parse)으로 Automation Test를 실행하고 결과를 구조화하여 반환.

**Parameters**:

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `test_name` | string | **Yes** | - | 테스트 이름 또는 프리픽스 (예: `"MyGame.Characters.TakeDamage"`) |
| `timeout_seconds` | integer | No | 120 | 최대 대기 시간 (max 600) |
| `poll_interval_seconds` | integer | No | 3 | 폴링 간격 (min 1) |
| `wait_for_completion` | boolean | No | true | false면 트리거만 하고 즉시 반환 |

**Return Schema**:

```json
{
  "success": true,
  "operation": "run_automation_tests",
  "test_name": "MyGame.Characters.TakeDamage",
  "test_status": "completed",
  "tests_run": 4,
  "passed": 3,
  "failed": 1,
  "expected_tests": 4,
  "test_count_match": true,
  "results": [
    {
      "name": "TakeDamage.ReducesHP",
      "status": "passed",
      "duration_ms": 1250,
      "log_line_span": 15,
      "test_index": 0
    },
    {
      "name": "TakeDamage.ZeroDamage",
      "status": "failed",
      "message": "HP unchanged assertion failed",
      "duration_ms": 890,
      "log_line_span": 12,
      "test_index": 1
    }
  ],
  "poll_attempts": 5,
  "log_snippet": "...(last 2000 chars of LogAutomationController)",
  "total_time_ms": 15200.0
}
```

**New Fields (v1.1.0)**:

| Field | Type | Optional | Description |
|-------|------|----------|-------------|
| `expected_tests` | u32 | No | Pre-flight `list_automation_tests` count (0 if query fails) |
| `test_count_match` | bool | No | `tests_run == expected_tests` |
| `results[].duration_ms` | i64 | Yes | BeginEvents→EndEvents 실제 경과 시간 (ms, chrono) |
| `results[].log_line_span` | usize | Yes | BeginEvents→EndEvents 로그 라인 수 (fallback) |
| `results[].test_index` | usize | No | 0-based 결과 순서 인덱스 |

**test_status values**: `completed` (정상 완료), `triggered` (wait_for_completion=false), `timeout` (시간 초과), `trigger_failed` (Editor 오프라인)

**Usage**:

```python
# 기본 사용 (권장)
ue_editor_automation(operation="run_automation_tests",
    test_name="MyGame.Characters.TakeDamage")

# Fire-and-forget (트리거만)
ue_editor_automation(operation="run_automation_tests",
    test_name="MyGame.Characters", wait_for_completion=false)

# 긴 테스트 (타임아웃 확장)
ue_editor_automation(operation="run_automation_tests",
    test_name="MyGame", timeout_seconds=300)
```

### list_automation_tests 오퍼레이션

Editor에 등록된 Automation Test 목록을 조회.

**Parameters**:

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `filter` | string | No | - | Substring filter (contains-match on path or display name) |

**Return Schema**:

```json
{
  "success": true,
  "operation": "list_automation_tests",
  "result": {
    "tests": [
      {"test_path": "MyGame.Characters.TakeDamage", "display_name": "Take Damage", "tags": ""},
      {"test_path": "MyGame.Characters.Movement", "display_name": "Movement", "tags": ""}
    ],
    "total_count": 2,
    "returned_count": 2,
    "truncated": false
  },
  "groups": {
    "MyGame.Characters": 2
  },
  "filter_mode": "contains",
  "method": "websocket_native",
  "total_time_ms": 45.0
}
```

**Response Fields**:

| Field | Description |
|-------|-------------|
| `result.tests[]` | 매칭된 테스트 배열 (`test_path`, `display_name`, `tags`) |
| `result.total_count` | 필터 매칭된 총 테스트 수 |
| `groups` | test_path 계층 기반 그룹 요약 (parent → count) |
| `filter_mode` | `"contains"` — path 또는 display_name에 substring 매치 |
| `method` | `"websocket_native"` — 사용된 통신 방식 |

**제한사항**: 최대 `max_results=500`개까지 반환. 초과 시 `truncated: true`.

**Usage**:

```python
# 전체 목록
ue_editor_automation(operation="list_automation_tests")

# 필터 적용
ue_editor_automation(operation="list_automation_tests", filter="MyGame.Characters")
```

---

## 12. Build.cs Module Dependencies

테스트 모듈에 필요한 Build.cs 의존성.

### Basic Automation Test

```csharp
// Source/MyGame/MyGame.Build.cs
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
});
```

### Functional Test

```csharp
// 추가 필요
PublicDependencyModuleNames.AddRange(new string[] {
    "FunctionalTesting",       // AFunctionalTest
});
```

### GAS + Test

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "GameplayAbilities",       // UAbilitySystemComponent
    "GameplayTags",            // FGameplayTag
    "GameplayTasks",           // UAbilityTask
});
```

### Enhanced Input + Test

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "EnhancedInput",           // UInputAction, UInputMappingContext
});
```

### Editor Utility Test

```csharp
// Editor-only 모듈에서만 사용
if (Target.bBuildEditor)
{
    PrivateDependencyModuleNames.Add("EditorTests");
    PrivateDependencyModuleNames.Add("UnrealEd");
}
```

### Automation Utils (Enhanced Assertions)

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "AutomationUtils",         // ASSERT_THAT macros
});
```

### PIE-Based Complex+Latent Test (FEditorLoadMap, FStartPIECommand)

```csharp
// FEditorLoadMap, FStartPIECommand, FEndPlayMapCommand 사용 시:
if (Target.bBuildEditor)
{
    PrivateDependencyModuleNames.Add("UnrealEd");
}
```

`#include "Tests/AutomationEditorCommon.h"` — UnrealEd 모듈 필요

### Gauntlet

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Gauntlet",                // UGauntletTestController
});
```

---

### create_test_level 오퍼레이션

격리된 테스트 전용 레벨을 생성. 기존 맵 의존성(Experience, 서브레벨 등)이 복잡할 때 사용.

**Parameters**:

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `level_name` | string | No | `"TestLevel"` | 레벨 이름 (영숫자 + 밑줄만 허용) |
| `include_floor` | bool | No | `true` | 바닥 메시 포함 여부 |
| `include_player_start` | bool | No | `true` | PlayerStart 포함 여부 |
| `include_lighting` | bool | No | `true` | DirectionalLight + SkyLight 포함 여부 |

**제약사항**:
- `level_name`은 영숫자와 밑줄(`_`)만 허용 (특수문자/공백 불가)
- 생성 경로: `/Game/Tests/{level_name}`
- 바닥 메시: `/Engine/BasicShapes/Plane` (scale `50,50,1`)

**Usage**:

```python
# 기본 테스트 레벨 생성
ue_editor_automation(operation="create_test_level")

# 커스텀 이름 + 조명 없이
ue_editor_automation(operation="create_test_level",
    level_name="DamageTestMap", include_lighting=false)
```

---

## Lyra/GAS Test Patterns (Issue #6796)

프로젝트에서 GAS/Experience 시스템이 감지되면 아래 패턴을 자동 적용합니다.
Step 1.1b (패턴 발견)에서 감지된 경우에만 활성화됩니다.

### PIE-Based Test Pattern

PIE(Play In Editor)에서 실행되는 통합 테스트에 필요한 공통 패턴:

```cpp
// 필수 include (프로젝트 헬퍼 존재 시)
#include "LyraTestHelpers.h"  // FindLyraPIEWorld(), FLyraSetWorldExperience

// PIE World 발견
UWorld* PIEWorld = FindLyraPIEWorld();  // GEngine->GetWorldContexts() 순회

// Experience 설정 (FProperty reflection bypass)
// ALyraWorldSettings의 DefaultGameplayExperience는 EditDefaultsOnly → 직접 설정 불가
// FProperty::ImportText_Direct로 우회
ADD_LATENT_AUTOMATION_COMMAND(FLyraSetWorldExperience(TEXT("TestName")));
```

### GAS Input Simulation Pattern

GAS 기반 입력 시뮬레이션 (Jump, Attack, Dash 등):

```cpp
// 1. InputTag 가져오기
FGameplayTag JumpTag = FGameplayTag::RequestGameplayTag(FName("InputTag.Jump"), false);

// 2. 입력 시작 (AbilityInputTagPressed)
ASC->AbilityInputTagPressed(JumpTag);

// 3. 매 틱: ProcessAbilityInput 호출 (ability 실행 유지)
ASC->ProcessAbilityInput(0.016f, false);

// 4. 입력 해제 (0.3초 후)
ASC->AbilityInputTagReleased(JumpTag);
```

**중요**: `Character->Jump()` 직접 호출은 GAS ability 시스템을 우회합니다.
Lyra 프로젝트에서는 반드시 InputTag 방식을 사용하세요.

### Latent Command Building Blocks

재사용 가능한 latent command 패턴:

| Command | 용도 | 핵심 로직 |
|---------|------|-----------|
| `FWaitForCharacterReady` | 캐릭터 + ASC 준비 대기 | PIE World → PC → Pawn → ASC 체인 확인 |
| `FMoveForward` | 직선 이동 + 거리 검증 | `AddMovementInput(ForwardDir, 1.0f)` + `FVector::Dist()` |
| `FPerformJump` | GAS 점프 + 착지 검증 | InputTag → IsFalling() → MaxZ 추적 → !IsFalling() |
| `FFireWeapon` | GAS 사격 2초 | InputTag.Weapon.Fire → ProcessAbilityInput → Released |
| `FVerifyResults` | 결과 검증 | AddInfo/AddError로 PASS/FAIL 보고 |

### 공유 상태 구조체 패턴

```cpp
struct FTestState {
    // 공통 필드
    FVector StartPosition = FVector::ZeroVector;
    bool bStartPositionRecorded = false;
    double PhaseStartTime = 0.0;

    // 캐시된 참조 (WeakObjectPtr로 GC 안전)
    TWeakObjectPtr<APawn> CachedPawn;
    TWeakObjectPtr<APlayerController> CachedPC;
    TWeakObjectPtr<ULyraAbilitySystemComponent> CachedASC;

    // 상수
    static constexpr float TargetDistanceCm = 1000.0f;  // 10m
    static constexpr float CharReadyTimeoutSec = 60.0f;
    static constexpr float MovementTimeoutSec = 30.0f;

    void Reset() { /* 모든 필드 초기화 */ }
};
static FTestState GState;
```

### RunTest 시퀀스 패턴 (Complex+Latent)

```cpp
bool FMyTest::RunTest(const FString& Parameters)
{
    GState.Reset();

    // 1. 레벨 로드
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Parameters));

    // 2. Experience 설정 (Lyra 프로젝트)
    ADD_LATENT_AUTOMATION_COMMAND(FLyraSetWorldExperience(TEXT("MyTest")));

    // 3. PIE 시작
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));

    // 4. 안정화 대기
    ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(3.0f));

    // 5. 캐릭터 준비 대기
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCharacterReady());

    // 6-N. 테스트 액션 (이동, 점프, 사격 등)
    ADD_LATENT_AUTOMATION_COMMAND(FMyTestAction(this));

    // N+1. 결과 검증
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyResults(this));

    // N+2. PIE 종료
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());

    return true;
}
```

---

## Related

- [SKILL.md](SKILL.md) — 6-Phase iterative cycle 정의
- [EXAMPLES.md](EXAMPLES.md) — 8개 완전한 워크스루
- [ADVANCED.md](ADVANCED.md) — Gauntlet, CI/CD, 성능 테스트
