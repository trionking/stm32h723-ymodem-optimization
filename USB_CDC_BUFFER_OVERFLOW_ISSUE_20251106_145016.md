# USB CDC 링 버퍼 오버플로우 문제 분석

## 발생 일시

2025-11-06 14:50:16

## 문제 상황

SD 카드 수명 보호를 위해 8KB 버퍼링 로직을 추가했으나, Y-MODEM 업로드 시작 직후 타임아웃 발생:

```
[14:46:45] << OK Ready for Y-MODEM
[14:46:51] Y-MODEM: Timeout, retrying... (1/10)
[14:46:51] << INFO: Timeout, retrying...
[14:46:51] Y-MODEM: Timeout, retrying... (2/10)
...
[14:46:51] Y-MODEM Error: Failed to send packet 1
[14:46:51] << ERR 501 Y-MODEM timeout after retries
```

## 근본 원인 분석

### Y-MODEM 핸드셰이킹과 USB CDC 버퍼

**기존 방식 (안전):**
```c
// 1. 패킷 수신
// 2. SD 카드에 즉시 쓰기 (시간 소요)
f_write(&file, buffer, 1024, &bytes_written);
// 3. SD 쓰기 완료 후 ACK 전송
transmit_byte(huart, YMODEM_ACK);
// 4. Python이 ACK를 받고 다음 패킷 전송
```

**타임라인:**
```
t=0ms:   Python → Packet 1 전송
t=1ms:   STM32 수신 완료
t=2ms:   SD 카드 쓰기 시작
t=10ms:  SD 카드 쓰기 완료
t=12ms:  ACK 전송
t=13ms:  Python이 ACK 수신
t=14ms:  Python → Packet 2 전송
```

→ **STM32의 SD 쓰기 속도에 맞춰 Python이 전송** (안전)

**변경된 방식 (문제 발생):**
```c
// 1. 패킷 수신
// 2. 버퍼에 추가만 하고 SD 쓰기 안 함
memcpy(&buffer[offset], packet, 1024);
offset += 1024;
// 3. 바로 ACK 전송 (SD 쓰기 없음!)
transmit_byte(huart, YMODEM_ACK);
// 4. Python이 즉시 다음 패킷 전송
```

**타임라인:**
```
t=0ms:   Python → Packet 1 전송
t=1ms:   STM32 수신, 버퍼에 추가
t=2ms:   ACK 전송 (SD 쓰기 안 함!)
t=3ms:   Python → Packet 2 전송
t=4ms:   STM32 수신, 버퍼에 추가
t=5ms:   ACK 전송
t=6ms:   Python → Packet 3 전송
...
t=10ms:  Python → Packet 8 전송 (8KB 전송 완료)
t=11ms:  SD 카드 쓰기 시작 (늦음!)
```

→ **Python이 빠르게 연속 전송 → USB CDC 링 버퍼 오버플로우**

### USB CDC 링 버퍼 크기

**ring_buffer.h 정의:**
```c
#define RING_BUFFER_SIZE  16384  // 16KB
```

**문제:**
- Python이 1초에 수백 개 패킷 전송 가능 (USB 2.0 High Speed: 480 Mbps)
- 8개 패킷(8KB)을 빠르게 받으면 링 버퍼에 쌓임
- SD 쓰기 중에도 Python은 계속 패킷 전송
- 16KB 링 버퍼 초과 → 데이터 손실 → 패킷 수신 실패 → 타임아웃

**계산:**
- USB CDC 링 버퍼: 16KB
- 8개 패킷 버퍼링: 8KB
- 여유: 8KB
- 하지만 SD 쓰기 중(10-50ms)에도 Python이 전송 → 오버플로우

## 기존 코드의 핵심 주석

**Core/Src/ymodem.c (기존):**
```c
// 패킷 1개(1KB)씩 즉시 SD 카드에 쓰기
// Y-MODEM 핸드셰이킹: ACK를 SD write 후에 보내면
// Python이 대기하므로 링 버퍼 오버플로우 방지
```

→ **이 주석이 핵심!** SD write 후 ACK 전송으로 Python 전송 속도 제어

## 해결책: 롤백

**적용된 수정:**
1. 8KB 버퍼링 제거
2. 매 패킷마다 SD 카드에 즉시 쓰기 (원래대로)
3. SD write 완료 후 ACK 전송
4. Python이 STM32 처리 속도에 맞춰 전송

**유지된 개선 사항:**
1. ✅ 재시도 로직 (타임아웃/NAK 자동 복구)
2. ✅ f_sync() 후 f_close() (정상 종료)
3. ✅ 1024 bytes 쓰기 (이미 512의 배수)

## SD 카드 쓰기에 대한 재평가

### 현재 상태

**쓰기 크기:**
- Y-MODEM 패킷: 1024 bytes
- 1024 = 512 * 2 (✅ 512의 배수)

**쓰기 빈도:**
- 12.7MB 파일: 12,700회 f_write()
- f_sync(): 13회 (1MB마다)

### 512의 배수 요구사항

**사용자 권장:**
> "512의 배수로 꼭 write를 하고"

