> 아래 내용을 복사하여 Claude에게 붙여넣으세요. (Copy and paste the following to Claude.)

# Gauntlet Orchestration — User Prompt

아래 정보를 제공하면 더 정확한 테스트 오케스트레이션을 수행할 수 있습니다:

## 필수
- **테스트 유형**: Smoke / Soak / Performance
  예: "게임 부팅 Smoke Test", "24시간 Soak Test"
- **프로젝트 경로**: .uproject 파일 경로
  예: "D:/MyGame/MyGame.uproject"

## 선택
- **플랫폼**: Win64(기본), PS5, XSX, Switch, iOS, Android
- **Configuration**: Development(기본), Shipping, Test
- **Timeout**: 테스트 제한 시간 (기본 120초, Soak은 14400+)
- **CI/CD**: GitHub Actions / Jenkins / 수동 실행
- **기존 테스트**: 이미 작성된 UGauntletTestController 클래스명
- **멀티플레이어**: 서버+클라이언트 수 (예: "1 server + 4 clients")

## 예시 요청
- "Lyra 프로젝트에 부팅 Smoke Test Gauntlet 설정해줘"
- "멀티플레이어 게임 5v5 Gauntlet 테스트 파이프라인"
- "Win64 + PS5에서 성능 벤치마크 GitHub Actions 워크플로우"
- "24시간 Soak Test로 메모리 누수 감지"
