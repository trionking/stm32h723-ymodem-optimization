# UART2 DMA TX 문제 해결 가이드

**작성일:** 2025-11-09
**프로젝트:** STM32H723 Audio Multiplexer Firmware
**문제:** UART2 DMA TX가 작동하지 않음

---

## 문제 증상

- `HAL_UART_Transmit_DMA()` 호출 시 `huart->gState`가 33 (HAL_UART_STATE_BUSY)로 남아있음
- DMA TX Complete 콜백 `HAL_UART_TxCpltCallback()`이 호출되지 않음
- DMA1_Stream1 인터럽트가 전혀 발생하지 않음
- UART2 출력 신호가 나가지 않음

---

## 근본 원인

### **STM32H7의 메모리 접근 제약사항**

STM32H7 시리즈에서 **DTCMRAM (0x20000000)은 CPU 전용 메모리**입니다.

```
DTCMRAM (0x20000000-0x2001FFFF)
├─ CPU 직접 접근: ✅ 가능
├─ DMA 접근: ❌ 불가능
└─ D-Cache: 사용 안 함 (zero wait state)

RAM_D2 (0x30000000-0x30007FFF)
├─ CPU 접근: ✅ 가능
├─ DMA 접근: ✅ 가능
└─ D-Cache: MPU로 OFF 설정됨
```

### 문제 코드

```c
// user_com.c (잘못된 코드)
__attribute__((section(".uart_dma_buffer"))) __attribute__((aligned(32)))
struct uart_Stat_ST uart2_stat_ST = {0};
```

링커 스크립트:
```ld
.dma_buffer (NOLOAD) :
{
    *(.uart_dma_buffer)
} >DTCMRAM    ← DMA 접근 불가!
```

**결과:**
1. `HAL_UART_Transmit_DMA(&huart2, uart2_stat_ST.uart_tx_usr_buf, q_len)` 호출
2. DMA가 DTCMRAM의 버퍼를 읽으려고 시도
3. **DMA는 DTCMRAM에 접근할 수 없어서 전송 실패**
4. DMA 인터럽트가 발생하지 않음
5. `huart->gState`가 BUSY로 계속 남음

---

## 해결 방법

### 1. DMA 버퍼를 RAM_D2로 이동

```c
// user_com.c (수정된 코드)
// UART DMA 버퍼는 RAM_D2에 배치 (DMA 접근 가능)
// DTCMRAM은 CPU만 접근 가능하므로 DMA 사용 불가!
__attribute__((section(".ram_d2"))) __attribute__((aligned(32)))
struct uart_Stat_ST uart2_stat_ST = {0};
```

링커 스크립트 (STM32H723ZGTX_FLASH.ld):
```ld
/* RAM_D2: SD MDMA buffers - Cache OFF (MPU Region 4: 0x30000000-0x30007FFF) */
.ram_d2 (NOLOAD) :
{
    . = ALIGN(32);
    *(.sd_mdma_buffer)
    *(.ram_d2)            ← UART2 DMA 버퍼 배치
    *(.ram_d2.*)
    *(.UsbCompositeSection)
    . = ALIGN(32);
} >RAM_D2
```

### 2. 추가 수정 사항

#### 2.1 중복 UART 초기화 제거

```c
// user_com.c - init_UART_COM()
void init_UART_COM(void)
{
    // UART2는 main.c에서 이미 115200으로 초기화됨
    // 재초기화하면 DMA 링크나 상태가 꼬일 수 있음
    //UART_baudrate_set(&huart2,115200);  ← 주석 처리

    // uart2 큐 초기화
    InitQueue(&rx_UART2_line_queue,256);
    InitQueue(&rx_UART2_queue,512);
    InitQueue(&tx_UART2_queue,512);

    // UART2 상태를 강제로 READY로 설정
    huart2.gState = HAL_UART_STATE_READY;

    // RX DMA는 필요시 활성화 (선택사항)
    //__HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
    //HAL_UART_Receive_DMA(&huart2, uart2_stat_ST.uart_rx_line_buf, DMA_RX_BUFFER_SIZE);
}
```

#### 2.2 printf() 직후 즉시 전송

