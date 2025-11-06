# Y-MODEM 재시도 메커니즘 구현

## 변경 개요

Y-MODEM 파일 전송이 일시적인 에러(USB 지연, SD 쓰기 지연 등)로 인해 멈추는 문제를 해결하기 위해 재시도 로직을 추가했습니다.

## 변경 일자

2025-11-06

## 문제 상황

**증상:**
- 12.7MB WAV 파일 업로드 중 전송이 멈춤 ("멈췄어")
- 일시적인 타임아웃 발생 시 즉시 전송 취소 (CAN)
- Python 송신측은 계속 패킷 전송, STM32는 종료 → 데드락

**근본 원인:**
```c
// 기존 코드 (Core/Src/ymodem.c:181-186)
if (status != HAL_OK) {
    transmit_byte(huart, YMODEM_CAN);  // ❌ 즉시 취소
    uart_send_error(501, "Y-MODEM timeout or receive error");
    result = YMODEM_TIMEOUT;
    break;  // ❌ 재시도 없음
}
```

- CRC/블록 번호 에러 시 무한 NAK 재시도 가능 (카운터 없음)

## 구현 내용

### 1. 재시도 상수 정의 (Core/Inc/ymodem.h)

```c
// 재시도 설정
#define YMODEM_MAX_TIMEOUT_RETRIES  5   // 타임아웃 재시도 최대 횟수
#define YMODEM_MAX_NAK_RETRIES      10  // NAK 재시도 최대 횟수
```

### 2. 재시도 카운터 초기화 (Core/Src/ymodem.c:175-177)

```c
// 재시도 카운터 초기화
uint8_t timeout_retries = 0;
uint8_t nak_retries = 0;
```

### 3. 타임아웃 재시도 로직 (Core/Src/ymodem.c:185-205)

```c
if (status != HAL_OK) {
    // 타임아웃 또는 에러 - 재시도
    timeout_retries++;
    if (timeout_retries >= YMODEM_MAX_TIMEOUT_RETRIES) {
        // 최대 재시도 횟수 초과 → 취소
        transmit_byte(huart, YMODEM_CAN);
        uart_send_error(501, "Y-MODEM timeout after retries");
        printf("[ERROR] Y-MODEM: timeout after %d retries\r\n", timeout_retries);
        result = YMODEM_TIMEOUT;
        break;
    }
    // 재시도
    printf("[WARN] Packet timeout, retry %d/%d\r\n",
           timeout_retries, YMODEM_MAX_TIMEOUT_RETRIES);
    uart_send_response(ANSI_YELLOW "INFO:" ANSI_RESET " Timeout, retrying...\r\n");
    HAL_Delay(100);  // 100ms 대기 후 재시도
    continue;  // ✅ 루프 재시작 (패킷 재수신)
}

// 수신 성공 시 타임아웃 카운터 리셋
timeout_retries = 0;
```

**동작:**
- `receive_packet()` 타임아웃 시 즉시 취소하지 않고 100ms 후 재시도
- 최대 5회 재시도 후에도 실패하면 전송 취소
- 패킷 수신 성공 시 카운터 리셋 (다음 패킷을 위해)

### 4. NAK 재시도 로직 - 블록 번호 에러 (Core/Src/ymodem.c:228-244)

```c
// 블록 번호 확인
if (blk_num != (uint8_t)(~blk_num_inv)) {
    // 블록 번호 오류 - NAK 재시도
    nak_retries++;
    if (nak_retries >= YMODEM_MAX_NAK_RETRIES) {
        // 최대 NAK 재시도 횟수 초과
        transmit_byte(huart, YMODEM_CAN);
        uart_send_error(501, "Too many NAK retries (block number)");
        printf("[ERROR] Y-MODEM: NAK retries exceeded (%d) - block number mismatch\r\n", nak_retries);
        result = YMODEM_ERROR;
        break;
    }
    printf("[ERROR] Block number mismatch: blk=%u, ~blk=%u (NAK retry %d/%d)\r\n",
           blk_num, blk_num_inv, nak_retries, YMODEM_MAX_NAK_RETRIES);
    transmit_byte(huart, YMODEM_NAK);
    continue;  // ✅ NAK 전송 후 재시도
}
```

### 5. NAK 재시도 로직 - CRC 에러 (Core/Src/ymodem.c:251-267)

```c
if (crc_received != crc_calculated) {
    // CRC 오류 - NAK 재시도
    nak_retries++;
    if (nak_retries >= YMODEM_MAX_NAK_RETRIES) {
        // 최대 NAK 재시도 횟수 초과
        transmit_byte(huart, YMODEM_CAN);
        uart_send_error(501, "Too many NAK retries (CRC error)");
        printf("[ERROR] Y-MODEM: NAK retries exceeded (%d) - CRC mismatch\r\n", nak_retries);
        result = YMODEM_ERROR;
        break;
    }
    printf("[ERROR] CRC mismatch! received=0x%04X, calculated=0x%04X (NAK retry %d/%d)\r\n",
           crc_received, crc_calculated, nak_retries, YMODEM_MAX_NAK_RETRIES);
    transmit_byte(huart, YMODEM_NAK);
    uart_send_response(ANSI_YELLOW "INFO:" ANSI_RESET " CRC error, retrying...\r\n");
    continue;  // ✅ NAK 전송 후 재시도
}
```

