# SPITEST 명령 사용 가이드

## 개요

`SPITEST` 명령은 Main 보드(STM32H723)와 Slave 보드(STM32H523) 간의 SPI 통신을 테스트하기 위한 통합 명령입니다. 이 명령은 USB CDC 또는 UART2를 통해 실행할 수 있습니다.

## 사전 준비

### 1. Slave 보드 준비

Slave 보드를 SPI 통신 테스트 모드로 설정해야 합니다:

```
1. Slave 보드를 시리얼 터미널에 연결 (115200 baud)
2. 메뉴에서 '5' 입력 (SPI Communication Test)
3. "SPI monitoring started. RDY pin set HIGH." 메시지 확인
```

### 2. Main 보드 준비

Main 보드를 시리얼 터미널 또는 USB CDC로 연결합니다:
- **UART2**: 115200 baud, 8N1
- **USB CDC**: Virtual COM Port

---

## 명령 형식

```
SPITEST <type> [slave_id]
```

### 파라미터

- **type**: 테스트 종류
  - `BASIC` : 기본 명령 패킷 테스트
  - `DATA` : 데이터 패킷 전송 테스트
  - `MULTI` : 멀티 Slave ID 필터링 테스트
  - `ERROR` : 에러 처리 테스트
  - `ALL` : 전체 테스트 (위 모든 테스트 실행)

- **slave_id**: Slave 보드 ID (0~2)
  - `MULTI` 테스트를 제외한 모든 테스트에 필수
  - 0: 첫 번째 Slave 보드
  - 1: 두 번째 Slave 보드
  - 2: 세 번째 Slave 보드

---

## 테스트 종류별 상세 설명

### 1. BASIC - 기본 명령 테스트

**목적**: SPI 명령 패킷이 정상적으로 전송되는지 확인

**명령어**:
```
SPITEST BASIC 0
```

**동작**:
1. PLAY 명령 전송 (채널 0)
2. STOP 명령 전송 (채널 0)
3. VOLUME 명령 전송 (채널 0, 볼륨 50)
4. VOLUME 명령 전송 (채널 1, 볼륨 80)

**예상 결과 (Slave 터미널)**:
```
[CMD #1] SlaveID=0, Ch=0, Cmd=0x01, Param=0
  -> For ME! PLAY command

[CMD #2] SlaveID=0, Ch=0, Cmd=0x02, Param=0
  -> For ME! STOP command

[CMD #3] SlaveID=0, Ch=0, Cmd=0x03, Param=50
  -> For ME! VOLUME command (vol=50)

[CMD #4] SlaveID=0, Ch=1, Cmd=0x03, Param=80
  -> For ME! VOLUME command (vol=80)
```

---

### 2. DATA - 데이터 전송 테스트

**목적**: SPI 데이터 패킷(오디오 샘플)이 정상적으로 전송되는지 확인

**명령어**:
```
SPITEST DATA 0
```

**동작**:
1. 작은 샘플 데이터 전송 (10개 샘플, 채널 0)
2. 중간 샘플 데이터 전송 (100개 샘플, 채널 1)
3. 큰 샘플 데이터 전송 (2048개 샘플, 채널 0, 최대 크기)

**예상 결과 (Slave 터미널)**:
```
[DATA #1] SlaveID=0, Ch=0, Samples=10
  -> For ME! (20 bytes audio data)

[DATA #2] SlaveID=0, Ch=1, Samples=100
  -> For ME! (200 bytes audio data)

[DATA #3] SlaveID=0, Ch=0, Samples=2048
  -> For ME! (4096 bytes audio data)
```

---

### 3. MULTI - 멀티 Slave 테스트

**목적**: 각 Slave 보드가 자신의 ID를 가진 패킷만 처리하는지 확인

**명령어**:
```
SPITEST MULTI
```

**동작**:
1. Slave 0, 1, 2에 순차적으로 PLAY 명령 전송
2. Slave 2, 1, 0에 역순으로 STOP 명령 전송

