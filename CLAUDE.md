# CLAUDE.md

이 파일은 Claude Code (claude.ai/code)가 이 저장소에서 작업할 때 참고하는 가이드입니다.

## 프로젝트 개요

**STM32H723 오디오 멀티플렉서 펌웨어** - STM32H723ZGT6 마이크로컨트롤러를 사용하는 오디오 라우팅 제어 보드용 임베디드 펌웨어입니다. 이 시스템은 릴레이를 통한 오디오 신호 라우팅을 관리하고, 로터리 엔코더와 DIP 스위치를 통해 모니터링하며, Ethernet, USB CDC, SD 카드 저장소 및 SPI 확장을 위한 인터페이스를 제공합니다.

## 빌드 시스템

### 사전 요구사항
- **STM32CubeCLT 1.19.0** 이상 (`C:/ST/STM32CubeCLT_1.19.0/`에 설치)
- **STM32CubeIDE 1.19.0** 이상 (GUI용, 선택사항)
- **arm-none-eabi-gcc** 툴체인 (CubeCLT에 포함)
- **GNU Make** (CubeIDE에 포함)

### 빌드 방법

**VSCode에서:**
- `Ctrl+Shift+B`를 눌러 Debug 빌드
- 또는 명령 팔레트 (`Ctrl+Shift+P`) → "Tasks: Run Task" → "Build (Debug)" 또는 "Build (Release)" 선택

**명령줄에서 (Git Bash):**
```bash
cd Debug

# PATH에 GNU tools 추가하여 빌드
PATH="/c/ST/STM32CubeCLT_1.19.0/GNU-tools-for-STM32/bin:$PATH" \
  "/c/ST/STM32CubeIDE_1.19.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845/tools/bin/make.exe" \
  -j all
```

**클린:**
```bash
cd Debug
PATH="/c/ST/STM32CubeCLT_1.19.0/GNU-tools-for-STM32/bin:$PATH" \
  "/c/ST/STM32CubeIDE_1.19.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845/tools/bin/make.exe" \
  clean
```

**빌드 결과:**
- 성공 시 `audio_mux_v101.elf`, `audio_mux_v101.map`, `audio_mux_v101.list` 생성
- 메모리 사용량: text ~100KB, data ~100B, bss ~114KB

### 플래싱 (Flash)

**VSCode에서:**
- 명령 팔레트 → "Tasks: Run Task" → "Flash (ST-Link)"
- 또는 "Build and Flash"를 선택하여 빌드 후 자동 플래시

**명령줄에서:**
```bash
"C:/ST/STM32CubeCLT_1.19.0/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe" -c port=SWD -w Debug/audio_mux_v101.elf -v -rst
```

## 아키텍처

### 하드웨어 구성

**MCU:** STM32H723ZGT6 (Cortex-M7 @ 220MHz)
- 1MB Flash, 564KB RAM
- FPU: FPv5-D16, Hard float ABI
- 전원: LDO 모드

**사용 중인 페리페럴:**
- **UART2** (115200 baud): `printf()`를 통한 디버그 시리얼 출력
- **SPI1**: 3개의 외부 슬레이브 장치를 위한 마스터 모드 (개별 칩 셀렉트)
- **SDMMC1**: 전원 제어 기능이 있는 4-bit SD 카드 인터페이스
- **Ethernet**: RMII 모드, MAC 체크섬 오프로드
- **USB OTG HS**: CDC Virtual COM Port
- **TIM14**: PWM 출력 모드
- **TIM16**: 범용 타이머

**GPIO 배치:**
- **GPIOF[0:7]**: 8x LED 출력 (`OT_LD0-7`)
- **GPIOG[10:15]**: 6x 릴레이 출력 (`OT_REL0-5`)
- **GPIOG[0:7]**: 8x DIP 스위치 입력 (`IN_DIP0-7`)
- **GPIOE[2:5]**, **GPIOD[8:9]**: 6x 로터리 엔코더 입력 (`IN_ROT0-5`)
- **GPIOB[0:2]**: 오디오 모니터 선택 (`OT_MON_SEL0-2`)
- **GPIOA[4]**, **GPIOF[11:14]**: SPI 칩 셀렉트 및 준비 신호

