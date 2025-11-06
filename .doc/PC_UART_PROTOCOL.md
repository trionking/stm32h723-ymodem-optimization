# PC ↔ Main Board UART 통신 프로토콜 명세서

**작성일**: 2025-11-01
**프로토콜 버전**: 1.1 (FORMAT 명령 추가)
**통신 포트**: UART2 / USB CDC
**보드레이트**: 115200 bps

---

## 📋 목차
1. [개요](#1-개요)
2. [통신 설정](#2-통신-설정)
3. [프로토콜 포맷](#3-프로토콜-포맷)
4. [명령어 목록](#4-명령어-목록)
5. [응답 코드](#5-응답-코드)
6. [사용 예시](#6-사용-예시)
7. [에러 처리](#7-에러-처리)
8. [Y-MODEM 프로토콜](#8-y-modem-프로토콜)

---

## 1. 개요

### 1.1 목적
PC와 Main Board(STM32H723) 간 텍스트 기반 명령 및 제어 인터페이스 제공

### 1.2 주요 기능
- 시스템 상태 조회
- 채널별 재생/정지 제어
- 파일 목록 조회
- 볼륨 제어
- Y-MODEM을 통한 파일 업로드
- 시스템 설정

### 1.3 통신 방식
- **타입**: 텍스트 기반 (ASCII)
- **포맷**: 명령-응답 방식
- **종료 문자**: `\r\n` (CR+LF)
- **인코딩**: UTF-8

---

## 2. 통신 설정

### 2.1 UART 파라미터
```
Port: UART2
Baud Rate: 115200 bps
Data Bits: 8
Parity: None
Stop Bits: 1
Flow Control: None
```

### 2.2 핀 연결
| Main Board | PC (USB-UART) |
|------------|---------------|
| UART2_TX (PD5) | RX |
| UART2_RX (PD6) | TX |
| GND | GND |

---

## 3. 프로토콜 포맷

### 3.1 명령 포맷
```
<COMMAND> [ARG1] [ARG2] ... [ARGn]\r\n
```

**규칙**:
- 명령어는 **대문자**
- 인수는 **공백**으로 구분
- 명령 종료는 `\r\n`

**예시**:
```
PLAY 0 /audio/test.wav\r\n
VOLUME 2 80\r\n
STATUS\r\n
```

### 3.2 응답 포맷

#### 성공 응답
```
OK [DATA]\r\n
```

#### 에러 응답
```
ERR <ERROR_CODE> <ERROR_MESSAGE>\r\n
```

#### 정보 출력
```
INFO: <MESSAGE>\r\n
```

**예시**:
```
OK\r\n
OK Status: PLAYING\r\n
ERR 404 File not found\r\n
INFO: SD card mounted\r\n
```

---

## 4. 명령어 목록

### 4.1 시스템 명령

#### `HELLO`
**설명**: 연결 확인 및 펌웨어 버전 조회
**인수**: 없음
**응답**:
```
OK AUDIO_MUX v1.00 STM32H723\r\n
```

**예시**:
```
>> HELLO\r\n
<< OK AUDIO_MUX v1.00 STM32H723\r\n
```

---

#### `STATUS`
**설명**: 전체 시스템 상태 조회
**인수**: 없음
**응답**:
```
OK STATUS
CH0: IDLE
CH1: PLAYING /audio/ch1.wav 45%
CH2: IDLE
CH3: STOPPED
CH4: IDLE
CH5: IDLE
SD: OK 15234MB free
\r\n
```

**예시**:
```
>> STATUS\r\n
<< OK STATUS
   CH0: IDLE
   ...
```

---

#### `RESET`
**설명**: 시스템 리셋 (소프트웨어 리셋)
**인수**: 없음
**응답**:
```
OK Resetting...\r\n
```

**주의**: 명령 후 약 2초 후 보드가 재시작됨

---

#### `FORMAT`
**설명**: SD 카드 포맷 (FAT32)
**인수**: 없음
**응답**:
```
OK SD card formatted (FAT32)\r\n
```

**주의**:
- SD 카드의 **모든 데이터가 삭제**됩니다!
- 포맷 후 자동으로 재마운트됩니다
- 포맷 실패 시 에러 코드 반환 (FR_DISK_ERR, FR_NOT_READY 등)

**예시**:
```
>> FORMAT\r\n
<< OK SD card formatted (FAT32)\r\n
```

**에러 응답 예시**:
```
>> FORMAT\r\n
<< ERR 500 SD format failed: FR_NOT_READY\r\n
```

---

### 4.2 파일 관리 명령

#### `LS [PATH]`
**설명**: 디렉토리 목록 조회
**인수**:
- `PATH` (선택): 디렉토리 경로 (기본값: `/audio`)

**응답**:
```
OK LS /audio
test.wav 1024KB
ch0.wav 2048KB
ch1.wav 512KB
END
\r\n
```

**예시**:
```
>> LS /audio\r\n
<< OK LS /audio
   test.wav 1024KB
   ch0.wav 2048KB
   END
```

---

#### `DELETE <PATH>`
**설명**: 파일 삭제
**인수**:
- `PATH` (필수): 삭제할 파일 경로

**응답**:
```
OK Deleted /audio/old.wav\r\n
```

**예시**:
```
>> DELETE /audio/old.wav\r\n
<< OK Deleted /audio/old.wav\r\n
```

---

#### `UPLOAD <CHANNEL> <FILENAME>`
**설명**: Y-MODEM 파일 업로드 시작
**인수**:
- `CHANNEL` (필수): 채널 번호 (0~5)
- `FILENAME` (필수): 저장할 파일명 (확장자 포함)

**동작**:
1. 명령 수신 후 `OK Ready for Y-MODEM` 응답
2. Y-MODEM 프로토콜로 파일 수신 시작
3. 파일은 `/audio/ch<N>/<FILENAME>` 경로에 저장
4. 전송 완료 후 `OK Upload complete` 응답

**응답**:
```
OK Ready for Y-MODEM\r\n
[Y-MODEM 전송]
OK Upload complete /audio/ch0/test.wav\r\n
```

**예시**:
```
>> UPLOAD 0 test.wav\r\n
<< OK Ready for Y-MODEM\r\n
[PC가 Y-MODEM 전송 시작]
<< OK Upload complete /audio/ch0/test.wav\r\n
```

---

### 4.3 재생 제어 명령

#### `PLAY <CHANNEL> <PATH>`
**설명**: 지정된 채널에서 파일 재생
**인수**:
- `CHANNEL` (필수): 채널 번호 (0~5)
- `PATH` (필수): 재생할 WAV 파일 경로

**응답**:
```
OK Playing ch0: /audio/test.wav\r\n
```

**예시**:
```
>> PLAY 0 /audio/ch0/test.wav\r\n
<< OK Playing ch0: /audio/ch0/test.wav\r\n
```

---

#### `STOP <CHANNEL>`
**설명**: 지정된 채널 재생 정지
**인수**:
- `CHANNEL` (필수): 채널 번호 (0~5)

**응답**:
```
OK Stopped ch0\r\n
```

**예시**:
```
>> STOP 0\r\n
<< OK Stopped ch0\r\n
```

---

#### `STOPALL`
**설명**: 모든 채널 재생 정지
**인수**: 없음
**응답**:
```
OK All channels stopped\r\n
```

---

#### `VOLUME <CHANNEL> <LEVEL>`
**설명**: 채널별 볼륨 설정
**인수**:
- `CHANNEL` (필수): 채널 번호 (0~5)
- `LEVEL` (필수): 볼륨 레벨 (0~100)

**응답**:
```
OK Volume ch0: 80\r\n
```

**예시**:
```
>> VOLUME 0 80\r\n
<< OK Volume ch0: 80\r\n
```

---

#### `LOOP <CHANNEL> <ON|OFF>`
**설명**: 루프 재생 설정
**인수**:
- `CHANNEL` (필수): 채널 번호 (0~5)
- `ON|OFF` (필수): 루프 활성화/비활성화

**응답**:
```
OK Loop ch0: ON\r\n
```

**예시**:
```
>> LOOP 0 ON\r\n
<< OK Loop ch0: ON\r\n
```

---

### 4.4 디버그 명령

#### `LOG <ON|OFF>`
**설명**: 디버그 로그 출력 활성화/비활성화
**인수**:
- `ON|OFF` (필수)

**응답**:
```
OK Debug log: ON\r\n
```

**주의**: 로그가 활성화되면 `INFO:` 메시지가 계속 출력됨

---

#### `MEM`
**설명**: 메모리 사용량 조회
**인수**: 없음
**응답**:
```
OK MEM
Heap: 45KB/128KB (35%)
Stack: 2KB/16KB (12%)
\r\n
```

---

## 5. 응답 코드

### 5.1 성공 응답
| 응답 | 의미 |
|------|------|
| `OK` | 명령 성공 |
| `OK <DATA>` | 명령 성공 + 데이터 반환 |

### 5.2 에러 코드

| 코드 | 메시지 | 의미 |
|------|--------|------|
| `ERR 400` | Invalid command | 알 수 없는 명령 |
| `ERR 401` | Invalid arguments | 인수 오류 (개수/형식) |
| `ERR 402` | Invalid channel | 채널 번호 범위 초과 (0~5) |
| `ERR 403` | Channel busy | 채널이 이미 재생 중 |
| `ERR 404` | File not found | 파일이 존재하지 않음 |
| `ERR 405` | SD error | SD 카드 에러 |
| `ERR 406` | Out of memory | 메모리 부족 |
| `ERR 500` | System error | 시스템 내부 에러 |
| `ERR 501` | Y-MODEM error | Y-MODEM 전송 실패 |

**에러 응답 예시**:
```
ERR 404 File not found: /audio/missing.wav\r\n
ERR 402 Invalid channel: 7 (must be 0~5)\r\n
```

---

## 6. 사용 예시

### 6.1 시나리오: 파일 업로드 및 재생

```
# 1. 연결 확인
>> HELLO\r\n
<< OK AUDIO_MUX v1.00 STM32H723\r\n

# 2. 현재 상태 확인
>> STATUS\r\n
<< OK STATUS
   CH0: IDLE
   ...
   SD: OK 15234MB free
   \r\n

# 3. 기존 파일 목록 확인
>> LS /audio/ch0\r\n
<< OK LS /audio/ch0
   old.wav 1024KB
   END
   \r\n

# 4. 기존 파일 삭제
>> DELETE /audio/ch0/old.wav\r\n
<< OK Deleted /audio/ch0/old.wav\r\n

# 5. 새 파일 업로드
>> UPLOAD 0 new.wav\r\n
<< OK Ready for Y-MODEM\r\n
[PC에서 Y-MODEM 전송]
<< INFO: Receiving... 10%\r\n
<< INFO: Receiving... 50%\r\n
<< INFO: Receiving... 100%\r\n
<< OK Upload complete /audio/ch0/new.wav\r\n

# 6. 재생
>> PLAY 0 /audio/ch0/new.wav\r\n
<< OK Playing ch0: /audio/ch0/new.wav\r\n

# 7. 볼륨 조절
>> VOLUME 0 75\r\n
<< OK Volume ch0: 75\r\n

# 8. 정지
>> STOP 0\r\n
<< OK Stopped ch0\r\n
```

---

## 7. 에러 처리

### 7.1 명령어 파싱 에러
```
>> INVALID_CMD\r\n
<< ERR 400 Invalid command: INVALID_CMD\r\n

>> PLAY\r\n
<< ERR 401 Invalid arguments: PLAY requires 2 arguments\r\n

>> PLAY 99 /test.wav\r\n
<< ERR 402 Invalid channel: 99 (must be 0~5)\r\n
```

### 7.2 파일 시스템 에러
```
>> PLAY 0 /nonexist.wav\r\n
<< ERR 404 File not found: /nonexist.wav\r\n

>> LS /audio\r\n
<< ERR 405 SD error: Card not mounted\r\n
```

### 7.3 재전송 정책
- 명령 전송 후 **2초 이내** 응답 없으면 타임아웃
- PC는 최대 **3회** 재시도
- 3회 실패 시 사용자에게 에러 알림

---

## 8. Y-MODEM 프로토콜

### 8.1 Y-MODEM 개요
Y-MODEM은 파일 전송을 위한 바이너리 프로토콜입니다.

**특징**:
- 128 또는 1024 바이트 블록 전송
- CRC-16 체크섬
- 파일명 및 크기 전송
- 재전송 메커니즘

### 8.2 전송 시퀀스

```
PC                          Main Board
|                           |
|  UPLOAD 0 test.wav\r\n    |
| ------------------------> |
|                           |
|  OK Ready for Y-MODEM\r\n |
| <------------------------ |
|                           |
|  [SOH][BLK#][DATA][CRC]   |
| ------------------------> |
|                           |
|  [ACK]                    |
| <------------------------ |
|                           |
|  ... (반복) ...          |
|                           |
|  [EOT]                    |
| ------------------------> |
|                           |
|  [ACK]                    |
| <------------------------ |
|                           |
|  OK Upload complete\r\n   |
| <------------------------ |
```

### 8.3 제어 문자

| 문자 | 값 | 의미 |
|------|-----|------|
| SOH | 0x01 | 128바이트 블록 시작 |
| STX | 0x02 | 1024바이트 블록 시작 |
| EOT | 0x04 | 전송 종료 |
| ACK | 0x06 | 긍정 응답 |
| NAK | 0x15 | 부정 응답 (재전송) |
| CAN | 0x18 | 전송 취소 |

### 8.4 파일 저장 경로

업로드된 파일은 다음 경로에 저장됩니다:
```
/audio/ch<CHANNEL>/<FILENAME>
```

**예시**:
- 채널 0, 파일명 `test.wav` → `/audio/ch0/test.wav`
- 채널 3, 파일명 `music.wav` → `/audio/ch3/music.wav`

### 8.5 Y-MODEM 타임아웃

| 단계 | 타임아웃 | 재시도 |
|------|----------|--------|
| 블록 수신 | 10초 | 10회 |
| ACK 대기 | 5초 | - |
| 전체 전송 | 300초 (5분) | - |

### 8.6 Y-MODEM 에러 처리

**CRC 오류**:
```
PC → [블록 + 잘못된 CRC]
PC ← [NAK]
PC → [같은 블록 재전송]
```

**타임아웃**:
```
PC → [블록]
(응답 없음 10초)
PC → [CAN] (전송 취소)
Board → ERR 501 Y-MODEM timeout
```

---

## 9. 명령어 요약표

| 분류 | 명령어 | 인수 | 설명 |
|------|--------|------|------|
| **시스템** | `HELLO` | - | 버전 조회 |
| | `STATUS` | - | 전체 상태 |
| | `RESET` | - | 시스템 리셋 |
| | `FORMAT` | - | SD 카드 포맷 (FAT32) |
| **파일** | `LS` | [PATH] | 목록 조회 |
| | `DELETE` | PATH | 파일 삭제 |
| | `UPLOAD` | CH FILE | Y-MODEM 업로드 |
| **재생** | `PLAY` | CH PATH | 재생 시작 |
| | `STOP` | CH | 정지 |
| | `STOPALL` | - | 전체 정지 |
| | `VOLUME` | CH LEVEL | 볼륨 설정 |
| | `LOOP` | CH ON\|OFF | 루프 설정 |
| **디버그** | `LOG` | ON\|OFF | 로그 출력 |
| | `MEM` | - | 메모리 정보 |

---

## 10. 구현 가이드

### 10.1 펌웨어 구현

**필요한 모듈**:
1. UART 명령 파서 (`uart_command.c`)
2. Y-MODEM 수신기 (`ymodem.c`)
3. 명령 핸들러 (`command_handler.c`)

**의존성**:
- FatFs (파일 시스템)
- HAL UART (통신)

### 10.2 PC 프로그램 구현

**필요한 라이브러리**:
- `pyserial`: UART 통신
- `PyQt5/6`: GUI
- `xmodem`: Y-MODEM 프로토콜 (또는 직접 구현)

---

## 부록 A: 명령어 파서 의사코드

```c
void parse_uart_command(char *cmd_line)
{
    char *token;
    char *argv[10];
    int argc = 0;

    // 공백으로 분리
    token = strtok(cmd_line, " ");
    while (token != NULL && argc < 10) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    if (argc == 0) return;

    // 명령어 처리
    if (strcmp(argv[0], "HELLO") == 0) {
        handle_hello();
    }
    else if (strcmp(argv[0], "PLAY") == 0) {
        if (argc < 3) {
            send_error(401, "Invalid arguments");
            return;
        }
        int channel = atoi(argv[1]);
        char *path = argv[2];
        handle_play(channel, path);
    }
    // ... 기타 명령어
    else {
        send_error(400, "Invalid command");
    }
}
```

---

## 부록 B: Python 클라이언트 예제

```python
import serial

class AudioMuxClient:
    def __init__(self, port, baudrate=115200):
        self.ser = serial.Serial(port, baudrate, timeout=2)

    def send_command(self, cmd):
        """명령 전송 및 응답 수신"""
        self.ser.write((cmd + '\r\n').encode())
        response = self.ser.readline().decode().strip()
        return response

    def hello(self):
        return self.send_command('HELLO')

    def play(self, channel, path):
        return self.send_command(f'PLAY {channel} {path}')

    def stop(self, channel):
        return self.send_command(f'STOP {channel}')

# 사용 예
client = AudioMuxClient('COM3')
print(client.hello())  # OK AUDIO_MUX v1.00 STM32H723
print(client.play(0, '/audio/test.wav'))  # OK Playing ch0: ...
```

---

**문서 버전**: 1.1
**최종 수정일**: 2025-11-01
**작성자**: System Architect

**변경 이력**:
- v1.1 (2025-11-01): FORMAT 명령 추가, USB CDC 통신 지원 명시
- v1.0 (2025-10-26): 최초 작성
