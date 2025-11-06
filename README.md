# STM32H723 Y-MODEM 파일 전송 최적화

STM32H723ZGT6 기반 오디오 멀티플렉서 펌웨어의 Y-MODEM 파일 전송 성능 최적화 프로젝트

## 프로젝트 개요

이 프로젝트는 STM32H723 마이크로컨트롤러에서 USB CDC를 통한 Y-MODEM 프로토콜로 SD 카드에 파일을 전송할 때, SD 카드 상태 체크를 통해 전송 속도를 25% 향상시킨 최적화 사례를 다룹니다.

### 주요 특징

- **STM32H723ZGT6** 마이크로컨트롤러 (Cortex-M7 @ 220MHz)
- **Y-MODEM 프로토콜** 파일 전송 (USB CDC)
- **8KB 버퍼링** SD 카드 쓰기 최적화
- **SD 상태 체크** 비동기 쓰기 완료 대기
- **재시도 로직** 타임아웃 및 NAK 자동 복구
- **6채널 오디오 스트리밍** (SPI DMA → 3x Slave MCU)

### 성능 개선

| 항목 | 최적화 전 | 최적화 후 | 개선율 |
|------|----------|----------|--------|
| 12.7MB 파일 전송 시간 | 약 2분 | 약 1.5분 | **25% 향상** |
| SD 쓰기 횟수 (vs 1KB/패킷) | 12,700회 | 1,588회 | **87.5% 감소** |
| 다음 쓰기 지연 | 가변 (SD 바쁨) | 최소화 (준비 확인) | 안정적 |

## 하드웨어 구성

### MCU 및 메모리

- **MCU:** STM32H723ZGT6
  - Core: Cortex-M7 @ 220MHz
  - Flash: 1MB
  - RAM: 564KB
  - FPU: FPv5-D16 (Hard float)

### 주요 페리페럴

- **UART2:** 디버그 출력 (115200 baud, 8N1)
- **USB OTG HS:** CDC Virtual COM Port
- **SDMMC1:** SD 카드 인터페이스 (4-bit mode)
- **SPI1:** 3x Slave STM32H523 통신 (6채널 오디오)
- **Ethernet:** RMII 모드 (구성됨, 미사용)

### GPIO

- **GPIOF[0:7]:** 8x LED 출력
- **GPIOG[10:15]:** 6x 릴레이 출력
- **GPIOG[0:7]:** 8x DIP 스위치 입력
- **GPIOE[2:5], GPIOD[8:9]:** 6x 로터리 엔코더 입력

## Y-MODEM 최적화 상세

### 핵심 문제: f_write()의 비동기 특성

```c
// f_write()는 FatFs 버퍼에 쓰고 즉시 반환
f_write(&file, buffer, 8192, &bytes_written);  // 즉시 반환
// ↑ 하지만 SD 카드는 아직 백그라운드에서 쓰는 중!

// 다음 패킷을 받으면...
f_write(&file, buffer, 8192, &bytes_written);  // SD 바쁨 → 대기 → 느림!
```

### 해결 방법: SD 상태 체크

```c
// 8KB 쓰기
if (write_buffer_offset >= 8192) {
    f_write(&file, sdmmc1_buffer, 8192, &bytes_written);
    write_buffer_offset = 0;
    sd_write_done = 1;
}

transmit_byte(huart, YMODEM_ACK);

// SD 쓰기 직후에만 상태 체크
if (sd_write_done) {
    // SD가 준비될 때까지 폴링 (최대 100ms)
    while (BSP_SD_GetCardState() != SD_TRANSFER_OK) {
        if (HAL_GetTick() - wait_start > 100) break;
        HAL_Delay(1);
    }
}

// USB CDC 안정성을 위한 필수 지연
HAL_Delay(8);
```


## 빌드 방법

### 사전 요구사항

- **STM32CubeCLT 1.19.0** 이상
- **STM32CubeIDE 1.19.0** (선택사항)
- **Git Bash** (Windows)

### 빌드 명령

```bash
cd Debug
PATH="/c/ST/STM32CubeCLT_1.19.0/GNU-tools-for-STM32/bin:$PATH" \
  make -j all
```

### 빌드 결과

```
✅ Build successful
   text: 147,064 bytes
   data: 356 bytes
   bss: 204,400 bytes
```

## UART 명령 프로토콜

### 연결 설정

- **포트:** UART2 (USB CDC)
- **보드레이트:** 115200 baud, 8N1

### 주요 명령

| 명령 | 설명 | 예시 |
|------|------|------|
| `UPLOAD <ch> <file>` | Y-MODEM 파일 업로드 | `UPLOAD 0 test.wav` |
| `PLAY <ch> <file>` | 오디오 재생 | `PLAY 0 test.wav` |
| `STOP <ch>` | 재생 정지 | `STOP 0` |
| `STATUS` | 상태 조회 | `STATUS` |

## 문서

### 최적화 과정 문서 (필독!)

1. **[Y-MODEM SD 상태 체크 최적화](YMODEM_SD_STATE_CHECK_OPTIMIZATION_20251106_161500.md)** ⭐
   - SD 상태 체크 최적화 전체 과정
   - 실패한 시도들 및 성공 사례
   - **이 문서부터 읽어보세요!**

2. **[8KB 버퍼링 롤백](ROLLBACK_TO_8KB_BUFFERING_20251106_152450.md)**
   - 32KB가 8KB보다 느린 이유
   - SD 카드 내부 처리 특성

3. **[Python 타임아웃 분석](PYTHON_TIMEOUT_ANALYSIS_AND_FIX_20251106_145617.md)**
   - Stop-and-Wait ARQ 프로토콜 이해

4. **[Y-MODEM 재시도 구현](YMODEM_RETRY_IMPLEMENTATION_20251106_141831.md)**
   - 타임아웃/NAK 재시도 로직

## 핵심 교훈

### 1. f_write()의 비동기 특성

```
f_write() 반환 ≠ SD 쓰기 완료
```

### 2. HAL_Delay(8)의 중요성

```
HAL_Delay(8) = USB CDC 필수 지연 (제거 금지!)
```

### 3. 측정 기반 최적화

```
이론 ≠ 실제
측정 없이는 최적화 아님
```

## 기여자

- **Developer:** trionking
- **AI Assistant:** Claude (Anthropic)

## 연락처

- **GitHub:** [@trionking](https://github.com/trionking)
- **Repository:** [stm32h723-ymodem-optimization](https://github.com/trionking/stm32h723-ymodem-optimization)

---

⭐ 이 프로젝트가 도움이 되었다면 Star를 눌러주세요!
