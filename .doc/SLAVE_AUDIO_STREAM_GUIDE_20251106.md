# Slave 보드 오디오 스트리밍 구현 가이드

**작성일**: 2025-11-06
**대상**: STM32H523 Slave 보드 개발자
**Master 보드**: STM32H723ZGT6
**프로토콜 버전**: 1.2
**상태**: 🚀 **오디오 스트리밍 기능 구현 시작**

---

## 📌 목적

이 문서는 Slave 보드(STM32H523)에서 **오디오 스트리밍 기능**을 구현하기 위한 가이드입니다.
Master 보드는 이미 RDY 핀 기반 핸드셰이킹을 포함한 오디오 스트리밍 시스템이 완성되어 있으며,
Slave 보드에서 이에 대응하는 기능을 구현해야 합니다.

---

## 🔍 Master 보드 현황 (2025-11-06 기준)

### ✅ 구현 완료된 기능

1. **SPI 프로토콜 완성**
   - 명령 패킷 전송 (PLAY, STOP, VOLUME, RESET)
   - 데이터 패킷 전송 (DMA 기반, 최대 2048 샘플)
   - 3개 Slave 독립 제어 (개별 CS 핀)

2. **RDY 핀 핸드셰이킹 구현**
   - 3개 RDY 입력 핀: `IN_SPI1_RDY1` (PF13), `IN_SPI1_RDY2` (PF14), `IN_SPI1_RDY3` (PF15)
   - 각 Slave별 RDY 상태 확인 함수: `spi_check_ready(slave_id)`
   - 오디오 스트리밍 루프에서 RDY HIGH 확인 후 전송

3. **6채널 오디오 스트리밍 시스템**
   - SD 카드에서 WAV 파일 읽기 (32kHz, 16-bit Mono)
   - 3개 Slave × 2채널 = 6채널 동시 재생
   - 채널별 독립 제어

4. **테스트 인터페이스**
   - UART2 명령어: `SPITEST` - SPI 통신 테스트
   - RDY 핀 상태 확인 기능

### 🔄 Master 측 RDY 핀 사용 방법

**Core/Src/spi_protocol.c:84-92**
```c
uint8_t spi_check_ready(uint8_t slave_id)
{
    if (slave_id >= SPI_SLAVE_COUNT) {
        return 0;
    }

    GPIO_PinState state = HAL_GPIO_ReadPin(slave_config[slave_id].rdy_port,
                                           slave_config[slave_id].rdy_pin);
    return (state == GPIO_PIN_SET) ? 1 : 0;  // HIGH = 준비됨, LOW = 바쁨
}
```

**Core/Src/audio_stream.c:238-244**
```c
static void process_channel(uint8_t channel_id)
{
    AudioChannel_t *ch = &channels[channel_id];

    /* RDY 핀 확인 (Slave가 데이터 수신 준비됨?) */
    if (!spi_check_ready(ch->slave_id)) {
        return;  // 아직 준비 안 됨 - 다음 루프에서 재시도
    }

    /* 오디오 데이터 전송 */
    send_audio_data(channel_id);
}
```

**동작 방식**:
- Master는 오디오 데이터를 전송하기 전에 **반드시** RDY 핀이 HIGH인지 확인
- RDY가 LOW이면 전송하지 않고 다음 루프에서 재시도
- RDY가 HIGH일 때만 CS를 LOW로 내리고 데이터 전송

---

## 🎯 Slave 보드에서 구현해야 할 내용

### 1. RDY 핀 제어 (필수)

Slave 보드는 **RDY 핀을 GPIO 출력**으로 설정하고, 상태에 따라 HIGH/LOW를 제어해야 합니다.

**권장 핀**: PA8 (또는 사용 가능한 GPIO)

#### RDY 핀 상태 천이 (Active Low)

```
초기화 → RDY = LOW (준비됨) ★ Active Low
    ↓
CS falling edge 감지 → RDY = HIGH (처리 중)
    ↓
패킷 수신 시작
    ↓
패킷 처리 완료 → RDY = LOW (다시 준비됨) ★ Active Low
```

#### 구현 예시

