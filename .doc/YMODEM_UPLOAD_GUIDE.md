# Y-MODEM 파일 업로드 가이드

**STM32H723 오디오 멀티플렉서 - WAV 파일 업로드 시스템**

작성일: 2025-01-31

---

## 목차

1. [시스템 개요](#시스템-개요)
2. [하드웨어 구성](#하드웨어-구성)
3. [소프트웨어 구성](#소프트웨어-구성)
4. [Y-MODEM 프로토콜](#y-modem-프로토콜)
5. [구현 과정](#구현-과정)
6. [문제 해결](#문제-해결)
7. [최종 설정](#최종-설정)
8. [사용 방법](#사용-방법)
9. [지원 명령어](#지원-명령어)

---

## 시스템 개요

### 목적
PC에서 STM32H723 메인 보드로 WAV 파일을 USB CDC를 통해 전송하여 SD 카드에 저장

### 주요 스펙
- **전송 프로토콜**: Y-MODEM (CRC-16)
- **통신 인터페이스**: USB CDC Virtual COM Port
- **파일 시스템**: FAT32 (FatFs)
- **저장 매체**: SD 카드 (SDMMC1, 4-bit mode)
- **대상 파일**: WAV 파일 (32kHz, 16-bit, Mono)

### 전송 성능
- **파일 크기**: 12.7MB (테스트 기준)
- **전송 시간**: 약 4-5분
- **전송 속도**: ~42-52 KB/s
- **안정성**: 타임아웃 없이 완료

---

## 하드웨어 구성

### MCU
- **모델**: STM32H723ZGT6
- **코어**: ARM Cortex-M7 @ 220MHz
- **메모리**: 1MB Flash, 564KB RAM
- **FPU**: FPv5-D16 (Hard float)

### 페리페럴
```
USB OTG HS     → CDC Virtual COM Port (전송 인터페이스)
SDMMC1         → SD 카드 (4-bit, MDMA)
UART2          → 디버그 출력 (115200 baud)
```

### 메모리 구성
```
0x08000000  FLASH (1024KB)        - 프로그램 코드
0x00000000  ITCMRAM (64KB)        - Stack + Heap
0x20000000  DTCMRAM (128KB)       - 일반 버퍼
0x24000000  RAM_D1_DMA (128KB)    - DMA 버퍼 (캐시 OFF)
0x30000000  RAM_D2 (32KB)         - USB CDC 버퍼 (캐시 OFF)
```

### USB CDC 링 버퍼
- **크기**: 32KB
- **용도**: Y-MODEM 패킷 임시 저장
- **위치**: 일반 RAM (캐시 사용 가능, DMA 미사용)

---

## 소프트웨어 구성

### 펌웨어 (STM32H723)

**주요 모듈:**
```
Core/Src/ymodem.c           - Y-MODEM 수신 구현
Core/Src/ring_buffer.c      - USB CDC용 링 버퍼
Core/Src/command_handler.c  - UART 명령 처리
Core/Src/user_def.c         - SD 카드 초기화
USB_DEVICE/App/usbd_cdc_if.c - USB CDC 인터페이스
```

**파일 구조:**
```
/audio/
  ├── ch0/          - 채널 0 오디오 파일
  ├── ch1/          - 채널 1 오디오 파일
  ├── ch2/          - 채널 2 오디오 파일
  ├── ch3/          - 채널 3 오디오 파일
  ├── ch4/          - 채널 4 오디오 파일
  └── ch5/          - 채널 5 오디오 파일
```

### Python 앱 (PC)

**주요 모듈:**
```
main.py               - 메인 UI 및 제어
serial_comm.py        - 시리얼 통신 (PySerial)
ymodem.py             - Y-MODEM 전송 구현
audio_converter.py    - WAV 변환 (FFmpeg)
```

**UI 구성:**
- 포트 연결 관리
- 파일 선택 및 검증 (32kHz, 16-bit, Mono)
- Y-MODEM 전송 진행률 표시
- 채널별 재생 제어
- 로그 및 명령어 입력창

---

## Y-MODEM 프로토콜

### 프로토콜 특징
- **패킷 크기**: 1024 bytes (STX)
- **CRC**: CRC-16 (CCITT)
- **핸드셰이킹**: Stop-and-wait ARQ
- **흐름 제어**: ACK/NAK

### 전송 흐름
```
PC (Python)                STM32 (Firmware)
    |                            |
    |    << UPLOAD 0 file.wav    |  1. 명령 전송
    |    >> OK Ready for Y-MODEM |
    |                            |
    |    << 'C'                  |  2. CRC 모드 요청
    |    >> [파일 정보 패킷]    |  3. 파일명, 크기 전송
    |    << ACK                  |
    |                            |
    |    >> [데이터 패킷 1]      |  4. 데이터 패킷 전송
    |         - SD write (10ms)  |     (1024 bytes)
    |    << ACK                  |  5. ACK 대기 (핸드셰이킹)
    |                            |
    |    >> [데이터 패킷 2]      |  6. 다음 패킷 반복
    |         - SD write         |
    |    << ACK                  |
    |                            |
    |    ...  (12,414 패킷)      |
    |                            |
    |    >> EOT                  |  7. 전송 완료
    |    << ACK                  |
    |    >> OK Upload complete   |
```

### 패킷 구조
```
┌─────────────────────────────────────────────┐
│ SOH/STX (1) │ 헤더                          │
├─────────────────────────────────────────────┤
│ BLK# (1)    │ 패킷 번호                      │
├─────────────────────────────────────────────┤
│ ~BLK# (1)   │ 패킷 번호 역수                 │
├─────────────────────────────────────────────┤
│ DATA (1024) │ 실제 데이터                    │
├─────────────────────────────────────────────┤
│ CRC-H (1)   │ CRC 상위 바이트                │
├─────────────────────────────────────────────┤
│ CRC-L (1)   │ CRC 하위 바이트                │
└─────────────────────────────────────────────┘
총 1029 bytes
```

---

## 구현 과정

### 1단계: 기본 Y-MODEM 구현

**초기 구현:**
```c
// ymodem.c - 단순 패킷 처리
receive_packet() → f_write(1KB) → send_ACK()
```

**문제점:**
- SD write 중 링 버퍼 오버플로우
- 타임아웃 발생 (30초)
- 전송 실패율 높음

### 2단계: 링 버퍼 크기 증가 시도

**시도:**
```c
#define RING_BUFFER_SIZE 128KB  // 32KB → 128KB
```

**결과:**
- ❌ 실패: 메모리 부족 (오디오 스트리밍용 메모리 필요)

### 3단계: Y-MODEM 핸드셰이킹 활용 (핵심 해결책)

**개선된 구현:**
```c
// ymodem.c - 동기화된 패킷 처리
receive_packet()           // 1. 패킷 수신
    ↓
f_write(1KB)              // 2. SD write (블로킹)
    ↓
send_ACK()                // 3. ACK 전송
    ↓
HAL_Delay(10)             // 4. USB 안정화
```

**핵심 원리:**
- Python이 ACK를 받기 전까지 다음 패킷을 보내지 않음
- SD write 중에는 새 패킷이 오지 않음
- 링 버퍼 오버플로우 방지

### 4단계: USB 타이밍 최적화

**transmit_byte() 함수:**
```c
// ACK 전송 완료 대기
while (hcdc->TxState != 0 && retry < max_retries) {
    retry++;
    HAL_Delay(1);  // USB 전송 완료 확인
}

// Python까지 도착 시간 확보
HAL_Delay(2);  // 추가 안정화
```

**패킷 루프 지연:**
```c
send_ACK();
HAL_Delay(8);  // 다음 패킷 수신 준비 시간
// 총 10ms = transmit_byte(2ms) + 패킷 루프(8ms)
```

### 5단계: 디렉토리 자동 생성

**문제:**
- `/audio/ch0/` 디렉토리 없을 시 f_open() 실패
- FAT 테이블 손상 발생

**해결:**
```c
// ymodem.c - 파일 열기 전
f_mkdir("/audio");          // 상위 디렉토리 생성
f_mkdir("/audio/ch0");      // 하위 디렉토리 생성
f_open(file_path, ...);     // 파일 생성
```

### 6단계: f_sync() 주기 조정

**실험 결과:**
```
f_sync() 없음      → SD write 실패 (FR_DISK_ERR)
f_sync() 100패킷   → FAT 손상
f_sync() 1000패킷  → ✅ 안정적
```

**최종 구현:**
```c
if (packet_number % 1000 == 0) {
    f_sync(&file);  // 1MB마다 FAT 플러시
}
```

---

## 문제 해결

### 문제 1: 타임아웃 (30초)

**증상:**
```
[ERROR] receive_packet: data read failed (expected=1028, got=964)
Python timeout after 30 seconds
```

**원인:**
- 링 버퍼(32KB) 오버플로우
- f_write() 블로킹 중 USB 패킷 계속 수신
- 버퍼 포화 → 데이터 손실 → 타임아웃

**해결:**
- Y-MODEM 핸드셰이킹 활용
- SD write 후 ACK 전송
- Python이 ACK 대기 중 새 패킷 전송 안 함

### 문제 2: USB 타이밍 이슈

**증상:**
```
[ERROR] receive_packet: data read failed (expected=1028, got=964)
Failed at packet 6 (very early)
```

**원인:**
- ACK 전송 후 바로 다음 패킷 수신 시작
- ACK가 Python까지 도착 전에 타임아웃

**해결:**
```c
transmit_byte(ACK);     // 2ms 지연 (USB 전송 완료)
HAL_Delay(8);           // 8ms 추가 (Python 도착 대기)
// 총 10ms
```

### 문제 3: SD write 에러

**증상:**
```
[ERROR] SD write failed: fres=2 (FR_INT_ERR)
[ERROR] SD write failed: fres=1 (FR_DISK_ERR)
```

**원인:**
- 디렉토리 없음
- f_sync() 주기 문제

**해결:**
- 디렉토리 자동 생성
- f_sync() 1000 패킷마다

### 문제 4: FAT 파일 시스템 손상

**증상:**
- 전송 완료 후 파일명 깨짐
- SD 카드 마운트 실패
- Windows에서 읽기 불가

**원인:**
- f_sync() 너무 자주 호출 (100 패킷마다)
- FAT 테이블 업데이트 충돌

**해결:**
- f_sync() 주기 증가: 100 → 1000 패킷
- 호출 횟수 감소: 124회 → 12회

---

## 최종 설정

### 펌웨어 설정

**타이밍:**
```c
// ymodem.h
#define YMODEM_TIMEOUT_MS  5000   // 패킷 수신 타임아웃: 5초

// ymodem.c
transmit_byte() 내부:  2ms (USB 전송 완료 대기)
패킷 루프 지연:       8ms (다음 패킷 수신 준비)
총 지연:             10ms
```

**버퍼:**
```c
// ring_buffer.h
#define RING_BUFFER_SIZE  32768   // 32KB (약 30개 패킷)
```

**f_sync() 주기:**
```c
// ymodem.c
if (packet_number % 1000 == 0) {
    f_sync(&file);  // 1MB(1000 패킷)마다 FAT 플러시
}
```

**디렉토리:**
```c
// 파일 열기 전 자동 생성
f_mkdir("/audio");
f_mkdir("/audio/ch0");
```

### 메모리 사용량

**빌드 결과:**
```
text    data    bss      dec      hex    filename
129004   352   191584   320940   4e5ac  audio_mux_v101.elf

- Code:   129 KB (Flash)
- Data:   0.3 KB (RAM)
- BSS:    187 KB (RAM, 32KB는 링 버퍼)
```

### 성능 지표

**전송 시간 분석:**
```
12.7MB 파일 (12,414 패킷)

지연 시간:    10ms × 12,414 = 124초 (2분 4초)
SD write:     10ms × 12,414 = 124초 (2분 4초)
f_sync():     100ms × 12회 = 1.2초
USB 오버헤드: ~20초

총 예상 시간: 약 4분 30초
실제 시간:    약 4-5분
```

---

## 사용 방법

### 1. 하드웨어 연결

```
PC <--USB--> STM32H723 <--SDMMC--> SD 카드
              |
              └--UART2--> 디버그 콘솔 (선택사항)
```

### 2. 펌웨어 플래시

```bash
cd audio_mux_v101/Debug
make -j all

# ST-Link로 플래시
STM32_Programmer_CLI.exe -c port=SWD -w audio_mux_v101.elf -v -rst
```

### 3. Python 앱 실행

```bash
cd audio_win_app
python main.py
```

### 4. 파일 업로드 절차

**단계 1: 연결**
- 포트 선택 (예: COM8)
- 보드레이트: 115200
- "연결" 버튼 클릭

**단계 2: 파일 준비**
- "파일 선택" 클릭
- WAV 파일 선택 (32kHz, 16-bit, Mono 검증됨)
- 또는 "오디오 변환기"로 변환

**단계 3: 업로드**
- 채널 선택 (0~5)
- "업로드" 버튼 클릭
- 진행률 확인

**단계 4: 확인**
```
>> LS 0                    # 파일 목록 확인
<< OK LS /audio/ch0/
   - file.wav (12711804 bytes)

>> PLAY 0 file.wav         # 재생 테스트
<< OK Playing file.wav
```

### 5. SD 카드 포맷 (필요 시)

```
>> FORMAT
<< [INFO] Formatting SD card...
<< [WARN] All data will be erased!
<< [INFO] SD card formatted successfully (FAT32)
<< OK SD card formatted (FAT32)
```

---

## 지원 명령어

### 시스템 명령

| 명령어 | 설명 | 예시 |
|--------|------|------|
| `HELLO` | 연결 테스트 | `HELLO` |
| `STATUS` | 전체 채널 상태 | `STATUS` |
| `RESET` | 보드 리셋 | `RESET` |
| `FORMAT` | SD 카드 포맷 (FAT32) | `FORMAT` |

### 파일 관리

| 명령어 | 설명 | 예시 |
|--------|------|------|
| `LS [ch]` | 파일 목록 | `LS 0` |
| `DELETE <ch> <file>` | 파일 삭제 | `DELETE 0 old.wav` |
| `UPLOAD <ch> <file>` | Y-MODEM 업로드 | `UPLOAD 0 new.wav` |

### 재생 제어

| 명령어 | 설명 | 예시 |
|--------|------|------|
| `PLAY <ch> <file>` | 재생 | `PLAY 0 test.wav` |
| `STOP <ch>` | 정지 | `STOP 0` |
| `STOPALL` | 전체 정지 | `STOPALL` |
| `VOLUME <ch> <vol>` | 볼륨 (0-100) | `VOLUME 0 75` |

### 응답 형식

**성공:**
```
OK [데이터]
```

**에러:**
```
ERR <코드> <메시지>
```

**정보:**
```
INFO: <메시지>
```

---

## 트러블슈팅

### 전송 실패 (타임아웃)

**증상:**
```
Y-MODEM Error: Failed to send packet XXX
```

**해결책:**
1. USB 케이블 확인 (데이터 케이블 사용)
2. 포트 재연결
3. SD 카드 재삽입
4. 보드 리셋 후 재시도

### 파일 손상

**증상:**
- 전송 완료 후 파일명 깨짐
- 파일 크기 0
- Windows에서 열기 실패

**해결책:**
```
>> FORMAT          # SD 카드 포맷
>> UPLOAD 0 file.wav   # 재전송
```

### SD 카드 인식 실패

**증상:**
```
[ERROR] f_mount failed: fres=3 (FR_NOT_READY)
```

**해결책:**
1. SD 카드 제거 후 재삽입
2. `FORMAT` 명령 실행
3. 다른 SD 카드 시도 (FAT32, 32GB 이하 권장)

### USB CDC 연결 실패

**증상:**
```
Error: Failed to connect
```

**해결책:**
1. 장치 관리자에서 COM 포트 확인
2. 다른 USB 포트 시도
3. USB 드라이버 재설치
4. 보드 전원 OFF/ON

---

## 기술적 고려사항

### 링 버퍼 설계

**크기 선정:**
- 최소: 1029 bytes (패킷 1개)
- 선택: 32KB (패킷 약 30개)
- 이유: 오디오 스트리밍용 메모리 확보

**오버플로우 방지:**
- Y-MODEM 핸드셰이킹 활용
- SD write 중 Python 대기
- 버퍼 사용률 < 10%

### SD 카드 쓰기 최적화

**클러스터 크기:**
- FAT32 기본: 4KB ~ 32KB
- f_write(1KB) → 여러 번 호출로 분산

**캐시 전략:**
- f_write(): 메모리 버퍼에 쓰기
- f_sync(): 디스크에 플러시
- f_close(): 자동 sync

### USB CDC 타이밍

**TxState 확인:**
```c
USBD_CDC_HandleTypeDef *hcdc = ...;
while (hcdc->TxState != 0) {  // 전송 중
    HAL_Delay(1);
}
```

**안정화 지연:**
- USB 컨트롤러 → PC: ~2ms
- PC OS 처리: ~5-10ms
- Python 수신: ~1ms
- 총: ~10ms 필요

### FatFs 안정성

**f_sync() 타이밍:**
- 너무 자주: FAT 손상 위험
- 너무 드물게: 쓰기 실패
- 최적: 1MB(1000 패킷)마다

**에러 처리:**
```c
FRESULT fres = f_write(...);
if (fres != FR_OK) {
    // 즉시 전송 중단
    // CAN 전송
    // f_close()
}
```

---

## 참고 문서

### 관련 파일
- `CLAUDE.md` - 프로젝트 전체 구조
- `PC_UART_PROTOCOL.md` - UART 명령 프로토콜
- `FIRMWARE_UART_YMODEM_GUIDE.md` - 펌웨어 UART/Y-MODEM 구현

### 외부 링크
- [Y-MODEM Protocol](http://pauillac.inria.fr/~doligez/zmodem/ymodem.txt)
- [FatFs Documentation](http://elm-chan.org/fsw/ff/)
- [STM32H7 USB Device Guide](https://www.st.com/resource/en/user_manual/dm00104712.pdf)

### 개발 도구
- **IDE**: STM32CubeIDE 1.19.0
- **툴체인**: arm-none-eabi-gcc
- **Python**: PyQt5, PySerial
- **변환**: FFmpeg

---

## 버전 이력

### v1.0 (2025-01-31)
- ✅ Y-MODEM 파일 전송 구현
- ✅ USB CDC 통신 안정화
- ✅ 디렉토리 자동 생성
- ✅ f_sync() 최적화
- ✅ FORMAT 명령 추가
- ✅ Python 명령어 입력창 추가

---

## 라이선스

Copyright (c) 2025. All rights reserved.

이 문서는 STM32H723 오디오 멀티플렉서 프로젝트의 일부입니다.
