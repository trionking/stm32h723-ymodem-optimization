# 프로젝트 업데이트 요약

**날짜**: 2025-11-01
**프로젝트**: Audio Mux v1.00 - Multi-Channel Audio Streaming System

---

## 📋 업데이트 개요

### 기존 시스템
- STM32H723 (Master) → SPI DMA → STM32H523 x3 (Slave)
- SD 카드 WAV 파일 재생
- 6채널 독립 오디오 출력

### 추가된 기능
1. **PC ↔ Main Board UART 통신**
   - 텍스트 기반 명령 프로토콜
   - 시스템 제어 및 상태 조회

2. **Y-MODEM 파일 전송**
   - PC에서 WAV 파일 업로드
   - SD 카드에 채널별 저장

3. **Python GUI 프로그램**
   - Windows용 제어 패널
   - 시리얼 통신 인터페이스
   - 오디오 파일 변환
   - Y-MODEM 전송

---

## 📁 프로젝트 구조

```
audio_mux_v101/
├── Core/
│   ├── Inc/
│   │   ├── uart_command.h          # NEW: UART 명령 파서
│   │   ├── ymodem.h                # NEW: Y-MODEM 프로토콜
│   │   ├── command_handler.h       # NEW: 명령 핸들러
│   │   ├── audio_stream.h          # 오디오 스트리밍
│   │   ├── spi_protocol.h          # SPI 프로토콜
│   │   ├── wav_parser.h            # WAV 파서
│   │   └── user_def.h              # 사용자 정의
│   └── Src/
│       ├── uart_command.c          # NEW: UART 명령 파서 구현
│       ├── ymodem.c                # NEW: Y-MODEM 구현
│       ├── command_handler.c       # NEW: 명령 핸들러 구현
│       ├── audio_stream.c          # 오디오 스트리밍 구현
│       ├── spi_protocol.c          # SPI 프로토콜 구현
│       ├── wav_parser.c            # WAV 파서 구현
│       └── user_def.c              # MODIFIED: UART 초기화 추가
│
├── audio_win_app/                  # NEW: PC GUI 프로그램
│   ├── main.py                     # 메인 애플리케이션
│   ├── mainwindow.ui               # Qt Designer UI
│   ├── serial_comm.py              # 시리얼 통신 모듈
│   ├── ymodem.py                   # Y-MODEM 프로토콜
│   ├── audio_converter.py          # 오디오 변환
│   ├── requirements.txt            # Python 패키지
│   └── README.md                   # 사용 설명서
│
├── CLAUDE.md                       # 프로젝트 전체 문서
├── AUDIO_STREAMING_IMPLEMENTATION.md  # 오디오 시스템 구현 요약
├── SLAVE_IMPLEMENTATION_SPEC.md    # 슬레이브 구현 명세
├── PC_UART_PROTOCOL.md             # NEW: UART 프로토콜 명세
├── FIRMWARE_UART_YMODEM_GUIDE.md   # NEW: 펌웨어 구현 가이드
└── PROJECT_UPDATE_SUMMARY.md       # NEW: 이 파일
```

---

## 🔧 펌웨어 변경사항

### 1. 새로 추가된 파일

#### uart_command.c/h
**목적**: UART2를 통한 PC 명령 파싱
**주요 기능**:
- 텍스트 명령 수신 및 파싱
- 명령-응답 인터페이스
- 인터럽트 기반 비동기 수신

**핵심 함수**:
```c
void uart_command_init(UART_HandleTypeDef *huart);
void parse_and_execute_command(char *cmd_line);
void uart_send_response(const char *format, ...);
void uart_send_error(int code, const char *message);
```

#### ymodem.c/h
**목적**: Y-MODEM 프로토콜 파일 수신
**주요 기능**:
- 1024바이트 블록 수신
- CRC-16 체크섬 검증
- 파일 정보 파싱
- 재전송 메커니즘

**핵심 함수**:
```c
YmodemResult_t ymodem_receive(UART_HandleTypeDef *huart, const char *file_path);
```

#### command_handler.c/h
**목적**: 명령별 처리 로직
**주요 기능**:
- HELLO, STATUS, PLAY, STOP, VOLUME 등 명령 처리
- LS, DELETE 파일 관리
- UPLOAD Y-MODEM 시작