### 메모리 맵 및 전략

**메모리 레이아웃 (`STM32H723ZGTX_FLASH.ld` - MPU 구성됨):**

```
0x08000000  FLASH (1024KB)           - 프로그램 코드, 상수, 읽기 전용 데이터

0x00000000  ITCMRAM (64KB)           - Stack (32KB) + Heap (32KB)
                                       빠른 접근, zero wait state

0x20000000  DTCMRAM (128KB)          - DMA 버퍼 (SPI, I2S, UART)
                                       캐시 불필요, zero wait state
                                       섹션: .dma_buffer, .spi_tx/rx_buffer,
                                             .i2s_tx/rx_buffer, .uart_dma_buffer

0x24000000  RAM_D1_DMA (128KB)       - 대형 DMA 버퍼 (MPU Region 1: 캐시 OFF)
                                       섹션: .ram_d1_dma, .audio_dma_buffer,
                                             .large_dma_buffer

0x24020000  RAM_D1_CACHE1 (64KB)     - 초기화된 데이터 (MPU Region 2: 캐시 ON)
                                       섹션: .data, .tdata, .tbss, .RamFunc

0x24030000  RAM_D1_CACHE2 (128KB)    - 초기화되지 않은 데이터 (MPU Region 3: 캐시 ON)
                                       섹션: .bss, 일반 데이터

0x30000000  RAM_D2 (32KB)            - SD MDMA 버퍼 (MPU Region 4: 캐시 OFF)
                                       섹션: .sd_mdma_buffer, .ram_d2,
                                             .UsbCompositeSection (USB DMA)

0x38000000  RAM_D3 (16KB)            - ADC3 BDMA 버퍼 (MPU Region 5: 캐시 OFF)
                                       섹션: .adc3_buffer, .ram_d3
```

**메모리 사용 최적화 전략:**

이 링커 스크립트는 STM32H7의 다양한 RAM 영역을 캐시 일관성과 DMA 성능을 고려하여 최적 배치합니다:

1. **ITCMRAM**: 가장 빠른 스택/힙 접근 (함수 호출, 지역 변수)
2. **DTCMRAM**: 작은 DMA 버퍼 (페리페럴 통신용, 캐시 bypass)
3. **RAM_D1_DMA**: 대용량 오디오/DMA 버퍼 (캐시 OFF로 DMA 일관성 보장)
4. **RAM_D1_CACHE1/2**: 일반 애플리케이션 데이터 (캐시 ON으로 성능 향상)
5. **RAM_D2**: SD 카드 MDMA, USB (캐시 OFF)
6. **RAM_D3**: ADC3 전용 BDMA (캐시 OFF)

### 코드 구조

```
Core/
├── Inc/
│   ├── main.h              - GPIO 핀 정의, 페리페럴 핸들
│   ├── user_def.h          - 애플리케이션 함수 프로토타입
│   ├── audio_stream.h      - 6채널 오디오 스트리밍 시스템
│   ├── spi_protocol.h      - SPI 통신 프로토콜 (Main ↔ Slave MCU)
│   ├── wav_parser.h        - WAV 파일 파서
│   ├── uart_command.h      - UART2 명령 파서 (PC 통신)
│   ├── ymodem.h            - Y-MODEM 파일 전송 프로토콜
│   ├── command_handler.h   - UART 명령 핸들러
│   ├── ansi_colors.h       - ANSI 색상 매크로 (UART 로그용)
│   └── stm32h7xx_hal_conf.h - HAL 모듈 선택
├── Src/
│   ├── main.c              - 진입점, 클럭 설정, 페리페럴 초기화
│   ├── user_def.c          - I/O 제어 함수, SD 초기화, 메인 루프
│   ├── audio_stream.c      - 오디오 채널 관리 및 SPI DMA 전송
│   ├── spi_protocol.c      - SPI 프로토콜 구현
│   ├── wav_parser.c        - WAV 파일 읽기 및 파싱
│   ├── uart_command.c      - UART 인터럽트 수신 및 파싱
│   ├── ymodem.c            - Y-MODEM 수신 구현 (SD 카드 저장)
│   ├── command_handler.c   - HELLO, UPLOAD, PLAY, STOP 등 명령 처리
│   ├── stm32h7xx_it.c      - 인터럽트 핸들러 (UART2 포함)
│   └── stm32h7xx_hal_msp.c - HAL MSP 초기화
├── Startup/
│   └── startup_stm32h723zgtx.s - Reset 핸들러, 벡터 테이블

Drivers/                    - STM32 HAL 및 CMSIS (읽기 전용)
Middlewares/
├── ST/STM32_USB_Device_Library/ - USB CDC 스택
└── Third_Party/FatFs/      - FAT 파일시스템 라이브러리

FATFS/                      - FatFs 구성 및 디스크 I/O
USB_DEVICE/                 - USB 장치 앱 레이어
```