```c
/* GPIO 초기화 (main.c 또는 초기화 함수) */
void init_rdy_pin(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_8;          // PA8 (예시)
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // 초기 상태: HIGH (준비됨)
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
}

/* CS falling edge 인터럽트 콜백 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == CS_Pin) {
        if (HAL_GPIO_ReadPin(CS_GPIO_Port, CS_Pin) == GPIO_PIN_RESET) {
            // CS가 LOW → 수신 시작

            // RDY 핀 LOW (처리 중)
            HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_RESET);

            // SPI 수신 시작
            start_spi_reception();
        }
    }
}

/* 패킷 처리 완료 후 */
void packet_processing_complete(void)
{
    // RDY 핀 HIGH (다시 준비됨)
    HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);
}
```

### 2. 오디오 스트리밍 시스템 (필수)

#### 이중 버퍼링 구조

각 DAC 채널당 2개의 버퍼를 사용하여 언더런 방지:

```c
/* 채널별 버퍼 구조 (audio_channel.h) */
typedef struct {
    uint16_t buffer_a[BUFFER_SIZE];   // 2048 샘플
    uint16_t buffer_b[BUFFER_SIZE];   // 2048 샘플
    uint16_t *active_buffer;          // 현재 DAC로 출력 중인 버퍼
    uint16_t *filling_buffer;         // 현재 SPI로 채우는 중인 버퍼
    uint16_t fill_index;              // 채우기 위치
    uint8_t is_playing;               // 재생 중 플래그
} AudioChannel_t;
```

#### 버퍼 스왑 로직

```c
/* DAC DMA 완료 콜백 (절반 완료 시) */
void HAL_DAC_ConvCpltCallback(DAC_HandleTypeDef *hdac)
{
    AudioChannel_t *ch = get_channel_from_dac(hdac);

    // 버퍼 스왑
    uint16_t *temp = ch->active_buffer;
    ch->active_buffer = ch->filling_buffer;
    ch->filling_buffer = temp;

    // 채우기 인덱스 초기화
    ch->fill_index = 0;

    // RDY 핀 HIGH (새 버퍼 받을 준비됨)
    HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);
}
```

### 3. SPI 데이터 수신 처리

#### 데이터 패킷 수신 상태 머신

```c
typedef enum {
    STATE_IDLE,
    STATE_WAIT_HEADER,
    STATE_WAIT_CHANNEL,
    STATE_WAIT_LENGTH_H,
    STATE_WAIT_LENGTH_L,
    STATE_RECEIVE_SAMPLES
} SPIState_t;

/* SPI 수신 완료 콜백 */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    static SPIState_t state = STATE_WAIT_HEADER;
    static uint8_t rx_byte;
    static uint16_t sample_count;
    static uint16_t samples_received;
    static uint8_t target_channel;

    switch(state) {
        case STATE_WAIT_HEADER:
            if (rx_byte == 0xDA) {  // 데이터 패킷 헤더
                state = STATE_WAIT_CHANNEL;
            } else if (rx_byte == 0xC0) {  // 명령 패킷
                state = STATE_HANDLE_COMMAND;
            }
            break;

        case STATE_WAIT_CHANNEL:
            target_channel = rx_byte;  // 0=DAC1, 1=DAC2
            state = STATE_WAIT_LENGTH_H;
            break;

        case STATE_WAIT_LENGTH_H:
            sample_count = rx_byte << 8;
            state = STATE_WAIT_LENGTH_L;
            break;

        case STATE_WAIT_LENGTH_L:
            sample_count |= rx_byte;
            samples_received = 0;
            state = STATE_RECEIVE_SAMPLES;
            // 16-bit 샘플 수신 시작
            start_16bit_sample_reception();
            break;

        case STATE_RECEIVE_SAMPLES:
            // 샘플을 채널 버퍼에 복사
            AudioChannel_t *ch = &channels[target_channel];
            ch->filling_buffer[ch->fill_index++] = rx_sample;
            samples_received++;

            if (samples_received >= sample_count) {
                state = STATE_WAIT_HEADER;
                // RDY 핀 HIGH (다음 패킷 준비)
                HAL_GPIO_WritePin(RDY_GPIO_Port, RDY_Pin, GPIO_PIN_SET);
            }
            break;
    }

    // 다음 바이트 수신 시작
    HAL_SPI_Receive_IT(hspi, &rx_byte, 1);
}
```