**핵심 함수**:
```c
void execute_command(UartCommand_t *cmd);
```

### 2. 수정된 파일

#### user_def.c
**변경사항**:
- `run_proc()`에 UART 초기화 추가
```c
// UART 명령 시스템 초기화
uart_command_init(&huart2);
```

#### stm32h7xx_it.c
**변경사항**:
- UART2 인터럽트 핸들러 추가
- RX 완료 콜백 추가

#### Makefile
**변경사항**:
- 새 소스 파일 추가:
  - uart_command.c
  - ymodem.c
  - command_handler.c

---

## 💻 PC 프로그램 (Python GUI)

### 개요
- **언어**: Python 3.8+
- **프레임워크**: PyQt5
- **UI**: Qt Designer (.ui 파일 분리)

### 주요 기능

#### 1. 시리얼 통신
- 포트 자동 검색
- 115200 baud 통신
- 실시간 로그 표시

#### 2. 파일 업로드
- Y-MODEM 프로토콜
- 진행률 표시
- 자동 변환 옵션

#### 3. 오디오 변환
- 입력: MP3, WAV, FLAC, OGG 등
- 출력: 32kHz 16-bit Mono WAV
- FFmpeg 기반

### 사용 방법

```bash
# 설치
cd audio_win_app
pip install -r requirements.txt

# FFmpeg 설치 (필수)
choco install ffmpeg

# 실행
python main.py
```

### UI 구성

```
┌─────────────────────────────────────┐
│ Serial Connection                   │
│ [Port] [Refresh] [Connect]          │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ File Upload                         │
│ File: [________] [Browse]           │
│ Channel: [0▼] [Upload (Y-MODEM)]    │
│ [Progress Bar]                      │
│ ☑ Auto-convert to 32kHz 16bit Mono  │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ Audio Converter                     │
│ Input:  [________] [Browse]         │
│ Output: [________] [Convert to WAV] │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ Communication Log                   │
│ [10:23:45] >> HELLO                 │
│ [10:23:45] << OK AUDIO_MUX v1.00    │
│ ...                                 │
│ [Clear Log]                         │
└─────────────────────────────────────┘
```

---

## 📡 UART 통신 프로토콜

### 설정
- **Port**: UART2
- **Baud**: 115200 bps
- **Format**: 8N1 (8-bit, No parity, 1 stop bit)
- **Encoding**: UTF-8

### 명령 포맷
```
<COMMAND> [ARG1] [ARG2] ...\r\n
```

### 주요 명령어

| 명령어 | 인수 | 기능 |
|--------|------|------|
| `HELLO` | - | 버전 확인 |
| `STATUS` | - | 시스템 상태 |
| `PLAY` | CH PATH | 파일 재생 |
| `STOP` | CH | 재생 정지 |
| `VOLUME` | CH LEVEL | 볼륨 설정 |
| `UPLOAD` | CH FILE | Y-MODEM 업로드 |
| `LS` | [PATH] | 파일 목록 |
| `DELETE` | PATH | 파일 삭제 |

### 응답 포맷
```
OK [DATA]\r\n              # 성공
ERR <CODE> <MESSAGE>\r\n   # 에러
```

자세한 내용: `PC_UART_PROTOCOL.md` 참조

---

## 📊 Y-MODEM 프로토콜

### 특징
- 1024바이트 블록 전송
- CRC-16 체크섬
- 파일명 및 크기 자동 전송
- 재전송 메커니즘

### 전송 시퀀스
```
PC → Board: UPLOAD 0 test.wav\r\n
PC ← Board: OK Ready for Y-MODEM\r\n

[Y-MODEM 전송 시작]
PC → Board: [파일 정보 패킷]
PC ← Board: [ACK]
PC → Board: [데이터 패킷 1]
PC ← Board: [ACK]
... (반복) ...
PC → Board: [EOT]
PC ← Board: [ACK]

PC ← Board: OK Upload complete /audio/ch0/test.wav\r\n
```

### 파일 저장 경로
```
/audio/ch<CHANNEL>/<FILENAME>
```

