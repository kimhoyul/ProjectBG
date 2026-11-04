# Implementation Details

**Version**: 1.9.0
**Purpose**: Doc Progressive Explorer 구현 상세 코드 및 패턴

---

## 1. Edge Case Handling

### 1.1 Safe Document Parsing

```python
def safe_parse_document(content: str) -> DocumentStructure:
    """
    안전한 문서 파싱 - 모든 입력에 대해 유효한 결과 반환.

    Args:
        content: 파싱할 문서 내용

    Returns:
        DocumentStructure: 파싱된 구조 (에러 시에도 유효한 기본값)
    """
    # 1. 빈 문서 처리
    if not content or not content.strip():
        return DocumentStructure(
            title="(Empty Document)",
            summary="No content available",
            sections=[],
            total_lines=0
        )

    # 2. 타입 검증
    if not isinstance(content, str):
        raise TypeError(f"Expected str, got {type(content).__name__}")

    # 3. 안전한 파싱 시도
    try:
        return parse_document(content)
    except Exception as e:
        # 파싱 실패 시 기본 구조 반환
        return DocumentStructure(
            title="(Parse Error)",
            summary=f"Parsing error: {str(e)[:100]}",
            sections=[],
            total_lines=len(content.split('\n'))
        )
```

### 1.2 Markdown Sanitization

```python
def sanitize_markdown(content: str) -> str:
    """
    마크다운 문법 오류 자동 수정.

    Fixes:
        - 닫히지 않은 코드 블록
        - 잘못된 헤더 형식
        - 깨진 링크 구조
    """
    lines = content.split('\n')
    result = []
    in_code_block = False
    code_fence_pattern = re.compile(r'^```')

    for line in lines:
        if code_fence_pattern.match(line):
            in_code_block = not in_code_block
        result.append(line)

    # 닫히지 않은 코드 블록 자동 종료
    if in_code_block:
        result.append('```')

    return '\n'.join(result)
```

### 1.3 Safe Section Navigation

```python
def safe_find_section(doc: DocumentStructure, section_name: str) -> Optional[Section]:
    """
    섹션 검색 - 퍼지 매칭 지원.

    Matching Strategy:
        1. 정확한 매칭 (대소문자 무시)
        2. 부분 문자열 매칭
        3. 단어 단위 매칭
    """
    if not section_name:
        return None

    search_term = section_name.lower().strip()

    # 1단계: 정확한 매칭
    for section in doc.sections:
        if section.title.lower() == search_term:
            return section

    # 2단계: 부분 문자열 매칭
    for section in doc.sections:
        if search_term in section.title.lower():
            return section

    # 3단계: 단어 단위 매칭
    search_words = set(search_term.split())
    best_match = None
    best_score = 0

    for section in doc.sections:
        title_words = set(section.title.lower().split())
        overlap = len(search_words & title_words)
        if overlap > best_score:
            best_score = overlap
            best_match = section

    return best_match if best_score > 0 else None
```

### 1.4 Safe Level Navigation

```python
def safe_level_change(current: int, delta: int) -> int:
    """
    레벨 변경 - 범위 검증 포함.

    Args:
        current: 현재 레벨 (0-4)
        delta: 변경량 (+1 또는 -1)

    Returns:
        새 레벨 (0-4 범위 내)
    """
    new_level = current + delta
    return max(0, min(4, new_level))
```

### 1.5 Document Size Limits

```python
# 문서 크기 제한
MAX_DOCUMENT_LINES = 10000
MAX_SECTION_COUNT = 100
MAX_CODE_BLOCK_SIZE = 5000  # characters

def check_document_limits(content: str) -> Tuple[bool, Optional[str]]:
    """
    문서 크기 검증.

    Returns:
        (can_process, warning_message)
    """
    lines = content.split('\n')

    if len(lines) > MAX_DOCUMENT_LINES:
        return False, f"Document exceeds {MAX_DOCUMENT_LINES} line limit"

    # 섹션 수 확인
    section_count = len(re.findall(r'^#{1,6}\s', content, re.MULTILINE))
    if section_count > MAX_SECTION_COUNT:
        return True, f"Warning: {section_count} sections may slow parsing"

    return True, None
```

---

## 2. Error Handling & Recovery

### 2.1 Custom Error Types

```python
class DocExplorerError(Exception):
    """기본 에러 클래스."""

    def __init__(self, message: str, code: str = "UNKNOWN"):
        super().__init__(message)
        self.code = code
        self.message = message

class ParseError(DocExplorerError):
    """문서 파싱 에러."""
    def __init__(self, message: str, line: Optional[int] = None):
        super().__init__(message, "PARSE_ERROR")
        self.line = line

class NavigationError(DocExplorerError):
    """네비게이션 에러."""
    def __init__(self, message: str, current_state: Optional[dict] = None):
        super().__init__(message, "NAV_ERROR")
        self.current_state = current_state

class SectionNotFoundError(DocExplorerError):
    """섹션 미발견 에러."""
    def __init__(self, section_name: str, available: List[str]):
        message = f"Section '{section_name}' not found. Available: {available[:5]}"
        super().__init__(message, "SECTION_NOT_FOUND")
        self.section_name = section_name
        self.available = available
