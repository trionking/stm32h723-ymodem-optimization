# SPI 통신 테스트 가이드 (Main 보드)

## 개요

Slave 보드(STM32H523)와 SPI 통신을 테스트하기 위한 가이드입니다.
Slave 보드는 **Test 5: SPI Communication Test** 모드에서 수신된 모든 SPI 패킷을 UART로 출력합니다.

---

## 1. 하드웨어 연결

### SPI 핀 연결 (Main → Slave)

| Main 보드 | Slave 보드 (STM32H523) | 신호 |
|-----------|------------------------|------|
| SPI_SCK   | PB3 (SPI1_SCK)         | Clock |
| SPI_MISO  | PB4 (SPI1_MISO)        | Master In Slave Out |
| SPI_MOSI  | PB5 (SPI1_MOSI)        | Master Out Slave In |
| SPI_NSS   | PA15 (SPI1_NSS)        | Chip Select |
| GPIO_OUT  | PA8 (RDY)              | Ready 신호 (Slave→Main) |
| GND       | GND                    | Ground |

### SPI 설정

- **모드**: SPI Mode 0 (CPOL=0, CPHA=0)
- **클럭 속도**: 최대 125MHz (Slave 지원 범위 내에서 설정)
- **데이터 크기**: 8비트
- **바이트 순서**: MSB First
- **Slave 선택**: 하드웨어 NSS 사용

### RDY 신호

- **HIGH**: Slave가 데이터 수신 준비 완료
- **LOW**: Slave가 데이터 처리 중 (대기 필요)

---

## 2. SPI 프로토콜 사양

### 2.1 Command Packet (명령 패킷)

**크기**: 6 바이트

```
Offset | Field      | Value     | Description
-------|------------|-----------|---------------------------
0      | header     | 0xC0      | Command packet header
1      | slave_id   | 0~2       | Slave ID (0, 1, 2)
2      | channel    | 0~1       | 0=DAC1_CH1, 1=DAC1_CH2
3      | command    | 0x01~0xFF | Command code
4      | param_h    | 0x00~0xFF | Parameter high byte
5      | param_l    | 0x00~0xFF | Parameter low byte
```

**Command Codes:**
- `0x01` (CMD_PLAY): 재생 시작
- `0x02` (CMD_STOP): 재생 정지
- `0x03` (CMD_VOLUME): 볼륨 설정 (param = 0~100)
- `0xFF` (CMD_RESET): 채널 리셋

**예제 - PLAY 명령:**
```c
uint8_t play_cmd[] = {
    0xC0,           // header
    0x00,           // slave_id = 0
    0x00,           // channel = DAC1
    0x01,           // command = PLAY
    0x00,           // param_h
    0x00            // param_l
};
HAL_SPI_Transmit(&hspi, play_cmd, 6, 100);
```

---

### 2.2 Data Packet (오디오 데이터 패킷)

**크기**: 5 바이트 헤더 + (샘플 수 × 2) 바이트

```
Offset | Field      | Value     | Description
-------|------------|-----------|---------------------------
0      | header     | 0xDA      | Data packet header
1      | slave_id   | 0~2       | Slave ID (0, 1, 2)
2      | channel    | 0~1       | 0=DAC1_CH1, 1=DAC1_CH2
3      | length_h   | 0x00~0xFF | Sample count high byte
4      | length_l   | 0x00~0xFF | Sample count low byte
5~N    | samples[]  | -         | Audio samples (16-bit each)
```

**샘플 형식:**
- 16비트 부호 없는 정수 (uint16_t)
- Little-endian (하위 바이트 먼저)
- DAC 범위: 0~4095 (12비트 사용)
- 최대 샘플 수: 2048개

**예제 - 100개 샘플 전송:**
```c
// 헤더
uint8_t data_header[] = {
    0xDA,           // header
    0x00,           // slave_id = 0
    0x00,           // channel = DAC1
    0x00,           // length_h (100 >> 8)
    0x64            // length_l (100 & 0xFF)
};

// 샘플 데이터 (100개)
uint16_t samples[100];
for (int i = 0; i < 100; i++) {
    samples[i] = 2048;  // 중간값
}

// 전송
HAL_SPI_Transmit(&hspi, data_header, 5, 100);
HAL_SPI_Transmit(&hspi, (uint8_t*)samples, 200, 1000);  // 100 samples * 2 bytes
```

---

## 3. 테스트 시나리오

### 3.1 기본 연결 테스트

**목적**: SPI 통신이 정상적으로 작동하는지 확인

**Slave 보드 준비:**
```
1. Slave 보드를 시리얼 터미널에 연결 (115200 baud)
2. 메뉴에서 '5' 입력 (SPI Communication Test)
3. "SPI monitoring started. RDY pin set HIGH." 메시지 확인
```