예: `/audio/ch0/test.wav`

---

## 🔨 빌드 및 테스트

### 펌웨어 빌드

1. 새 소스 파일 Makefile에 추가 확인
2. 빌드:
```bash
cd Debug
make clean
make -j8 all
```

3. 플래시:
```
Ctrl+Shift+B → Flash (Debug)
```

### PC 프로그램 테스트

1. 가상 환경 생성 (권장):
```bash
python -m venv venv
venv\Scripts\activate
```

2. 패키지 설치:
```bash
pip install -r requirements.txt
```

3. 실행:
```bash
python main.py
```

4. 테스트 시나리오:
   - ✅ 포트 연결
   - ✅ HELLO 명령
   - ✅ 파일 선택
   - ✅ 오디오 변환
   - ✅ Y-MODEM 업로드
   - ✅ 로그 확인

---

## 📚 문서

| 문서 | 내용 |
|------|------|
| `CLAUDE.md` | 전체 프로젝트 구조 및 빌드 방법 |
| `README.md` | 프로젝트 개요 및 SD 카드 캐시 일관성 문제 해결 |
| `AUDIO_STREAMING_IMPLEMENTATION.md` | 오디오 시스템 구현 상세 |
| `SLAVE_IMPLEMENTATION_SPEC.md` | STM32H523 슬레이브 펌웨어 명세 |
| `PC_UART_PROTOCOL.md` | UART 명령 프로토콜 완전 명세 |
| `FIRMWARE_UART_YMODEM_GUIDE.md` | 펌웨어 UART/Y-MODEM 구현 가이드 |
| `YMODEM_UPLOAD_GUIDE.md` | **NEW** Y-MODEM 업로드 시스템 완전 가이드 (USB CDC) |
| `SDCARD_CACHE_COHERENCY_FIX.md` | SD 카드 D-Cache 일관성 문제 해결 |
| `audio_win_app/README.md` | PC 프로그램 사용 설명서 |
| `PROJECT_UPDATE_SUMMARY.md` | 이 문서 - 프로젝트 전체 진행사항 요약 |

---

## ✅ 구현 체크리스트

### 펌웨어 (STM32H723)

- [x] UART2 초기화
- [x] 명령 파서 구현
- [x] 명령 핸들러 구현
- [x] HELLO, STATUS 명령
- [x] PLAY, STOP, STOPALL, VOLUME 명령
- [x] LS, DELETE 명령
- [x] Y-MODEM 수신 구현
- [x] UPLOAD 명령 및 통합 (USB CDC)
- [x] SD 카드 디렉토리 자동 생성
- [x] FORMAT 명령 (SD 카드 포맷)
- [x] RESET 명령
- [x] USB CDC 명령 입력 지원
- [x] 빌드 테스트
- [x] 하드웨어 테스트 (Y-MODEM 업로드 성공)

### PC 프로그램 (Python)

- [x] PyQt5 GUI 구현
- [x] UI 파일 (.ui) 분리
- [x] 시리얼 통신 모듈
- [x] 명령 송수신
- [x] Y-MODEM 전송 구현 (USB CDC)
- [x] 오디오 변환 모듈 (FFmpeg)
- [x] 로그 출력 (ANSI 색상 지원)
- [x] 진행률 표시
- [x] 명령 입력창 (Enter로 전송)
- [x] USB CDC 통신 지원
- [x] 실제 하드웨어 테스트 (Y-MODEM 업로드 성공)
- [ ] FFmpeg 설치 가이드 (README에 일부 포함)
- [ ] 상세 사용자 매뉴얼

---

## 🚀 다음 단계

### ✅ 완료된 단기 목표 (2025-10-26 ~ 2025-11-01)
1. ✅ 펌웨어 구현 완료
   - uart_command.c, ymodem.c, command_handler.c 추가
   - 빌드 및 기본 테스트
2. ✅ PC 프로그램 실행 확인
   - FFmpeg 연동
   - USB CDC 연결 테스트
3. ✅ 통합 테스트
   - UART 명령 테스트
   - Y-MODEM 파일 업로드 테스트 성공

