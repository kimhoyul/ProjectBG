# Doc Progressive Explorer - API Reference

**Version**: 1.9.0
**Purpose**: Navigation, Auto-Level Selection, 및 Caching API 완전 레퍼런스

---

## 📑 Table of Contents

1. [Navigation State Model](#navigation-state-model)
2. [Navigation Commands](#navigation-commands)
   - [cmd_level](#cmd_level---전체-문서-레벨-조정)
   - [cmd_drill](#cmd_drill---섹션-드릴다운)
   - [cmd_focus](#cmd_focus---키워드-포커스)
   - [cmd_back / cmd_history](#cmd_back--cmd_history---히스토리-관리)
3. [Auto-Level Selection API](#auto-level-selection-api)
   - [DocumentComplexity](#documentcomplexity)
   - [QueryIntent](#queryintent)
   - [recommend_initial_level](#recommend_initial_level)
4. [Caching API](#caching-api)
5. [Cross-Reference API](#cross-reference-api)

---

## Navigation State Model

```python
@dataclass
class NavigationState:
    """문서 탐색 상태를 추적하는 클래스"""
    document_path: str              # 현재 문서 경로
    current_level: int = 0          # 전체 문서 레벨 (0-4)
    section_levels: Dict[str, int] = field(default_factory=dict)  # 섹션별 레벨
    focused_keyword: Optional[str] = None  # 활성 키워드 필터
    history: List[str] = field(default_factory=list)  # 탐색 히스토리

    def __post_init__(self):
        self.history.append(f"open:{self.document_path}@L{self.current_level}")

# 전역 상태 (세션 내 유지)
_nav_state: Optional[NavigationState] = None
```

---

## Navigation Commands

### `cmd_level` - 전체 문서 레벨 조정

```python
def cmd_level(action: str, state: NavigationState) -> NavigationState:
    """
    전체 문서의 레벨을 조정합니다.

    Commands:
        level up     → 레벨 +1 (더 상세히)
        level down   → 레벨 -1 (더 요약)
        level <n>    → 특정 레벨로 이동 (0-4)
        level reset  → 기본값(0)으로 복원

    Args:
        action: 'up', 'down', '0'-'4', 'reset'
        state: 현재 네비게이션 상태

    Returns:
        업데이트된 NavigationState

    Performance:
        Target: <500ms (즉시 응답)
    """
```

**Usage Examples**:
```bash
level up       # Level 0 → Level 1
level down     # Level 2 → Level 1
level 3        # 직접 Level 3으로 이동
level reset    # Level 0으로 초기화 + 섹션별 레벨 클리어
```

**Level Indicator Rendering**:
```python
def render_level_indicator(level: int) -> str:
    """현재 레벨을 시각적으로 표시"""
    levels = ['○', '○', '○', '○', '○']  # 5개 빈 원
    levels[level] = '●'  # 현재 레벨은 채워진 원

    labels = ['Summary', 'Overview', 'Structure', 'Details', 'Complete']

    return f"""
📍 **Current Level**: {level} ({labels[level]})
   {''.join(levels)}
   ↑              ↑
   0              4

💡 Navigation: `level up` | `level down` | `level <0-4>`
"""
```

---

### `cmd_drill` - 섹션 드릴다운

```python
def cmd_drill(section_name: str, state: NavigationState,
              doc: DocumentStructure, depth: int = 1) -> str:
    """
    특정 섹션만 더 깊은 레벨로 탐색합니다.

    Commands:
        drill "섹션명"        → 해당 섹션만 +1 레벨
        drill "섹션명" --depth 2  → 해당 섹션만 +2 레벨
        drill "섹션명" --full     → 해당 섹션을 Level 4로
        drill reset           → 모든 섹션을 기본 레벨로

    Args:
        section_name: 드릴다운할 섹션 이름 (부분 매칭 지원)
        state: 현재 네비게이션 상태
        doc: 파싱된 문서 구조
        depth: 드릴다운 깊이 (기본 1)

    Returns:
        드릴다운된 섹션 내용

    Accuracy Target: ≥95% (섹션 매칭 정확도)
    """
```

**Section Matching Algorithm**:
```python
def find_best_match(query: str, sections: List[Section]) -> Optional[Section]:
    """
    섹션 이름을 Fuzzy 매칭으로 찾습니다.

    Matching Strategy (우선순위):
    1. 정확 매칭 (case-insensitive)
    2. 시작 매칭 (query로 시작하는 섹션)
    3. 포함 매칭 (query를 포함하는 섹션)
    4. 유사도 매칭 (Levenshtein distance ≤ 3)
    """
```

**Usage Examples**:
```bash
drill "Cooldown System"           # 해당 섹션 Level +1
drill "Cooldown" --depth 2        # Level +2
drill "Cooldown System" --full    # Level 4로 직접 이동
drill reset                       # 모든 섹션 레벨 초기화
```

**Disambiguation Handling**:
```python
def format_disambiguation_prompt(query: str, candidates: List[Section]) -> str:
    """여러 매칭 후보가 있을 때 선택 프롬프트"""
    # Returns:
    # 🔍 **'query'와 매칭되는 섹션이 N개 있습니다:**
    #    1. **Section A** (Line 10)
    #    2. **Section B** (Line 50)
    # 💡 정확한 섹션명을 입력하세요: `drill "Section A"`
```

---

### `cmd_focus` - 키워드 포커스

```python
def cmd_focus(keyword: str, state: NavigationState,
              doc: DocumentStructure) -> str:
    """
    키워드와 관련된 섹션만 필터링하여 표시합니다.

    Commands:
        focus "키워드"       → 해당 키워드 포함 섹션만 표시
        focus "키워드" --expand  → 관련 섹션 + 컨텍스트 확장
        focus clear          → 포커스 해제

    Args:
        keyword: 검색할 키워드 (대소문자 무시)
        state: 현재 네비게이션 상태
        doc: 파싱된 문서 구조

    Returns:
        필터링된 결과 + 관련성 점수

    Relevance Target: ≥90%
    """
```

**Relevance Scoring**:
```python
def calculate_relevance_score(keyword: str, section: Section) -> float:
    """
    키워드와 섹션의 관련성 점수를 계산합니다.

    Scoring:
        - 제목 매칭: +50점
        - 정확 매칭 보너스: +10점
        - 첫 문단 매칭: +30점
        - 코드 블록 매칭: +20점
        - 본문 매칭: +10점 (빈도수 × 2, 최대 20점)

    Returns:
        0-100 범위의 관련성 점수
    """
```

**Results Grouping**:
| 관련성 등급 | 점수 범위 | 아이콘 |
|------------|----------|--------|
| 높음 | ≥60점 | ●●● (80+) or ●●○ |
| 중간 | 30-59점 | ●○○ |
| 낮음 | <30점 | (숨김) |

---

### `cmd_back` / `cmd_history` - 히스토리 관리

```python
def cmd_back(state: NavigationState) -> str:
    """
    이전 상태로 되돌립니다.

    Commands:
        back       → 직전 상태로
        back 3     → 3단계 전으로
        history    → 탐색 히스토리 표시
    """

def cmd_history(state: NavigationState) -> str:
    """탐색 히스토리 표시"""
    # Output:
    # ## 📜 Navigation History
    #
    # · 1. open:path@L0
    # · 2. level:0→1
    # → 3. drill:section@L2  (current)
    #
    # 💡 되돌리기: `back` | 히스토리 초기화: `level reset`
```

---

## Auto-Level Selection API

### DocumentComplexity

```python
@dataclass
class DocumentComplexity:
    """문서 복잡도 분석 결과"""
    total_lines: int
    section_count: int
    code_block_count: int
    code_block_ratio: float      # 코드 비율 (0.0-1.0)
    avg_section_length: float    # 섹션 평균 길이
    nesting_depth: int           # 최대 헤더 깊이 (H1-H4)
    has_examples: bool           # 예제 포함 여부
    complexity_score: float      # 종합 복잡도 (0-100)
```

**Complexity Scoring Weights**:
| Factor | Weight | Scoring |
|--------|--------|---------|
| 라인 수 | 25% | 100줄=10, 500줄=20, 1000+=25 |
| 섹션 수 | 20% | 5개=10, 10개=15, 15+=20 |
| 코드 비율 | 25% | 0.1=10, 0.3=20, 0.5+=25 |
| 평균 섹션 길이 | 15% | 50줄=7, 100줄=10, 200+=15 |
| 네스팅 깊이 | 15% | depth × 4 (max 15) |

**Complexity Tiers**:
| Tier | Score Range | Visual |
|------|-------------|--------|
| Low | <40 | 🟢 |
| Medium | 40-69 | 🟡 |
| High | ≥70 | 🔴 |

---

### QueryIntent

```python
@dataclass
class QueryIntent:
    """사용자 쿼리 의도 분석 결과"""
    intent_type: str        # 'overview' | 'specific' | 'code' | 'troubleshoot'
    target_section: Optional[str]  # 특정 섹션 타겟
    keywords: List[str]     # 추출된 키워드
    depth_hint: int         # 추론된 깊이 힌트 (0-4)
    confidence: float       # 분석 신뢰도 (0-1)
```

**Intent Pattern Definitions**:

```python
INTENT_PATTERNS = {
    'overview': {
        'patterns': [
            r'(요약|summary|개요|overview|구조|structure)',
            r'(뭐가 있|what.*contain|목록|list)',
            r'(간단히|briefly|짧게|quick)'
        ],
        'depth_hint': 0,
        'weight': 1.0
    },
    'specific': {
        'patterns': [
            r'(어떻게|how to|방법|구현)',
            r'("[\w\s]+")\s*(섹션|section|부분)',
            r'(자세히|detail|상세|in-depth)'
        ],
        'depth_hint': 2,
        'weight': 1.2
    },
    'code': {
        'patterns': [
            r'(코드|code|예제|example|샘플|sample)',
            r'(구현|implement|작성|write)',
            r'(클래스|class|함수|function|메서드|method)'
        ],
        'depth_hint': 3,
        'weight': 1.3
    },
    'troubleshoot': {
        'patterns': [
            r'(에러|error|오류|문제|issue|버그|bug)',
            r'(안되|doesn\'t work|실패|fail)',
            r'(해결|solve|fix|수정)'
        ],
        'depth_hint': 3,
        'weight': 1.4
    }
}
```

---

### recommend_initial_level

```python
def recommend_initial_level(
    doc: DocumentComplexity,
    query: QueryIntent
) -> Tuple[int, str]:
    """
    문서 복잡도와 쿼리 의도를 기반으로 최적 초기 레벨을 추천합니다.

    Returns:
        Tuple[int, str]: (추천 레벨, 추천 이유)
    """
```

**Decision Matrix**:

| 복잡도 | overview | specific | code | troubleshoot |
|--------|----------|----------|------|--------------|
| Low (<40) | 1 | 2 | 3 | 3 |
| Medium (40-69) | 0 | 2 | 2 | 3 |
| High (≥70) | 0 | 1 | 2 | 2 |

**Level Adjustments**:
- 쿼리 신뢰도 <0.5 → level -1
- 특정 섹션 타겟 지정 → level +1

---

### auto_select_level (통합 함수)

```python
def auto_select_level(content: str, query: str) -> Tuple[int, str, DocumentComplexity, QueryIntent]:
    """
    통합 자동 레벨 선택 함수

    Args:
        content: 문서 내용
        query: 사용자 쿼리

    Returns:
        Tuple[int, str, DocumentComplexity, QueryIntent]:
            - 추천 레벨
            - 추천 이유
            - 문서 복잡도
            - 쿼리 의도

    Usage:
        level, reason, doc_complexity, query_intent = auto_select_level(
            content=document_content,
            query="GAS 문서에서 쿨다운 구현 방법 알려줘"
        )
        # level=2, reason="Level 2 (Structure) 추천..."
    """
```

---

## Caching API

### LRUDocumentCache

```python
class LRUDocumentCache:
    """LRU 캐시 with TTL"""
    max_size: int = 50       # 최대 항목 수
    ttl_seconds: int = 300   # 5분 TTL

    def get(self, key: str) -> Optional[CacheEntry]
    def put(self, key: str, value: DocumentStructure) -> None
    def invalidate(self, key: str) -> bool
    def clear(self) -> None
    def stats(self) -> CacheStats
```

**Cache Key Format**:
```python
cache_key = f"{document_path}:{mtime_hash}"
```

**CacheStats**:
```python
@dataclass
class CacheStats:
    size: int              # 현재 항목 수
    hits: int              # 캐시 히트 수
    misses: int            # 캐시 미스 수
    hit_rate: float        # 히트율 (0.0-1.0)
    evictions: int         # 퇴출 횟수
```

---

## Cross-Reference API

### DocumentRelationship

```python
@dataclass
class DocumentRelationship:
    """문서 간 관계 정보"""
    source_doc: str
    target_doc: str
    relationship_type: str  # 'imports', 'references', 'extends', 'similar'
    strength: float         # 관계 강도 (0.0-1.0)
    evidence: List[str]     # 관계 근거
```

### build_relationship_graph

```python
def build_relationship_graph(documents: List[str]) -> RelationshipGraph:
    """
    문서 집합에서 관계 그래프를 구축합니다.

    Relationship Types:
    1. Class References: 문서 A가 문서 B의 클래스 참조
    2. Module Dependencies: 모듈 의존성 관계
    3. Topic Similarity: 키워드 기반 유사성
    """
```

### suggest_related_documents

```python
def suggest_related_documents(
    current_doc: str,
    graph: RelationshipGraph,
    max_suggestions: int = 5
) -> List[DocumentSuggestion]:
    """
    현재 문서와 관련된 문서를 추천합니다.

    Returns:
        List[DocumentSuggestion]: 관련 문서 목록 (관계 강도순)
    """
```

---

## Performance Targets

| Operation | Target | Measured |
|-----------|--------|----------|
| level change | <500ms | ✅ |
| drill lookup | <200ms | ✅ |
| focus search | <500ms | ✅ |
| cache hit | <10ms | ✅ |
| auto-level | <300ms | ✅ |

---

## Error Handling

### Common Exceptions

```python
class NavigationError(Exception):
    """네비게이션 관련 오류"""
    pass

class SectionNotFoundError(NavigationError):
    """섹션을 찾을 수 없음"""
    def __init__(self, query: str, suggestions: List[str]):
        self.query = query
        self.suggestions = suggestions

class InvalidLevelError(NavigationError):
    """유효하지 않은 레벨"""
    def __init__(self, level: int):
        self.level = level
        super().__init__(f"Invalid level: {level}. Must be 0-4.")
```

---

**Related**:
- [Implementation Details](implementation.md)
- [Algorithms](algorithms.md)
- [Main SKILL.md](../SKILL.md)
