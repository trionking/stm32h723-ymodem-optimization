# STM32H7 SDMMC DMA 모드 설정 가이드

STM32CubeMX/CubeIDE로 생성된 프로젝트에서 SDMMC를 DMA 모드로 동작시키기 위한 설정 가이드입니다.

## 목차
1. [STM32CubeMX 설정](#1-stm32cubemx-설정)
2. [생성된 파일 수정](#2-생성된-파일-수정)
3. [핵심 개념](#3-핵심-개념)
4. [트러블슈팅](#4-트러블슈팅)

---

## 1. STM32CubeMX 설정

### 1.1 SDMMC 기본 설정
- **Mode**: SD 4 bits Wide bus
- **Clock Divide Factor**: 8 (안정성을 위해 낮게 설정)
- **Hardware Flow Control**: Enable ✅
- **Global Interrupt**: Enable ✅

### 1.2 클록 설정
- SDMMC Clock 확인 (PLL 설정에 따라 결정됨)
- Divide Factor로 실제 SD 클럭 조정

### 1.3 캐시 설정
- **I-Cache**: Enable
- **D-Cache**: Enable
- **MPU**: 설정 필요 (non-cacheable 영역)

---

## 2. 생성된 파일 수정

### 2.1 `Core/Src/main.c`

#### MX_SDMMC1_SD_Init() 함수 수정

**위치**: `USER CODE BEGIN SDMMC1_Init 2` 섹션

```c
void MX_SDMMC1_SD_Init(void)
{
  /* ... CubeMX 생성 코드 ... */

  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;  // ✅ ENABLE
  hsd1.Init.ClockDiv = 8;  // ✅ 안정성을 위해 8로 설정

  if (HAL_SD_Init(&hsd1) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN SDMMC1_Init 2 */
  /* ⭐ CRITICAL: IDMA 인터럽트 활성화 */
  __HAL_SD_ENABLE_IT(&hsd1, SDMMC_IT_IDMABTC);
  /* USER CODE END SDMMC1_Init 2 */
}
```

**핵심 포인트**:
- `HardwareFlowControl = ENABLE`: 데이터 흐름 제어
- `ClockDiv = 8`: RXOVERR 방지를 위한 클럭 감속
- `__HAL_SD_ENABLE_IT()`: **STM32H7 IDMA 인터럽트는 HAL이 자동으로 활성화하지 않음!**

---

### 2.2 `FATFS/Target/sd_diskio.c`

#### 2.2.1 캐시 메인터넌스 활성화

**위치**: 파일 상단 define 섹션

```c
/* USER CODE BEGIN enableSDDmaCacheMaintenance */
#define ENABLE_SD_DMA_CACHE_MAINTENANCE  1  // ✅ 주석 해제하고 1로 설정
/* USER CODE END enableSDDmaCacheMaintenance */
```

#### 2.2.2 DMA 플래그 변수 선언 수정

**위치**: Private variables 섹션

```c
// ❌ 잘못된 선언 (static으로 인해 외부 접근 불가)
// static volatile UINT WriteStatus = 0, ReadStatus = 0;

// ✅ 올바른 선언 (USB MSC 등 외부 모듈에서 사용 가능)
/* Made non-static to allow USB MSC to use DMA with these flags */
volatile UINT WriteStatus = 0, ReadStatus = 0;
```

#### 2.2.3 SD_read() 함수 - DMA 버전 구현

**위치**: `USER CODE BEGIN beforeReadSection` ~ `USER CODE END beforeReadSection`

```c
/* USER CODE BEGIN beforeReadSection */

/**
  * @brief  Reads Sector(s) - DMA version with cache coherency
  */
DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
  DRESULT res = RES_ERROR;
  uint32_t timeout;
#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)
  uint32_t alignedAddr;
#endif

  /* Ensure SD card is ready */
  if (SD_CheckStatusWithTimeout(SD_TIMEOUT) < 0)
  {
    return res;
  }

  /* ⭐ 직접 사용자 버퍼에 DMA (중간 버퍼 없음) */
  if(BSP_SD_ReadBlocks_DMA((uint32_t*)buff,
                           (uint32_t)sector,
                           count) == MSD_OK)
  {
    ReadStatus = 0;

    /* DMA 완료 대기 (콜백에서 ReadStatus = 1 설정) */
    timeout = HAL_GetTick();
    while((ReadStatus == 0) && ((HAL_GetTick() - timeout) < SD_TIMEOUT))
    {
    }

    if (ReadStatus == 0)
    {
      res = RES_ERROR;  // DMA 타임아웃
    }
    else
    {
      /* ⭐ CRITICAL: DMA 완료 후 캐시 무효화 */
      /* 캐시된 데이터 대신 DMA로 받은 실제 데이터를 읽도록 보장 */
#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)
      alignedAddr = (uint32_t)buff & ~0x1F;  // 32-byte 정렬
      SCB_InvalidateDCache_by_Addr((uint32_t*)alignedAddr,
                                    count*BLOCKSIZE + ((uint32_t)buff - alignedAddr));
#endif
      ReadStatus = 0;
      timeout = HAL_GetTick();

      /* 카드 READY 대기 */
      while((HAL_GetTick() - timeout) < SD_TIMEOUT)
      {
        if (BSP_SD_GetCardState() == SD_TRANSFER_OK)
        {
          res = RES_OK;
          break;
        }
      }
    }
  }

  return res;
}

/* USER CODE END beforeReadSection */
```

#### 2.2.4 SD_write() 함수 - DMA 버전 구현

**위치**: `USER CODE BEGIN beforeWriteSection` ~ `USER CODE END beforeWriteSection`

```c
/* USER CODE BEGIN beforeWriteSection */

#if _USE_WRITE == 1

/**
  * @brief  Writes Sector(s) - DMA version with cache coherency
  */
DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
  DRESULT res = RES_ERROR;
  uint32_t timeout;
#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)
  uint32_t alignedAddr;
#endif

  WriteStatus = 0;

  /* Ensure SD card is ready */
  if (SD_CheckStatusWithTimeout(SD_TIMEOUT) < 0)
  {
    return res;
  }

#if (ENABLE_SD_DMA_CACHE_MAINTENANCE == 1)
  /* ⭐ CRITICAL: DMA 시작 전 캐시 클린 */
  /* 캐시에 있는 수정된 데이터를 메모리로 플러시하여 DMA가 최신 데이터를 읽도록 보장 */
  alignedAddr = (uint32_t)buff & ~0x1F;  // 32-byte 정렬
  SCB_CleanDCache_by_Addr((uint32_t*)alignedAddr,
                          count*BLOCKSIZE + ((uint32_t)buff - alignedAddr));
#endif

  /* ⭐ 직접 사용자 버퍼에서 DMA (중간 버퍼 없음) */
  if(BSP_SD_WriteBlocks_DMA((uint32_t*)buff,
                            (uint32_t)sector,
                            count) == MSD_OK)
  {
    /* DMA 완료 대기 (콜백에서 WriteStatus = 1 설정) */
    timeout = HAL_GetTick();
    while((WriteStatus == 0) && ((HAL_GetTick() - timeout) < SD_TIMEOUT))
    {
    }

    if (WriteStatus == 0)
    {
      res = RES_ERROR;  // DMA 타임아웃
    }
    else
    {
      WriteStatus = 0;
      timeout = HAL_GetTick();

      /* 카드 READY 대기 */
      while((HAL_GetTick() - timeout) < SD_TIMEOUT)
      {
        if (BSP_SD_GetCardState() == SD_TRANSFER_OK)
        {
          res = RES_OK;
          break;
        }
      }
    }
  }

  return res;
}
#endif /* _USE_WRITE == 1 */

/* USER CODE END beforeWriteSection */
```

#### 2.2.5 콜백 구현 (선택사항 - 디버그용)

**위치**: CallBacksSection_D

```c
/* USER CODE BEGIN CallBacksSection_D */

void BSP_SD_ReadCpltCallback(void)
{
  // printf("[IRQ] Read complete\r\n");  // 디버그용
  ReadStatus = 1;
}

void BSP_SD_WriteCpltCallback(void)
{
  // printf("[IRQ] Write complete\r\n");  // 디버그용
  WriteStatus = 1;
}

/* USER CODE END CallBacksSection_D */
```

---

### 2.3 `FATFS/Target/bsp_driver_sd.c` (수정 불필요)

CubeMX가 생성한 코드가 이미 올바릅니다:
- `BSP_SD_ReadBlocks_DMA()` → `HAL_SD_ReadBlocks_DMA()` 호출
- `BSP_SD_WriteBlocks_DMA()` → `HAL_SD_WriteBlocks_DMA()` 호출
- `HAL_SD_RxCpltCallback()` → `BSP_SD_ReadCpltCallback()` 호출
- `HAL_SD_TxCpltCallback()` → `BSP_SD_WriteCpltCallback()` 호출

---

### 2.4 `Core/Src/stm32h7xx_it.c` (수정 불필요)

CubeMX가 생성한 인터럽트 핸들러가 이미 올바릅니다:

```c
void SDMMC1_IRQHandler(void)
{
  /* USER CODE BEGIN SDMMC1_IRQn 0 */

  /* USER CODE END SDMMC1_IRQn 0 */
  HAL_SD_IRQHandler(&hsd1);  // ✅ HAL 인터럽트 핸들러 호출
  /* USER CODE BEGIN SDMMC1_IRQn 1 */

  /* USER CODE END SDMMC1_IRQn 1 */
}
```

---

### 2.5 `Core/Src/stm32h7xx_hal_msp.c` (수정 불필요)

NVIC 설정이 이미 올바릅니다:

```c
void HAL_SD_MspInit(SD_HandleTypeDef* hsd)
{
  /* ... GPIO 설정 ... */

  /* SDMMC1 interrupt Init */
  HAL_NVIC_SetPriority(SDMMC1_IRQn, 2, 0);  // ✅ 우선순위 설정
  HAL_NVIC_EnableIRQ(SDMMC1_IRQn);          // ✅ 인터럽트 활성화
}
```

**주의**: STM32H7의 SDMMC는 **IDMA (Internal DMA)**를 사용하므로,
DMA1/DMA2 핸들 설정이 **필요 없습니다**. (외부 DMA 컨트롤러를 사용하지 않음)

---

## 3. 핵심 개념

### 3.1 STM32H7 SDMMC IDMA

STM32H7의 SDMMC는 **IDMA (Internal DMA)**를 내장하고 있습니다:

- **DMA1/DMA2와 무관**: 외부 DMA 컨트롤러 설정 불필요
- **IDMA 전용 인터럽트**: `SDMMC_IT_IDMABTC` (Buffer Transfer Complete)
- **수동 활성화 필요**: `__HAL_SD_ENABLE_IT(&hsd1, SDMMC_IT_IDMABTC)`

### 3.2 캐시 Coherency (일관성)

STM32H7은 **D-Cache**를 사용하므로 DMA와 캐시 간 동기화가 필수입니다:

#### DMA Read (SD → RAM)
```
1. CPU가 buff 주소의 데이터를 캐시에 가지고 있을 수 있음
2. DMA가 메모리에 새 데이터 씀
3. ⚠️ CPU는 여전히 캐시의 오래된 데이터를 읽음
4. ✅ 해결: DMA 완료 후 SCB_InvalidateDCache_by_Addr()
   → 캐시를 무효화하여 메모리에서 새로 읽어오도록 강제
```

#### DMA Write (RAM → SD)
```
1. CPU가 buff에 데이터를 쓰면 캐시에만 저장될 수 있음
2. ⚠️ DMA가 메모리를 읽으면 캐시에 있는 최신 데이터를 못 봄
3. ✅ 해결: DMA 시작 전 SCB_CleanDCache_by_Addr()
   → 캐시의 수정된 데이터를 메모리로 플러시
```

#### 32-byte 정렬
D-Cache는 32-byte 단위로 동작하므로 주소를 정렬해야 합니다:

```c
// 예: buff = 0x24001234 인 경우
uint32_t alignedAddr = (uint32_t)buff & ~0x1F;
// alignedAddr = 0x24001220 (32-byte 경계로 내림)

// 크기도 정렬 경계를 포함하도록 계산
uint32_t size = count * BLOCKSIZE + ((uint32_t)buff - alignedAddr);
```

### 3.3 메모리 구조 (STM32H723)

```
┌─────────────────────────────────────────────────┐
│ FLASH (0x08000000, 1MB)                         │
│  - 코드, 상수, 읽기 전용 데이터                  │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│ ITCMRAM (0x00000000, 64KB)                      │
│  - Stack, Heap (빠른 접근)                       │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│ DTCMRAM (0x20000000, 128KB)                     │
│  - DMA 버퍼 (SPI, I2S, UART) - Zero wait        │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│ RAM_D1 (0x24000000, 320KB) - AXI SRAM           │
│  - 0x24000000~0x2401FFFF (128KB): Cache OFF     │ MPU Region 1
│  - 0x24020000~0x2402FFFF (64KB):  Cache ON      │ MPU Region 2
│  - 0x24030000~0x2404FFFF (128KB): Cache ON      │ MPU Region 3
│    .data, .bss, 일반 변수                        │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│ RAM_D2 (0x30000000, 32KB) - AHB SRAM            │ MPU Region 4
│  - Cache OFF (SDMMC DMA 버퍼용)                 │
│  - .sd_mdma_buffer, .ram_d2                     │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│ RAM_D3 (0x38000000, 16KB) - SRAM4               │ MPU Region 5
│  - Cache OFF (ADC3 BDMA 버퍼용)                 │
│  - .adc3_buffer, .ram_d3                        │
└─────────────────────────────────────────────────┘
```

**중요**:
- **SDMMC IDMA**: 모든 RAM 영역 접근 가능 (D-Cache coherency만 처리하면 됨)
- **BDMA**: SRAM4 (RAM_D3)만 접근 가능 (ADC3, LPUART 등 D3 도메인 페리퍼럴용)

---

## 4. 트러블슈팅

### 4.1 DMA 타임아웃 발생

**증상**: `ReadStatus` 또는 `WriteStatus`가 0으로 유지되어 타임아웃

**체크리스트**:
1. ✅ `__HAL_SD_ENABLE_IT(&hsd1, SDMMC_IT_IDMABTC)` 호출했는가?
2. ✅ `WriteStatus`/`ReadStatus`가 `static`으로 선언되지 않았는가?
3. ✅ `SDMMC1_IRQHandler()`가 `HAL_SD_IRQHandler(&hsd1)` 호출하는가?
4. ✅ `HAL_NVIC_EnableIRQ(SDMMC1_IRQn)` 호출되었는가?

**디버깅**:
```c
// stm32h7xx_it.c에 카운터 추가
void SDMMC1_IRQHandler(void)
{
  static volatile uint32_t irq_count = 0;
  irq_count++;  // 디버거로 확인
  HAL_SD_IRQHandler(&hsd1);
}
```

### 4.2 데이터 손상 (잘못된 데이터 읽힘)

**증상**: 파일 시스템 에러, 읽은 데이터가 올바르지 않음

**원인**: 캐시 coherency 문제

**해결**:
1. ✅ `ENABLE_SD_DMA_CACHE_MAINTENANCE` 활성화 확인
2. ✅ Read: DMA 완료 **후** `SCB_InvalidateDCache_by_Addr()` 호출 확인
3. ✅ Write: DMA 시작 **전** `SCB_CleanDCache_by_Addr()` 호출 확인
4. ✅ 정렬 계산 확인 (`& ~0x1F`)

### 4.3 HardFault 발생

**증상**: 시스템이 HardFault로 멈춤

**가능한 원인**:
1. **MPU 설정 오류**: D-Cache가 켜진 상태에서 MPU가 올바르게 설정되지 않음
2. **잘못된 메모리 접근**: DMA 버퍼가 접근 불가능한 영역에 있음
3. **정렬 문제**: 캐시 작업 시 잘못된 주소 정렬

**해결**:
- MPU 설정 확인 (main.c 초기화 코드)
- 버퍼가 올바른 RAM 영역에 있는지 확인

### 4.4 성능 저하

**증상**: DMA 모드인데도 느림

**체크리스트**:
1. ✅ `ClockDiv` 값 확인 (8은 안정적이지만 느림, 2~4 시도 가능)
2. ✅ `HardwareFlowControl` 활성화 확인
3. ✅ 중간 버퍼 복사하지 않고 **직접 DMA** 사용하는지 확인
4. ✅ SD 카드 품질 확인 (Class 10 이상 권장)

---

## 5. 검증 방법

### 5.1 DMA 동작 확인

```c
/* 콜백에 printf 추가 */
void BSP_SD_ReadCpltCallback(void)
{
  printf("[DMA] Read complete\r\n");
  ReadStatus = 1;
}
```

로그에 `[DMA] Read complete`가 출력되면 DMA가 정상 동작하는 것입니다.

### 5.2 인터럽트 발생 확인

```c
void SDMMC1_IRQHandler(void)
{
  static volatile uint32_t irq_count = 0;
  irq_count++;  // 디버거 중단점에서 확인
  HAL_SD_IRQHandler(&hsd1);
}
```

디버거로 `irq_count`가 증가하는지 확인합니다.

### 5.3 파일 시스템 테스트

```c
FRESULT res;
FIL file;
UINT bytes_written;
char test_data[] = "Hello SDMMC DMA!";

// 파일 쓰기
res = f_open(&file, "test.txt", FA_CREATE_ALWAYS | FA_WRITE);
if (res == FR_OK)
{
  f_write(&file, test_data, strlen(test_data), &bytes_written);
  f_close(&file);
  printf("Write OK: %u bytes\r\n", bytes_written);
}

// 파일 읽기
char read_buf[32];
UINT bytes_read;
res = f_open(&file, "test.txt", FA_READ);
if (res == FR_OK)
{
  f_read(&file, read_buf, sizeof(read_buf), &bytes_read);
  f_close(&file);
  printf("Read OK: %s\r\n", read_buf);
}
```

---

## 6. 요약

### 필수 수정 사항

| 파일 | 위치 | 수정 내용 |
|------|------|-----------|
| `main.c` | `SDMMC1_Init 2` | `__HAL_SD_ENABLE_IT(&hsd1, SDMMC_IT_IDMABTC);` 추가 |
| `sd_diskio.c` | enableSDDmaCacheMaintenance | `#define ENABLE_SD_DMA_CACHE_MAINTENANCE 1` |
| `sd_diskio.c` | Private variables | `volatile UINT WriteStatus, ReadStatus;` (`static` 제거) |
| `sd_diskio.c` | beforeReadSection | DMA + 캐시 무효화 구현 |
| `sd_diskio.c` | beforeWriteSection | 캐시 클린 + DMA 구현 |

### 핵심 개념

1. **IDMA 인터럽트 수동 활성화**: HAL이 자동으로 활성화하지 않음
2. **캐시 동기화**:
   - Read: DMA 완료 **후** InvalidateCache
   - Write: DMA 시작 **전** CleanCache
3. **직접 DMA**: 중간 버퍼 없이 사용자 버퍼에 직접 DMA
4. **32-byte 정렬**: D-Cache 라인 크기에 맞춤

### 동작 확인

- 콜백 호출 로그: `[DMA] Read complete`
- FatFs 마운트 성공: `FatFs: Mounted successfully`
- 파일 읽기/쓰기 정상 동작

---

## 참고 자료

- STM32H7 Reference Manual (RM0468): SDMMC, IDMA 섹션
- AN4838: Managing memory protection unit in STM32 MCUs
- AN4839: Level 1 cache on STM32F7 Series and STM32H7 Series

---

**작성일**: 2025-01-26
**버전**: 1.0
**테스트 환경**: STM32H723ZGT6, STM32CubeIDE 1.17.0, FW Package v1.11.3