**Main 보드 동작:**
```c
// 1. PLAY 명령 전송
uint8_t play_cmd[] = {0xC0, 0x00, 0x00, 0x01, 0x00, 0x00};
HAL_SPI_Transmit(&hspi, play_cmd, 6, 100);
HAL_Delay(100);

// 2. STOP 명령 전송
uint8_t stop_cmd[] = {0xC0, 0x00, 0x00, 0x02, 0x00, 0x00};
HAL_SPI_Transmit(&hspi, stop_cmd, 6, 100);
HAL_Delay(100);

// 3. VOLUME 명령 전송 (볼륨 50)
uint8_t vol_cmd[] = {0xC0, 0x00, 0x00, 0x03, 0x00, 0x32};
HAL_SPI_Transmit(&hspi, vol_cmd, 6, 100);
```

**예상 결과 (Slave 터미널):**
```
[CMD #1] SlaveID=0, Ch=0, Cmd=0x01, Param=0
  -> For ME! PLAY command

[CMD #2] SlaveID=0, Ch=0, Cmd=0x02, Param=0
  -> For ME! STOP command

[CMD #3] SlaveID=0, Ch=0, Cmd=0x03, Param=50
  -> For ME! VOLUME command (vol=50)
```

---

### 3.2 데이터 전송 테스트

**목적**: 오디오 데이터 패킷이 정상적으로 수신되는지 확인

**Main 보드 동작:**
```c
// 1. 작은 샘플 데이터 전송 (10개)
uint8_t data_hdr[] = {0xDA, 0x00, 0x00, 0x00, 0x0A};  // 10 samples
uint16_t samples[10] = {2048, 2048, 2048, 2048, 2048,
                        2048, 2048, 2048, 2048, 2048};

HAL_SPI_Transmit(&hspi, data_hdr, 5, 100);
HAL_SPI_Transmit(&hspi, (uint8_t*)samples, 20, 1000);
HAL_Delay(100);

// 2. 큰 샘플 데이터 전송 (2048개 - 최대)
uint8_t data_hdr_large[] = {0xDA, 0x00, 0x01, 0x08, 0x00};  // 2048 samples, CH2
uint16_t large_samples[2048];
for (int i = 0; i < 2048; i++) {
    large_samples[i] = 2048 + (i % 1000);  // 변화하는 값
}

HAL_SPI_Transmit(&hspi, data_hdr_large, 5, 100);
HAL_SPI_Transmit(&hspi, (uint8_t*)large_samples, 4096, 5000);
```

**예상 결과 (Slave 터미널):**
```
[DATA #1] SlaveID=0, Ch=0, Samples=10
  -> For ME! (20 bytes audio data)

[DATA #2] SlaveID=0, Ch=1, Samples=2048
  -> For ME! (4096 bytes audio data)
```

---

### 3.3 멀티 Slave 테스트

**목적**: 다른 Slave ID 패킷 필터링 확인

**Main 보드 동작:**
```c
// Slave 0에게 전송
uint8_t cmd_slave0[] = {0xC0, 0x00, 0x00, 0x01, 0x00, 0x00};
HAL_SPI_Transmit(&hspi, cmd_slave0, 6, 100);
HAL_Delay(100);

// Slave 1에게 전송 (현재 Slave는 0이므로 무시해야 함)
uint8_t cmd_slave1[] = {0xC0, 0x01, 0x00, 0x01, 0x00, 0x00};
HAL_SPI_Transmit(&hspi, cmd_slave1, 6, 100);
HAL_Delay(100);

// Slave 2에게 전송 (무시)
uint8_t cmd_slave2[] = {0xC0, 0x02, 0x00, 0x01, 0x00, 0x00};
HAL_SPI_Transmit(&hspi, cmd_slave2, 6, 100);
```

**예상 결과 (Slave 터미널):**
```
[CMD #1] SlaveID=0, Ch=0, Cmd=0x01, Param=0
  -> For ME! PLAY command

[CMD #2] SlaveID=1, Ch=0, Cmd=0x01, Param=0
  -> For Slave 1 (not me)

[CMD #3] SlaveID=2, Ch=0, Cmd=0x01, Param=0
  -> For Slave 2 (not me)
```

---

### 3.4 에러 테스트

**목적**: 잘못된 패킷 처리 확인

**Main 보드 동작:**
```c
// 1. 잘못된 헤더
uint8_t invalid_header[] = {0xFF, 0x00, 0x00, 0x01, 0x00, 0x00};
HAL_SPI_Transmit(&hspi, invalid_header, 6, 100);
HAL_Delay(100);

// 2. 잘못된 Slave ID (3 이상)
uint8_t invalid_id[] = {0xC0, 0x05, 0x00, 0x01, 0x00, 0x00};
HAL_SPI_Transmit(&hspi, invalid_id, 6, 100);
```