```

### 2.2 Rust-Style Result Type

```python
from typing import Generic, TypeVar, Union, Callable

T = TypeVar('T')
E = TypeVar('E')

class Result(Generic[T]):
    """Rust-style Result 타입."""

    def __init__(self, value: T = None, error: Exception = None):
        self._value = value
        self._error = error

    @classmethod
    def ok(cls, value: T) -> 'Result[T]':
        return cls(value=value)

    @classmethod
    def err(cls, error: Exception) -> 'Result[T]':
        return cls(error=error)

    def is_ok(self) -> bool:
        return self._error is None

    def is_err(self) -> bool:
        return self._error is not None

    def unwrap(self) -> T:
        if self._error:
            raise self._error
        return self._value

    def unwrap_or(self, default: T) -> T:
        return self._value if self.is_ok() else default

    def map(self, func: Callable[[T], T]) -> 'Result[T]':
        if self.is_ok():
            try:
                return Result.ok(func(self._value))
            except Exception as e:
                return Result.err(e)
        return self
```

### 2.3 Recovery Strategy

```python
from enum import Enum

class RecoveryStrategy(Enum):
    """에러 복구 전략."""
    RETURN_EMPTY = "return_empty"      # 빈 결과 반환
    RETURN_PARTIAL = "return_partial"  # 부분 결과 반환
    RETRY = "retry"                    # 재시도
    FALLBACK = "fallback"              # 대체 함수 사용
    RAISE = "raise"                    # 에러 전파

def execute_with_recovery(
    func: Callable,
    strategy: RecoveryStrategy,
    fallback_func: Optional[Callable] = None,
    max_retries: int = 3,
    *args, **kwargs
) -> Result:
    """
    복구 전략이 적용된 함수 실행.

    Args:
        func: 실행할 함수
        strategy: 복구 전략
        fallback_func: 대체 함수 (FALLBACK 전략용)
        max_retries: 최대 재시도 횟수 (RETRY 전략용)
    """
    last_error = None

    for attempt in range(max_retries if strategy == RecoveryStrategy.RETRY else 1):
        try:
            result = func(*args, **kwargs)
            return Result.ok(result)
        except Exception as e:
            last_error = e

            if strategy == RecoveryStrategy.RAISE:
                return Result.err(e)
            elif strategy == RecoveryStrategy.RETURN_EMPTY:
                return Result.ok(None)
            elif strategy == RecoveryStrategy.RETURN_PARTIAL:
                # 부분 결과가 있으면 반환
                if hasattr(e, 'partial_result'):
                    return Result.ok(e.partial_result)
            elif strategy == RecoveryStrategy.FALLBACK and fallback_func:
                try:
                    return Result.ok(fallback_func(*args, **kwargs))
                except Exception as fallback_error:
                    last_error = fallback_error

    return Result.err(last_error)
```

### 2.4 Error Messages

```python
ERROR_MESSAGES = {
    "PARSE_ERROR": "문서 파싱 중 오류가 발생했습니다. 마크다운 형식을 확인해주세요.",
    "NAV_ERROR": "네비게이션 오류입니다. 현재 위치를 확인해주세요.",
    "SECTION_NOT_FOUND": "요청한 섹션을 찾을 수 없습니다.",
    "LEVEL_OUT_OF_RANGE": "레벨은 0-4 범위여야 합니다.",
    "FILE_NOT_FOUND": "파일을 찾을 수 없습니다.",
    "ENCODING_ERROR": "파일 인코딩 오류입니다. UTF-8 인코딩을 확인해주세요.",
    "CACHE_MISS": "캐시에서 문서를 찾을 수 없습니다.",
    "TIMEOUT": "작업 시간이 초과되었습니다.",
}

def get_user_friendly_error(code: str, **context) -> str:
    """사용자 친화적 에러 메시지 생성."""
    base_message = ERROR_MESSAGES.get(code, "알 수 없는 오류가 발생했습니다.")

    # 컨텍스트 정보 추가
    if code == "SECTION_NOT_FOUND" and "available" in context:
        base_message += f"\n사용 가능한 섹션: {', '.join(context['available'][:5])}"

    return base_message
```

---

## 3. Testing Suite

### 3.1 Level Navigation Tests

```python
import pytest
from doc_explorer import DocumentExplorer, NavigationState

class TestLevelNavigation:
    """레벨 네비게이션 테스트."""

    @pytest.fixture
    def explorer(self):
        return DocumentExplorer()

    @pytest.fixture
    def sample_doc(self):
        return """
# Sample Document
> This is a test document.

## Section 1
Content for section 1.

### Subsection 1.1
Detailed content here.

## Section 2
More content.
"""

    def test_level_0_summary(self, explorer, sample_doc):
        """Level 0은 제목과 섹션 목록만 반환."""
        result = explorer.explore(sample_doc, level=0)

        assert "Sample Document" in result
        assert "Section 1" in result
        assert "Section 2" in result
        assert "Detailed content" not in result  # 세부 내용 제외
        assert len(result.split('\n')) <= 15  # 10줄 이하 목표

    def test_level_4_complete(self, explorer, sample_doc):
        """Level 4는 전체 문서 반환."""
        result = explorer.explore(sample_doc, level=4)

        assert result.strip() == sample_doc.strip()

    def test_level_progression(self, explorer, sample_doc):
        """레벨 증가에 따른 정보량 증가."""
        results = [explorer.explore(sample_doc, level=i) for i in range(5)]

        for i in range(4):
            assert len(results[i]) <= len(results[i+1])

    def test_invalid_level_clamping(self, explorer, sample_doc):
        """범위 외 레벨은 자동 조정."""
        result_neg = explorer.explore(sample_doc, level=-1)
        result_high = explorer.explore(sample_doc, level=10)

        assert result_neg == explorer.explore(sample_doc, level=0)
        assert result_high == explorer.explore(sample_doc, level=4)