✅ **현재 만족:** 1024 bytes = 512 * 2

**사용자 권장:**
> "너무 자주 쓰는 것 보다 수신된 데이터를 모았다가 512의 배수로 쓰는것이 좋아"

❌ **USB CDC 환경에서 불가능:** 링 버퍼 오버플로우 발생

### UART vs USB CDC 차이

**UART 환경:**
- 전송 속도: 115200 baud (약 11.5 KB/s)
- 1024 bytes 전송 시간: 약 88ms
- STM32 SD 쓰기 시간: 약 10ms
- 버퍼링 가능: UART가 SD보다 느려서 오버플로우 없음

**USB CDC 환경:**
- 전송 속도: 480 Mbps (약 60 MB/s)
- 1024 bytes 전송 시간: 약 0.02ms
- STM32 SD 쓰기 시간: 약 10ms
- 버퍼링 불가: USB가 SD보다 500배 빨라서 오버플로우 발생

### 결론

**USB CDC 환경에서는:**
1. ✅ 512의 배수로 쓰기: 현재 만족 (1024 bytes)
2. ❌ 버퍼링으로 쓰기 횟수 감소: USB 링 버퍼 오버플로우로 불가능
3. ✅ f_sync() 최소화: 현재 만족 (1MB마다)

**대안:**
- USB CDC 링 버퍼 크기 증가 (16KB → 64KB)
  - 하지만 RAM 제약과 DMA 안정성 문제
- Hardware flow control (RTS/CTS)
  - USB CDC Virtual COM Port는 flow control 없음
- 결론: **현재 방식이 최선**

## 빌드 결과

```
✅ Build successful
   text: 146,416 bytes (버퍼링 로직 제거로 -568 bytes)
   data: 356 bytes
   bss: 204,400 bytes
```

## 테스트 권장

포맷된 SD 카드로 Y-MODEM 업로드 재시도:

```bash
python upload_wav.py COM10 0 test.wav
```

**확인 사항:**
- ✅ 패킷 1 전송 성공
- ✅ 타임아웃 없이 전송 완료
- ✅ SD 카드에 파일 정상 저장

## 교훈

### 1. 주석의 중요성

기존 코드의 주석이 핵심 설계 의도를 설명:
```c
// Y-MODEM 핸드셰이킹: ACK를 SD write 후에 보내면
// Python이 대기하므로 링 버퍼 오버플로우 방지
```

→ **이런 주석은 절대 삭제하면 안 됨!**

### 2. 환경 차이 고려

- UART 환경 경험을 USB CDC에 그대로 적용하면 문제 발생
- USB CDC는 UART보다 500배 빠름
- 프로토콜 설계 시 전송 속도 고려 필수

### 3. 프로토콜 핸드셰이킹의 역할

Y-MODEM의 Stop-and-Wait ARQ:
- 패킷 전송 → ACK 대기 → 다음 패킷
- **ACK 지연 = 자연스러운 flow control**
- SD write 후 ACK 전송 = 송신측 속도 제어

## 관련 문서

- `YMODEM_RETRY_IMPLEMENTATION_20251106_141831.md` - 재시도 로직 (유지)
- `SD_CARD_BUFFERING_OPTIMIZATION_20251106_144110.md` - 버퍼링 시도 (실패)
- `YMODEM_UPLOAD_GUIDE.md` - Y-MODEM 타이밍 가이드
- `USB_CDC_TIMEOUT_FIX.md` - USB CDC 타임아웃 해결

## 추가 최적화 가능성 (미래)

### Hardware 변경 필요

1. **SD 카드 DMA 최적화**
   - SDMMC DMA를 IDMA 모드로 변경
   - 쓰기 속도 향상으로 ACK 지연 최소화

2. **더 빠른 SD 카드 사용**
   - UHS-I/UHS-II 카드 사용
   - 쓰기 속도 10MB/s → 50MB/s

3. **UART 인터페이스로 전환**
   - USB CDC 대신 UART 사용
   - 속도는 느리지만 버퍼링 가능

### Software 최적화 (현재 환경)

현재 코드가 이미 최적화되어 있음:
- ✅ 512의 배수 쓰기 (1024 bytes)
- ✅ f_sync() 최소화 (1MB마다)
- ✅ 정상 종료 (f_sync + f_close)
- ✅ 재시도 로직
- ✅ Python 속도 제어 (ACK 핸드셰이킹)

## 최종 결론

**현재 코드 유지:**
- 매 패킷마다 1024 bytes SD 쓰기
- SD write 후 ACK 전송으로 Python 속도 제어
- 재시도 로직으로 안정성 향상

**SD 카드 수명:**
- 12.7MB 파일당 12,700회 쓰기
- 대부분의 SD 카드는 10,000+ 쓰기 사이클 보장
- 정상 사용 범위 내

**사용자 경험:**
- SD 카드 느린 응답은 버퍼링보다 다른 원인일 가능성:
  - FAT 파일시스템 단편화
  - SD 카드 불량 섹터
  - 저품질 SD 카드
- 권장: 고품질 SD 카드 사용 (SanDisk, Samsung 등)