### 🔧 현재 진행 중 (2025-11-01 ~)
1. **오디오 재생 안정성 테스트**
   - PLAY/STOP 명령 장시간 테스트
   - 6채널 개별 제어 검증
   - 볼륨 제어 검증

2. **대용량 파일 전송 검증**
   - 50MB 이상 WAV 파일 전송 테스트
   - 장시간 연속 업로드 안정성 확인

### 중기 (1개월)
1. **슬레이브 펌웨어 구현**
   - `SLAVE_IMPLEMENTATION_SPEC.md` 참조
   - 3개 STM32H523 슬레이브 보드 펌웨어 개발
   - SPI 프로토콜 통신 검증

2. **전체 시스템 통합**
   - Main ↔ 3x Slave 연동
   - 6채널 동시 재생 테스트
   - 오디오 품질 검증 (32kHz 16-bit)

### 장기 (3개월)
1. **기능 확장**
   - 채널별 믹싱 기능
   - 이퀄라이저
   - 실시간 모니터링 (DIP 스위치, 로터리 엔코더)

2. **성능 최적화**
   - 메모리 사용량 최적화
   - CPU 부하 최소화
   - DMA 전송 효율 개선

3. **안정화 및 양산 준비**
   - 장시간 테스트 (24시간+)
   - 에러 복구 시나리오 구현
   - 제품 매뉴얼 작성

---

## ✅ 해결된 주요 문제

### 1. Y-MODEM USB CDC 타이밍 최적화 (2025-11-01)

**문제**: USB CDC를 통한 Y-MODEM 전송 시 타이밍 이슈
- 증상: 전송 중 랜덤 패킷 실패, Python이 ACK를 제때 받지 못함
- 원인: USB CDC 비동기 전송, ACK가 Python에 도달하기 전에 다음 패킷 전송

**해결책**:
```c
// ymodem.c transmit_byte()
HAL_Delay(2);  // USB 전송 완료 대기

// ymodem.c 패킷 루프
HAL_Delay(8);  // 다음 패킷 준비 시간
```
총 10ms 지연으로 안정적 전송 확보

### 2. FAT Filesystem 손상 문제

**문제**: 대용량 파일 전송 후 SD 카드 FAT 테이블 손상
- 증상: 짧은 파일 OK, 긴 파일(>1MB) 전송 후 파일명 깨짐
- 원인: f_sync() 너무 자주 호출 (100 패킷마다)

**해결책**:
```c
// 1000 패킷(1MB)마다 f_sync()
if (packet_number % 1000 == 0) {
    f_sync(&file);
}
```

### 3. SD 카드 디렉토리 자동 생성

**문제**: 디렉토리가 없을 때 파일 쓰기 실패
**해결책**: ymodem.c에서 파일 열기 전 자동으로 상위/하위 디렉토리 생성
```c
f_mkdir("/audio");
f_mkdir("/audio/ch0");
```

### 4. SD 카드 포맷 기능 추가

**문제**: FAT 손상 시 복구 방법 없음
**해결책**: FORMAT 명령 추가, f_mkfs()로 FAT32 포맷
```
>> FORMAT
OK SD card formatted (FAT32)
```

## 🐛 알려진 이슈 (남은 작업)

1. **펌웨어**
   - [x] Y-MODEM 수신 타임아웃 조정 (완료: 5s)
   - [x] SD 카드 디렉토리 자동 생성 (완료)
   - [ ] UART RX 버퍼 오버플로우 처리 개선 (현재 안정적 동작 중)

2. **PC 프로그램**
   - [ ] FFmpeg 경로 자동 감지 개선
   - [ ] 대용량 파일 (>50MB) 전송 장시간 테스트
   - [ ] UI 응답성 개선 (현재 안정적 동작 중)

3. **통합**
   - [x] 명령 타임아웃 정책 수립 (완료: 5s)
   - [ ] 에러 복구 시나리오 문서화

---

## 📞 문의 및 지원

- **펌웨어**: 펌웨어 개발팀
- **PC 프로그램**: Python/PyQt 담당
- **프로토콜**: 시스템 아키텍트

---

**작성자**: System Integration Team
**최종 수정**: 2025-11-01
**버전**: 2.0 (Y-MODEM USB CDC 업로드 시스템 완성)