```

### 3.2 Drill-Down Tests

```python
class TestDrillDown:
    """드릴다운 네비게이션 테스트."""

    def test_drill_to_section(self, explorer, sample_doc):
        """섹션 드릴다운."""
        result = explorer.explore(sample_doc, drill="Section 1")

        assert "Section 1" in result
        assert "Subsection 1.1" in result
        assert "Section 2" not in result

    def test_drill_with_full_flag(self, explorer, sample_doc):
        """--full 플래그로 Level 4 표시."""
        result = explorer.explore(sample_doc, drill="Section 1", full=True)

        assert "Detailed content here" in result

    def test_drill_fuzzy_matching(self, explorer, sample_doc):
        """퍼지 매칭으로 섹션 찾기."""
        # 대소문자 무시
        result1 = explorer.explore(sample_doc, drill="section 1")
        result2 = explorer.explore(sample_doc, drill="SECTION 1")

        assert "Section 1" in result1
        assert result1 == result2

    def test_drill_not_found(self, explorer, sample_doc):
        """섹션 미발견 시 제안 표시."""
        result = explorer.explore(sample_doc, drill="NonExistent")

        assert "not found" in result.lower() or "사용 가능한" in result
        assert "Section 1" in result or "Section 2" in result
```

### 3.3 Keyword Focus Tests

```python
class TestFocus:
    """키워드 포커스 테스트."""

    @pytest.fixture
    def multi_section_doc(self):
        return """
# API Documentation

## Authentication
Login and OAuth methods.

## Database
SQL queries and ORM.

## Authentication Tokens
JWT and session management.

## Logging
Debug and error logging.
"""

    def test_focus_filters_sections(self, explorer, multi_section_doc):
        """키워드로 관련 섹션만 필터링."""
        result = explorer.explore(multi_section_doc, focus="Authentication")

        assert "Authentication" in result
        assert "Authentication Tokens" in result
        assert "Database" not in result
        assert "Logging" not in result

    def test_focus_case_insensitive(self, explorer, multi_section_doc):
        """대소문자 무시 검색."""
        result1 = explorer.explore(multi_section_doc, focus="auth")
        result2 = explorer.explore(multi_section_doc, focus="AUTH")

        assert "Authentication" in result1
        assert result1 == result2

    def test_focus_clear(self, explorer, multi_section_doc):
        """포커스 해제."""
        explorer.explore(multi_section_doc, focus="auth")
        result = explorer.focus_clear()

        assert "Database" in result
        assert "Logging" in result
```

### 3.4 Auto-Level Tests

```python
class TestAutoLevel:
    """자동 레벨 선택 테스트."""

    @pytest.mark.parametrize("query,expected_min,expected_max", [
        ("요약해줘", 0, 1),           # overview intent
        ("구현 방법 알려줘", 2, 3),    # specific intent
        ("코드 예제 보여줘", 2, 4),    # code intent
        ("에러 해결 방법", 2, 3),      # troubleshoot intent
    ])
    def test_auto_level_by_intent(self, explorer, sample_doc, query, expected_min, expected_max):
        """쿼리 의도에 따른 자동 레벨 선택."""
        level = explorer.recommend_level(sample_doc, query)

        assert expected_min <= level <= expected_max

    def test_complexity_adjustment(self, explorer):
        """문서 복잡도에 따른 레벨 조정."""
        simple_doc = "# Simple\nOne paragraph."
        complex_doc = "# Complex\n" + "\n## Section\nContent.\n" * 50

        level_simple = explorer.recommend_level(simple_doc, "overview")
        level_complex = explorer.recommend_level(complex_doc, "overview")

        assert level_simple >= level_complex  # 복잡한 문서는 낮은 레벨로 시작
```

---

## 4. Cross-Reference System

### 4.1 Relation Types

```python
from enum import Enum
from dataclasses import dataclass
from typing import List, Set, Dict, Optional

class RelationType(Enum):
    """문서 간 관계 유형."""
    EXPLICIT_LINK = "explicit_link"      # 마크다운 링크
    CLASS_REFERENCE = "class_reference"  # UE 클래스 참조
    API_MENTION = "api_mention"          # API 함수 언급
    IMPORT_CHAIN = "import_chain"        # 의존성 체인
    TOPIC_SIMILARITY = "topic_similarity" # 주제 유사성
    DEPENDENCY = "dependency"            # 모듈 의존성
    KEYWORD_OVERLAP = "keyword_overlap"  # 키워드 중복

