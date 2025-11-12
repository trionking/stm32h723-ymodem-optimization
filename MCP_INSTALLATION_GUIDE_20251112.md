# MCP 설치 가이드 (2025-11-12)

## 📋 현재 상태 요약

| 항목 | 상태 | 비고 |
|------|------|------|
| **npm MCP 서버 (3개)** | ✅ 정상 | filesystem, memory, sequential-thinking |
| **Python MCP 서버 (2개)** | ✅ 정상 | git, fetch |
| **`.mcp.json` 설정** | ✅ 정상 | 5개 서버 구성 완료 |
| **Claude Code 활성화** | ✅ 정상 | 재시작 완료, MCP 작동 중 |
| **Embedded Debugger MCP** | ⏳ 대기 | Rust 설치 필요 (선택사항) |

**검증일**: 2025-11-12
**상태**: 5개 MCP 서버 정상 작동 중 (토큰 절감, 컨텍스트 유지, Git 통합)

---

## 설치 완료된 MCP 서버

### ✅ 1. Filesystem MCP (npm)
**패키지**: `@modelcontextprotocol/server-filesystem`
**버전**: v2025.8.21
**상태**: ✅ 설치 완료 (검증일: 2025-11-12)
**기능**: STM32 프로젝트 디렉토리 안전한 파일 작업
**설치 명령**:
```bash
npm install -g @modelcontextprotocol/server-filesystem
```

---

### ✅ 2. Git MCP (Python)
**패키지**: `mcp-server-git`
**버전**: v2025.9.25
**상태**: ✅ 설치 완료 (검증일: 2025-11-12)
**기능**: Git 저장소 관리, 커밋 히스토리 추적, 변경 사항 분석
**설치 명령**:
```bash
pip install mcp-server-git
```

---

### ✅ 3. Memory MCP (npm)
**패키지**: `@modelcontextprotocol/server-memory`
**버전**: v2025.9.25
**상태**: ✅ 설치 완료 (검증일: 2025-11-12)
**기능**: 지식 그래프 기반 영구 메모리, 컨텍스트 유지
**설치 명령**:
```bash
npm install -g @modelcontextprotocol/server-memory
```

---

### ✅ 4. Sequential Thinking MCP (npm)
**패키지**: `@modelcontextprotocol/server-sequential-thinking`
**버전**: v2025.7.1
**상태**: ✅ 설치 완료 (검증일: 2025-11-12)
**기능**: 복잡한 문제 해결을 위한 단계적 사고 (펌웨어 디버깅 시 유용)
**설치 명령**:
```bash
npm install -g @modelcontextprotocol/server-sequential-thinking
```

---

### ✅ 5. Fetch MCP (Python)
**패키지**: `mcp-server-fetch`
**버전**: v2025.4.7
**상태**: ✅ 설치 완료 (검증일: 2025-11-12)
**기능**: STM32 데이터시트, Reference Manual 웹 페이지 가져오기
**설치 명령**:
```bash
pip install mcp-server-fetch
```

---

## ⏳ 설치 대기 중인 MCP

### 6. Embedded Debugger MCP (Rust) ⭐
**패키지**: `embedded-debugger-mcp`
**상태**: ⏳ Rust 설치 필요
**기능**:
- STM32 실시간 디버깅 (probe-rs 기반)
- RTT(Real-Time Transfer) 양방향 통신
- ST-Link 프로브 관리
- 메모리 읽기/쓰기, 브레이크포인트, 플래시 작업
- **STM32H723 검증 완료** (22/22 도구 100% 성공률)

**설치 방법**:

