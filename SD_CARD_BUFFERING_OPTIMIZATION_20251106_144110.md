# SD 카드 버퍼링 최적화 구현

## 변경 개요

Y-MODEM 파일 전송 시 SD 카드 수명을 보호하고 안정성을 향상시키기 위해 8KB 버퍼링 로직을 추가했습니다.

## 변경 일시

2025-11-06 14:41:10

## 문제 배경

**사용자 경험 기반 문제점:**
- SD 카드 PC 연결 시 느린 응답 → SD 카드 손상 징후
- 과거 경험: 이런 증상 지속 시 SD 카드 고장 발생
- 기존 코드: 매 Y-MODEM 패킷(1024 bytes)마다 즉시 f_write() 호출 → SD 카드 부담

**사용자 권장 사항:**
1. 512의 배수로 쓰기
2. 너무 자주 쓰지 말고 데이터를 모았다가 한 번에 쓰기
3. 마지막 데이터도 512 배수로 패딩하여 쓰기
4. 정상 종료 시 f_sync() 후 f_close()

## 구현 내용

### 1. SD 쓰기 버퍼 크기 정의 (Core/Src/ymodem.c:14-16)

```c
// SD 카드 쓰기 최적화 설정
// SD 카드 수명 보호: 8KB 버퍼링 (512 배수 보장)
#define SD_WRITE_BUFFER_SIZE  8192  // 8KB = 512 * 16 (최적 블록 크기)
```

**선택 이유:**
- 8KB = 8개 Y-MODEM 패킷 (1024 * 8)
- 8192 = 512 * 16 (완벽한 512 배수)
- sdmmc1_buffer는 32KB이므로 충분히 사용 가능

### 2. 버퍼링 변수 추가 (Core/Src/ymodem.c:43-44)

```c
// SD 카드 쓰기 버퍼링 (sdmmc1_buffer 재사용, 32KB 크기)
uint32_t write_buffer_offset = 0;  // 현재 버퍼에 쌓인 데이터 크기
```

### 3. 패킷 데이터 버퍼링 (Core/Src/ymodem.c:303-338)

```c
// 패킷 데이터를 버퍼에 추가 (8KB 버퍼링으로 SD 카드 수명 보호)
memcpy(&sdmmc1_buffer[write_buffer_offset], &packet_buffer[3], data_size);
write_buffer_offset += data_size;
total_bytes += data_size;
packet_number++;

// 패킷 성공 시 NAK 카운터 리셋
nak_retries = 0;

// 버퍼가 8KB 이상 차면 SD 카드에 쓰기 (512 * 16 = 최적 블록 크기)
if (write_buffer_offset >= SD_WRITE_BUFFER_SIZE) {
    UINT bytes_written;
    fres = f_write(&file, sdmmc1_buffer, write_buffer_offset, &bytes_written);

    if (fres != FR_OK || bytes_written != write_buffer_offset) {
        // 쓰기 에러 처리
    }

    // 버퍼 리셋
    write_buffer_offset = 0;

    // 주기적으로 f_sync() 호출 (1MB마다)
    if (total_bytes % (1024 * 1024) == 0) {
        fres = f_sync(&file);
    }
}
```

**동작 방식:**
1. Y-MODEM 패킷(1024 bytes) 수신
2. sdmmc1_buffer[write_buffer_offset]에 추가
3. write_buffer_offset이 8KB 이상이면 SD 카드에 한 번에 쓰기
4. 버퍼 리셋하고 다음 패킷 계속 누적

**f_sync() 주기 변경:**
- 기존: 1000 패킷(1MB)마다 f_sync()
- 변경: 1MB 단위로 f_sync() (동일하지만 total_bytes 기준으로 변경)

### 4. EOT 시 512 배수 패딩 (Core/Src/ymodem.c:216-243)