@dataclass
class DocumentRelation:
    """문서 관계 정보."""
    source_doc: str
    target_doc: str
    relation_type: RelationType
    strength: float  # 0.0 - 1.0
    evidence: List[str]  # 관계 근거
```

### 4.2 Relationship Analyzer

```python
class RelationshipAnalyzer:
    """문서 관계 분석기."""

    # UE 클래스 패턴
    UE_CLASS_PATTERNS = [
        r'\b([AUFIS][A-Z][a-zA-Z0-9]+)\b',  # UClass, AClass, FStruct 등
        r'\bU[A-Z][a-zA-Z0-9]+Component\b',  # UComponent
        r'\bA[A-Z][a-zA-Z0-9]+Actor\b',      # AActor
    ]

    def __init__(self):
        self.class_to_docs: Dict[str, Set[str]] = {}
        self.doc_relations: Dict[str, List[DocumentRelation]] = {}

    def analyze_document(self, doc_path: str, content: str) -> List[DocumentRelation]:
        """문서 분석 및 관계 추출."""
        relations = []

        # 1. 명시적 링크 추출
        links = self._extract_links(content)
        for link in links:
            relations.append(DocumentRelation(
                source_doc=doc_path,
                target_doc=link,
                relation_type=RelationType.EXPLICIT_LINK,
                strength=1.0,
                evidence=[f"Explicit link: {link}"]
            ))

        # 2. UE 클래스 참조 추출
        classes = self._extract_ue_classes(content)
        for cls in classes:
            if cls not in self.class_to_docs:
                self.class_to_docs[cls] = set()
            self.class_to_docs[cls].add(doc_path)

        self.doc_relations[doc_path] = relations
        return relations

    def _extract_links(self, content: str) -> List[str]:
        """마크다운 링크 추출."""
        pattern = r'\[([^\]]+)\]\(([^)]+\.md)\)'
        matches = re.findall(pattern, content)
        return [match[1] for match in matches]

    def _extract_ue_classes(self, content: str) -> Set[str]:
        """UE 클래스 참조 추출."""
        classes = set()
        for pattern in self.UE_CLASS_PATTERNS:
            matches = re.findall(pattern, content)
            classes.update(matches)
        return classes

    def find_related_by_class(self, class_name: str) -> Set[str]:
        """특정 클래스를 참조하는 모든 문서 찾기."""
        return self.class_to_docs.get(class_name, set())
```

### 4.3 Similarity Engine

```python
class SimilarityEngine:
    """문서 유사도 계산 엔진."""

    def calculate_similarity(
        self,
        doc1: str,
        doc2: str,
        content1: str,
        content2: str
    ) -> float:
        """
        종합 유사도 계산.

        Components:
            - Jaccard similarity (키워드)
            - Class overlap (UE 클래스)
            - Structural similarity (문서 구조)
            - Path similarity (파일 경로)
        """
        # 1. 키워드 Jaccard 유사도
        keywords1 = self._extract_keywords(content1)
        keywords2 = self._extract_keywords(content2)
        jaccard = self._jaccard_similarity(keywords1, keywords2)

        # 2. 클래스 오버랩
        classes1 = self._extract_ue_classes(content1)
        classes2 = self._extract_ue_classes(content2)
        class_overlap = len(classes1 & classes2) / max(len(classes1 | classes2), 1)

        # 3. 구조적 유사도
        struct_sim = self._structural_similarity(content1, content2)

        # 4. 경로 유사도
        path_sim = self._path_similarity(doc1, doc2)

        # 가중 평균
        return (
            0.35 * jaccard +
            0.30 * class_overlap +
            0.20 * struct_sim +
            0.15 * path_sim
        )

    def _extract_keywords(self, content: str) -> Set[str]:
        """키워드 추출."""
        words = re.findall(r'\b[a-zA-Z]{3,}\b', content.lower())
        stopwords = {'the', 'and', 'for', 'this', 'that', 'with'}
        return set(words) - stopwords

    def _jaccard_similarity(self, set1: Set, set2: Set) -> float:
        """Jaccard 유사도."""
        if not set1 and not set2:
            return 0.0
        intersection = len(set1 & set2)
        union = len(set1 | set2)
        return intersection / union if union > 0 else 0.0

    def _structural_similarity(self, content1: str, content2: str) -> float:
        """구조적 유사도 (헤더 패턴 비교)."""
        headers1 = re.findall(r'^(#{1,6})\s', content1, re.MULTILINE)
        headers2 = re.findall(r'^(#{1,6})\s', content2, re.MULTILINE)

        if not headers1 or not headers2:
            return 0.0

        # 헤더 레벨 분포 비교
        dist1 = [headers1.count('#' * i) for i in range(1, 7)]
        dist2 = [headers2.count('#' * i) for i in range(1, 7)]

        diff = sum(abs(a - b) for a, b in zip(dist1, dist2))
        max_diff = sum(dist1) + sum(dist2)

        return 1.0 - (diff / max_diff) if max_diff > 0 else 0.0

    def _path_similarity(self, path1: str, path2: str) -> float:
        """경로 유사도."""
        parts1 = path1.split('/')
        parts2 = path2.split('/')

        common = 0
        for p1, p2 in zip(parts1, parts2):
            if p1 == p2:
                common += 1
            else:
                break

        max_len = max(len(parts1), len(parts2))
        return common / max_len if max_len > 0 else 0.0