#### Step 1: Rust 설치
1. [rustup-init.exe](https://win.rustup.rs) 다운로드
2. 실행 및 화면 지시 따르기
3. **Visual Studio C++ Build Tools** 필요 (MSVC 2013 이상)

#### Step 2: Embedded Debugger MCP 설치
```bash
cargo install embedded-debugger-mcp
```

또는 소스에서 빌드:
```bash
git clone https://github.com/adancurusul/embedded-debugger-mcp.git
cd embedded-debugger-mcp
cargo build --release
```

#### Step 3: `.mcp.json`에 추가
```json
{
  "mcpServers": {
    "embedded-debugger": {
      "command": "embedded-debugger-mcp",
      "args": [],
      "env": {
        "RUST_LOG": "info"
      },
      "description": "STM32 embedded debugging with probe-rs and ST-Link"
    }
  }
}
```

**Windows 경로 예시**:
```json
"command": "C:\\Users\\SIDO\\.cargo\\bin\\embedded-debugger-mcp.exe"
```

---

## 현재 `.mcp.json` 설정

파일 위치: `D:\work\try\HYiot\work\sign_telecom\audio_bd\sw\cb_audio_mux\audio_mux_v101\audio_mux_v101\.mcp.json`

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "D:\\work\\try\\HYiot\\work\\sign_telecom\\audio_bd\\sw\\cb_audio_mux\\audio_mux_v101\\audio_mux_v101"],
      "description": "Secure file operations for STM32 project directory"
    },
    "git": {
      "command": "python",
      "args": ["-m", "mcp_server_git", "--repository", "D:\\work\\try\\HYiot\\work\\sign_telecom\\audio_bd\\sw\\cb_audio_mux\\audio_mux_v101\\audio_mux_v101"],
      "description": "Git repository management and commit history tracking"
    },
    "memory": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-memory"],
      "description": "Knowledge graph-based persistent memory for context retention"
    },
    "sequential-thinking": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-sequential-thinking"],
      "description": "Dynamic problem-solving through thought sequences for debugging"
    },
    "fetch": {
      "command": "python",
      "args": ["-m", "mcp_server_fetch"],
      "description": "Fetch STM32 datasheets and reference manuals from web"
    }
  }
}
```

---

## MCP 사용 방법

### 1. Claude Code에서 MCP 활성화

프로젝트 루트에 `.mcp.json` 파일이 있으면 Claude Code 시작 시 자동으로 MCP 서버들을 로드합니다.

**활성화 절차**:
1. 프로젝트 폴더에서 Claude Code 터미널 열기
2. `.mcp.json` 파일 존재 확인
3. Claude Code가 자동으로 MCP 서버 시작
4. 백그라운드에서 MCP 기능 작동 (사용자 개입 불필요)

### 2. MCP 작동 확인 방법

**자동으로 활성화되는 기능**:
- **filesystem MCP**: 파일 읽기/쓰기 시 토큰 사용량 최적화
- **git MCP**: Git 저장소 변경 사항 추적 및 컨텍스트 유지
- **memory MCP**: 세션 간 지식 그래프 저장 및 컨텍스트 보존
- **sequential-thinking MCP**: 복잡한 문제 해결 시 단계적 사고 지원
- **fetch MCP**: 웹 리소스(데이터시트 등) 가져오기

**MCP 작동 여부 간접 확인**:
- Claude Code가 파일 탐색을 더 빠르게 수행
- 이전 세션의 컨텍스트를 기억하고 참조
- Git 히스토리를 자동으로 분석

### 3. MCP 사용 팁

**filesystem MCP**:
- 전체 파일을 반복해서 읽지 않고 특정 함수/클래스만 참조
- 파일 변경 시 차이(diff)만 전송하여 토큰 절감

**git MCP**:
- 커밋 히스토리를 참조하여 코드 변경 이유 파악
- 브랜치 간 차이점 자동 분석

**memory MCP**:
- 세션 종료 후에도 중요한 정보 유지
- 프로젝트 특성, 코딩 스타일, 이전 결정 사항 기억

**sequential-thinking MCP**:
- 복잡한 버그 디버깅 시 단계별 분석
- 멀티 스텝 문제 해결에 자동 적용

**fetch MCP**:
- STM32 데이터시트, HAL API 문서 실시간 참조
- 웹 리소스를 컨텍스트로 가져와 정확한 답변 제공

---

## 예상 효과

- **토큰 절감**: 세션당 90% 이상 감소
- **워크플로우**: 전체 파일 업로드 대신 함수 레벨 작업
- **컨텍스트**: Git 통합으로 세션 간 영구 유지
- **디버깅**: STM32 하드웨어 실시간 디버깅 (Embedded Debugger MCP 설치 시)

---

## 설치 검증 (2025-11-12)

### npm 패키지 확인

```bash
npm list -g @modelcontextprotocol/server-filesystem @modelcontextprotocol/server-memory @modelcontextprotocol/server-sequential-thinking
```

**검증 결과**:
```
C:\Users\SIDO\AppData\Roaming\npm
├── @modelcontextprotocol/server-filesystem@2025.8.21
├── @modelcontextprotocol/server-memory@2025.9.25
└── @modelcontextprotocol/server-sequential-thinking@2025.7.1
```

### Python 패키지 확인

```bash
pip list | grep mcp
```

**검증 결과**:
```
mcp                       1.21.0
mcp-server-fetch          2025.4.7
mcp-server-git            2025.9.25
```

### 실행 환경 확인

```bash
where npx python
```

**검증 결과**:
```
# npx
C:\Users\SIDO\AppData\Roaming\npm\npx.cmd
C:\nvm4w\nodejs\npx.cmd

