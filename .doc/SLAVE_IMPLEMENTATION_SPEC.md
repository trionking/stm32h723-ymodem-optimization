# STM32H523 슬레이브 펌웨어 구현 명세서

**작성일**: 2025-11-01
**최종 업데이트**: 2025-11-03
**대상**: STM32H523 슬레이브 MCU 프로그래머
**마스터**: STM32H723ZGT6 (Main Board)
**프로토콜 버전**: 1.2
**상태**: ⚠️ **긴급 수정 필요** - 1-byte shift 문제 해결 요망

---

## 🚨 긴급 공지 (2025-11-03) - Slave 개발자 필독!

### 문제 상황: SPI 수신 시 1-byte shift 발생

**Master-Slave 통합 테스트 결과, Slave가 첫 번째 바이트를 놓치는 문제가 발견되었습니다.**

#### 증상
```
Master가 보낸 데이터:
[0xC0, 0x00, 0x00, 0x01, 0x00, 0x00]
 ^^^^  ^^^^  ^^^^  ^^^^  ^^^^  ^^^^
 HDR   ID    CH    CMD   PH    PL

Slave가 수신한 데이터:
[????, 0xC0, 0x00, 0x00, 0x01, 0x00]
       ^^^^  ^^^^  ^^^^  ^^^^  ^^^^
       ID    CH    CMD   PH    PL
```

**결과**: Slave가 `SlaveID=192` (0xC0)로 인식 → 헤더 바이트가 SlaveID 위치로 shift됨

#### 원인 분석
- **CS falling edge 직후** 첫 번째 SPI 클럭에서 전송되는 바이트를 Slave가 놓침
- Master는 CS 선택 후 100us 대기하지만, Slave의 SPI 수신 준비가 완료되기 전에 전송 시작
- NSS 하드웨어 신호와 SPI 페리페럴의 동기화 타이밍 문제

#### 현재 Master 측 임시 해결책 (비효율적)
```c
// spi_protocol.c - 현재 구현 (제거 예정)
spi_select_slave(slave_id);
uint8_t dummy = 0x00;
HAL_SPI_Transmit(hspi_protocol, &dummy, 1, 10);  // 더미 바이트 전송
spi_deselect_slave(slave_id);
HAL_Delay(5);  // 지연
spi_select_slave(slave_id);
HAL_SPI_Transmit(hspi_protocol, (uint8_t*)&packet, sizeof(packet), 100);  // 실제 패킷
spi_deselect_slave(slave_id);
```
→ **문제점**: CS 토글 2회, 지연 시간 추가, 효율 저하

---

### ✅ Slave 측 해결 방법 (필수 구현)

#### 해결책 1: 첫 바이트를 dummy로 읽고 버리기 (권장)

**CS falling edge 인터럽트에서 즉시 1 dummy byte를 수신하여 버립니다.**

```c
// CS 핀 EXTI 콜백
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == CS_Pin) {
        if (HAL_GPIO_ReadPin(CS_GPIO_Port, CS_Pin) == GPIO_PIN_RESET) {
            // CS가 LOW가 됨 → 수신 시작

            // ⚠️ 중요: 첫 바이트는 항상 손실되므로 더미로 읽어서 버림
            uint8_t dummy_byte;
            HAL_SPI_Receive(&hspi, &dummy_byte, 1, 10);

            // RDY 핀 LOW (처리 중)
            HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_RESET);

            // 실제 헤더부터 수신 시작
            HAL_SPI_Receive_IT(&hspi, &rx_header, 1);
            rx_state = STATE_WAIT_HEADER;
        }
    }
}
```

**장점**:
- 구현 간단
- Master 측 수정 불필요 (dummy byte 전송 로직 제거 가능)
- 모든 패킷에 동일하게 적용

**단점**:
- 매 패킷마다 1 byte overhead (무시할 수 있는 수준)

---

#### 해결책 2: N+1 바이트 수신 후 1바이트 shift (대안)

```c
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    static uint8_t rx_buffer[7];  // 명령 패킷: 6+1 = 7 bytes

    switch(rx_state) {
        case STATE_WAIT_HEADER:
            // 7 바이트 수신 (1 dummy + 6 actual)
            HAL_SPI_Receive_IT(hspi, rx_buffer, 7);
            rx_state = STATE_RECEIVE_CMD;
            break;

        case STATE_RECEIVE_CMD:
            // 첫 바이트(dummy)는 버리고, 2번째부터 사용
            memcpy(&rx_cmd_packet, rx_buffer + 1, 6);

            if (rx_cmd_packet.header == 0xC0) {
                process_command_packet(&rx_cmd_packet);
            }

            rx_state = STATE_WAIT_HEADER;
            break;
    }
}
```

**장점**:
- CS 인터럽트 불필요

**단점**:
- 모든 수신 로직을 N+1 크기로 변경 필요
- 복잡도 증가

---

### 권장 구현 순서

#### Step 1: CS 핀 EXTI 인터럽트 추가
```c
// GPIO 초기화에 추가
void MX_GPIO_Init(void)
{
    // ... 기존 코드 ...

    // CS 핀을 EXTI로 추가 설정 (SPI NSS와 동시 사용)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = CS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;  // Falling edge 감지
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(CS_GPIO_Port, &GPIO_InitStruct);

    // EXTI 인터럽트 활성화
    HAL_NVIC_SetPriority(EXTIx_IRQn, 0, 0);  // 최고 우선순위
    HAL_NVIC_EnableIRQ(EXTIx_IRQn);
}
```

#### Step 2: EXTI 콜백 구현
```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == CS_Pin) {
        GPIO_PinState cs_state = HAL_GPIO_ReadPin(CS_GPIO_Port, CS_Pin);

        if (cs_state == GPIO_PIN_RESET) {
            // CS falling edge: 전송 시작

            // 첫 바이트는 손실되므로 블로킹으로 1바이트 읽어 버림
            uint8_t dummy;
            HAL_SPI_Receive(&hspi, &dummy, 1, 10);

            // RDY LOW
            HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_RESET);

            // 실제 수신 시작 (헤더)
            HAL_SPI_Receive_IT(&hspi, &rx_header, 1);
            rx_state = STATE_WAIT_HEADER;
        }
        else {
            // CS rising edge: 전송 종료
            // RDY HIGH (처리 완료 후)
        }
    }
}
```

#### Step 3: 기존 SPI 수신 로직은 그대로 유지
```c
// HAL_SPI_RxCpltCallback은 수정 불필요
// 이미 dummy byte가 제거된 상태에서 수신이 시작되므로 정상 동작
```

---

### 테스트 방법

#### 1. 수정 전 (문제 상황)
```
Master → SPITEST BASIC 0
Slave 출력:
[ERROR] Unknown header: 0x00  (5회 - dummy bytes)
SlaveID=192, Channel=0, CMD=0  (잘못된 해석)
```

#### 2. 수정 후 (정상 동작)
```
Master → SPITEST BASIC 0
Slave 출력:
SlaveID=0, Channel=0, CMD=PLAY (0x01)  ✅
SlaveID=0, Channel=1, CMD=PLAY (0x01)  ✅
SlaveID=0, Channel=0, CMD=STOP (0x02)  ✅
SlaveID=0, Channel=1, CMD=STOP (0x02)  ✅
```

---

### Master 측 변경 사항 (Slave 수정 후)

Slave 펌웨어가 수정된 후, Master에서 다음 코드를 제거할 예정:

```c
// spi_protocol.c - 제거 예정 코드
// ❌ 더미 바이트 전송 (불필요)
spi_select_slave(slave_id);
uint8_t dummy = 0x00;
HAL_SPI_Transmit(hspi_protocol, &dummy, 1, 10);
spi_deselect_slave(slave_id);
for (volatile int i = 0; i < 5000; i++);

// ✅ 직접 실제 패킷 전송
spi_select_slave(slave_id);
status = HAL_SPI_Transmit(hspi_protocol, (uint8_t*)&packet, sizeof(packet), 100);
spi_deselect_slave(slave_id);
```

이렇게 하면 전송 효율이 크게 향상됩니다.

---

### 요약

**Slave 개발자가 해야 할 일**:
1. ✅ CS 핀을 EXTI로 설정 (falling edge 감지)
2. ✅ CS falling edge 콜백에서 첫 1 byte를 블로킹으로 읽어 버림
3. ✅ 그 후 정상적으로 헤더부터 수신 시작
4. ✅ 테스트 후 Master 팀에 정상 동작 확인

**예상 소요 시간**: 1~2시간 (구현 + 테스트)

**Master 팀 연락처**: 정상 동작 확인 후 dummy byte 전송 로직 제거 요청

---

## 🎯 프로젝트 현황 (2025-11-01)