```c
if (header == YMODEM_EOT) {
    // 전송 완료 - 남은 버퍼 데이터를 512 배수로 패딩하여 쓰기
    if (write_buffer_offset > 0) {
        // 512 배수로 올림 (SD 카드 최적화)
        uint32_t padded_size = ((write_buffer_offset + 511) / 512) * 512;

        // 패딩 영역을 0으로 채움
        if (padded_size > write_buffer_offset) {
            memset(&sdmmc1_buffer[write_buffer_offset], 0,
                   padded_size - write_buffer_offset);
        }

        // SD 카드에 마지막 데이터 쓰기
        UINT bytes_written;
        fres = f_write(&file, sdmmc1_buffer, padded_size, &bytes_written);

        if (fres != FR_OK) {
            // 에러 처리
        }

        printf("[DEBUG] Final write: %lu bytes data + %lu bytes padding = %u bytes written\r\n",
               write_buffer_offset, padded_size - write_buffer_offset, bytes_written);
        write_buffer_offset = 0;
    }

    transmit_byte(huart, YMODEM_ACK);
    uart_send_response(ANSI_GREEN "INFO:" ANSI_RESET " Transfer complete (%lu bytes)\r\n", total_bytes);
    result = YMODEM_OK;
    break;
}
```

**512 배수 패딩 계산:**
- `padded_size = ((write_buffer_offset + 511) / 512) * 512`
- 예시:
  - write_buffer_offset = 1000 → padded_size = 1024 (24 bytes 패딩)
  - write_buffer_offset = 2000 → padded_size = 2048 (48 bytes 패딩)
  - write_buffer_offset = 8192 → padded_size = 8192 (패딩 없음)

### 5. 정상 종료 (Core/Src/ymodem.c:364-375)

```c
// 파일 정상 종료: f_sync() 후 f_close() (SD 카드 데이터 무결성 보장)
fres = f_sync(&file);
if (fres != FR_OK) {
    printf("[WARN] f_sync before close failed: fres=%d\r\n", fres);
}

fres = f_close(&file);
if (fres != FR_OK) {
    printf("[ERROR] f_close failed: fres=%d\r\n", fres);
} else {
    printf("[DEBUG] File closed successfully\r\n");
}
```

**보장 사항:**
1. f_sync() 호출로 모든 버퍼 데이터를 SD 카드에 flush
2. f_close() 호출로 FAT 테이블 업데이트
3. 각 단계별 에러 로그 출력

## 개선 효과

### Before (기존)

**쓰기 패턴:**
```
Packet 1: f_write(1024 bytes) → SD 카드
Packet 2: f_write(1024 bytes) → SD 카드
Packet 3: f_write(1024 bytes) → SD 카드
...
Packet 1000: f_write(1024 bytes) + f_sync() → SD 카드
```

- ❌ 매 패킷마다 SD 카드 쓰기 (높은 부하)
- ❌ 마지막 데이터 512 배수 보장 안 함
- ✅ 1024 bytes는 512의 배수 (OK)

### After (개선 후)

**쓰기 패턴:**
```
Packet 1-8: 버퍼에 누적 (8KB = 8192 bytes)
→ f_write(8192 bytes) → SD 카드 (512 * 16)

Packet 9-16: 버퍼에 누적 (8KB)
→ f_write(8192 bytes) → SD 카드

...

Packet 12360 (마지막): 버퍼에 1024 bytes 남음
→ 512 배수 패딩: 1024 bytes (패딩 없음)
→ f_write(1024 bytes) + f_sync() + f_close() → SD 카드
```

- ✅ 8개 패킷마다 한 번 쓰기 (1/8 부하)
- ✅ 항상 512 배수로 쓰기 (SD 카드 최적화)
- ✅ 마지막 데이터도 512 배수 보장
- ✅ 정상 종료 보장 (f_sync + f_close)

## SD 카드 쓰기 횟수 비교

**12.7MB WAV 파일 업로드 시:**

| 항목 | 기존 | 개선 후 | 개선율 |
|------|------|---------|--------|
| 총 패킷 수 | 12,700개 | 12,700개 | - |
| f_write() 호출 횟수 | 12,700회 | 1,588회 | **87.5% 감소** |
| 평균 쓰기 크기 | 1024 bytes | 8192 bytes | 8배 증가 |
| f_sync() 호출 횟수 | 13회 (1MB마다) | 13회 | 동일 |
| 512 배수 보장 | ✅ | ✅ | 동일 |
| 마지막 패딩 | ❌ | ✅ | 개선 |

**SD 카드 수명 보호:**
- 쓰기 횟수 87.5% 감소 → SD 카드 wear leveling 부담 대폭 감소
- 512 배수 쓰기 → SD 카드 내부 블록 정렬 최적화
- f_sync() 후 f_close() → FAT 손상 방지

## 디버그 로그 예시