### 초기화 플로우

```
Reset Vector → startup_stm32h723zgtx.s
    ↓
main() → MPU_Config() → HAL_Init() → SystemClock_Config()
    ↓
MX_GPIO_Init() → MX_USART2_UART_Init() → MX_TIM16_Init() → MX_TIM14_Init() → MX_SPI1_Init()
    ↓
run_proc() → sound_mon_test()  [MON_SEL2를 토글하는 무한 루프]
```

## 주요 패턴 및 규칙

### STM32CubeMX 통합

파일에는 `/* USER CODE BEGIN */`과 `/* USER CODE END */` 마커가 포함되어 있습니다. `.ioc` 파일에서 재생성할 때 사용자 코드를 보존하려면 **이 섹션 내에서만 코드를 편집**해야 합니다.

페리페럴 초기화 재생성 방법:
1. STM32CubeMX에서 `audio_mux_v101.ioc` 열기
2. 필요에 따라 페리페럴/클럭 수정
3. 코드 생성 (사용자 코드 섹션 보존됨)

### HAL 사용

모든 하드웨어 접근은 STM32 HAL을 사용합니다:
- `HAL_GPIO_WritePin()` - GPIO 출력
- `HAL_GPIO_ReadPin()` - GPIO 입력
- `HAL_SPI_Transmit()` - SPI 통신
- `HAL_UART_Transmit()` - 시리얼 디버그

### 디버그 출력

`printf()`는 `user_def.c`의 `__io_putchar()`를 통해 UART2로 리디렉션됩니다. 115200 baud로 시리얼 터미널을 연결하면 디버그 메시지를 볼 수 있습니다.

### I/O 제어 함수 (user_def.c)

```c
void led_out(uint8_t ld_val);      // 8개 LED 제어 (반전 로직)
void rel_out(uint8_t rel_val);     // 6개 릴레이 제어
uint8_t read_dip(void);            // 8개 DIP 스위치 읽기 (반전 로직)
void sound_mon_test(void);         // 현재 애플리케이션 메인 루프
void init_sdmmc(void);             // SD 카드 전원 사이클 및 초기화
```

**참고:** GPIO 함수는 성능 향상을 위해 HAL 대신 BSRR 레지스터 직접 접근을 사용하여 원자적 연산을 수행합니다.

## 현재 애플리케이션 상태

### 6채널 오디오 스트리밍 시스템

**아키텍처:**
- Main STM32H723 → SPI DMA → 3x Slave STM32H523 (각 2채널, 총 6채널)
- 32kHz 16-bit Mono WAV 파일 재생
- SD 카드에서 파일 로드 (FatFs)
- PC와 UART2 통신 (115200 baud, 8N1)

**주요 기능:**
1. **오디오 재생:** `audio_load_file()`, `audio_play()`, `audio_stop()`
2. **SPI 프로토콜:** Main과 Slave MCU 간 명령/데이터 전송
3. **WAV 파싱:** RIFF 헤더 검증 및 PCM 데이터 추출
4. **PC 통신:** UART2를 통한 텍스트 명령 처리
5. **Y-MODEM 전송:** PC에서 SD 카드로 WAV 파일 업로드

**활성 상태:**
- ✅ SD 카드 FatFs (초기화 및 마운트됨)
- ✅ UART2 명령 인터페이스 (인터럽트 기반)
- ✅ 오디오 스트리밍 시스템 (SPI DMA)
- ✅ LED 상태 토글 (500ms)

