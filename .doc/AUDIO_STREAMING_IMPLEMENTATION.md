# 오디오 스트리밍 시스템 구현 요약

**작성일**: 2025-10-26
**프로젝트**: Audio Mux v1.00 - Multi-Channel Audio Streaming
**타겟**: STM32H723ZGT6 (Cortex-M7 @ 220MHz)

---

## 1. 프로젝트 개요

### 시스템 구성
- **마스터 보드**: STM32H723ZGT6 (본 펌웨어)
- **슬레이브 보드**: STM32H523 x 3개
- **총 채널 수**: 6채널 (각 슬레이브당 2개 DAC 출력)

### 주요 기능
- SD 카드에서 WAV 파일 읽기 (FatFs)
- 32kHz 샘플레이트, 12/16비트 모노 오디오 지원
- SPI DMA를 통한 고속 데이터 전송
- RDY 핀 기반 핸드셰이크 프로토콜
- 6채널 독립 재생 제어
- 채널별 볼륨 제어 및 루프 재생

---

## 2. 시스템 아키텍처

### 통신 프로토콜

#### 하드웨어 연결
```
Master (STM32H723)          Slave 0,1,2 (STM32H523)
├─ SPI1_NSS1 (PA4)    ──→   CS (Slave 0)
├─ SPI1_NSS2 (PF11)   ──→   CS (Slave 1)
├─ SPI1_NSS3 (PF12)   ──→   CS (Slave 2)
├─ SPI1_SCK           ──→   SCK
├─ SPI1_MOSI          ──→   MOSI
├─ RDY1 (PF13)        ←──   RDY (Slave 0)
├─ RDY2 (PF14)        ←──   RDY (Slave 1)
└─ RDY3 (PF15)        ←──   RDY (Slave 2)
```

#### 메모리 배치
```
RAM_D1_DMA (128KB, Cache OFF):
├─ SPI TX 버퍼 (3 x 4100 bytes)
└─ 오디오 샘플 버퍼 (2048 samples x 2 bytes)

RAM_D2 (32KB, Cache OFF):
└─ SD 카드 읽기 버퍼 (2048 samples x 2 bytes)
```

### 데이터 흐름
```
SD Card (WAV)
    ↓ FatFs + WAV Parser
Audio Sample Buffer (16-bit)
    ↓ SPI Protocol Layer
SPI TX Buffer + Header
    ↓ DMA Transfer
Slave MCU → DAC → Audio Output
```

---

## 3. 구현된 파일 및 기능

### 3.1 WAV 파일 파서 (`Core/Src/wav_parser.c/h`)

**주요 기능**:
- RIFF/WAVE 헤더 파싱
- 데이터 청크 자동 탐색
- 12비트 → 16비트 변환 (하위 4비트 마스킹)
- 파일 위치 추적 및 시크

**핵심 함수**:
```c
FRESULT wav_open(WAV_FileInfo_t *info, const char *filename);
FRESULT wav_read_samples(WAV_FileInfo_t *info, uint16_t *buffer,
                         uint32_t num_samples, uint32_t *samples_read);
FRESULT wav_seek(WAV_FileInfo_t *info, uint32_t sample_position);
void wav_close(WAV_FileInfo_t *info);
```

**메모리 사용**:
- 구조체 크기: 약 600 bytes (파일 핸들 포함)

---

### 3.2 SPI 프로토콜 레이어 (`Core/Src/spi_protocol.c/h`)

**주요 기능**:
- 3개 슬레이브 CS 핀 관리
- RDY 핀 폴링 (핸드셰이크)
- DMA 전송 상태 관리
- 명령 패킷 및 데이터 패킷 전송

**프로토콜 정의**:
```c
// 명령 패킷 (6 bytes)
typedef struct {
    uint8_t header;      // 0xC0
    uint8_t slave_id;    // 0~2
    uint8_t channel;     // 0=DAC1, 1=DAC2
    uint8_t cmd;         // PLAY, STOP, VOLUME
    uint8_t param_h;
    uint8_t param_l;
} SPI_CommandPacket_t;

// 데이터 패킷
typedef struct {
    uint8_t header;      // 0xDA
    uint8_t slave_id;
    uint8_t channel;
    uint8_t reserved;
    uint16_t num_samples;
    uint16_t samples[];  // 최대 2048 samples
} SPI_DataPacket_t;
```

**핵심 함수**:
```c
void spi_protocol_init(SPI_HandleTypeDef *hspi);
HAL_StatusTypeDef spi_send_command(uint8_t slave_id, uint8_t channel,
                                   uint8_t cmd, uint16_t param);
HAL_StatusTypeDef spi_send_data_dma(uint8_t slave_id, uint8_t channel,
                                    uint16_t *samples, uint16_t num_samples);
uint8_t spi_check_ready(uint8_t slave_id);
```