### Master 측 완료 사항
- ✅ SD 카드 FATFS (D-Cache 일관성 문제 해결)
- ✅ WAV 파일 파서 (32kHz 16-bit Mono)
- ✅ **SPI 프로토콜 구현** (명령/데이터 패킷 전송)
- ✅ 6채널 오디오 스트리밍 시스템
- ✅ UART2 명령 인터페이스 (115200 baud)
- ✅ USB CDC 통신 (Virtual COM Port)
- ✅ **Y-MODEM 파일 업로드** (PC → SD 카드)
- ✅ Python GUI 프로그램 (파일 변환 및 업로드)

### Slave 측 완료 사항 (Slave ID 0 기준)
- ✅ SPI 프로토콜 수신 구현 (명령/데이터 패킷)
- ✅ RDY 핀 제어 로직
- ✅ 2채널 DAC 출력 (DAC1, DAC2)
- ✅ 이중 버퍼링 시스템
- ✅ 32kHz 타이머 트리거
- ✅ 에러 처리 및 통계

### 프로토콜 호환성 검증 (2025-11-01)
**Master와 Slave 프로토콜 비교 완료:**
- ✅ 패킷 구조 완벽 일치 (명령 6 bytes, 데이터 5 bytes header)
- ✅ 명령 코드 일치 (PLAY, STOP, VOLUME)
- ✅ **RESET 명령 추가** (Master에 0xFF 추가됨)
- ✅ **CS 셋업 타임 50μs 구현** (Master 측 개선)
- ✅ Big-endian/Little-endian 인코딩 일치
- ✅ 핀 매핑 일치 (CS, RDY)
- ✅ DMA 콜백 최적화 (Master)

### 통합 테스트 계획
1. **Phase 1**: Master-Slave 통신 테스트 (1개 Slave, 1개 채널)
2. **Phase 2**: 2채널 동시 재생 테스트 (1개 Slave)
3. **Phase 3**: 전체 시스템 통합 (3개 Slave, 6채널 동시 재생)

### Slave 개발자가 확인해야 할 Master 측 파일

#### 필수 참조 파일
| 파일 | 내용 | 확인 사항 |
|------|------|----------|
| `Core/Inc/spi_protocol.h` | 패킷 구조체 정의 | 명령/데이터 패킷 포맷 |
| `Core/Src/spi_protocol.c` | SPI 전송 로직 | CS/RDY 핀 제어, DMA 전송 방식 |
| `Core/Inc/main.h` | GPIO 핀 매핑 | 실제 핀 번호 (NSS1/2/3, RDY1/2/3) |

#### 참고 파일
| 파일 | 내용 |
|------|------|
| `Core/Inc/audio_stream.h` | 오디오 시스템 인터페이스 |
| `Core/Src/audio_stream.c` | 실제 스트리밍 구현 |
| `Core/Inc/wav_parser.h` | WAV 파일 구조체 |

#### 문서
| 문서 | 내용 |
|------|------|
| `AUDIO_STREAMING_IMPLEMENTATION.md` | 전체 시스템 아키텍처 |
| `PC_UART_PROTOCOL.md` | PC 명령 프로토콜 |

---