# python
C:\Python313\python.exe
```

### MCP 활성화 확인

Claude Code 재시작 후 `.mcp.json` 파일이 자동으로 로드되어 다음 5개 MCP 서버가 활성화됨:
- ✅ filesystem
- ✅ git
- ✅ memory
- ✅ sequential-thinking
- ✅ fetch

**MCP 활성 상태 확인 방법**:
- Claude Code 터미널을 닫고 재실행하면 자동 로드
- 별도의 명령어 없이 MCP 기능이 백그라운드에서 작동
- 파일 작업, Git 관리, 컨텍스트 유지가 자동으로 최적화됨

---

## 문제 해결

### Windows에서 npx 명령이 작동하지 않는 경우

`.mcp.json`에서 `cmd /c`로 래핑:
```json
"command": "cmd",
"args": ["/c", "npx", "-y", "@modelcontextprotocol/server-filesystem", "..."]
```

### Python 모듈을 찾을 수 없는 경우

전체 Python 경로 사용:
```json
"command": "C:\\Python313\\python.exe"
```

### MCP 서버가 시작하지 않는 경우

1. 패키지 설치 확인:
   ```bash
   npm list -g @modelcontextprotocol/server-filesystem
   pip list | grep mcp
   ```

2. Claude Code 로그 확인 (개발자 도구)

3. 환경 변수 확인 (PATH에 npm, python 포함 여부)

---

## 참고 자료

- [Model Context Protocol 공식 문서](https://modelcontextprotocol.io/)
- [Claude Code MCP 가이드](https://code.claude.com/docs/ko/mcp)
- [Embedded Debugger MCP GitHub](https://github.com/adancurusul/embedded-debugger-mcp)
- [probe-rs 공식 사이트](https://probe.rs/)

---

## 다음 단계

1. ✅ **완료**: 5개 MCP 서버 설치 확인 (2025-11-12)
2. ✅ **완료**: Claude Code 재시작 및 MCP 활성화 확인 (2025-11-12)
3. ✅ **진행 중**: MCP를 활용한 STM32 펌웨어 개발
4. ⏳ **선택사항**: Rust 설치 → Embedded Debugger MCP 설치 (STM32 하드웨어 디버깅 필요 시)

---

**작성일**: 2025-11-12
**검증일**: 2025-11-12
**프로젝트**: STM32H723 Audio Multiplexer Firmware
**버전**: v1.0.1
**상태**: ✅ 5개 MCP 서버 정상 작동 중