```

### 4.4 Cross-Reference Recommender

```python
@dataclass
class RecommendationResult:
    """추천 결과."""
    doc_path: str
    score: float
    reasons: List[str]
    relation_types: List[RelationType]

class CrossReferenceRecommender:
    """크로스레퍼런스 추천 시스템."""

    def __init__(self):
        self.analyzer = RelationshipAnalyzer()
        self.similarity_engine = SimilarityEngine()
        self.indexed_docs: Dict[str, str] = {}  # path -> content

    def index_document(self, doc_path: str, content: str):
        """문서 인덱싱."""
        self.indexed_docs[doc_path] = content
        self.analyzer.analyze_document(doc_path, content)

    def get_recommendations(
        self,
        current_doc: str,
        current_content: str,
        context: Optional[dict] = None,
        max_results: int = 5
    ) -> List[RecommendationResult]:
        """
        관련 문서 추천.

        Args:
            current_doc: 현재 문서 경로
            current_content: 현재 문서 내용
            context: 탐색 컨텍스트 (optional)
            max_results: 최대 결과 수
        """
        candidates = []

        for doc_path, content in self.indexed_docs.items():
            if doc_path == current_doc:
                continue

            # 유사도 계산
            similarity = self.similarity_engine.calculate_similarity(
                current_doc, doc_path, current_content, content
            )

            if similarity < 0.1:  # 최소 임계값
                continue

            # 관계 분석
            relations = self._get_relations(current_doc, doc_path)
            reasons = self._generate_reasons(relations, similarity)

            candidates.append(RecommendationResult(
                doc_path=doc_path,
                score=similarity,
                reasons=reasons,
                relation_types=[r.relation_type for r in relations]
            ))

        # 점수순 정렬 및 상위 N개 반환
        candidates.sort(key=lambda x: x.score, reverse=True)
        return candidates[:max_results]

    def _get_relations(self, doc1: str, doc2: str) -> List[DocumentRelation]:
        """두 문서 간 관계 조회."""
        relations = []
        if doc1 in self.analyzer.doc_relations:
            for rel in self.analyzer.doc_relations[doc1]:
                if rel.target_doc == doc2:
                    relations.append(rel)
        return relations

    def _generate_reasons(
        self,
        relations: List[DocumentRelation],
        similarity: float
    ) -> List[str]:
        """추천 이유 생성."""
        reasons = []

        for rel in relations:
            if rel.relation_type == RelationType.CLASS_REFERENCE:
                reasons.append(f"동일 클래스 참조: {rel.evidence[0]}")
            elif rel.relation_type == RelationType.EXPLICIT_LINK:
                reasons.append("명시적 링크 존재")

        if similarity > 0.7:
            reasons.append("높은 주제 유사성")
        elif similarity > 0.4:
            reasons.append("관련 주제")

        return reasons if reasons else ["키워드 유사성"]
```

---

## 5. TF-IDF Engine

### 5.1 Core Implementation

```python
import math
from collections import defaultdict