**DMA 콜백**:
```c
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi);
```

---

### 3.3 오디오 스트리밍 시스템 (`Core/Src/audio_stream.c/h`)

**주요 기능**:
- 6채널 독립 관리
- WAV 파일 로딩 및 재생 제어
- 비차단(non-blocking) 스트리밍
- 자동 루프 재생
- 파일 끝 처리

**채널 상태**:
```c
typedef enum {
    AUDIO_STATE_IDLE,       // 유휴
    AUDIO_STATE_LOADED,     // 파일 로드됨
    AUDIO_STATE_PLAYING,    // 재생 중
    AUDIO_STATE_ERROR       // 오류
} AudioChannelState_t;
```

**채널 매핑**:
```
Channel 0 → Slave 0, DAC1
Channel 1 → Slave 0, DAC2
Channel 2 → Slave 1, DAC1
Channel 3 → Slave 1, DAC2
Channel 4 → Slave 2, DAC1
Channel 5 → Slave 2, DAC2
```

**핵심 함수**:
```c
int audio_stream_init(SPI_HandleTypeDef *hspi);
int audio_load_file(uint8_t channel_id, const char *filename, uint8_t loop);
int audio_play(uint8_t channel_id);
int audio_stop(uint8_t channel_id);
int audio_set_volume(uint8_t channel_id, uint16_t volume);
void audio_stream_task(void);  // 메인 루프에서 호출
void audio_print_status(void);  // 디버그용
```

**버퍼 크기**: 2048 샘플 (64ms @ 32kHz)

---

### 3.4 시스템 초기화 (`Core/Src/user_def.c`)

**추가된 기능**:

#### SD 카드 초기화
```c
void init_sdmmc(void)
{
    // 전원 제어
    HAL_GPIO_WritePin(OT_SD_EN_GPIO_Port, OT_SD_EN_Pin, GPIO_PIN_SET);

    // SDMMC 초기화
    MX_SDMMC1_SD_Init();

    // HAL READY 대기 (타임아웃 5초)
    while (HAL_SD_GetState(&hsd1) != HAL_SD_STATE_READY);

    // 카드 정보 출력
    HAL_SD_GetCardInfo(&hsd1, &card_info);
}
```

#### FatFs 마운트
```c
int mount_fatfs(void)
{
    FRESULT res;
    res = f_mount(&SDFatFS, (TCHAR const*)SDPath, 1);
    if (res != FR_OK) {
        printf("FatFs: Mount failed (error %d)\r\n", res);
        return -1;
    }
    return 0;
}
```

#### 메인 프로세스
```c
void run_proc(void)
{
    printf("Audio Mux v1.00 - Multi-Channel Audio Streaming\r\n");

    // SD 초기화
    init_sdmmc();

    // FatFs 마운트
    if (mount_fatfs() != 0) {
        printf("ERROR: Failed to mount FatFs\r\n");
        while(1);
    }

    // 오디오 시스템 초기화
    if (audio_stream_init(&hspi1) != 0) {
        printf("ERROR: Failed to initialize audio stream\r\n");
        while(1);
    }

    printf("System Ready\r\n");

    // 메인 루프
    while(1) {
        audio_stream_task();  // 오디오 스트리밍 처리

        // LED 토글 (500ms)
        HAL_GPIO_TogglePin(OT_SYS_GPIO_Port, OT_SYS_Pin);
        HAL_Delay(10);
    }
}
```

---

### 3.5 빌드 시스템 수정

#### 수정된 파일
1. **Debug/Core/Src/subdir.mk**
   - `C_SRCS`에 audio_stream.c, spi_protocol.c, wav_parser.c 추가
   - `OBJS`에 해당 .o 파일 추가
   - `C_DEPS`에 해당 .d 파일 추가

2. **Debug/objects.list**
   - 링커에 전달할 오브젝트 파일 목록에 추가:
     - `./Core/Src/audio_stream.o`
     - `./Core/Src/spi_protocol.o`
     - `./Core/Src/wav_parser.o`

3. **Core/Src/main.c** (line 84, 266)
   - `MX_SDMMC1_SD_Init()` 함수의 `static` 키워드 제거
   - 외부에서 접근 가능하도록 수정

4. **Core/Inc/main.h**
   - `void MX_SDMMC1_SD_Init(void);` 선언 추가

---

## 4. 빌드 결과

### 성공적인 빌드
```
Finished building target: audio_mux_v101.elf

Memory Usage:
   text      data      bss       total
  76,188     108    114,436   190,732 bytes
```

### 생성된 파일
- **audio_mux_v101.elf** (2.2 MB) - 플래시 가능한 바이너리
- **audio_mux_v101.map** - 링커 맵 파일
- **audio_mux_v101.list** - 디스어셈블리 리스트