### 4. DAC 출력 시스템

#### DAC 초기화 (32kHz 타이머 트리거)

```c
/* 타이머 설정: 32kHz 샘플레이트 */
void init_dac_timer(void)
{
    // 타이머 클럭: 예) 64MHz
    // 타이머 주파수 = 64MHz / (Prescaler+1) / (Period+1)
    // 32kHz = 64MHz / 1 / 2000

    htim.Init.Prescaler = 0;
    htim.Init.Period = 1999;  // 64MHz / 2000 = 32kHz
    htim.Init.CounterMode = TIM_COUNTERMODE_UP;

    HAL_TIM_Base_Init(&htim);
}

/* DAC DMA 시작 */
void start_dac_playback(uint8_t channel)
{
    AudioChannel_t *ch = &channels[channel];

    ch->is_playing = 1;
    ch->active_buffer = ch->buffer_a;
    ch->filling_buffer = ch->buffer_b;

    // DAC DMA 시작 (순환 모드)
    HAL_DAC_Start_DMA(&hdac,
                      (channel == 0) ? DAC_CHANNEL_1 : DAC_CHANNEL_2,
                      (uint32_t*)ch->active_buffer,
                      BUFFER_SIZE,
                      DAC_ALIGN_12B_R);

    // 타이머 시작
    HAL_TIM_Base_Start(&htim);
}
```

### 5. 명령 패킷 처리

```c
/* 명령 패킷 처리 함수 */
void handle_command_packet(uint8_t channel, uint8_t cmd, uint16_t param)
{
    switch(cmd) {
        case 0x01:  // PLAY
            printf("CMD: PLAY on CH%d\r\n", channel);
            start_dac_playback(channel);
            break;

        case 0x02:  // STOP
            printf("CMD: STOP on CH%d\r\n", channel);
            stop_dac_playback(channel);
            break;

        case 0x03:  // VOLUME
            printf("CMD: VOLUME on CH%d: %d\r\n", channel, param);
            set_channel_volume(channel, param);
            break;

        case 0xFF:  // RESET
            printf("CMD: RESET on CH%d\r\n", channel);
            reset_channel(channel);
            break;
    }
}
```

---

## 🔄 전체 통신 플로우

### 재생 시작 시퀀스 (Active Low)

```
[Slave] 초기화: RDY = LOW ★ (준비됨)
    ↓
[Master] RDY 핀 체크 (LOW 확인)
    ↓
[Master] CS = LOW (Slave 선택)
    ↓ (100us 지연)
[Slave] RDY = HIGH (처리 중)
    ↓
[Master] 명령 패킷 전송 (0xC0, CH, PLAY, 0x00, 0x00)
    ↓
[Slave] 명령 수신 및 처리
    ↓
[Slave] DAC 및 타이머 시작
    ↓
[Slave] RDY = LOW ★ (데이터 준비됨)
    ↓
[Master] CS = HIGH (명령 전송 완료)
```

### 오디오 데이터 전송 시퀀스 (반복, Active Low)

```
[Slave] RDY = LOW ★ (버퍼 A를 DAC로 출력 중, 준비됨)
    ↓
[Master] RDY 핀 체크 (LOW 확인)
    ↓
[Master] CS = LOW
    ↓
[Slave] RDY = HIGH (처리 중)
    ↓
[Master] 데이터 패킷 전송 (0xDA, CH, LEN_H, LEN_L, 샘플들...)
    ↓
[Slave] 버퍼 B에 샘플 채우기 (fill_index++)
    ↓
[Slave] 샘플 수신 완료 → RDY = LOW ★
    ↓
[Master] CS = HIGH
    ↓
[Slave] 버퍼 A 출력 완료 (DMA 콜백)
    ↓
[Slave] 버퍼 스왑: A ↔ B
    ↓
[Slave] RDY = LOW ★ (새 버퍼 B에 데이터 받을 준비)
    ↓
반복...
```

### 재생 정지 시퀀스 (Active Low)