```c
// user_def.c - run_proc()
void run_proc(void)
{
    init_proc();

    printf("\r\n========================================\r\n");
    printf("  Audio Mux v1.00\r\n");
    printf("========================================\r\n\r\n");
    UART2_Process_TX_Queue();  // printf 출력 즉시 전송 시작

    printf("Initializing SD card...\r\n");
    UART2_Process_TX_Queue();  // 즉시 전송
    init_sdmmc();

    // ... (각 printf 블록 후마다 호출)
}
```

이유:
- 초기화 중 printf()가 많이 호출되어 큐에 데이터가 계속 쌓임
- 첫 `UART2_Process_TX_Queue()`는 500ms 후에나 호출됨
- 즉시 전송하지 않으면 큐 오버플로우 가능

#### 2.3 간소화된 UART2_Process_TX_Queue()

```c
// user_com.c
void UART2_Process_TX_Queue(void)
{
    // Check if there's data in TX queue
    uint16_t q_len = Len_queue(&tx_UART2_queue);
    if (q_len == 0)
        return;  // Nothing to send

    // Check if DMA is ready (레퍼런스 코드 방식)
    if (hdma_usart2_tx.State == HAL_DMA_STATE_READY)
    {
        // Limit chunk size to DMA buffer size
        if (q_len > DMA_TX_BUFFER_SIZE)
            q_len = DMA_TX_BUFFER_SIZE;

        // Copy data from queue to DMA buffer
        Dequeue_bytes(&tx_UART2_queue, uart2_stat_ST.uart_tx_usr_buf, q_len);

        // RAM_D2는 캐시 OFF이므로 D-Cache flush 불필요
        // Start DMA transmission
        HAL_UART_Transmit_DMA(&huart2, uart2_stat_ST.uart_tx_usr_buf, q_len);
    }
}

void UART2_TX_Complete_Callback(void)
{
    // Immediately process next chunk if available
    UART2_Process_TX_Queue();
}

void UART2_Error_Callback(void)
{
    // Clear error flags
    __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_PEF | UART_CLEAR_FEF |
                                    UART_CLEAR_NEF | UART_CLEAR_OREF);
}
```

#### 2.4 HAL 콜백 등록

```c
// stm32h7xx_it.c
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        extern void UART2_TX_Complete_Callback(void);
        UART2_TX_Complete_Callback();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        extern void UART2_Error_Callback(void);
        UART2_Error_Callback();
    }
}
```

---

## STM32H7 메모리 맵 (DMA 관련)

| 메모리 영역 | 주소 범위 | DMA 접근 | 캐시 | 용도 |
|------------|----------|---------|------|------|
| ITCMRAM | 0x00000000-0x0000FFFF | ❌ | - | Stack, Heap (CPU 전용) |
| DTCMRAM | 0x20000000-0x2001FFFF | ❌ | - | 빠른 데이터 (CPU 전용) |
| RAM_D1_DMA | 0x24000000-0x2401FFFF | ✅ | OFF | 대형 DMA 버퍼 |
| RAM_D1_CACHE1 | 0x24020000-0x2402FFFF | ✅ | ON | 초기화된 데이터 |
| RAM_D1_CACHE2 | 0x24030000-0x2404FFFF | ✅ | ON | 초기화되지 않은 데이터 |
| RAM_D2 | 0x30000000-0x30007FFF | ✅ | OFF | SDMMC, USB, **UART DMA** |
| RAM_D3 | 0x38000000-0x38003FFF | ✅ | OFF | ADC3 BDMA |

---

## DMA 버퍼 배치 가이드라인

### ✅ DMA 사용 가능 영역

```c
// 작은 DMA 버퍼 (UART, SPI) - RAM_D2 권장
uint8_t uart_tx_buf[512] __attribute__((section(".ram_d2"))) __attribute__((aligned(32)));

// 대형 DMA 버퍼 (오디오) - RAM_D1_DMA 권장
uint32_t audio_buf[8192] __attribute__((section(".ram_d1_dma"))) __attribute__((aligned(32)));

// SD 카드 MDMA - RAM_D2
uint8_t sd_buf[4096] __attribute__((section(".sd_mdma_buffer"))) __attribute__((aligned(32)));
```

### ❌ DMA 사용 불가 영역

```c
// 절대 사용 금지!
uint8_t dma_buf[512] __attribute__((section(".dma_buffer")));  // → DTCMRAM
```

### 정렬 (Alignment) 규칙