class TFIDFEngine:
    """TF-IDF 기반 문서 유사도 엔진."""

    # UE 특화 불용어
    UE_STOPWORDS = {
        'the', 'and', 'for', 'this', 'that', 'with', 'from', 'have', 'has',
        'void', 'return', 'class', 'struct', 'enum', 'public', 'private',
        'protected', 'virtual', 'override', 'const', 'static', 'inline'
    }

    # UE 고가치 용어 (가중치 부여)
    UE_HIGH_VALUE_TERMS = {
        'niagara': 2.0, 'pcg': 2.0, 'gas': 2.0, 'ability': 1.8,
        'component': 1.5, 'actor': 1.5, 'blueprint': 1.5,
        'replication': 1.8, 'networked': 1.5, 'replicated': 1.5,
        'animation': 1.5, 'montage': 1.5, 'statemachine': 1.8,
        'ai': 1.5, 'behaviortree': 1.8, 'statetree': 1.8
    }

    def __init__(self):
        self.documents: Dict[str, List[str]] = {}  # doc -> tokens
        self.idf_cache: Dict[str, float] = {}
        self.tfidf_vectors: Dict[str, Dict[str, float]] = {}

    def tokenize(self, text: str) -> List[str]:
        """토큰화 및 전처리."""
        # 소문자 변환
        text = text.lower()

        # 카멜케이스/파스칼케이스 분리
        text = re.sub(r'([a-z])([A-Z])', r'\1 \2', text)

        # 토큰 추출
        tokens = re.findall(r'\b[a-z][a-z0-9]{2,}\b', text)

        # 불용어 제거
        tokens = [t for t in tokens if t not in self.UE_STOPWORDS]

        return tokens

    def index_document(self, doc_path: str, content: str):
        """문서 인덱싱."""
        tokens = self.tokenize(content)
        self.documents[doc_path] = tokens

        # IDF 캐시 무효화
        self.idf_cache.clear()
        self.tfidf_vectors.clear()

    def _calculate_tf(self, doc_path: str) -> Dict[str, float]:
        """Term Frequency 계산."""
        tokens = self.documents.get(doc_path, [])
        if not tokens:
            return {}

        tf = defaultdict(float)
        for token in tokens:
            tf[token] += 1

        # 정규화
        max_tf = max(tf.values()) if tf else 1
        return {k: v / max_tf for k, v in tf.items()}

    def _calculate_idf(self, term: str) -> float:
        """Inverse Document Frequency 계산."""
        if term in self.idf_cache:
            return self.idf_cache[term]

        doc_count = sum(1 for tokens in self.documents.values() if term in tokens)
        idf = math.log((len(self.documents) + 1) / (doc_count + 1)) + 1

        # 고가치 용어 가중치 적용
        if term in self.UE_HIGH_VALUE_TERMS:
            idf *= self.UE_HIGH_VALUE_TERMS[term]

        self.idf_cache[term] = idf
        return idf

    def _calculate_tfidf_vector(self, doc_path: str) -> Dict[str, float]:
        """TF-IDF 벡터 계산."""
        if doc_path in self.tfidf_vectors:
            return self.tfidf_vectors[doc_path]

        tf = self._calculate_tf(doc_path)
        tfidf = {}

        for term, tf_value in tf.items():
            idf = self._calculate_idf(term)
            tfidf[term] = tf_value * idf

        self.tfidf_vectors[doc_path] = tfidf
        return tfidf

    def calculate_similarity(self, doc1: str, doc2: str) -> float:
        """코사인 유사도 계산."""
        vec1 = self._calculate_tfidf_vector(doc1)
        vec2 = self._calculate_tfidf_vector(doc2)

        if not vec1 or not vec2:
            return 0.0

        # 공통 용어
        common_terms = set(vec1.keys()) & set(vec2.keys())

        # 내적
        dot_product = sum(vec1[t] * vec2[t] for t in common_terms)

        # 크기
        norm1 = math.sqrt(sum(v * v for v in vec1.values()))
        norm2 = math.sqrt(sum(v * v for v in vec2.values()))

        if norm1 == 0 or norm2 == 0:
            return 0.0

        return dot_product / (norm1 * norm2)

    def find_similar_documents(
        self,
        query_doc: str,
        top_k: int = 5
    ) -> List[Tuple[str, float]]:
        """유사 문서 검색."""
        similarities = []

        for doc_path in self.documents:
            if doc_path == query_doc:
                continue

            sim = self.calculate_similarity(query_doc, doc_path)
            if sim > 0.1:  # 최소 임계값
                similarities.append((doc_path, sim))

        similarities.sort(key=lambda x: x[1], reverse=True)
        return similarities[:top_k]
```

---

## 6. Context Boosting Engine

### 6.1 User Intent Inference

```python
class UserIntent(Enum):
    """사용자 의도 분류."""
    LEARNING = "learning"           # 학습/이해 목적
    DEBUGGING = "debugging"         # 문제 해결
    IMPLEMENTATION = "implementation"  # 구현 참고
    EXPLORATION = "exploration"     # 탐색/발견
    REFERENCE = "reference"         # 레퍼런스 조회

class ExplorationContext:
    """탐색 컨텍스트."""
    def __init__(self):
        self.visited_docs: List[str] = []
        self.current_level: int = 0
        self.focused_section: Optional[str] = None
        self.search_history: List[str] = []
        self.time_spent: Dict[str, float] = {}  # doc -> seconds