```
[Master] RDY 핀 체크 (LOW 확인)
    ↓
[Master] CS = LOW
    ↓
[Slave] RDY = HIGH (처리 중)
    ↓
[Master] 명령 패킷 전송 (0xC0, CH, STOP, 0x00, 0x00)
    ↓
[Slave] 명령 수신 및 처리
    ↓
[Slave] DAC 정지, 타이머 정지
    ↓
[Slave] 버퍼 클리어
    ↓
[Slave] RDY = LOW ★ (준비됨)
    ↓
[Master] CS = HIGH
```

---

## ⏱️ 타이밍 요구사항

### SPI 통신 타이밍

| 항목 | 값 | 설명 |
|------|-----|------|
| **SPI 클럭** | 10MHz | Master 설정값 |
| **CS 셋업 시간** | 100μs | Master가 CS LOW 후 대기 |
| **RDY 응답 시간** | < 50μs | CS falling 후 RDY LOW까지 |
| **패킷 간 간격** | > 100μs | 패킷 간 최소 간격 |

### 오디오 타이밍

| 항목 | 값 | 설명 |
|------|-----|------|
| **샘플레이트** | 32,000 Hz | 정확히 유지 필요 |
| **버퍼 크기** | 2048 샘플 | 각 버퍼 |
| **버퍼 재생 시간** | 64ms | 2048 / 32000 |
| **전송 주기** | 50-60ms | Master 전송 간격 |
| **여유 시간** | 4-14ms | 버퍼 언더런 방지 |

**중요**: 버퍼 재생 시간(64ms) 이내에 다음 버퍼를 채워야 합니다!

---

## 📊 메모리 사용량 (예상)

### 각 Slave 보드당

```
DAC1 이중 버퍼: 2048 * 2 * 2 bytes = 8,192 bytes
DAC2 이중 버퍼: 2048 * 2 * 2 bytes = 8,192 bytes
SPI 수신 버퍼:  4,101 bytes
제어 구조체:    약 100 bytes
─────────────────────────────────────────
총 메모리:      약 20KB (STM32H523: 32KB RAM 사용 가능)
```

### DMA 사용 채널

- SPI1 RX: DMA 채널 1개
- DAC1: DMA 채널 1개
- DAC2: DMA 채널 1개 (또는 TIM 트리거)

---

## 🧪 테스트 절차

### Phase 1: RDY 핀 동작 확인

1. **Slave 보드 초기화**
   - RDY 핀을 GPIO 출력으로 설정
   - 초기 상태: HIGH

2. **Master에서 테스트**
   ```
   UART2 명령어:
   SPITEST BASIC 0
   ```

3. **확인 사항**
   - Master 터미널에 "Slave 0 RDY pin: HIGH (Ready)" 출력
   - Slave 터미널에 패킷 수신 확인

### Phase 2: 명령 패킷 수신 테스트

1. **Master에서 명령 전송**
   ```
   SPITEST BASIC 0
   ```

2. **Slave에서 확인**
   - PLAY 명령 수신: "CMD: PLAY on CH0"
   - VOLUME 명령 수신: "CMD: VOLUME on CH0: 75"
   - STOP 명령 수신: "CMD: STOP on CH0"

### Phase 3: 데이터 패킷 수신 및 DAC 출력 테스트

1. **Master에서 간단한 오디오 재생**
   ```
   PLAY 0 test.wav
   ```

2. **Slave에서 확인**
   - 데이터 패킷 수신 확인
   - 버퍼에 샘플 채워지는지 확인
   - DAC 출력 파형 확인 (오실로스코프)

### Phase 4: 이중 버퍼링 동작 확인

1. **장시간 재생 (10초 이상)**
   ```
   PLAY 0 longfile.wav
   ```

2. **확인 사항**
   - 버퍼 스왑이 정상 동작하는지
   - RDY 핀이 정확히 HIGH/LOW 천이하는지
   - 오디오 끊김 없이 재생되는지

### Phase 5: 2채널 동시 재생 테스트

1. **두 채널 동시 재생**
   ```
   PLAY 0 file1.wav
   PLAY 1 file2.wav
   ```

2. **확인 사항**
   - 두 채널 독립 동작
   - 상호 간섭 없음

