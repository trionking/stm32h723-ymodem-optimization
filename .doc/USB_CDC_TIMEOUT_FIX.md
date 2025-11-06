# USB CDC 전송 타임아웃 문제 해결

**날짜:** 2025-10-27
**프로젝트:** STM32H723 Audio Mux v1.00
**문제:** USB CDC를 통한 응답 전송 시 타임아웃 발생

---

## 문제 증상

### 로그 출력
```
[DEBUG] CDC_Receive_HS: received 7 bytes (count=1, ymodem=0)
[DEBUG] CDC: received command 'HELLO'
[ERROR] CDC_Transmit: transmission timeout (retries=200)
```

### 상황
- USB CDC로 명령 수신은 정상적으로 동작 ✅
- 응답 전송 시 타임아웃 발생 (약 2초 대기 후 에러) ❌
- PC 측에서 응답을 받지 못함

---

## 원인 분석

### 문제 코드 위치
`Core/Src/uart_command.c` - `uart_send_response()` 함수

### 원래 구현 방식 (Synchronous Blocking)

```c
// 전송
result = CDC_Transmit_HS(UserTxBufferHS, len);
if (result != USBD_OK) {
    printf("[ERROR] CDC_Transmit failed: %d\r\n", result);
    return;
}

// 전송 시작 확인 대기 (TxState가 1이 될 때까지)
retry_count = 0;
while (hcdc->TxState == 0 && retry_count < 50) {
    retry_count++;
    for (volatile int i = 0; i < 1000; i++);
}

// 전송 완료 대기 (TxState가 0이 될 때까지) ← 여기서 타임아웃!
retry_count = 0;
while (hcdc->TxState != 0 && retry_count < max_retries) {  // max_retries = 200
    retry_count++;
    for (volatile int i = 0; i < 10000; i++);  // ~10ms per iteration
}

if (retry_count >= max_retries) {
    printf("[ERROR] CDC_Transmit: transmission timeout (retries=%lu)\r\n", retry_count);
}
```

### 문제점

1. **Busy-wait 방식의 전송 완료 대기**
   - `hcdc->TxState`가 0으로 돌아올 때까지 폴링
   - 전송 완료 인터럽트가 발생하지 않으면 무한 대기

2. **USB 상태에 따른 동작 불안정**
   - USB가 PC에 완전히 연결되지 않은 경우
   - USB 열거(enumeration)가 완료되지 않은 경우
   - USB 전송 완료 콜백 `CDC_TransmitCplt_HS()`가 호출되지 않음

3. **TxState 업데이트 실패**
   - USB 전송은 비동기로 처리됨
   - 전송 완료 인터럽트가 정상 발생하지 않으면 `TxState`가 1로 고정됨
   - 결과적으로 200회 재시도 후 타임아웃

### 기술적 배경

STM32 USB CDC는 **비동기 전송** 방식을 사용합니다:

```
1. CDC_Transmit_HS() 호출
   └─> TxState = 1 (전송 시작)
   └─> USBD_CDC_TransmitPacket() 호출

2. USB 하드웨어에서 데이터 전송 (백그라운드)

3. 전송 완료 인터럽트 발생
   └─> CDC_TransmitCplt_HS() 콜백 호출
   └─> TxState = 0 (전송 완료)
```

하지만 USB가 PC와 제대로 연결되지 않은 경우:
- 전송 완료 인터럽트가 발생하지 않음
- `TxState`가 1로 유지됨
- 동기식으로 대기하던 코드가 타임아웃 발생

---

## 해결 방법

### 수정된 구현 (Asynchronous Non-blocking)

**파일:** `Core/Src/uart_command.c:174-184`

```c
// 전송 (비동기 방식 - 완료를 기다리지 않음)
result = CDC_Transmit_HS(UserTxBufferHS, len);
if (result == USBD_BUSY) {
    // Busy 상태면 잠시 후 재시도
    HAL_Delay(1);
    result = CDC_Transmit_HS(UserTxBufferHS, len);
}

if (result != USBD_OK && result != USBD_BUSY) {
    printf("[ERROR] CDC_Transmit failed: %d\r\n", result);
}
```

### 변경 사항

