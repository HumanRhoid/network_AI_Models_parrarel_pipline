# network_AI_Models_parrarel_pipline

이 저장소는 팀 협업을 위한 기본 GitHub 구성과 함께 AI 모델 병렬 처리 파이프라인 작업을 준비합니다.

## 현재 상태

- 프로젝트 초기 상태입니다.
- GitHub 협업을 위한 기본 템플릿과 CI 구성이 추가되었습니다.

## 팀 협업 요구 사항 검토

1. 이슈 및 PR 템플릿
   - `bug_report.md`, `feature_request.md`, `pull_request_template.md`를 추가하여 팀원이 동일한 형식으로 요청과 변경을 제출할 수 있게 했습니다.
2. 자동화된 검증
   - `.github/workflows/ci.yml`에서 푸시/PR 시 기본 Python 설치, 의존성 설치, 컴파일 검사, 테스트 실행을 지원합니다.
3. 참여 가이드
   - `CONTRIBUTING.md`에 브랜치 전략, 커밋 메시지, PR 작성, 리뷰 체크리스트, 테스트 기준을 정리했습니다.
4. 소유자 및 보안
   - `CODEOWNERS`와 `SECURITY.md`를 추가하여 책임자 지정 및 보안 이슈 보고 절차를 마련했습니다.

## 추가된 GitHub 구성

- `.github/workflows/ci.yml`
- `.github/pull_request_template.md`
- `.github/ISSUE_TEMPLATE/bug_report.md`
- `.github/ISSUE_TEMPLATE/feature_request.md`
- `.github/CONTRIBUTING.md`
- `.github/SECURITY.md`
- `.github/CODEOWNERS`
- `.gitignore`

## 다음 개선 작업 제안

- `requirements.txt` 또는 `pyproject.toml`을 추가하여 의존성 관리를 명시합니다.
- `tests/` 디렉터리를 만들고 단위/통합 테스트를 도입합니다.
- 실제 GitHub 사용자명 또는 팀명을 `CODEOWNERS`에 반영합니다.
- 프로젝트 목적과 아키텍처를 README에 상세히 문서화합니다.

## 커밋 메시지 예시

- `feat: 새로운 모델 파이프라인 추가`
- `fix: 데이터 로드 버그 수정`
- `docs: README에 설치 가이드 추가`
- `chore: CI 워크플로 이름 수정`