**예상 결과 (Slave 0 터미널)**:
```
[CMD #1] SlaveID=0, Ch=0, Cmd=0x01, Param=0
  -> For ME! PLAY command

[CMD #2] SlaveID=1, Ch=0, Cmd=0x01, Param=0
  -> For Slave 1 (not me)

[CMD #3] SlaveID=2, Ch=0, Cmd=0x01, Param=0
  -> For Slave 2 (not me)

[CMD #4] SlaveID=2, Ch=0, Cmd=0x02, Param=0
  -> For Slave 2 (not me)

[CMD #5] SlaveID=1, Ch=0, Cmd=0x02, Param=0
  -> For Slave 1 (not me)

[CMD #6] SlaveID=0, Ch=0, Cmd=0x02, Param=0
  -> For ME! STOP command
```

---

### 4. ERROR - 에러 처리 테스트

**목적**: Slave 보드가 잘못된 패킷을 올바르게 처리하는지 확인

**명령어**:
```
SPITEST ERROR 0
```

**동작**:
1. 잘못된 헤더 전송 (0xFF, 올바른 값은 0xC0 또는 0xDA)
2. 잘못된 Slave ID 전송 (5, 유효 범위 0~2)
3. 알 수 없는 명령 코드 전송 (0x99)

**예상 결과 (Slave 터미널)**:
```
[ERROR] Unknown header: 0xFF

[CMD #1] SlaveID=5, Ch=0, Cmd=0x01, Param=0
  -> For Slave 5 (not me)

[CMD #2] SlaveID=0, Ch=0, Cmd=0x99, Param=0
  -> For ME! Unknown command 0x99
```

---

### 5. ALL - 전체 테스트

**목적**: 위의 모든 테스트를 순차적으로 실행

**명령어**:
```
SPITEST ALL 0
```

**동작**:
1. 2초 대기 (Slave 준비 시간)
2. BASIC 테스트 실행
3. 1초 대기
4. DATA 테스트 실행
5. 1초 대기
6. MULTI 테스트 실행
7. 1초 대기
8. ERROR 테스트 실행

**실행 시간**: 약 30~40초

---

## 사용 예제

### 예제 1: 단일 Slave 기본 테스트

```
> SPITEST BASIC 0
OK Starting Basic Test (Slave 0)

=== SPI Basic Test (Slave 0) ===
Test 1: Sending PLAY command...
  -> PLAY command sent successfully
Test 2: Sending STOP command...
  -> STOP command sent successfully
Test 3: Sending VOLUME command (vol=50)...
  -> VOLUME command sent successfully
Test 4: Sending VOLUME command (ch=1, vol=80)...
  -> VOLUME command sent successfully
=== Basic Test Completed ===
Check Slave terminal for received packets.

OK Test complete
```

### 예제 2: 데이터 전송 테스트

```
> SPITEST DATA 1
OK Starting Data Transfer Test (Slave 1)

=== SPI Data Transfer Test (Slave 1) ===
Test 1: Sending 10 samples to CH0...
  -> DMA started, waiting for completion...
  -> 10 samples sent successfully
Test 2: Sending 100 samples to CH1...
  -> DMA started, waiting for completion...
  -> 100 samples sent successfully
Test 3: Sending 2048 samples to CH0 (max size)...
  -> DMA started, waiting for completion...
  -> 2048 samples sent successfully
=== Data Transfer Test Completed ===
Check Slave terminal for received data packets.

OK Test complete
```

### 예제 3: 전체 테스트

```
> SPITEST ALL 0
OK Starting Full Test Suite (Slave 0)

╔══════════════════════════════════════════════╗
║  SPI Communication Full Test Suite          ║
║  Target: Slave 0                            ║
╚══════════════════════════════════════════════╝

Make sure Slave board is in SPI Test Mode (Test 5)
Press any key to start, or 'q' to cancel...

[... 모든 테스트 실행 ...]

╔══════════════════════════════════════════════╗
║  All Tests Completed!                        ║
╚══════════════════════════════════════════════╝

Review Slave terminal output for detailed results.
Expected statistics: ~15 total packets, some errors in error test.

OK All tests complete
```

---

## 에러 메시지

### 401: Invalid arguments

**원인**: 필수 파라미터 누락