---

## ⚠️ 주의사항 및 팁

### 1. RDY 핀 타이밍

- **CS falling edge 감지 즉시** RDY를 LOW로 설정
- 패킷 처리가 완전히 끝난 후에만 RDY를 HIGH로 복원
- RDY가 LOW인 상태에서는 Master가 전송하지 않으므로, 처리 시간 제약 없음

### 2. 버퍼 언더런 방지

- 이중 버퍼링 필수
- 버퍼 스왑은 DAC DMA 절반 완료 또는 완료 콜백에서 수행
- RDY는 새 버퍼를 받을 준비가 된 후에만 HIGH

### 3. SPI 수신 최적화

- DMA 기반 수신 권장 (인터럽트 부하 감소)
- 상태 머신을 간결하게 유지
- 에러 처리 로직 포함 (잘못된 헤더, 타임아웃 등)

### 4. DAC 출력

- 16-bit 샘플을 12-bit로 변환: `dac_value = sample >> 4`
- DAC alignment: `DAC_ALIGN_12B_R` (12-bit 오른쪽 정렬)
- 타이머 트리거 정확도 확인 (32kHz 정확히)

### 5. 디버깅

- UART 터미널로 상태 출력 (패킷 수신, 버퍼 스왑 등)
- RDY 핀 상태를 LED로 표시 (디버깅 용이)
- 로직 분석기로 SPI 신호 모니터링 권장

---

## 📁 권장 파일 구조

```
Core/
├── Inc/
│   ├── spi_protocol.h         ← Master와 동일한 프로토콜 정의
│   ├── spi_handler.h          ← SPI 수신 핸들러
│   ├── audio_channel.h        ← 오디오 채널 관리
│   ├── rdy_control.h          ← RDY 핀 제어 (새로 추가)
│   └── main.h
├── Src/
│   ├── spi_handler.c          ← 상태 머신 및 패킷 처리
│   ├── audio_channel.c        ← 이중 버퍼링, DAC 제어
│   ├── rdy_control.c          ← RDY 핀 초기화 및 제어 (새로 추가)
│   ├── main.c                 ← 초기화 및 메인 루프
│   └── stm32h5xx_it.c         ← 인터럽트 핸들러
```

---

## 📞 Master 측 연락 사항

### 구현 완료된 Master 기능

- ✅ RDY 핀 확인 함수: `spi_check_ready(slave_id)`
- ✅ CS 셋업 타임: 100μs (충분한 준비 시간)
- ✅ 오디오 전송 주기: 50-60ms (여유 있게)
- ✅ SPI 테스트 명령: `SPITEST ALL 0`

### Master 측 주요 파일

- `Core/Src/spi_protocol.c:84-92` - RDY 핀 체크 함수
- `Core/Src/audio_stream.c:238-244` - 채널 처리 루프 (RDY 확인)
- `Core/Inc/spi_protocol.h` - 프로토콜 정의 (Slave와 공유)

### 테스트 협조 방법

1. **Slave 보드 준비 완료 시**
   - Master 보드와 SPI 연결 (SCK, MOSI, CS, RDY, GND)
   - 두 보드의 UART 터미널 동시 모니터링

2. **Master에서 테스트 명령 실행**
   ```
   SPITEST ALL 0
   ```

3. **양측 터미널 출력 확인**
   - Master: RDY 상태, 전송 결과
   - Slave: 패킷 수신, 처리 상태

---

## 🚀 구현 우선순위

### 1단계: 기본 통신 확립 (필수)

- [ ] RDY 핀 GPIO 출력 설정 및 초기화
- [ ] CS falling edge 인터럽트 설정
- [ ] RDY 핀 제어 (CS falling → RDY LOW)
- [ ] 명령 패킷 수신 상태 머신
- [ ] 기본 명령 처리 (PLAY, STOP)

### 2단계: 데이터 수신 구현 (필수)

- [ ] 데이터 패킷 수신 상태 머신
- [ ] 샘플 데이터 버퍼에 저장
- [ ] RDY 핀 복원 (수신 완료 → RDY HIGH)

### 3단계: DAC 출력 시스템 (필수)