## 📋 목차
1. [개요](#1-개요)
2. [하드웨어 인터페이스 명세](#2-하드웨어-인터페이스-명세)
3. [SPI 통신 설정](#3-spi-통신-설정)
4. [프로토콜 상세 명세](#4-프로토콜-상세-명세)
5. [핸드셰이크 시퀀스](#5-핸드셰이크-시퀀스)
6. [오디오 출력 시스템](#6-오디오-출력-시스템)
7. [메모리 및 버퍼 관리](#7-메모리-및-버퍼-관리)
8. [타이밍 요구사항](#8-타이밍-요구사항)
9. [구현 가이드](#9-구현-가이드)
10. [테스트 절차](#10-테스트-절차)
11. [에러 처리](#11-에러-처리)
12. [최적화 가이드](#12-최적화-가이드)

---

## 1. 개요

### 1.1 시스템 구성
```
Master STM32H723 (1개)
    ↓ SPI1 + DMA
Slave STM32H523 (3개)
    ↓ DAC1, DAC2
Audio Output (6 채널)
```

### 1.2 슬레이브 역할
각 STM32H523 슬레이브는:
- **2개의 독립적인 DAC 출력** (DAC1, DAC2)
- **SPI 슬레이브 모드**로 마스터와 통신
- **RDY 핀**으로 데이터 수신 가능 상태 알림
- **32kHz 샘플레이트**로 오디오 출력
- **이중 버퍼링**으로 끊김 없는 재생

### 1.3 슬레이브 ID
- **Slave 0**: 채널 0 (DAC1), 채널 1 (DAC2)
- **Slave 1**: 채널 2 (DAC1), 채널 3 (DAC2)
- **Slave 2**: 채널 4 (DAC1), 채널 5 (DAC2)

**중요**: 슬레이브 ID는 하드웨어 핀(DIP 스위치 또는 저항)으로 설정하거나 펌웨어에 하드코딩해야 합니다.

---

## 2. 하드웨어 인터페이스 명세

### 2.1 필수 핀 연결

#### SPI 슬레이브 인터페이스
| 신호 | 방향 | 설명 | Master 핀 (STM32H723) | 비고 |
|------|------|------|-----------|------|
| **SCK** | Input | SPI 클럭 | SPI1_SCK (PA5) | 10MHz (실제 동작 확인됨) |
| **MOSI** | Input | 마스터→슬레이브 데이터 | SPI1_MOSI (PB5) | |
| **MISO** | Output | 슬레이브→마스터 데이터 | SPI1_MISO (PB4) | 현재 미사용 (향후 상태 응답용) |
| **CS/NSS** | Input | 칩 선택 (Active Low) | PA4/PF11/PF12 | Slave별 다름 (아래 표 참조) |
| **RDY** | Output | 수신 준비 신호 | PF13/PF14/PF15 | GPIO, Active High |

#### Slave별 핀 매핑 (Master 측)
| Slave ID | CS 핀 (Master) | RDY 핀 (Master) | 채널 |
|----------|---------------|----------------|------|
| **Slave 0** | PA4 (OT_SPI1_NSS1) | PF13 (IN_SPI1_RDY1) | CH0 (DAC1), CH1 (DAC2) |
| **Slave 1** | PF11 (OT_SPI1_NSS2) | PF14 (IN_SPI1_RDY2) | CH2 (DAC1), CH3 (DAC2) |
| **Slave 2** | PF12 (OT_SPI1_NSS3) | PF15 (IN_SPI1_RDY3) | CH4 (DAC1), CH5 (DAC2) |

#### DAC 출력
| 신호 | 핀 | 설명 |
|------|-----|------|
| **DAC1_OUT** | PA4 (일반적) | 오디오 출력 1 |
| **DAC2_OUT** | PA5 (일반적) | 오디오 출력 2 |

**출력 사양**:
- 전압 범위: 0 ~ 3.3V (12-bit 해상도)
- 임피던스: Low-impedance 버퍼 사용 권장
- 필터: 외부 RC 로우패스 필터 필요 (16kHz 차단주파수)

### 2.2 전원 및 클럭
- **전원**: 3.3V 단일 전원
- **시스템 클럭**: 최소 64MHz 권장 (SPI + DAC 타이머)
- **HSE**: 외부 크리스탈 사용 권장 (타이밍 정확도)

### 2.3 선택 사항
- **LED**: 동작 상태 표시용 (재생 중, 에러 등)
- **디버그 UART**: 개발/디버깅용

---

## 3. SPI 통신 설정

### 3.1 SPI 파라미터

**CubeMX 설정값**:
```
Mode: Slave
Frame Format: Motorola
Data Size: 8 Bits
First Bit: MSB First
Clock Polarity (CPOL): Low (0)
Clock Phase (CPHA): 1 Edge (0)
NSS Signal Type: Hardware Input
```

**코드 설정 예시**:
```c
hspi.Instance = SPI1;  // 또는 사용 가능한 SPI 인스턴스
hspi.Init.Mode = SPI_MODE_SLAVE;
hspi.Init.Direction = SPI_DIRECTION_2LINES;
hspi.Init.DataSize = SPI_DATASIZE_8BIT;
hspi.Init.CLKPolarity = SPI_POLARITY_LOW;
hspi.Init.CLKPhase = SPI_PHASE_1EDGE;
hspi.Init.NSS = SPI_NSS_HARD_INPUT;
hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;  // Slave에서는 무시됨
hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
hspi.Init.TIMode = SPI_TIMODE_DISABLE;
hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
```

### 3.2 DMA 설정 (필수)

**RX DMA**:
- **우선순위**: High
- **모드**: Normal (패킷마다 재시작)
- **데이터 정렬**: Byte
- **인터럽트**: Transfer Complete, Transfer Error

**설정 예시**:
```c
// DMA 핸들 설정
hdma_spi_rx.Instance = DMA1_Stream0;  // 사용 가능한 스트림
hdma_spi_rx.Init.Request = DMA_REQUEST_SPI1_RX;
hdma_spi_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
hdma_spi_rx.Init.PeriphInc = DMA_PINC_DISABLE;
hdma_spi_rx.Init.MemInc = DMA_MINC_ENABLE;
hdma_spi_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
hdma_spi_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
hdma_spi_rx.Init.Mode = DMA_NORMAL;
hdma_spi_rx.Init.Priority = DMA_PRIORITY_HIGH;

// SPI에 DMA 링크
__HAL_LINKDMA(&hspi, hdmarx, hdma_spi_rx);
```

### 3.3 인터럽트 우선순위
```c
// SPI 인터럽트
HAL_NVIC_SetPriority(SPI1_IRQn, 1, 0);  // 높은 우선순위
HAL_NVIC_EnableIRQ(SPI1_IRQn);

// DMA 인터럽트
HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 1, 0);
HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);

// DAC 타이머 인터럽트
HAL_NVIC_SetPriority(TIM6_IRQn, 2, 0);  // SPI보다 낮음
HAL_NVIC_EnableIRQ(TIM6_IRQn);
```

---

## 4. 프로토콜 상세 명세

### 4.1 패킷 포맷

#### 명령 패킷 (Command Packet)
**크기**: 6 bytes (고정)

| Offset | 필드 | 크기 | 설명 | 값 |
|--------|------|------|------|-----|
| 0 | Header | 1 byte | 패킷 식별자 | **0xC0** (고정) |
| 1 | Slave ID | 1 byte | 대상 슬레이브 | 0~2 |
| 2 | Channel | 1 byte | DAC 채널 | 0=DAC1, 1=DAC2 |
| 3 | Command | 1 byte | 명령 코드 | 아래 표 참조 |
| 4 | Param High | 1 byte | 파라미터 상위 바이트 | 명령별 상이 |
| 5 | Param Low | 1 byte | 파라미터 하위 바이트 | 명령별 상이 |

**C 구조체**:
```c
typedef struct __attribute__((packed)) {
    uint8_t header;         // 0xC0
    uint8_t slave_id;       // 0~2
    uint8_t channel;        // 0=DAC1, 1=DAC2
    uint8_t command;
    uint8_t param_h;
    uint8_t param_l;
} CommandPacket_t;
```

#### 데이터 패킷 (Data Packet)
**크기**: 5 bytes (헤더) + N*2 bytes (샘플)

| Offset | 필드 | 크기 | 설명 | 값 |
|--------|------|------|------|-----|
| 0 | Header | 1 byte | 패킷 식별자 | **0xDA** (고정) |
| 1 | Slave ID | 1 byte | 대상 슬레이브 | 0~2 |
| 2 | Channel | 1 byte | DAC 채널 | 0=DAC1, 1=DAC2 |
| 3 | Length High | 1 byte | 샘플 개수 상위 바이트 | Big-endian |
| 4 | Length Low | 1 byte | 샘플 개수 하위 바이트 | Big-endian |
| 5~ | Samples | N*2 bytes | 오디오 샘플 | 16-bit Little-endian |

**최대 샘플 수**: 2048 (4096 bytes)
**총 최대 크기**: 5 + 4096 = 4101 bytes

**C 구조체 (헤더)**:
```c
typedef struct __attribute__((packed)) {
    uint8_t header;         // 0xDA
    uint8_t slave_id;       // 0~2
    uint8_t channel;        // 0=DAC1, 1=DAC2
    uint8_t length_h;       // 샘플 수 상위 바이트
    uint8_t length_l;       // 샘플 수 하위 바이트
    // 이후 오디오 데이터 (num_samples * 2 바이트)
} SPI_DataPacketHeader_t;
```

**샘플 개수 디코딩**:
```c
uint16_t num_samples = (header->length_h << 8) | header->length_l;
```

### 4.2 명령 코드

| 명령 | 코드 | 파라미터 | 설명 |
|------|------|----------|------|
| **PLAY** | 0x01 | 0x0000 | 재생 시작 |
| **STOP** | 0x02 | 0x0000 | 재생 정지 |
| **VOLUME** | 0x03 | 0~100 | 볼륨 설정 (0=음소거, 100=최대) |
| **RESET** | 0xFF | 0x0000 | 채널 리셋 |

**파라미터 인코딩**:
```c
uint16_t param = (packet.param_h << 8) | packet.param_l;
```

### 4.3 패킷 수신 로직

```c
// 1단계: 헤더 1바이트 수신
uint8_t header;
HAL_SPI_Receive_DMA(&hspi, &header, 1);

// DMA 완료 콜백에서:
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (header == 0xC0) {
        // 명령 패킷: 나머지 5바이트 수신
        HAL_SPI_Receive_DMA(&hspi, cmd_buffer + 1, 5);
        rx_state = RX_STATE_CMD;
    }
    else if (header == 0xDA) {
        // 데이터 패킷: 나머지 5바이트(헤더 정보) 수신
        HAL_SPI_Receive_DMA(&hspi, data_header_buffer + 1, 5);
        rx_state = RX_STATE_DATA_HEADER;
    }
    else {
        // 에러: 헤더 재수신
        HAL_SPI_Receive_DMA(&hspi, &header, 1);
        rx_state = RX_STATE_IDLE;
    }
}
```

---

## 5. 핸드셰이크 시퀀스

### 5.1 RDY 핀 프로토콜

**RDY 핀 동작 (Active Low)**:
- **LOW (0)**: 슬레이브가 데이터 수신 가능 ★ Active Low
- **HIGH (1)**: 슬레이브가 처리 중 (수신 불가)

**타이밍 (Active Low)**:
```
Initial: RDY = LOW ★ (준비됨)
    ↓
Master checks RDY (LOW 확인)
    ↓
Master asserts CS (LOW)
    ↓
[50us delay] ← 중요!
    ↓
Slave sets RDY = HIGH (처리 중)
    ↓
Master sends packet via SPI
    ↓
Master deasserts CS (HIGH)
    ↓
Slave processes packet
    ↓
Slave sets RDY = LOW ★ (준비됨)
```

### 5.2 RDY 핀 제어 코드

```c
// GPIO 설정
GPIO_InitTypeDef GPIO_InitStruct = {0};
GPIO_InitStruct.Pin = RDY_Pin;  // 예: GPIO_PIN_0
GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
GPIO_InitStruct.Pull = GPIO_NOPULL;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
HAL_GPIO_Init(RDY_GPIO_Port, &GPIO_InitStruct);

// 초기화
HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);  // Ready

// CS 감지 인터럽트 (EXTI)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == CS_Pin) {
        if (HAL_GPIO_ReadPin(CS_GPIO_Port, CS_Pin) == GPIO_PIN_RESET) {
            // CS asserted (LOW) → 수신 시작
            HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_RESET);  // Busy
            start_packet_reception();
        }
    }
}

// 패킷 처리 완료 후
void packet_processing_done(void)
{
    HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);  // Ready
}
```

### 5.3 CS 핀 인터럽트 설정

```c
// CS 핀을 EXTI로 설정
GPIO_InitStruct.Pin = CS_Pin;  // NSS 핀
GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;  // Falling edge (HIGH→LOW)
GPIO_InitStruct.Pull = GPIO_PULLUP;
HAL_GPIO_Init(CS_GPIO_Port, &GPIO_InitStruct);

// EXTI 인터럽트 활성화
HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);  // 최고 우선순위
HAL_NVIC_EnableIRQ(EXTI0_IRQn);
```

**중요**: CS 신호를 SPI NSS 핀과 별도의 GPIO EXTI로 동시에 사용해야 합니다.

---

## 6. 오디오 출력 시스템

### 6.1 DAC 설정

#### DAC 파라미터
```c
// DAC1 설정
hdac.Instance = DAC1;
HAL_DAC_Init(&hdac);

DAC_ChannelConfTypeDef sConfig = {0};
sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;  // TIM6 TRGO
sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;  // 버퍼 활성화
HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_1);
HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_2);
```

#### 12비트 → DAC 값 변환
```c
// 수신한 16비트 샘플 → 12비트 DAC 값
uint16_t sample_16bit;  // 0x0000 ~ 0xFFFF
uint16_t dac_value = sample_16bit >> 4;  // 상위 12비트 추출
```

### 6.2 타이머 설정 (32kHz 샘플레이트)

#### TIM6 설정 (DAC 트리거용)
```c
// 가정: APB1 클럭 = 64MHz
// 목표: 32kHz 출력

htim6.Instance = TIM6;
htim6.Init.Prescaler = 0;  // 분주 없음
htim6.Init.Period = (64000000 / 32000) - 1;  // = 1999
htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
HAL_TIM_Base_Init(&htim6);

// TRGO 출력 설정 (DAC 트리거용)
TIM_MasterConfigTypeDef sMasterConfig = {0};
sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig);
```

**공식**:
```
Timer Period = (APB Clock / Sample Rate) - 1
Sample Rate = APB Clock / (Timer Period + 1)

예시:
64,000,000 Hz / 2000 = 32,000 Hz ✓
```

**중요**: 실제 클럭 주파수는 CubeMX에서 확인하세요!

### 6.3 DMA로 DAC 출력

```c
// DMA 설정 (DAC1_CH1용)
hdma_dac1_ch1.Instance = DMA1_Stream5;  // 사용 가능한 스트림
hdma_dac1_ch1.Init.Request = DMA_REQUEST_DAC1_CH1;
hdma_dac1_ch1.Init.Direction = DMA_MEMORY_TO_PERIPH;
hdma_dac1_ch1.Init.PeriphInc = DMA_PINC_DISABLE;
hdma_dac1_ch1.Init.MemInc = DMA_MINC_ENABLE;
hdma_dac1_ch1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;  // 16-bit
hdma_dac1_ch1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;     // 16-bit
hdma_dac1_ch1.Init.Mode = DMA_CIRCULAR;  // 순환 모드
hdma_dac1_ch1.Init.Priority = DMA_PRIORITY_HIGH;

// DAC에 DMA 링크
__HAL_LINKDMA(&hdac, DMA_Handle1, hdma_dac1_ch1);

// DMA Half/Complete 콜백 활성화
HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 2, 0);
HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
```

---

## 7. 메모리 및 버퍼 관리

### 7.1 이중 버퍼 구조

**각 DAC 채널마다**:
- **버퍼 A** (2048 samples = 4096 bytes)
- **버퍼 B** (2048 samples = 4096 bytes)

**동작 원리**:
```
[버퍼 A] ← DMA가 DAC로 출력 중
[버퍼 B] ← SPI로 새 데이터 수신

DMA Half Transfer 콜백:
    상위 절반(버퍼 A)을 업데이트

DMA Complete 콜백:
    하위 절반(버퍼 B)을 업데이트
    또는 버퍼 스왑
```

### 7.2 메모리 배치

```c
// 32바이트 정렬 (캐시 라인)
#define AUDIO_BUFFER_SIZE   2048

// DAC1 이중 버퍼
__attribute__((aligned(32)))
uint16_t dac1_buffer_a[AUDIO_BUFFER_SIZE];

__attribute__((aligned(32)))
uint16_t dac1_buffer_b[AUDIO_BUFFER_SIZE];

// DAC2 이중 버퍼
__attribute__((aligned(32)))
uint16_t dac2_buffer_a[AUDIO_BUFFER_SIZE];

__attribute__((aligned(32)))
uint16_t dac2_buffer_b[AUDIO_BUFFER_SIZE];

// SPI 수신 버퍼 (임시)
__attribute__((aligned(32)))
uint8_t spi_rx_buffer[4101];  // 최대 패킷 크기 (5 byte 헤더 + 4096 byte 샘플)
```

**총 메모리**: 약 24KB (STM32H523 SRAM 충분)

**참고 - Master 측 구현**:
- Master는 명령 패킷을 **블로킹 모드**로 전송 (`HAL_SPI_Transmit`)
- Master는 데이터 패킷을 **DMA 모드**로 전송 (`HAL_SPI_Transmit_DMA`)
- 전송 버퍼는 DTCMRAM에 배치되어 zero wait state 보장
- 실제 구현 확인됨: `Core/Src/spi_protocol.c`

### 7.3 버퍼 관리 로직

```c
typedef struct {
    uint16_t *buffer_a;
    uint16_t *buffer_b;
    uint16_t *active_buffer;   // 현재 출력 중인 버퍼
    uint16_t *fill_buffer;     // 채울 버퍼
    uint16_t fill_index;       // 채우기 위치
    uint8_t  is_playing;
    uint8_t  underrun;         // 버퍼 언더런 플래그
} AudioChannel_t;

AudioChannel_t dac1_channel;
AudioChannel_t dac2_channel;

// 초기화
void audio_channel_init(AudioChannel_t *ch, uint16_t *buf_a, uint16_t *buf_b)
{
    ch->buffer_a = buf_a;
    ch->buffer_b = buf_b;
    ch->active_buffer = buf_a;
    ch->fill_buffer = buf_b;
    ch->fill_index = 0;
    ch->is_playing = 0;
    ch->underrun = 0;

    memset(buf_a, 0, AUDIO_BUFFER_SIZE * 2);
    memset(buf_b, 0, AUDIO_BUFFER_SIZE * 2);
}

// 샘플 채우기
void audio_channel_fill(AudioChannel_t *ch, uint16_t *samples, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++) {
        ch->fill_buffer[ch->fill_index++] = samples[i] >> 4;  // 12-bit 변환

        if (ch->fill_index >= AUDIO_BUFFER_SIZE) {
            // 버퍼가 가득 참 → 스왑 준비
            ch->fill_index = 0;
            break;
        }
    }
}
```

### 7.4 DMA 콜백 구현

```c
void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    // 버퍼 A의 전반부(0~1023)가 출력 완료
    // 이 시점에 버퍼 A의 전반부를 새 데이터로 업데이트 가능
    dac1_buffer_half_complete = 1;
}

void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    // 버퍼 A의 후반부(1024~2047)가 출력 완료
    // 버퍼 스왑 또는 버퍼 B 업데이트

    if (dac1_channel.fill_index >= AUDIO_BUFFER_SIZE) {
        // 버퍼 B가 가득 참 → 스왑
        uint16_t *temp = dac1_channel.active_buffer;
        dac1_channel.active_buffer = dac1_channel.fill_buffer;
        dac1_channel.fill_buffer = temp;
        dac1_channel.fill_index = 0;

        // RDY 핀을 HIGH로 (다음 데이터 수신 가능)
        HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);
    } else {
        // 언더런 발생
        dac1_channel.underrun = 1;
    }
}
```

---

## 8. 타이밍 요구사항

### 8.1 SPI 통신 타이밍

| 항목 | 최소값 | 최대값 | 단위 | 비고 |
|------|--------|--------|------|------|
| **SPI 클럭** | - | 25 | MHz | STM32H523 한계 |
| **CS 셋업 시간** | 50 | - | μs | CS LOW → 첫 클럭 |
| **패킷 간 간격** | 100 | - | μs | CS HIGH 유지 시간 |
| **RDY 응답 시간** | - | 50 | μs | 패킷 처리 완료 시간 |

### 8.2 오디오 타이밍

| 항목 | 값 | 단위 | 비고 |
|------|-----|------|------|
| **샘플레이트** | 32000 | Hz | 정확해야 함 |
| **버퍼 크기** | 2048 | samples | 64ms |
| **버퍼 채우기 주기** | 64 | ms | 최대 간격 |
| **타이머 지터** | < 1 | % | 크리스탈 사용 권장 |

### 8.3 처리 시간 제약

```c
// 2048 샘플 @ 32kHz = 64ms
// 따라서 64ms 이내에 다음 버퍼를 채워야 함

// SPI 전송 시간 계산:
// 4102 bytes @ 10MHz SPI = 약 3.3ms
// 충분한 여유 있음!

// 슬레이브 처리 시간:
// - 패킷 수신: ~3.3ms (DMA)
// - 데이터 복사: ~0.5ms (memcpy)
// - 버퍼 스왑: ~10μs
// 총: ~4ms << 64ms ✓
```

---

## 9. 구현 가이드

### 9.1 개발 단계

#### Phase 1: 하드웨어 검증
```c
// 1. LED 블링크 테스트
void test_led_blink(void) {
    while(1) {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        HAL_Delay(500);
    }
}

// 2. DAC 정현파 출력 테스트
void test_dac_sine_wave(void) {
    uint16_t sine_table[32];
    for (int i = 0; i < 32; i++) {
        sine_table[i] = (uint16_t)(2048 + 2047 * sin(2 * M_PI * i / 32));
    }

    HAL_TIM_Base_Start(&htim6);
    HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, (uint32_t*)sine_table, 32, DAC_ALIGN_12B_R);

    while(1);  // 1kHz sine wave 출력 확인
}

// 3. RDY 핀 토글 테스트
void test_rdy_pin(void) {
    while(1) {
        HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);
        HAL_Delay(100);
        HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_RESET);
        HAL_Delay(100);
    }
}
```

#### Phase 2: SPI 통신 구현
```c
// 4. SPI 에코 테스트 (MISO=MOSI)
void test_spi_echo(void) {
    uint8_t rx_buffer[16];

    // RDY HIGH
    HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);

    while(1) {
        // CS가 LOW가 되면 수신 시작
        if (HAL_GPIO_ReadPin(CS_GPIO_Port, CS_Pin) == GPIO_PIN_RESET) {
            HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_RESET);

            HAL_SPI_Receive(&hspi, rx_buffer, 6, 1000);

            // 수신된 데이터 확인 (UART로 출력)
            printf("RX: %02X %02X %02X %02X %02X %02X\r\n",
                   rx_buffer[0], rx_buffer[1], rx_buffer[2],
                   rx_buffer[3], rx_buffer[4], rx_buffer[5]);

            HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);
        }
    }
}

// 5. 명령 패킷 파싱 테스트
void test_command_parsing(void) {
    CommandPacket_t cmd;

    while(1) {
        if (receive_command_packet(&cmd) == HAL_OK) {
            // Slave ID 확인
            if (cmd.slave_id == MY_SLAVE_ID) {
                uint16_t param = (cmd.param_h << 8) | cmd.param_l;

                switch(cmd.command) {
                    case CMD_PLAY:
                        printf("PLAY on CH%d\r\n", cmd.channel);
                        break;
                    case CMD_STOP:
                        printf("STOP on CH%d\r\n", cmd.channel);
                        break;
                    case CMD_VOLUME:
                        printf("VOLUME=%d on CH%d\r\n", param, cmd.channel);
                        break;
                }
            }
        }
    }
}
```

#### Phase 3: 데이터 패킷 및 오디오 출력
```c
// 6. 데이터 패킷 수신 및 버퍼 채우기
void test_data_reception(void) {
    DataPacket_t *data_pkt = (DataPacket_t*)spi_rx_buffer;

    audio_channel_init(&dac1_channel, dac1_buffer_a, dac1_buffer_b);

    while(1) {
        if (receive_data_packet(spi_rx_buffer, sizeof(spi_rx_buffer)) == HAL_OK) {
            if (data_pkt->slave_id == MY_SLAVE_ID && data_pkt->channel == 0) {
                audio_channel_fill(&dac1_channel, data_pkt->samples, data_pkt->num_samples);
                printf("Filled %d samples\r\n", data_pkt->num_samples);
            }
        }
    }
}

// 7. 전체 시스템 통합
void test_full_system(void) {
    // 초기화
    init_all_peripherals();

    // 메인 루프
    while(1) {
        handle_spi_packets();
        handle_audio_output();
        check_underrun();
    }
}
```

### 9.2 메인 루프 구조

```c
typedef enum {
    STATE_IDLE,
    STATE_WAIT_HEADER,
    STATE_RECEIVE_CMD,
    STATE_RECEIVE_DATA_HEADER,
    STATE_RECEIVE_DATA_SAMPLES,
    STATE_PROCESS_PACKET
} RxState_t;

RxState_t rx_state = STATE_IDLE;
uint8_t rx_header;
CommandPacket_t rx_cmd_packet;
DataPacket_t *rx_data_packet = (DataPacket_t*)spi_rx_buffer;

int main(void)
{
    // HAL 초기화
    HAL_Init();
    SystemClock_Config();

    // 주변장치 초기화
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI_Init();
    MX_DAC_Init();
    MX_TIM6_Init();
    MX_USART_Init();  // 디버그용

    // Slave ID 읽기 (GPIO 또는 하드코딩)
    MY_SLAVE_ID = read_slave_id();  // 예: DIP 스위치

    // 오디오 채널 초기화
    audio_channel_init(&dac1_channel, dac1_buffer_a, dac1_buffer_b);
    audio_channel_init(&dac2_channel, dac2_buffer_a, dac2_buffer_b);

    // RDY 핀 초기화 (HIGH)
    HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);

    // 첫 SPI 수신 시작 (헤더 1바이트)
    HAL_SPI_Receive_IT(&hspi, &rx_header, 1);
    rx_state = STATE_WAIT_HEADER;

    printf("Slave %d Ready\r\n", MY_SLAVE_ID);

    // 메인 루프
    while (1)
    {
        // 상태 머신은 인터럽트에서 처리

        // 주기적 작업
        check_audio_status();
        update_leds();

        // 저전력 모드 (옵션)
        // HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }
}
```

### 9.3 SPI 인터럽트 핸들러

```c
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    static uint16_t data_samples_to_receive = 0;

    switch(rx_state) {
        case STATE_WAIT_HEADER:
            if (rx_header == 0xC0) {
                // 명령 패킷
                HAL_SPI_Receive_IT(&hspi, ((uint8_t*)&rx_cmd_packet) + 1, 5);
                rx_cmd_packet.header = rx_header;
                rx_state = STATE_RECEIVE_CMD;
            }
            else if (rx_header == 0xDA) {
                // 데이터 패킷
                HAL_SPI_Receive_IT(&hspi, ((uint8_t*)rx_data_packet) + 1, 5);
                rx_data_packet->header = rx_header;
                rx_state = STATE_RECEIVE_DATA_HEADER;
            }
            else {
                // 잘못된 헤더 → 재시작
                HAL_SPI_Receive_IT(&hspi, &rx_header, 1);
                rx_state = STATE_WAIT_HEADER;
            }
            break;

        case STATE_RECEIVE_CMD:
            // 명령 패킷 수신 완료
            rx_state = STATE_PROCESS_PACKET;
            process_command_packet(&rx_cmd_packet);

            // 다음 헤더 대기
            HAL_SPI_Receive_IT(&hspi, &rx_header, 1);
            rx_state = STATE_WAIT_HEADER;
            break;

        case STATE_RECEIVE_DATA_HEADER:
            // 데이터 헤더 수신 완료 → 샘플 개수 확인
            data_samples_to_receive = rx_data_packet->num_samples;

            if (data_samples_to_receive > 0 && data_samples_to_receive <= 2048) {
                // 샘플 데이터 수신 시작
                HAL_SPI_Receive_IT(&hspi, (uint8_t*)rx_data_packet->samples,
                                   data_samples_to_receive * 2);
                rx_state = STATE_RECEIVE_DATA_SAMPLES;
            } else {
                // 에러
                HAL_SPI_Receive_IT(&hspi, &rx_header, 1);
                rx_state = STATE_WAIT_HEADER;
            }
            break;

        case STATE_RECEIVE_DATA_SAMPLES:
            // 데이터 샘플 수신 완료
            rx_state = STATE_PROCESS_PACKET;
            process_data_packet(rx_data_packet);

            // 다음 헤더 대기
            HAL_SPI_Receive_IT(&hspi, &rx_header, 1);
            rx_state = STATE_WAIT_HEADER;
            break;
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    // SPI 에러 처리
    spi_error_count++;

    // 재시작
    HAL_SPI_Receive_IT(&hspi, &rx_header, 1);
    rx_state = STATE_WAIT_HEADER;
}
```

### 9.4 패킷 처리 함수

```c
void process_command_packet(CommandPacket_t *cmd)
{
    // Slave ID 확인
    if (cmd->slave_id != MY_SLAVE_ID) {
        return;  // 다른 슬레이브로 전송된 명령
    }

    // 채널 선택
    AudioChannel_t *channel;
    DAC_HandleTypeDef *hdac_handle = &hdac;
    uint32_t dac_channel;

    if (cmd->channel == 0) {
        channel = &dac1_channel;
        dac_channel = DAC_CHANNEL_1;
    } else {
        channel = &dac2_channel;
        dac_channel = DAC_CHANNEL_2;
    }

    // 명령 처리
    uint16_t param = (cmd->param_h << 8) | cmd->param_l;

    switch(cmd->command) {
        case CMD_PLAY:
            if (!channel->is_playing) {
                channel->is_playing = 1;
                channel->underrun = 0;

                // DAC + TIM 시작
                HAL_TIM_Base_Start(&htim6);
                HAL_DAC_Start_DMA(hdac_handle, dac_channel,
                                  (uint32_t*)channel->active_buffer,
                                  AUDIO_BUFFER_SIZE,
                                  DAC_ALIGN_12B_R);

                printf("Play CH%d\r\n", cmd->channel);
            }
            break;

        case CMD_STOP:
            if (channel->is_playing) {
                channel->is_playing = 0;

                // DAC 정지
                HAL_DAC_Stop_DMA(hdac_handle, dac_channel);

                // 버퍼 클리어
                memset(channel->buffer_a, 0, AUDIO_BUFFER_SIZE * 2);
                memset(channel->buffer_b, 0, AUDIO_BUFFER_SIZE * 2);
                channel->fill_index = 0;

                printf("Stop CH%d\r\n", cmd->channel);
            }
            break;

        case CMD_VOLUME:
            channel->volume = param;  // 0~100
            printf("Volume=%d CH%d\r\n", param, cmd->channel);
            // TODO: 실제 볼륨 조절 구현 (DAC 출력 스케일링)
            break;

        case CMD_RESET:
            // 채널 리셋
            channel->is_playing = 0;
            channel->fill_index = 0;
            channel->underrun = 0;
            HAL_DAC_Stop_DMA(hdac_handle, dac_channel);
            printf("Reset CH%d\r\n", cmd->channel);
            break;

        default:
            printf("Unknown CMD 0x%02X\r\n", cmd->command);
            break;
    }
}

void process_data_packet(DataPacket_t *data)
{
    // Slave ID 확인
    if (data->slave_id != MY_SLAVE_ID) {
        return;
    }

    // 채널 선택
    AudioChannel_t *channel;
    if (data->channel == 0) {
        channel = &dac1_channel;
    } else {
        channel = &dac2_channel;
    }

    // 재생 중이 아니면 무시
    if (!channel->is_playing) {
        return;
    }

    // 샘플 채우기
    audio_channel_fill(channel, data->samples, data->num_samples);

    printf("RX %d samples CH%d\r\n", data->num_samples, data->channel);
}
```

---

## 10. 테스트 절차

### 10.1 개별 테스트

#### Test 1: GPIO 테스트
```
목표: LED, RDY 핀 동작 확인
방법:
1. LED 블링크 (1Hz)
2. RDY 핀 토글 (1Hz) - 오실로스코프로 확인
예상: LED 깜빡임, RDY 핀 파형 확인
```

#### Test 2: DAC 테스트
```
목표: DAC 출력 확인
방법:
1. 정현파 테이블 생성 (32 샘플)
2. TIM6 + DAC DMA로 출력
3. 오실로스코프로 DAC_OUT 확인
예상: 1kHz 정현파 (32kHz / 32 = 1kHz)
```

#### Test 3: SPI 루프백 테스트
```
목표: SPI 수신 확인
방법:
1. MOSI → MISO 단락 (물리적)
2. 마스터에서 테스트 패킷 전송
3. 슬레이브에서 수신 데이터 UART 출력
예상: 송신 데이터 = 수신 데이터
```

#### Test 4: 명령 패킷 테스트
```
목표: 명령 처리 확인
방법:
1. 마스터에서 PLAY 명령 전송
2. 슬레이브 UART로 "Play CH0" 출력 확인
3. STOP, VOLUME 명령도 동일하게 테스트
예상: 모든 명령이 정상 처리됨
```

#### Test 5: 데이터 패킷 테스트
```
목표: 오디오 데이터 수신 및 출력
방법:
1. 마스터에서 1kHz 정현파 데이터 전송 (2048 샘플)
2. 슬레이브 버퍼에 채우기
3. DAC 출력 확인 (오실로스코프)
예상: 1kHz 정현파 출력
```

### 10.2 통합 테스트

#### Test 6: 단일 채널 연속 재생
```
목표: 한 채널에서 긴 파일 재생
방법:
1. 마스터에서 10초 WAV 파일 재생
2. 슬레이브 DAC1 출력 확인
3. 버퍼 언더런 발생 여부 확인
예상: 끊김 없는 재생, 언더런 없음
```

#### Test 7: 2채널 동시 재생
```
목표: DAC1, DAC2 동시 재생
방법:
1. 마스터에서 2개 파일 동시 재생
   - CH0: 1kHz 정현파
   - CH1: 500Hz 정현파
2. 오실로스코프로 DAC1_OUT, DAC2_OUT 확인
예상: 두 출력 모두 정상, 간섭 없음
```

#### Test 8: 장시간 안정성 테스트
```
목표: 장시간 재생 안정성
방법:
1. 30분간 연속 재생
2. 에러 카운터 확인
   - SPI 에러
   - 버퍼 언더런
   - DMA 에러
예상: 에러 0, 안정적 동작
```

### 10.3 성능 테스트

#### Test 9: CPU 사용률 측정
```c
// 메인 루프에 추가
uint32_t idle_count = 0;

while(1) {
    idle_count++;

    if ((HAL_GetTick() % 1000) == 0) {
        printf("CPU Idle: %lu\r\n", idle_count);
        idle_count = 0;
    }
}

// 목표: CPU 사용률 < 30%
```

#### Test 10: 타이밍 분석
```c
// DMA 콜백에 추가
uint32_t callback_time = HAL_GetTick();
uint32_t interval = callback_time - last_callback_time;
last_callback_time = callback_time;

printf("Interval: %lu ms (expected: 64ms)\r\n", interval);

// 목표: 64ms ± 1ms
```

---

## 11. 에러 처리

### 11.1 에러 종류 및 대응

| 에러 | 원인 | 대응 방법 |
|------|------|----------|
| **SPI 에러** | 클럭 이상, 노이즈 | SPI 재초기화, 패킷 재요청 |
| **버퍼 언더런** | 데이터 미도착 | 무음 출력, RDY 핀 유지 |
| **잘못된 헤더** | 동기 실패 | 헤더 재동기화 |
| **DMA 에러** | 메모리 문제 | DMA 재시작 |
| **타이머 지터** | 클럭 불안정 | HSE 사용, PLL 확인 |

### 11.2 에러 처리 코드

```c
typedef struct {
    uint32_t spi_error_count;
    uint32_t underrun_count;
    uint32_t invalid_header_count;
    uint32_t dma_error_count;
} ErrorCounters_t;

ErrorCounters_t error_counters = {0};

// SPI 에러
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    error_counters.spi_error_count++;

    // LED로 에러 표시
    HAL_GPIO_WritePin(ERROR_LED_GPIO_Port, ERROR_LED_Pin, GPIO_PIN_SET);

    // 재초기화
    HAL_SPI_DeInit(hspi);
    HAL_SPI_Init(hspi);

    // 수신 재시작
    HAL_SPI_Receive_IT(hspi, &rx_header, 1);
    rx_state = STATE_WAIT_HEADER;
}

// 버퍼 언더런
void handle_underrun(AudioChannel_t *channel)
{
    if (channel->underrun) {
        error_counters.underrun_count++;

        // 무음 출력 (현재 버퍼를 0으로 채움)
        memset(channel->active_buffer, 0, AUDIO_BUFFER_SIZE * 2);

        printf("Underrun on CH%d\r\n", channel == &dac1_channel ? 0 : 1);

        channel->underrun = 0;
    }
}

// 주기적 에러 체크
void check_errors(void)
{
    static uint32_t last_check = 0;

    if ((HAL_GetTick() - last_check) > 5000) {  // 5초마다
        last_check = HAL_GetTick();

        if (error_counters.spi_error_count > 0 ||
            error_counters.underrun_count > 0 ||
            error_counters.dma_error_count > 0) {

            printf("Errors: SPI=%lu, Underrun=%lu, DMA=%lu\r\n",
                   error_counters.spi_error_count,
                   error_counters.underrun_count,
                   error_counters.dma_error_count);
        }
    }
}
```

### 11.3 와치독 타이머

```c
// IWDG 설정 (옵션)
void init_watchdog(void)
{
    IWDG_HandleTypeDef hiwdg;
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Window = 4095;
    hiwdg.Init.Reload = 4095;  // 약 32초 타임아웃
    HAL_IWDG_Init(&hiwdg);
}

// 메인 루프에서 주기적으로 호출
void feed_watchdog(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}
```

---

## 12. 최적화 가이드

### 12.1 성능 최적화

#### DMA 최적화
```c
// Burst 모드 활성화
hdma.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
hdma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
hdma.Init.MemBurst = DMA_MBURST_INC4;
hdma.Init.PeriphBurst = DMA_PBURST_INC4;
```

#### 캐시 관리 (STM32H523는 캐시 없을 수 있음)
```c
// 만약 D-Cache가 있다면:
// DMA 버퍼 전후에 캐시 무효화
SCB_InvalidateDCache_by_Addr((uint32_t*)spi_rx_buffer, sizeof(spi_rx_buffer));
SCB_CleanDCache_by_Addr((uint32_t*)dac1_buffer_a, AUDIO_BUFFER_SIZE * 2);
```

#### 인터럽트 최소화
```c
// SPI는 DMA 사용, 폴링 금지
// DAC도 DMA 사용
// GPIO는 EXTI 사용 (폴링 금지)
```

### 12.2 메모리 최적화

```c
// 전역 변수를 최소화하고 static 사용
static AudioChannel_t dac1_channel;
static AudioChannel_t dac2_channel;

// 큰 버퍼는 정렬 및 섹션 배치
__attribute__((section(".dma_buffers")))
__attribute__((aligned(32)))
static uint16_t dac1_buffer_a[AUDIO_BUFFER_SIZE];
```

### 12.3 전력 최적화

```c
// 대기 중 Sleep 모드 진입
void enter_sleep_if_idle(void)
{
    if (!dac1_channel.is_playing && !dac2_channel.is_playing) {
        // 인터럽트로 깨어남
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }
}
```

---

## 13. 디버깅 팁

### 13.1 UART 디버그 출력

```c
// printf 리다이렉트 (syscalls.c에 추가)
int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}

// 디버그 매크로
#define DEBUG_PRINT(fmt, ...) printf("[%lu] " fmt, HAL_GetTick(), ##__VA_ARGS__)

// 사용 예:
DEBUG_PRINT("Received packet: header=0x%02X\r\n", rx_header);
```

### 13.2 로직 분석기 신호

```c
// 디버그용 GPIO 토글 (타이밍 측정)
#define DEBUG_PIN_SET()   HAL_GPIO_WritePin(DEBUG_GPIO_Port, DEBUG_Pin, GPIO_PIN_SET)
#define DEBUG_PIN_RESET() HAL_GPIO_WritePin(DEBUG_GPIO_Port, DEBUG_Pin, GPIO_PIN_RESET)

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    DEBUG_PIN_SET();
    // ... 처리 ...
    DEBUG_PIN_RESET();
}
```

### 13.3 Assertion 사용

```c
#define ASSERT(x) do { if(!(x)) { printf("ASSERT FAILED: %s:%d\r\n", __FILE__, __LINE__); while(1); } } while(0)

// 사용 예:
ASSERT(data->num_samples <= 2048);
ASSERT(cmd->slave_id < 3);
```

---

## 14. 체크리스트

### 14.1 개발 전 확인사항
- [ ] STM32H523 보드 준비 완료
- [ ] SPI, DAC, GPIO 핀 맵 확인
- [ ] 마스터 보드와 연결 가능한지 확인
- [ ] 오실로스코프/로직 분석기 준비
- [ ] CubeMX 프로젝트 생성

### 14.2 구현 체크리스트
- [ ] GPIO 초기화 (RDY, LED, CS)
- [ ] SPI 슬레이브 모드 설정
- [ ] DMA 설정 (SPI RX, DAC TX)
- [ ] DAC 초기화 (2채널)
- [ ] TIM6 초기화 (32kHz TRGO)
- [ ] 이중 버퍼 할당 및 초기화
- [ ] SPI 인터럽트 핸들러 구현
- [ ] 명령 패킷 파싱 함수
- [ ] 데이터 패킷 처리 함수
- [ ] DAC DMA 콜백 구현
- [ ] 에러 처리 로직
- [ ] UART 디버그 출력

### 14.3 테스트 체크리스트
- [ ] LED 블링크 테스트
- [ ] RDY 핀 토글 테스트
- [ ] DAC 정현파 출력 테스트
- [ ] SPI 루프백 테스트
- [ ] 명령 패킷 수신 테스트
- [ ] 데이터 패킷 수신 테스트
- [ ] 단일 채널 재생 테스트
- [ ] 2채널 동시 재생 테스트
- [ ] 장시간 안정성 테스트
- [ ] 에러 복구 테스트

---

## 15. 참고 자료

### 15.1 STM32 문서
- **STM32H523 Reference Manual** (RM0481)
- **STM32H523 Datasheet**
- **AN4031**: Using the STM32 DMA controller
- **AN4073**: How to improve ADC/DAC accuracy

### 15.2 HAL 드라이버 예제
- `STM32Cube_FW_H5`: SPI 슬레이브 예제
- `STM32Cube_FW_H5`: DAC DMA 예제
- `STM32Cube_FW_H5`: TIM 트리거 예제

### 15.3 마스터 펌웨어 참조 (필수!)

**Master 측 코드를 반드시 참조하세요** - 실제 구현된 프로토콜을 확인할 수 있습니다:

#### SPI 프로토콜 구현
- `Core/Inc/spi_protocol.h`: 패킷 구조체 정의 (명령/데이터 패킷 포맷)
- `Core/Src/spi_protocol.c`: Master 측 SPI 전송 로직
  - `spi_send_command()`: 명령 패킷 전송 (블로킹 모드)
  - `spi_send_data_dma()`: 데이터 패킷 전송 (DMA 모드)
  - `spi_select_slave()`, `spi_deselect_slave()`: CS 핀 제어
  - `spi_check_ready()`: RDY 핀 상태 확인

#### 오디오 스트리밍 시스템
- `Core/Inc/audio_stream.h`: 6채널 오디오 시스템 인터페이스
- `Core/Src/audio_stream.c`: 오디오 채널 관리 및 스트리밍 로직
  - `audio_load_file()`: WAV 파일 로드
  - `audio_play()`, `audio_stop()`: 재생 제어
  - `audio_stream_task()`: 주기적 데이터 전송

#### WAV 파일 파서
- `Core/Inc/wav_parser.h`: WAV 파일 구조체 정의
- `Core/Src/wav_parser.c`: WAV 파일 읽기 및 샘플 추출

#### 전체 시스템 문서
- `AUDIO_STREAMING_IMPLEMENTATION.md`: 전체 시스템 아키텍처
- `PC_UART_PROTOCOL.md`: PC 통신 프로토콜 (명령 참조)
- `YMODEM_UPLOAD_GUIDE.md`: Y-MODEM 파일 업로드 가이드
- `README.md`: 프로젝트 개요 및 빌드 방법

---

## 16. Master 측 동작 시퀀스 (참고)

### Master의 실제 전송 플로우

```c
// 1. 재생 시작 명령
spi_send_command(slave_id, channel, SPI_CMD_PLAY, 0);

// 2. 데이터 패킷 전송 (주기적)
while (playing) {
    // RDY 핀 확인
    if (spi_check_ready(slave_id)) {
        // 샘플 로드 (SD 카드에서)
        wav_read_samples(&wav_file, samples, 2048);

        // DMA 전송
        spi_send_data_dma(slave_id, channel, samples, 2048);

        // 전송 완료 대기
        spi_wait_dma_complete(100);
    }

    // 32kHz, 2048 샘플 = 64ms 간격
    HAL_Delay(50);  // 여유 있게 대기
}

// 3. 재생 정지 명령
spi_send_command(slave_id, channel, SPI_CMD_STOP, 0);
```

**중요 타이밍**:
- 2048 샘플 @ 32kHz = 64ms 재생 시간
- Master는 약 50~60ms마다 다음 버퍼 전송
- RDY 핀이 HIGH일 때만 전송 시작
- DMA 전송 시간: 약 3~5ms @ 10MHz SPI

---

## 17. 질문 및 지원

### 구현 중 발생하는 문제
1. **SPI 통신이 안 됨**
   - CS 핀 연결 확인
   - SPI 클럭 모드 확인 (CPOL=0, CPHA=0)
   - RDY 핀 상태 확인

2. **오디오 출력에 노이즈**
   - DAC 출력에 RC 필터 추가
   - GND 연결 확인
   - 전원 디커플링 확인

3. **버퍼 언더런 발생**
   - 마스터의 전송 주기 확인 (< 64ms)
   - RDY 핀 응답 속도 확인
   - DMA 우선순위 확인

4. **타이밍 지터**
   - HSE 크리스탈 사용
   - PLL 설정 확인
   - 타이머 프리스케일러 확인

---

## 부록 A: 전체 코드 스켈레톤

```c
/* main.c - 슬레이브 메인 로직 */

#include "main.h"
#include <stdio.h>
#include <string.h>

// 설정
#define MY_SLAVE_ID             0  // 0, 1, 2 중 선택
#define AUDIO_BUFFER_SIZE       2048

// 프로토콜 정의
#define HEADER_CMD              0xC0
#define HEADER_DATA             0xDA
#define CMD_PLAY                0x01
#define CMD_STOP                0x02
#define CMD_VOLUME              0x03
#define CMD_RESET               0xFF

// 패킷 구조체
typedef struct __attribute__((packed)) {
    uint8_t header;
    uint8_t slave_id;
    uint8_t channel;
    uint8_t command;
    uint8_t param_h;
    uint8_t param_l;
} CommandPacket_t;

typedef struct __attribute__((packed)) {
    uint8_t header;
    uint8_t slave_id;
    uint8_t channel;
    uint8_t reserved;
    uint16_t num_samples;
    uint16_t samples[];
} DataPacket_t;

// 오디오 채널 구조체
typedef struct {
    uint16_t *buffer_a;
    uint16_t *buffer_b;
    uint16_t *active_buffer;
    uint16_t *fill_buffer;
    uint16_t fill_index;
    uint8_t  is_playing;
    uint8_t  underrun;
    uint8_t  volume;
} AudioChannel_t;

// 수신 상태 머신
typedef enum {
    STATE_WAIT_HEADER,
    STATE_RECEIVE_CMD,
    STATE_RECEIVE_DATA_HEADER,
    STATE_RECEIVE_DATA_SAMPLES
} RxState_t;

// 전역 변수
SPI_HandleTypeDef hspi1;
DAC_HandleTypeDef hdac;
TIM_HandleTypeDef htim6;
UART_HandleTypeDef huart1;

// 버퍼
__attribute__((aligned(32)))
uint16_t dac1_buffer_a[AUDIO_BUFFER_SIZE];
__attribute__((aligned(32)))
uint16_t dac1_buffer_b[AUDIO_BUFFER_SIZE];
__attribute__((aligned(32)))
uint16_t dac2_buffer_a[AUDIO_BUFFER_SIZE];
__attribute__((aligned(32)))
uint16_t dac2_buffer_b[AUDIO_BUFFER_SIZE];
__attribute__((aligned(32)))
uint8_t spi_rx_buffer[4102];

AudioChannel_t dac1_channel;
AudioChannel_t dac2_channel;

RxState_t rx_state = STATE_WAIT_HEADER;
uint8_t rx_header;
CommandPacket_t rx_cmd_packet;
DataPacket_t *rx_data_packet = (DataPacket_t*)spi_rx_buffer;

// 함수 프로토타입
void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_DMA_Init(void);
void MX_SPI1_Init(void);
void MX_DAC_Init(void);
void MX_TIM6_Init(void);
void MX_USART1_Init(void);

void audio_channel_init(AudioChannel_t *ch, uint16_t *buf_a, uint16_t *buf_b);
void audio_channel_fill(AudioChannel_t *ch, uint16_t *samples, uint16_t count);
void process_command_packet(CommandPacket_t *cmd);
void process_data_packet(DataPacket_t *data);

int main(void)
{
    // HAL 초기화
    HAL_Init();
    SystemClock_Config();

    // 주변장치 초기화
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI1_Init();
    MX_DAC_Init();
    MX_TIM6_Init();
    MX_USART1_Init();

    // 오디오 채널 초기화
    audio_channel_init(&dac1_channel, dac1_buffer_a, dac1_buffer_b);
    audio_channel_init(&dac2_channel, dac2_buffer_a, dac2_buffer_b);

    // RDY 핀 HIGH
    HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);

    // SPI 수신 시작
    HAL_SPI_Receive_IT(&hspi1, &rx_header, 1);

    printf("Slave %d Ready\r\n", MY_SLAVE_ID);

    // 메인 루프
    while (1)
    {
        // 인터럽트에서 모든 처리
        // 여기서는 모니터링만
    }
}

// 오디오 채널 초기화
void audio_channel_init(AudioChannel_t *ch, uint16_t *buf_a, uint16_t *buf_b)
{
    ch->buffer_a = buf_a;
    ch->buffer_b = buf_b;
    ch->active_buffer = buf_a;
    ch->fill_buffer = buf_b;
    ch->fill_index = 0;
    ch->is_playing = 0;
    ch->underrun = 0;
    ch->volume = 100;

    memset(buf_a, 0, AUDIO_BUFFER_SIZE * 2);
    memset(buf_b, 0, AUDIO_BUFFER_SIZE * 2);
}

// 샘플 채우기
void audio_channel_fill(AudioChannel_t *ch, uint16_t *samples, uint16_t count)
{
    for (uint16_t i = 0; i < count && ch->fill_index < AUDIO_BUFFER_SIZE; i++) {
        // 16비트 → 12비트 변환 및 볼륨 적용
        uint16_t sample = samples[i] >> 4;  // 상위 12비트
        sample = (sample * ch->volume) / 100;  // 볼륨 조절
        ch->fill_buffer[ch->fill_index++] = sample;
    }
}

// SPI RX 완료 콜백
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    static uint16_t samples_to_receive = 0;

    switch(rx_state) {
        case STATE_WAIT_HEADER:
            if (rx_header == HEADER_CMD) {
                rx_cmd_packet.header = rx_header;
                HAL_SPI_Receive_IT(hspi, ((uint8_t*)&rx_cmd_packet) + 1, 5);
                rx_state = STATE_RECEIVE_CMD;
            }
            else if (rx_header == HEADER_DATA) {
                rx_data_packet->header = rx_header;
                HAL_SPI_Receive_IT(hspi, ((uint8_t*)rx_data_packet) + 1, 5);
                rx_state = STATE_RECEIVE_DATA_HEADER;
            }
            else {
                HAL_SPI_Receive_IT(hspi, &rx_header, 1);
            }
            break;

        case STATE_RECEIVE_CMD:
            process_command_packet(&rx_cmd_packet);
            HAL_SPI_Receive_IT(hspi, &rx_header, 1);
            rx_state = STATE_WAIT_HEADER;
            break;

        case STATE_RECEIVE_DATA_HEADER:
            samples_to_receive = rx_data_packet->num_samples;
            if (samples_to_receive > 0 && samples_to_receive <= 2048) {
                HAL_SPI_Receive_IT(hspi, (uint8_t*)rx_data_packet->samples,
                                  samples_to_receive * 2);
                rx_state = STATE_RECEIVE_DATA_SAMPLES;
            } else {
                HAL_SPI_Receive_IT(hspi, &rx_header, 1);
                rx_state = STATE_WAIT_HEADER;
            }
            break;

        case STATE_RECEIVE_DATA_SAMPLES:
            process_data_packet(rx_data_packet);
            HAL_SPI_Receive_IT(hspi, &rx_header, 1);
            rx_state = STATE_WAIT_HEADER;
            break;
    }
}

// 명령 처리
void process_command_packet(CommandPacket_t *cmd)
{
    if (cmd->slave_id != MY_SLAVE_ID) return;

    AudioChannel_t *ch = (cmd->channel == 0) ? &dac1_channel : &dac2_channel;
    uint32_t dac_ch = (cmd->channel == 0) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
    uint16_t param = (cmd->param_h << 8) | cmd->param_l;

    switch(cmd->command) {
        case CMD_PLAY:
            if (!ch->is_playing) {
                ch->is_playing = 1;
                HAL_TIM_Base_Start(&htim6);
                HAL_DAC_Start_DMA(&hdac, dac_ch, (uint32_t*)ch->active_buffer,
                                 AUDIO_BUFFER_SIZE, DAC_ALIGN_12B_R);
            }
            break;
        case CMD_STOP:
            ch->is_playing = 0;
            HAL_DAC_Stop_DMA(&hdac, dac_ch);
            break;
        case CMD_VOLUME:
            ch->volume = param;
            break;
    }
}

// 데이터 처리
void process_data_packet(DataPacket_t *data)
{
    if (data->slave_id != MY_SLAVE_ID) return;

    AudioChannel_t *ch = (data->channel == 0) ? &dac1_channel : &dac2_channel;

    if (ch->is_playing) {
        audio_channel_fill(ch, data->samples, data->num_samples);
    }
}

// DAC DMA 완료 콜백
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    // 버퍼 스왑 로직
    if (dac1_channel.fill_index >= AUDIO_BUFFER_SIZE) {
        uint16_t *temp = dac1_channel.active_buffer;
        dac1_channel.active_buffer = dac1_channel.fill_buffer;
        dac1_channel.fill_buffer = temp;
        dac1_channel.fill_index = 0;
    } else {
        dac1_channel.underrun = 1;
    }
}

// printf 리다이렉트
int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}
```

---

**문서 버전**: 1.1
**최종 수정일**: 2025-11-01
**작성자**: Master Firmware Developer

**변경 이력**:
- v1.1 (2025-11-01): Master 측 SPI 프로토콜 구현 완료 반영, 실제 핀 매핑 추가, 동작 시퀀스 추가
- v1.0 (2025-10-26): 최초 작성

**구현 우선순위**:
1. **Phase 1 (1주)**: SPI 슬레이브 수신 + DAC 출력 기본 구현
2. **Phase 2 (1주)**: 이중 버퍼링 + 안정성 개선
3. **Phase 3 (1주)**: 3개 Slave 보드 통합 테스트

이 문서를 기반으로 슬레이브 펌웨어를 구현하시기 바랍니다.
질문이나 문제가 있으면 Master 펌웨어 팀에 문의하거나 `Core/Src/spi_protocol.c` 코드를 참조하세요.