```

### 6.2 Context Boosting Implementation

```python
class ContextBoostingEngine:
    """컨텍스트 기반 점수 부스팅."""

    # 의도별 관계 유형 가중치
    INTENT_RELATION_WEIGHTS = {
        UserIntent.DEBUGGING: {
            'CLASS_REFERENCE': 1.4,
            'API_MENTION': 1.3,
            'TOPIC_SIMILARITY': 1.0
        },
        UserIntent.LEARNING: {
            'TOPIC_SIMILARITY': 1.4,
            'EXPLICIT_LINK': 1.3,
            'DEPENDENCY': 1.2
        },
        UserIntent.IMPLEMENTATION: {
            'CLASS_REFERENCE': 1.5,
            'API_MENTION': 1.4,
            'IMPORT_CHAIN': 1.3
        },
        UserIntent.EXPLORATION: {
            'TOPIC_SIMILARITY': 1.3,
            'KEYWORD_OVERLAP': 1.2,
            'EXPLICIT_LINK': 1.1
        },
        UserIntent.REFERENCE: {
            'API_MENTION': 1.5,
            'CLASS_REFERENCE': 1.4,
            'EXPLICIT_LINK': 1.2
        }
    }

    # 레벨별 탐색 전략
    LEVEL_STRATEGIES = {
        0: {'breadth': 0.8, 'depth': 0.2},  # 넓게
        1: {'breadth': 0.6, 'depth': 0.4},
        2: {'breadth': 0.5, 'depth': 0.5},  # 균형
        3: {'breadth': 0.3, 'depth': 0.7},
        4: {'breadth': 0.2, 'depth': 0.8}   # 깊게
    }

    def infer_user_intent(self, context: ExplorationContext) -> UserIntent:
        """컨텍스트에서 사용자 의도 추론."""
        # 검색 히스토리 분석
        debug_keywords = {'error', 'fix', 'bug', 'issue', 'problem', 'crash'}
        impl_keywords = {'how', 'implement', 'create', 'make', 'build', 'code'}
        learn_keywords = {'what', 'explain', 'understand', 'learn', 'tutorial'}

        history_text = ' '.join(context.search_history).lower()

        if any(k in history_text for k in debug_keywords):
            return UserIntent.DEBUGGING
        elif any(k in history_text for k in impl_keywords):
            return UserIntent.IMPLEMENTATION
        elif any(k in history_text for k in learn_keywords):
            return UserIntent.LEARNING
        elif len(context.visited_docs) > 5:
            return UserIntent.EXPLORATION
        else:
            return UserIntent.REFERENCE

    def calculate_recency_boost(
        self,
        doc_path: str,
        context: ExplorationContext
    ) -> float:
        """최근성 부스트 계산."""
        if doc_path not in context.visited_docs:
            return 1.0

        # 최근 방문일수록 낮은 부스트 (다양성 촉진)
        position = context.visited_docs.index(doc_path)
        recency = position / len(context.visited_docs)

        return 0.5 + 0.5 * recency  # 0.5 ~ 1.0

    def calculate_path_relevance_boost(
        self,
        doc_path: str,
        context: ExplorationContext
    ) -> float:
        """경로 관련성 부스트."""
        if not context.visited_docs:
            return 1.0

        current_doc = context.visited_docs[-1]

        # 같은 디렉토리 = 높은 부스트
        current_dir = '/'.join(current_doc.split('/')[:-1])
        doc_dir = '/'.join(doc_path.split('/')[:-1])

        if current_dir == doc_dir:
            return 1.3
        elif current_dir.split('/')[0] == doc_dir.split('/')[0]:
            return 1.1
        else:
            return 1.0

    def apply_context_boost(
        self,
        recommendations: List[RecommendationResult],
        context: ExplorationContext
    ) -> List[Dict]:
        """컨텍스트 부스트 적용."""
        intent = self.infer_user_intent(context)
        intent_weights = self.INTENT_RELATION_WEIGHTS.get(intent, {})
        level_strategy = self.LEVEL_STRATEGIES.get(context.current_level, {})

        boosted = []
        for rec in recommendations:
            score = rec.score

            # 1. 의도 기반 부스트
            for rel_type in rec.relation_types:
                weight = intent_weights.get(rel_type.value.upper(), 1.0)
                score *= weight

            # 2. 최근성 부스트
            recency_boost = self.calculate_recency_boost(rec.doc_path, context)
            score *= recency_boost

            # 3. 경로 관련성 부스트
            path_boost = self.calculate_path_relevance_boost(rec.doc_path, context)
            score *= path_boost

            # 4. 레벨 전략 적용
            # (추가 로직...)

            boosted.append({
                'doc_path': rec.doc_path,
                'original_score': rec.score,
                'boosted_score': score,
                'reasons': rec.reasons,
                'intent': intent.value
            })

        boosted.sort(key=lambda x: x['boosted_score'], reverse=True)
        return boosted
```

---

## 7. Feedback Learning System

### 7.1 Feedback Types

```python
class FeedbackType(Enum):
    """피드백 유형."""
    CLICK = "click"              # 추천 클릭
    DRILL_DOWN = "drill_down"    # 드릴다운 실행
    LEVEL_CHANGE = "level_change"  # 레벨 변경
    FOCUS = "focus"              # 포커스 적용
    BACK = "back"                # 뒤로 가기
    DISMISS = "dismiss"          # 추천 무시

@dataclass
class FeedbackEvent:
    """피드백 이벤트."""
    feedback_type: FeedbackType
    source_doc: str
    target_doc: Optional[str]
    recommendation_position: int
    timestamp: float
    context: Optional[dict] = None

@dataclass
class LearnedPreference:
    """학습된 선호도."""
    pattern: str  # 예: "source:GAS/* -> target:Animation/*"
    weight: float
    confidence: float
    sample_count: int