- [ ] DAC 초기화 (32kHz 타이머 트리거)
- [ ] 이중 버퍼링 구현
- [ ] DMA 기반 DAC 출력
- [ ] 버퍼 스왑 로직

### 4단계: 통합 및 테스트 (필수)

- [ ] Master-Slave 통신 테스트
- [ ] 단일 채널 재생 테스트
- [ ] 2채널 동시 재생 테스트
- [ ] 장시간 안정성 테스트

### 5단계: 최적화 (선택)

- [ ] DMA 기반 SPI 수신
- [ ] 에러 처리 강화
- [ ] 통계 및 모니터링 기능
- [ ] 볼륨 제어 구현

---

## 📚 참고 문서

### Master 보드 관련

- **CLAUDE.md**: 프로젝트 전체 구조 및 빌드 가이드
- **PROTOCOL_SUMMARY.md**: SPI 프로토콜 요약
- **PC_UART_PROTOCOL.md**: UART 명령어 가이드
- **SPITEST_COMMAND_GUIDE.md**: SPI 테스트 명령어

### Slave 보드 관련

- **SLAVE_IMPLEMENTATION_SPEC.md**: 상세 구현 명세서 (1700줄)
- **SLAVE_DEBUG_GUIDE.md**: 디버깅 가이드
- **URGENT_NOTICE_TO_SLAVE.md**: 긴급 공지 (1-byte shift 문제)

### 프로토콜 공통

- `Core/Inc/spi_protocol.h`: 프로토콜 정의 (Master-Slave 공유)

---

## ❓ FAQ

### Q1: RDY 핀을 사용하지 않고 구현할 수 있나요?

**A**: 이론적으로 가능하지만 **강력히 비권장**합니다. RDY 핀 없이 구현하면:
- 버퍼 오버런 위험 (Slave가 준비되지 않았는데 데이터 수신)
- Master가 타이밍에 의존 (불안정)
- 에러 복구 어려움

Master 코드는 RDY 핀 사용을 전제로 작성되어 있으므로, 반드시 구현해야 합니다.

### Q2: 버퍼 크기를 2048보다 작게 할 수 있나요?

**A**: 가능하지만 **권장하지 않습니다**. 버퍼가 작을수록:
- 전송 주기가 짧아짐 (SPI 오버헤드 증가)
- 타이밍 여유 감소 (언더런 위험)
- Master 코드 수정 필요

2048 샘플 = 64ms 재생 시간은 안정적인 스트리밍에 적합한 크기입니다.

### Q3: SPI 클럭을 10MHz보다 높일 수 있나요?

**A**: STM32H523은 더 높은 SPI 클럭을 지원하지만:
- Master-Slave 간 클럭 협의 필요
- 신호 품질 확인 필요 (케이블 길이, 노이즈)
- 10MHz는 안정적으로 검증된 속도

현재는 10MHz 유지를 권장하며, 필요 시 양측 협의 후 변경하세요.

### Q4: 3개 Slave 보드의 펌웨어가 동일한가요?

**A**: 기본적으로 동일하지만, **Slave ID만 다르게 설정**해야 합니다:
- Slave 0: `MY_SLAVE_ID = 0`
- Slave 1: `MY_SLAVE_ID = 1`
- Slave 2: `MY_SLAVE_ID = 2`

향후에는 DIP 스위치나 GPIO 핀으로 자동 감지하도록 개선 예정입니다.

---

## 📞 문의 및 지원

**문서 작성자**: Master 보드 개발팀
**작성일**: 2025-11-06
**프로젝트**: STM32 오디오 멀티플렉서 (6채널)

**Master 보드 구현 상태**: ✅ 완료 (오디오 스트리밍, RDY 핸드셰이킹 포함)
**Slave 보드 구현 상태**: 🚀 시작 예정

Slave 보드 구현 중 문제가 발생하거나 Master 측 수정이 필요한 경우,
Master 개발팀과 협의하여 진행하시기 바랍니다.

---

**다음 단계**: Slave 보드에서 위 가이드를 따라 오디오 스트리밍 기능을 구현하고,
Master 보드와 통합 테스트를 진행합니다.

Good luck! 🎵