**예시**:
```
> SPITEST
ERR 401 Invalid arguments: SPITEST requires test type
```

**해결**: 올바른 형식으로 명령 입력
```
> SPITEST BASIC 0
```

### 402: Invalid slave_id

**원인**: 잘못된 Slave ID (0~2 범위 외)

**예시**:
```
> SPITEST BASIC 5
ERR 402 Invalid slave_id (must be 0~2)
```

**해결**: 0~2 범위의 Slave ID 사용
```
> SPITEST BASIC 0
```

### 400: Invalid test type

**원인**: 지원하지 않는 테스트 종류

**예시**:
```
> SPITEST FOO 0
ERR 400 Invalid test type
Valid types: BASIC, DATA, MULTI, ERROR, ALL
```

**해결**: 지원하는 테스트 종류 사용 (BASIC, DATA, MULTI, ERROR, ALL)

---

## 문제 해결

### 1. 패킷이 Slave에서 수신되지 않음

**증상**: Slave 터미널에 아무 메시지도 출력되지 않음

**원인**:
- SPI 핀 연결 불량
- Slave가 SPI 테스트 모드가 아님
- SPI 클럭 속도 문제

**해결 방법**:
1. 하드웨어 연결 확인:
   - SCK, MOSI, MISO, NSS, RDY 핀 연결 확인
   - GND 공통 연결 확인
2. Slave 보드에서 'Test 5' 실행 확인
3. SPI 클럭 속도 감소 테스트 (main.c에서 설정)

### 2. DMA timeout 에러

**증상**: `DMA timeout` 메시지 출력

**원인**:
- SPI DMA 전송 완료 인터럽트 미발생
- RDY 핀이 LOW로 고정됨

**해결 방법**:
1. Slave 보드 재시작
2. 타임아웃 시간 증가 (user_def.c에서 수정)
3. 인터럽트 우선순위 확인

### 3. 일부 패킷만 수신됨

**증상**: 일부 명령/데이터만 Slave에서 출력됨

**원인**:
- 패킷 간 지연 시간 부족
- Slave 버퍼 오버플로우

**해결 방법**:
1. 패킷 전송 간 지연 시간 증가 (HAL_Delay() 값 증가)
2. Slave 버퍼 크기 확인
3. 전송 속도 감소

---

## 통계 확인

Slave 보드는 5초마다 자동으로 통계를 출력합니다:

```
[STATS] Total=10, CMD=5, DATA=5, Errors=1
```

- **Total**: 총 수신 패킷 수
- **CMD**: Command 패킷 수
- **DATA**: Data 패킷 수
- **Errors**: 에러 발생 횟수

### 예상 통계 (SPITEST ALL 0 실행 후)

```
[STATS] Total=15, CMD=10, DATA=3, Errors=2
```

- 10개의 명령 패킷 (BASIC 4개 + MULTI 6개)
- 3개의 데이터 패킷 (DATA 테스트)
- 2개의 에러 (ERROR 테스트의 의도적 에러)

---

## 관련 문서

- **SPI_TEST_GUIDE.md**: 상세한 SPI 테스트 시나리오
- **PC_UART_PROTOCOL.md**: UART 통신 프로토콜
- **SLAVE_IMPLEMENTATION_SPEC.md**: Slave 보드 구현 사양
- **spi_protocol.h**: SPI 프로토콜 정의

---

## 추가 정보

### SPI 프로토콜 버전

- **Command Packet**: 6 바이트 고정
- **Data Packet**: 5 바이트 헤더 + 샘플 데이터
- **헤더 코드**: 0xC0 (Command), 0xDA (Data)
- **최대 샘플 수**: 2048개 (4096 바이트)

### 빌드 정보

- 컴파일 시 SPI 테스트 함수가 자동 포함됨
- 추가 메모리 사용량: 약 4KB (코드) + 4KB (static 버퍼)
- 의존성: spi_protocol.c, user_def.c, command_handler.c

---

## 연락처

문제 발생 시 다음 정보를 포함하여 문의:
- Main/Slave 보드의 UART 터미널 출력 전체
- 하드웨어 연결 사진 (가능한 경우)
- 실행한 SPITEST 명령어