**예상 결과 (Slave 터미널):**
```
[ERROR] Unknown header: 0xFF

[CMD #1] SlaveID=5, Ch=0, Cmd=0x01, Param=0
  -> For Slave 5 (not me)
```

---

## 4. 통계 확인

Slave 보드는 5초마다 자동으로 통계를 출력합니다:

```
[STATS] Total=10, CMD=5, DATA=5, Errors=1
```

- **Total**: 총 수신 패킷 수
- **CMD**: Command 패킷 수
- **DATA**: Data 패킷 수
- **Errors**: 에러 발생 횟수

---

## 5. 전체 테스트 코드 예제

```c
void test_spi_slave_communication(void)
{
    printf("=== SPI Slave Communication Test ===\n");

    // 1. PLAY 명령
    uint8_t play_cmd[] = {0xC0, 0x00, 0x00, 0x01, 0x00, 0x00};
    HAL_SPI_Transmit(&hspi, play_cmd, 6, 100);
    printf("Sent: PLAY command\n");
    HAL_Delay(500);

    // 2. 작은 데이터 전송
    uint8_t data_hdr[] = {0xDA, 0x00, 0x00, 0x00, 0x64};  // 100 samples
    uint16_t samples[100];
    for (int i = 0; i < 100; i++) {
        samples[i] = 2048 + (i * 10);  // 증가하는 값
    }
    HAL_SPI_Transmit(&hspi, data_hdr, 5, 100);
    HAL_SPI_Transmit(&hspi, (uint8_t*)samples, 200, 1000);
    printf("Sent: 100 samples\n");
    HAL_Delay(500);

    // 3. VOLUME 명령
    uint8_t vol_cmd[] = {0xC0, 0x00, 0x00, 0x03, 0x00, 0x50};  // Volume 80
    HAL_SPI_Transmit(&hspi, vol_cmd, 6, 100);
    printf("Sent: VOLUME command (80)\n");
    HAL_Delay(500);

    // 4. 큰 데이터 전송
    uint8_t data_hdr2[] = {0xDA, 0x00, 0x01, 0x08, 0x00};  // 2048 samples, CH2
    uint16_t large_samples[2048];
    for (int i = 0; i < 2048; i++) {
        large_samples[i] = 2048;
    }
    HAL_SPI_Transmit(&hspi, data_hdr2, 5, 100);
    HAL_SPI_Transmit(&hspi, (uint8_t*)large_samples, 4096, 5000);
    printf("Sent: 2048 samples to CH2\n");
    HAL_Delay(500);

    // 5. STOP 명령
    uint8_t stop_cmd[] = {0xC0, 0x00, 0x00, 0x02, 0x00, 0x00};
    HAL_SPI_Transmit(&hspi, stop_cmd, 6, 100);
    printf("Sent: STOP command\n");

    printf("\nTest completed. Check Slave terminal for results.\n");
}
```

---

## 6. 문제 해결

### 6.1 패킷이 수신되지 않음

**원인:**
- SPI 핀 연결 불량
- SPI 클럭 속도 너무 빠름
- NSS 신호 문제

**확인 사항:**
1. 하드웨어 연결 재확인
2. SPI 클럭을 낮춰서 테스트 (1MHz 이하)
3. 오실로스코프로 신호 확인

### 6.2 에러 카운트 증가

**원인:**
- 잘못된 헤더 값
- 패킷 크기 불일치
- 타이밍 문제

**확인 사항:**
1. 패킷 구조가 사양과 일치하는지 확인
2. NSS 신호가 패킷 전체를 감싸는지 확인
3. 패킷 간 충분한 지연 시간 추가

### 6.3 데이터 패킷 수신 실패

**원인:**
- 샘플 수가 헤더와 불일치
- 타임아웃 발생
- 버퍼 오버플로우

**확인 사항:**
1. length_h, length_l 값이 올바른지 확인
2. 실제 전송 샘플 수와 헤더 일치 확인
3. 타임아웃 시간 증가

---

## 7. 테스트 종료

Slave 보드에서 'q' 또는 ESC 키를 눌러 테스트 모드를 종료하고 메뉴로 돌아갑니다.

```
q [Enter]
```

출력:
```
[EXIT] SPI Communication Test stopped by user
Total: 15 packets (8 cmd, 7 data, 0 errors)

Test completed. Type 'help' for menu.
```

---

## 8. 참고 정보

### Slave 보드 설정
- **Slave ID**: 0 (spi_protocol.h의 MY_SLAVE_ID)
- **버퍼 크기**: 2048 samples
- **SPI 모드**: Slave
- **UART 출력**: 115200 baud, COM9

### 다음 단계
테스트가 성공하면 **Test 0: Run Slave Mode**로 실제 오디오 스트리밍을 테스트할 수 있습니다.

---

## 연락처

문제 발생 시 Slave 보드 터미널 출력을 캡처하여 공유해주세요.