- **모든 DMA 버퍼는 32바이트 정렬 필수**: `__attribute__((aligned(32)))`
- D-Cache 라인 크기(32 bytes)와 일치하여 캐시 일관성 보장
- MPU로 캐시 OFF 설정된 영역이라도 정렬 권장

---

## 캐시 관리

### RAM_D2는 캐시 OFF

```c
// RAM_D2 (0x30000000)는 MPU로 캐시 OFF 설정됨
// D-Cache flush/invalidate 불필요!

void UART2_Process_TX_Queue(void)
{
    // ...
    Dequeue_bytes(&tx_UART2_queue, uart2_stat_ST.uart_tx_usr_buf, q_len);

    // ❌ 불필요 (RAM_D2는 캐시 사용 안 함)
    // SCB_CleanDCache_by_Addr(...);

    HAL_UART_Transmit_DMA(&huart2, uart2_stat_ST.uart_tx_usr_buf, q_len);
}
```

### 캐시가 있는 영역 (RAM_D1_CACHE)

```c
// RAM_D1_CACHE1/2 (0x24020000-)는 캐시 ON
// DMA 전에 반드시 D-Cache flush 필요!

uint8_t tx_buf[512] __attribute__((section(".data"))) __attribute__((aligned(32)));

void send_data(void)
{
    // DMA가 최신 데이터를 읽도록 캐시 플러시
    uint32_t cache_size = (len + 31) & ~31;  // 32바이트 배수로 올림
    SCB_CleanDCache_by_Addr((uint32_t*)tx_buf, cache_size);

    HAL_UART_Transmit_DMA(&huart2, tx_buf, len);
}
```

---

## 체크리스트

DMA 사용 시 확인해야 할 사항:

- [ ] DMA 버퍼가 DMA 접근 가능한 메모리 영역에 배치되었는가?
  - ✅ RAM_D1_DMA, RAM_D1_CACHE, RAM_D2, RAM_D3
  - ❌ ITCMRAM, DTCMRAM

- [ ] 32바이트 정렬이 적용되었는가?
  - `__attribute__((aligned(32)))`

- [ ] 캐시가 켜진 영역이면 D-Cache flush를 수행하는가?
  - RAM_D1_CACHE: 필요
  - RAM_D1_DMA, RAM_D2, RAM_D3: 불필요

- [ ] DMA 인터럽트 우선순위가 적절한가?
  - 높은 우선순위: DMA TX (1-2)
  - 중간 우선순위: UART, SDMMC (2-3)
  - 낮은 우선순위: USB (3-5)

- [ ] DMA 완료 콜백이 등록되어 있는가?
  - `HAL_UART_TxCpltCallback()` 구현

---

## 레퍼런스 코드

STM32H7에서 UART DMA TX의 올바른 사용 예시:

```c
// 1. DMA 버퍼 선언 (RAM_D2)
__attribute__((section(".ram_d2"))) __attribute__((aligned(32)))
struct uart_Stat_ST {
    uint8_t uart_tx_usr_buf[512];
    uint8_t uart_rx_line_buf[256];
} uart2_stat_ST = {0};

// 2. 전송 함수
void UART2_Process_TX_Queue(void)
{
    uint16_t q_len = Len_queue(&tx_UART2_queue);
    if (q_len == 0) return;

    if (hdma_usart2_tx.State == HAL_DMA_STATE_READY)
    {
        if (q_len > DMA_TX_BUFFER_SIZE)
            q_len = DMA_TX_BUFFER_SIZE;

        Dequeue_bytes(&tx_UART2_queue, uart2_stat_ST.uart_tx_usr_buf, q_len);

        // RAM_D2는 캐시 OFF이므로 flush 불필요
        HAL_UART_Transmit_DMA(&huart2, uart2_stat_ST.uart_tx_usr_buf, q_len);
    }
}

// 3. 완료 콜백
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        UART2_Process_TX_Queue();  // 다음 청크 전송
    }
}
```

---

## 참고 문서

- STM32H723 Reference Manual (RM0468)
  - Section 2.3: Memory organization
  - Section 3.4.2: MPU region attribute and size register
- STM32H7x3 Datasheet
  - Table 7: Memory map
- AN4891: STM32H7 system architecture and performance

---

## 변경 이력

| 날짜 | 작성자 | 내용 |
|------|-------|------|
| 2025-11-09 | Claude | 최초 작성 - UART2 DMA TX 문제 해결 |