### 정상 전송 완료

```
[DEBUG] Y-MODEM: waiting for sender (sending 'C' every 1 sec)...
[DEBUG] Y-MODEM: first packet received after 0 retries
[DEBUG] Y-MODEM: file info packet ACKed
[DEBUG] Y-MODEM: starting data reception...

(8KB 버퍼링으로 패킷 1-8 누적, SD 쓰기)
(8KB 버퍼링으로 패킷 9-16 누적, SD 쓰기)
...

[DEBUG] Final write: 3584 bytes data + 0 bytes padding = 3584 bytes written
INFO: Transfer complete (13017088 bytes)
[DEBUG] File closed successfully
```

### 512 배수 패딩 발생

```
[DEBUG] Final write: 1500 bytes data + 36 bytes padding = 1536 bytes written
INFO: Transfer complete (12701500 bytes)
[DEBUG] File closed successfully
```

### 에러 처리

```
[ERROR] SD write failed: fres=3, written=0/8192
ERR 405 SD write error
```

## 빌드 결과

```
✅ Build successful (경고 없음)
   text: 146,984 bytes (+800 bytes from retry logic)
   data: 356 bytes
   bss: 204,400 bytes
```

## 테스트 권장 사항

### 1. 정상 전송 테스트

```bash
# Python 업로드
python upload_wav.py COM10 0 test.wav

# 확인 사항:
# - 전송 완료 메시지 확인
# - SD 카드에 파일 생성 확인
# - 파일 크기 일치 확인
# - WAV 재생 가능 여부 확인
```

### 2. SD 카드 수명 모니터링

- 전송 전후 SD 카드 응답 속도 비교
- PC 연결 시 팝업 속도 확인
- 장기 사용 후 SD 카드 상태 확인

### 3. 에러 복구 테스트

- USB 케이블 흔들기 → 재시도 로직 동작 확인
- 전송 중단 후 재시작 → f_sync() 정상 동작 확인

## 관련 문서

- `YMODEM_RETRY_IMPLEMENTATION_20251106_141831.md` - Y-MODEM 재시도 로직
- `PC_UART_PROTOCOL.md` - UART 명령 프로토콜
- `YMODEM_UPLOAD_GUIDE.md` - Y-MODEM 타이밍 가이드
- `USB_CDC_TIMEOUT_FIX.md` - USB CDC 타임아웃 해결

## 기술 참고

### FatFs f_write() 동작

- f_write()는 내부 버퍼에 데이터를 쓴 후 디스크에 flush
- 512 배수로 쓰면 섹터 경계에 정렬되어 효율적
- f_sync()는 내부 버퍼를 강제로 디스크에 flush

### SD 카드 내부 구조

- SD 카드는 512 bytes 섹터 단위로 동작
- 512 배수가 아닌 쓰기 시 내부 Read-Modify-Write 발생 → 성능 저하
- wear leveling을 위해 쓰기 횟수 최소화 필요

### STM32H7 DMA-safe 메모리

- sdmmc1_buffer는 `.ram_d1_dma` 섹션 (캐시 OFF)
- DMA 일관성 보장을 위해 32 bytes 정렬
- 32KB 크기이므로 8KB 버퍼링에 충분

## 추가 최적화 가능성

### 더 큰 버퍼 사용 (선택사항)

현재 8KB 사용 중, 필요 시 16KB 또는 32KB로 증가 가능:

```c
#define SD_WRITE_BUFFER_SIZE  16384  // 16KB = 512 * 32
#define SD_WRITE_BUFFER_SIZE  32768  // 32KB = 512 * 64 (전체 sdmmc1_buffer)
```

**트레이드오프:**
- 장점: SD 카드 쓰기 횟수 더욱 감소
- 단점: 재시도 시 재전송해야 할 데이터 증가

**권장:** 현재 8KB 사용 후 문제 없으면 유지

## 결론

SD 카드 수명 보호를 위한 3가지 핵심 개선:
1. ✅ 8KB 버퍼링으로 쓰기 횟수 87.5% 감소
2. ✅ 512 배수 패딩으로 섹터 정렬 최적화
3. ✅ f_sync() + f_close() 정상 종료로 FAT 손상 방지

사용자 경험 기반 권장 사항을 모두 반영하여 SD 카드 안정성과 수명을 향상시켰습니다.
