# MCP 설치 가이드 (2025-11-12)

## 설치 완료된 MCP 서버

### ✅ 1. Filesystem MCP (npm)
**패키지**: `@modelcontextprotocol/server-filesystem`
**상태**: ✅ 설치 완료
**기능**: STM32 프로젝트 디렉토리 안전한 파일 작업
**설치 명령**:
```bash
npm install -g @modelcontextprotocol/server-filesystem
```

---

### ✅ 2. Git MCP (Python)
**패키지**: `mcp-server-git`
**상태**: ✅ 설치 완료
**기능**: Git 저장소 관리, 커밋 히스토리 추적, 변경 사항 분석
**설치 명령**:
```bash
pip install mcp-server-git
```

---

### ✅ 3. Memory MCP (npm)
**패키지**: `@modelcontextprotocol/server-memory`
**상태**: ✅ 설치 완료
**기능**: 지식 그래프 기반 영구 메모리, 컨텍스트 유지
**설치 명령**:
```bash
npm install -g @modelcontextprotocol/server-memory
```

---

### ✅ 4. Sequential Thinking MCP (npm)
**패키지**: `@modelcontextprotocol/server-sequential-thinking`
**상태**: ✅ 설치 완료
**기능**: 복잡한 문제 해결을 위한 단계적 사고 (펌웨어 디버깅 시 유용)
**설치 명령**:
```bash
npm install -g @modelcontextprotocol/server-sequential-thinking
```

---

### ✅ 5. Fetch MCP (Python)
**패키지**: `mcp-server-fetch`
**상태**: ✅ 설치 완료
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

Claude Code를 재시작하면 자동으로 `.mcp.json` 파일을 읽어 MCP 서버들을 활성화합니다.

### 2. MCP 명령어 사용

- `/mcp` - MCP 서버 상태 확인 및 인증
- `@서버이름:프로토콜://경로` - MCP 리소스 참조

### 3. MCP 상태 확인

```bash
claude mcp list
```

---

## 예상 효과

- **토큰 절감**: 세션당 90% 이상 감소
- **워크플로우**: 전체 파일 업로드 대신 함수 레벨 작업
- **컨텍스트**: Git 통합으로 세션 간 영구 유지
- **디버깅**: STM32 하드웨어 실시간 디버깅 (Embedded Debugger MCP 설치 시)

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

1. ✅ Claude Code 재시작하여 MCP 활성화 확인
2. ⏳ Rust 설치 (선택사항 - Embedded Debugger 사용 시)
3. ⏳ Embedded Debugger MCP 설치 및 설정
4. ✅ MCP를 활용한 STM32 펌웨어 개발 시작

---

**작성일**: 2025-11-12
**프로젝트**: STM32H723 Audio Multiplexer Firmware
**버전**: v1.0.1