**구성되었지만 비활성 상태:**
- USB CDC 장치 (main에서 초기화되지 않음)
- Ethernet 인터페이스 (구성되었지만 시작되지 않음)

## 중요 참고사항

### 새로운 기능 추가

1. **새 페리페럴:** STM32CubeMX를 사용하여 구성한 후 코드 재생성
2. **애플리케이션 로직:** USER CODE 마커 사이의 `user_def.c`에 추가
3. **인터럽트:** `stm32h7xx_it.c`의 USER CODE 섹션에 핸들러 추가

### 새 소스 파일을 빌드 시스템에 추가

새로운 `.c` 파일을 `Core/Src/`에 추가한 경우, 수동으로 빌드 시스템에 포함시켜야 합니다:

1. **`Debug/Core/Src/subdir.mk` 수정:**
   ```makefile
   # C_SRCS, OBJS, C_DEPS 섹션에 새 파일 추가 (알파벳 순서)
   C_SRCS += \
   ../Core/Src/audio_stream.c \
   ../Core/Src/new_file.c \      # <-- 추가
   ../Core/Src/main.c \
   ...
   ```

2. **`Debug/objects.list` 수정:**
   ```
   "./Core/Src/audio_stream.o"
   "./Core/Src/new_file.o"       # <-- 추가
   "./Core/Src/main.o"
   ...
   ```

3. **빌드 재실행:** 변경 후 clean 빌드 권장

**참고:** STM32CubeIDE에서 빌드하면 자동으로 업데이트되지만, CLI에서 빌드할 때는 수동 편집 필요

### 일반적인 문제

**헤더 누락 에러:**
- FatFs 타입 (`FATFS`, `FIL`) 사용 시 `"ff.h"` 포함
- USB 타입 사용 시 `"usb_device.h"` 포함

**빌드 구성:**
- Debug 빌드: `-O0 -g3` (최적화 없음, 전체 디버그 심볼)
- Release 빌드: `-Os -g0` (크기 최적화, 디버그 없음)

### DMA 버퍼 메모리 배치

**중요:** DMA를 사용하는 버퍼는 반드시 적절한 메모리 섹션에 배치해야 합니다. 캐시 일관성 문제를 방지하기 위해 다음 속성을 사용하세요:

**작은 DMA 버퍼 (SPI, I2S, UART - DTCMRAM):**
```c
uint8_t spi_tx_buffer[256] __attribute__((section(".spi_tx_buffer")));
uint8_t uart_dma_buffer[512] __attribute__((section(".uart_dma_buffer")));
```

**대형 오디오/DMA 버퍼 (RAM_D1_DMA - 캐시 OFF):**
```c
uint32_t audio_dma_buffer[8192] __attribute__((section(".audio_dma_buffer")));
uint8_t large_dma_buffer[32768] __attribute__((section(".ram_d1_dma")));
```

**SD 카드 MDMA 버퍼 (RAM_D2 - 캐시 OFF):**
```c
uint8_t sd_mdma_buffer[4096] __attribute__((section(".sd_mdma_buffer"))) __attribute__((aligned(32)));
```

**USB DMA 버퍼 (RAM_D2):**
```c
USB_CompositeHandle usb_handle __attribute__((section(".UsbCompositeSection")));
```

**ADC3 BDMA 버퍼 (RAM_D3 - 캐시 OFF):**
```c
uint16_t adc3_buffer[1024] __attribute__((section(".adc3_buffer"))) __attribute__((aligned(32)));
```

**정렬(Alignment) 규칙:**
- 모든 DMA 버퍼는 32바이트 경계 정렬 권장 (`__attribute__((aligned(32)))`)
- D-Cache 라인 크기와 일치하여 캐시 일관성 문제 방지

**메모리 사용량 확인:**
- 빌드 후 `.map` 파일에서 각 섹션의 크기 확인
- `arm-none-eabi-size` 출력으로 전체 메모리 사용량 확인

### 전원 공급 고려사항

MCU는 LDO 공급 모드를 사용합니다 (`USE_PWR_LDO_SUPPLY` 정의됨). SMPS 또는 외부 전원으로 전환하는 경우 CubeMX에서 업데이트하고 재생성하세요.

### 클럭 구성