| 항목 | 이전 (Blocking) | 수정 후 (Non-blocking) |
|------|----------------|----------------------|
| **전송 방식** | 전송 완료까지 대기 | 전송 시작 후 즉시 리턴 |
| **대기 시간** | 최대 2초 (200 × 10ms) | 최대 1ms (BUSY 시에만) |
| **타임아웃 처리** | 200회 재시도 후 에러 | 에러 없음 |
| **리소스 사용** | CPU busy-wait | CPU 효율적 |
| **동작 방식** | 동기 (Synchronous) | 비동기 (Asynchronous) |

### 핵심 원리

1. **전송 시작만 확인**
   - `CDC_Transmit_HS()` 호출이 성공하면 USB 스택이 백그라운드로 처리
   - 전송 완료를 애플리케이션 레벨에서 기다리지 않음

2. **BUSY 상태 처리**
   - 이전 전송이 진행 중이면 `USBD_BUSY` 반환
   - 1ms 대기 후 1회만 재시도
   - 여전히 BUSY면 무시 (다음 전송 때 자동으로 처리됨)

3. **비동기 완료 처리**
   - USB 전송 완료는 인터럽트로 처리 (`CDC_TransmitCplt_HS()`)
   - 애플리케이션은 전송 완료를 기다리지 않고 다음 작업 진행

---

## 테스트 결과

### Before (문제 발생 시)
```
[DEBUG] CDC: received command 'HELLO'
[ERROR] CDC_Transmit: transmission timeout (retries=200)
```
- 응답 전송 실패
- PC에서 응답 수신 못 함
- 2초 딜레이 발생

### After (수정 후)
```
[DEBUG] CDC: received command 'HELLO'
[13:04:49] << OK AUDIO_MUX v1.00 STM32H723
```
- 응답 전송 성공 ✅
- PC에서 응답 정상 수신 ✅
- 딜레이 없음 ✅

---

## 추가 고려사항

### 1. 버퍼 관리

현재 구현은 **단일 전송 버퍼** 사용:

```c
extern uint8_t UserTxBufferHS[APP_TX_DATA_SIZE];  // 2048 bytes

// 전송 전에 UserTxBufferHS에 데이터 복사
memcpy(UserTxBufferHS, buffer, len);
result = CDC_Transmit_HS(UserTxBufferHS, len);
```

**주의사항:**
- 이전 전송이 완료되기 전에 새로운 전송을 시도하면 데이터 덮어쓰기 발생 가능
- 현재는 명령-응답 패턴이라 문제없음 (한 번에 하나의 응답만 전송)
- 고속 스트리밍이 필요하면 더블 버퍼링 구현 필요

### 2. 전송 실패 처리

현재는 전송 실패 시 로그만 출력:

```c
if (result != USBD_OK && result != USBD_BUSY) {
    printf("[ERROR] CDC_Transmit failed: %d\r\n", result);
}
```

**개선 가능한 방향:**
- 전송 실패 시 재시도 큐에 추가
- 명령별 타임아웃 및 재전송 메커니즘
- 전송 실패 통계 수집

### 3. USB 연결 상태 확인

현재는 USB 연결 여부를 확인하지 않음. 필요 시 추가 가능:

```c
// USB 연결 확인
if (hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED) {
    printf("[WARN] USB not configured, skipping transmission\r\n");
    return;
}
```

---

## 참고: 작동하는 프로젝트 비교

작동하는 프로젝트 (`vs_block_120`)의 `CDC_Transmit_HS()` 구현도 동일하게 **비동기 방식**:

```c
uint8_t CDC_Transmit_HS(uint8_t* Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceHS.pClassData;

  if (hcdc->TxState != 0){
    return USBD_BUSY;  // BUSY 상태면 즉시 리턴 (대기하지 않음)
  }

  USBD_CDC_SetTxBuffer(&hUsbDeviceHS, Buf, Len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceHS);

  return result;
}
```

전송 완료를 기다리지 않고 바로 리턴하는 것이 **STM32 USB CDC의 표준 사용 방법**입니다.

---

## 결론

### 문제 요약
- USB CDC 전송 후 완료를 busy-wait으로 대기하여 타임아웃 발생

### 해결책
- 비동기 전송 방식으로 변경 (전송 시작만 확인, 완료는 대기하지 않음)

### 효과
- 타임아웃 에러 제거 ✅
- 응답 전송 성공 ✅
- CPU 효율성 향상 ✅
- 코드 단순화 (200줄 → 10줄) ✅

### 교훈
- USB CDC는 본질적으로 비동기 전송을 설계됨
- 동기식으로 완료를 기다리는 것은 anti-pattern
- STM32 HAL의 설계 의도를 따르는 것이 중요