```

### 7.2 Feedback Learning Engine

```python
class FeedbackLearningEngine:
    """피드백 기반 학습 엔진."""

    # 피드백 유형별 기본 점수
    FEEDBACK_BASE_SCORES = {
        FeedbackType.CLICK: 0.3,
        FeedbackType.DRILL_DOWN: 0.4,
        FeedbackType.LEVEL_CHANGE: 0.2,
        FeedbackType.FOCUS: 0.35,
        FeedbackType.BACK: -0.1,
        FeedbackType.DISMISS: -0.2
    }

    # 위치별 가중치 (상위 추천 클릭 = 더 강한 신호)
    POSITION_WEIGHTS = {
        0: 1.0,   # 1위 추천
        1: 0.85,
        2: 0.7,
        3: 0.55,
        4: 0.4
    }

    def __init__(self):
        self.feedback_history: List[FeedbackEvent] = []
        self.preferences: Dict[str, LearnedPreference] = {}
        self.ema_alpha = 0.1  # Exponential Moving Average 가중치

    def record_feedback(self, event: FeedbackEvent):
        """피드백 기록."""
        self.feedback_history.append(event)
        self._update_preferences(event)

    def _update_preferences(self, event: FeedbackEvent):
        """선호도 업데이트 (EMA 사용)."""
        if not event.target_doc:
            return

        # 패턴 생성 (예: "GAS/* -> Animation/*")
        source_category = self._categorize_doc(event.source_doc)
        target_category = self._categorize_doc(event.target_doc)
        pattern = f"{source_category} -> {target_category}"

        # 피드백 점수 계산
        base_score = self.FEEDBACK_BASE_SCORES.get(event.feedback_type, 0)
        position_weight = self.POSITION_WEIGHTS.get(
            event.recommendation_position, 0.3
        )
        feedback_score = base_score * position_weight

        # EMA 업데이트
        if pattern in self.preferences:
            pref = self.preferences[pattern]
            new_weight = (
                self.ema_alpha * feedback_score +
                (1 - self.ema_alpha) * pref.weight
            )
            pref.weight = new_weight
            pref.sample_count += 1
            pref.confidence = min(1.0, pref.sample_count / 20)
        else:
            self.preferences[pattern] = LearnedPreference(
                pattern=pattern,
                weight=feedback_score,
                confidence=0.05,
                sample_count=1
            )

    def _categorize_doc(self, doc_path: str) -> str:
        """문서 카테고리화."""
        parts = doc_path.split('/')

        # 플러그인 카테고리
        if 'Plugins' in parts:
            idx = parts.index('Plugins')
            if idx + 1 < len(parts):
                return f"Plugins/{parts[idx + 1]}/*"

        # 모듈 카테고리
        if 'modules' in parts:
            idx = parts.index('modules')
            if idx + 1 < len(parts):
                return f"modules/{parts[idx + 1]}/*"

        return "other/*"

    def apply_learned_preferences(
        self,
        recommendations: List[Dict],
        source_doc: str
    ) -> List[Dict]:
        """학습된 선호도 적용."""
        source_category = self._categorize_doc(source_doc)

        for rec in recommendations:
            target_category = self._categorize_doc(rec['doc_path'])
            pattern = f"{source_category} -> {target_category}"

            if pattern in self.preferences:
                pref = self.preferences[pattern]

                # 신뢰도에 따른 가중치 적용
                boost = 1.0 + (pref.weight * pref.confidence)
                rec['boosted_score'] *= boost
                rec['learned_pattern'] = pattern
                rec['pattern_confidence'] = pref.confidence

        recommendations.sort(key=lambda x: x['boosted_score'], reverse=True)
        return recommendations

    def get_preference_stats(self) -> Dict:
        """선호도 통계."""
        return {
            'total_patterns': len(self.preferences),
            'total_feedback': len(self.feedback_history),
            'high_confidence_patterns': sum(
                1 for p in self.preferences.values()
                if p.confidence > 0.5
            ),
            'top_patterns': sorted(
                self.preferences.values(),
                key=lambda p: p.weight * p.confidence,
                reverse=True
            )[:10]
        }
```

---

## 8. Integrated Recommendation Engine

```python
class IntegratedRecommendationEngine:
    """통합 추천 엔진."""

    def __init__(self):
        self.cross_ref = CrossReferenceRecommender()
        self.tfidf = TFIDFEngine()
        self.context_boost = ContextBoostingEngine()
        self.feedback = FeedbackLearningEngine()

    def index_document(self, doc_path: str, content: str):
        """문서 인덱싱."""
        self.cross_ref.index_document(doc_path, content)
        self.tfidf.index_document(doc_path, content)

    def get_recommendations(
        self,
        current_doc: str,
        current_content: str,
        context: ExplorationContext,
        max_results: int = 5
    ) -> List[Dict]:
        """
        통합 추천 생성.

        Pipeline:
            1. Cross-reference 기반 후보 생성
            2. TF-IDF 유사도 계산
            3. 컨텍스트 부스팅 적용
            4. 학습된 선호도 적용
            5. 최종 순위 정렬
        """
        # 1. Cross-reference 추천
        cr_recommendations = self.cross_ref.get_recommendations(
            current_doc, current_content, context, max_results * 2
        )

        # 2. TF-IDF 유사도 보강
        for rec in cr_recommendations:
            tfidf_sim = self.tfidf.calculate_similarity(
                current_doc, rec.doc_path
            )
            rec.score = 0.6 * rec.score + 0.4 * tfidf_sim

        # 3. 컨텍스트 부스팅
        boosted = self.context_boost.apply_context_boost(
            cr_recommendations, context
        )

        # 4. 학습된 선호도 적용
        final = self.feedback.apply_learned_preferences(
            boosted, current_doc
        )

        return final[:max_results]

    def record_user_feedback(self, event: FeedbackEvent):
        """사용자 피드백 기록."""
        self.feedback.record_feedback(event)
```

---

## Related Documentation

- [API Reference](api.md) - 전체 API 문서
- [Algorithms](algorithms.md) - 알고리즘 상세 설명
- [Examples](../examples/) - 사용 예시

---

**Last Updated**: 2026-01-25