### 6. 패킷 성공 시 NAK 카운터 리셋 (Core/Src/ymodem.c:290-291)

```c
total_bytes += bytes_written;
packet_number++;

// 패킷 성공 시 NAK 카운터 리셋
nak_retries = 0;
```

**동작:**
- CRC/블록 번호 에러 시 NAK 전송 후 재시도
- 최대 10회 NAK 재시도 후에도 실패하면 전송 취소
- 패킷 성공 시 NAK 카운터 리셋 (일시적 에러 복구)

## 재시도 시나리오

### 시나리오 1: USB 일시적 지연
```
Packet 1000: OK (timeout_retries=0, nak_retries=0)
Packet 1001: TIMEOUT (USB 지연)
  → timeout_retries=1, 100ms 대기, 재시도
Packet 1001: OK (수신 성공)
  → timeout_retries=0 (리셋)
Packet 1002: OK
```

### 시나리오 2: CRC 노이즈 에러
```
Packet 2000: OK (nak_retries=0)
Packet 2001: CRC ERROR (USB 노이즈)
  → nak_retries=1, NAK 전송, 재시도
Packet 2001: OK (Python 재전송)
  → nak_retries=0 (리셋)
Packet 2002: OK
```

### 시나리오 3: 지속적인 타임아웃 (5회 초과)
```
Packet 3000: TIMEOUT → retry 1/5
Packet 3000: TIMEOUT → retry 2/5
Packet 3000: TIMEOUT → retry 3/5
Packet 3000: TIMEOUT → retry 4/5
Packet 3000: TIMEOUT → retry 5/5
  → "Y-MODEM timeout after retries" → CAN 전송 → 전송 종료
```

### 시나리오 4: 지속적인 CRC 에러 (10회 초과)
```
Packet 4000: CRC ERROR → NAK retry 1/10
Packet 4000: CRC ERROR → NAK retry 2/10
...
Packet 4000: CRC ERROR → NAK retry 10/10
  → "Too many NAK retries (CRC error)" → CAN 전송 → 전송 종료
```

## 개선 효과

### Before (기존)
- ❌ 일시적 타임아웃 시 즉시 전송 실패
- ❌ Python과 STM32 간 데드락 발생
- ❌ 사용자가 수동으로 재시작 필요
- ❌ NAK 무한 루프 가능성

### After (개선 후)
- ✅ 일시적 타임아웃 자동 복구 (최대 5회)
- ✅ 일시적 CRC 에러 자동 복구 (최대 10회)
- ✅ 지속적 에러 시 명확한 에러 메시지와 함께 취소
- ✅ NAK 재시도 제한으로 무한 루프 방지
- ✅ 12.7MB 대용량 파일 전송 안정성 향상

## 디버그 로그 예시

### 성공적인 재시도
```
[WARN] Packet timeout, retry 1/5
INFO: Timeout, retrying...
[DEBUG] Y-MODEM: packet received
[DEBUG] Progress: packet 1234, total=1265664 bytes
```

### NAK 재시도
```
[ERROR] CRC mismatch! received=0x1234, calculated=0x5678 (NAK retry 1/10)
INFO: CRC error, retrying...
[DEBUG] Y-MODEM: packet received (retry success)
```

### 재시도 한계 초과
```
[WARN] Packet timeout, retry 1/5
[WARN] Packet timeout, retry 2/5
[WARN] Packet timeout, retry 3/5
[WARN] Packet timeout, retry 4/5
[WARN] Packet timeout, retry 5/5
[ERROR] Y-MODEM: timeout after 5 retries
ERR 501 Y-MODEM timeout after retries
```

## 테스트 권장 사항

1. **정상 전송 테스트**: 12.7MB 파일이 중단 없이 전송되는지 확인
2. **USB 케이블 흔들기**: 일시적 접촉 불량 시 자동 복구 확인
3. **장시간 전송**: 4-5분 전송 중 타임아웃 발생 시 재시도 동작 확인
4. **SD 카드 상태**: SD 카드 포맷 후 테스트 (FAT 손상 방지)

## 관련 문서

- `PC_UART_PROTOCOL.md` - UART 명령 프로토콜
- `YMODEM_UPLOAD_GUIDE.md` - Y-MODEM 타이밍 가이드
- `USB_CDC_TIMEOUT_FIX.md` - USB CDC 타임아웃 해결 방법
- `Core/Inc/ymodem.h` - Y-MODEM 상수 정의
- `Core/Src/ymodem.c` - Y-MODEM 구현