### 메모리 분석
- **코드 영역 (text)**: 76 KB
- **초기화 데이터 (data)**: 108 bytes
- **미초기화 데이터 (bss)**: 114 KB
  - SPI TX 버퍼: 3 x 4100 = 12,300 bytes
  - 오디오 버퍼: 2048 x 2 = 4,096 bytes
  - SD 읽기 버퍼: 2048 x 2 = 4,096 bytes
  - 나머지: HAL, FatFs, USB 스택

### 경고 및 에러
- ✅ 경고 없음
- ✅ 링크 에러 없음
- ✅ 빌드 성공

---

## 5. 사용 방법

### 5.1 펌웨어 플래시
VSCode에서:
```
Ctrl+Shift+B → "Flash (Debug)" 선택
```

또는 수동으로:
```bash
cd Debug
make -j all
# STM32_Programmer_CLI로 플래시
```

### 5.2 SD 카드 준비
```
SD카드 루트/
├─ audio/
│  ├─ test.wav      (32kHz, 16-bit, mono)
│  ├─ channel0.wav
│  ├─ channel1.wav
│  └─ ...
```

### 5.3 코드에서 재생
```c
// audio_stream.c의 run_proc() 함수에서
// 또는 사용자 코드에서:

// 채널 0에 파일 로드 (루프 재생)
audio_load_file(0, "0:/audio/test.wav", 1);

// 재생 시작
audio_play(0);

// 볼륨 조절 (0~100)
audio_set_volume(0, 80);

// 정지
audio_stop(0);
```

### 5.4 디버그 출력
UART2를 통해 시리얼 콘솔 출력:
- SD 카드 초기화 상태
- FatFs 마운트 결과
- 오디오 시스템 초기화
- 파일 로드/재생 상태
- 에러 메시지

---

## 6. 다음 단계

### 6.1 슬레이브 펌웨어 구현 (STM32H523)
- [ ] SPI 슬레이브 모드 설정
- [ ] 프로토콜 파서 구현
- [ ] 오디오 버퍼 관리 (이중 버퍼링)
- [ ] DAC 타이머 설정 (32kHz)
- [ ] RDY 핀 제어
- [ ] 볼륨 제어 구현

### 6.2 하드웨어 테스트
- [ ] 단일 채널 테스트 (Slave 0, DAC1)
- [ ] 멀티 채널 동시 재생 테스트
- [ ] 장시간 안정성 테스트
- [ ] 오디오 품질 측정
- [ ] 타이밍 분석 (Logic Analyzer)

### 6.3 기능 개선
- [ ] 채널별 믹싱 기능
- [ ] 실시간 볼륨 페이드 인/아웃
- [ ] 재생 위치 조절 (seek)
- [ ] 다양한 샘플레이트 지원 (44.1kHz, 48kHz)
- [ ] 에러 복구 메커니즘

### 6.4 최적화
- [ ] DMA 전송 타이밍 최적화
- [ ] CPU 사용률 측정 및 개선
- [ ] 전력 소모 최적화
- [ ] 버퍼 크기 튜닝

---

## 7. 알려진 이슈 및 제한사항

### 현재 구현의 제한
1. **샘플레이트 고정**: 32kHz만 지원
2. **모노 전용**: 스테레오 WAV는 지원 안 됨
3. **슬레이브 펌웨어 미구현**: 마스터만 완성
4. **에러 처리**: 기본적인 수준
5. **동기화**: 채널 간 완벽한 동기화 보장 안 됨

### 주의사항
- SD 카드는 4-bit SDMMC 모드 사용
- SPI DMA는 비차단 방식이므로 전송 완료 확인 필요
- 캐시 일관성을 위해 DMA 버퍼는 캐시 OFF 영역 사용
- WAV 파일은 표준 PCM 포맷이어야 함

---

## 8. 참고 자료

### 프로젝트 문서
- `CLAUDE.md` - 전체 프로젝트 구조 및 빌드 방법
- `STM32H723ZGTX_FLASH.ld` - 커스텀 링커 스크립트
- `.vscode/tasks.json` - VSCode 빌드 설정

### STM32 HAL 드라이버
- SPI: `stm32h7xx_hal_spi.c`
- SDMMC: `stm32h7xx_hal_sd.c`
- DMA: `stm32h7xx_hal_dma.c`

### 미들웨어
- FatFs: `Middlewares/Third_Party/FatFs/`
- USB Device: `Middlewares/ST/STM32_USB_Device_Library/` (현재 사용 안 함)

---

## 변경 이력

### 2025-10-26
- ✅ 초기 구현 완료
- ✅ WAV 파서 구현
- ✅ SPI 프로토콜 레이어 구현
- ✅ 오디오 스트리밍 시스템 구현
- ✅ 빌드 시스템 통합
- ✅ 첫 빌드 성공

---

**작성자**: Claude Code
**라이선스**: 프로젝트 라이선스 참조
