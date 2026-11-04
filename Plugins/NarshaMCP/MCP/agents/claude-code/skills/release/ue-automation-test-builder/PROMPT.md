> 아래 내용을 복사하여 Claude에게 붙여넣으세요. (Copy and paste the following to Claude.)

# UE Automation Test Builder — User Prompt

아래 정보를 제공하면 더 정확한 테스트를 생성할 수 있습니다:

## 필수
- **테스트 대상**: 클래스명, 함수명, 또는 시스템 설명
  예: "ULyraHealthSet의 TakeDamage 함수", "인벤토리 추가/삭제"

## 선택
- **테스트 타입**: Simple / BDD Spec(기본) / Complex+Latent / Functional / EditorUtility
- **Edge cases**: 테스트하고 싶은 경계값 (0, negative, null, overflow 등)
- **의존성**: GAS, Enhanced Input 등 서브시스템 사용 여부
- **에디터**: Editor 연결 여부 (Offline이면 Phase 1-2만 진행)
- **프로젝트 경로**: 프로젝트 루트 (예: `D:/Lyra/LyraStarterGame`) — Phase 1 심볼 분석에 사용

## 예시 요청
- "ULyraHealthSet의 TakeDamage를 BDD Spec으로 테스트 만들어줘"
- "인벤토리 시스템에 대한 자동화 테스트 생성"
- "GA_Weapon_Fire ability의 activation flow를 functional test로"