- HSE: 25MHz 크리스탈 (HSI48 디바이더 입력)
- 시스템 클럭: 220MHz (PLL: M=2, N=44, P=1)
- USB 클럭: HSI48 (48MHz 전용)
- Ethernet: HCLK/2

## UART 명령 프로토콜

### PC ↔ Main Board 통신

**설정:**
- UART2, 115200 baud, 8 data bits, No parity, 1 stop bit (8N1)
- 텍스트 기반 명령 (CR 또는 LF로 종료)
- ANSI 색상 코드 지원

**지원 명령:**

| 명령 | 설명 | 예시 |
|------|------|------|
| `HELLO` | 연결 테스트 | `HELLO` → `OK AUDIO_MUX v1.00 STM32H723` |
| `STATUS` | 모든 채널 상태 확인 | `STATUS` → `OK STATUS\r\nCH0: IDLE\r\n...` |
| `PLAY <ch> <file>` | 채널에서 파일 재생 | `PLAY 0 test.wav` |
| `STOP <ch>` | 채널 정지 | `STOP 0` |
| `STOPALL` | 모든 채널 정지 | `STOPALL` |
| `VOLUME <ch> <vol>` | 볼륨 설정 (0-100) | `VOLUME 0 75` |
| `LS [ch]` | 파일 목록 조회 | `LS 0` → `/audio/ch0/` 파일 리스트 |
| `DELETE <ch> <file>` | 파일 삭제 | `DELETE 0 old.wav` |
| `UPLOAD <ch> <file>` | Y-MODEM 파일 업로드 | `UPLOAD 0 new.wav` |
| `SPITEST <type> [id]` | SPI 통신 테스트 | `SPITEST ALL 0` |
| `RESET` | 시스템 리셋 | `RESET` |

**응답 형식:**
- 성공: `OK [데이터]`
- 에러: `ERR <코드> <메시지>`
- 정보: `INFO: <메시지>` (ANSI 색상 포함)

**Y-MODEM 전송 프로토콜:**
1. PC → `UPLOAD 0 file.wav\r\n`
2. Board → `OK Ready for Y-MODEM\r\n`
3. Board → 'C' (CRC 모드 시작)
4. PC → Y-MODEM 파일 정보 패킷
5. PC → 1024-byte 데이터 패킷들 (CRC-16)
6. Board → ACK
7. PC → EOT
8. Board → `OK Upload complete /audio/ch0/file.wav\r\n`

자세한 내용은 `PC_UART_PROTOCOL.md` 및 `FIRMWARE_UART_YMODEM_GUIDE.md` 참조

**SPI 테스트 명령 (SPITEST):**

`SPITEST` 명령은 Main 보드와 Slave 보드 간의 SPI 통신을 테스트합니다.

| 서브 명령 | 설명 | 예시 |
|----------|------|------|
| `SPITEST BASIC <id>` | 기본 명령 패킷 테스트 | `SPITEST BASIC 0` |
| `SPITEST DATA <id>` | 데이터 패킷 전송 테스트 | `SPITEST DATA 0` |
| `SPITEST MULTI` | 멀티 Slave ID 필터링 테스트 | `SPITEST MULTI` |
| `SPITEST ERROR <id>` | 에러 처리 테스트 | `SPITEST ERROR 0` |
| `SPITEST ALL <id>` | 전체 테스트 실행 | `SPITEST ALL 0` |

**사용 방법:**
1. Slave 보드를 SPI 테스트 모드(Test 5)로 설정
2. Main 보드에서 SPITEST 명령 실행
3. Slave 터미널에서 수신 패킷 확인

자세한 내용은 `SPITEST_COMMAND_GUIDE.md` 및 `SPI_TEST_GUIDE.md` 참조

## 테스트

현재 자동화된 테스트 스위트는 존재하지 않습니다. 수동 테스트 방법:
- 시리얼 콘솔 출력 (UART2, 115200 baud)
- LED/릴레이 육안 확인
- DIP 스위치 입력 읽기
- 외부 슬레이브와 SPI 통신
- UART 명령 테스트 (TeraTerm, PuTTY 등)
- Y-MODEM 파일 전송 테스트
- md 파일을 만들 때는 날짜,시간을 꼭 파일 이름에 추가해줘.