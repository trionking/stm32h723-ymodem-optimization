# Y-MODEM 전송 추가 개선 제안

## 작성 일시

2025-11-06 15:05:02

## 현재 상태 (잘 동작 중)

**사용자 피드백:**
> "많이 좋아 졌어. 중간에 끊겨도 약간의 시간이 지난 뒤 재시작하고 끝까지 잘 써지는 것 같아."

**구현된 기능:**
- ✅ 재시도 로직 (타임아웃 5회, NAK 10회)
- ✅ 8KB 버퍼링 (SD 쓰기 횟수 87.5% 감소)
- ✅ 512 배수 패딩
- ✅ f_sync() + f_close() 정상 종료
- ✅ 논블럭 printf (UART2 DMA TX)

**성능:**
- 12.7MB 파일 전송: 약 1.6분 (36% 빠름)
- 재시도로 인한 중단 복구 성공

## 추가 개선 제안

### 1. 진행률 표시 복원 (우선순위: 높음)

**현재 상태:**
```c
// 진행 상황 로그 제거 (printf 오버헤드로 인한 타임아웃 방지)
// if (packet_number % 500 == 0) {
//     printf("[DEBUG] Progress: packet %d, total=%lu bytes\r\n", packet_number, total_bytes);
// }
```

**문제:**
- 진행률이 표시되지 않아 전송 상태 파악 어려움
- 12.7MB 파일은 1.6분 소요되므로 사용자가 대기 중 불안

**해결책:**
- 논블럭 printf 적용했으므로 진행률 표시 복원 가능
- 단, 너무 자주 출력하면 여전히 오버헤드 발생

**권장 구현:**
```c
// 1MB마다 진행률 출력 (1000 패킷 = 약 1MB)
if (packet_number % 1000 == 0) {
    uint32_t progress_pct = (total_bytes * 100) / expected_file_size;
    printf("[INFO] Progress: %lu/%lu bytes (%lu%%)\r\n",
           total_bytes, expected_file_size, progress_pct);
}
```

**장점:**
- 사용자가 전송 상태 파악 가능
- 1MB마다 출력하므로 오버헤드 최소화 (12회 출력)
- 논블럭 printf로 타이밍 영향 없음

**구현 복잡도:** 낮음 (주석 제거 + 파일 크기 전달)

---

### 2. 전송 통계 추가 (우선순위: 중간)

**현재 상태:**
- 전송 완료 시 총 바이트만 표시
- 전송 시간, 속도, 재시도 횟수 미표시

**제안 기능:**
```c
// 전송 시작 시간 기록
uint32_t start_tick = HAL_GetTick();

// 전송 완료 시
uint32_t elapsed_ms = HAL_GetTick() - start_tick;
uint32_t speed_kbps = (total_bytes * 8) / elapsed_ms;  // Kbps

printf("[INFO] Transfer complete!\r\n");
printf("       Size: %lu bytes (%.2f MB)\r\n",
       total_bytes, (float)total_bytes / (1024.0 * 1024.0));
printf("       Time: %lu.%03lu sec\r\n",
       elapsed_ms / 1000, elapsed_ms % 1000);
printf("       Speed: %lu Kbps (%.2f KB/s)\r\n",
       speed_kbps, (float)total_bytes / elapsed_ms);
printf("       Retries: timeout=%d, NAK=%d\r\n",
       timeout_retry_count, nak_retry_count);
```

**출력 예시:**
```
[INFO] Transfer complete!
       Size: 13017088 bytes (12.41 MB)
       Time: 96.234 sec
       Speed: 1082 Kbps (132.5 KB/s)
       Retries: timeout=2, NAK=0
```

**장점:**
- 전송 품질 모니터링
- SD 카드 성능 파악
- 네트워크/USB 안정성 평가

**구현 복잡도:** 낮음 (변수 추가 + printf)

---

### 3. f_sync() 빈도 최적화 (우선순위: 낮음)

**현재 설정:**
```c
// 주기적으로 f_sync() 호출 (1MB마다)
if (total_bytes % (1024 * 1024) == 0) {
    f_sync(&file);
}
```

**트레이드오프 분석:**

| f_sync() 빈도 | 장점 | 단점 |
|--------------|------|------|
| 매 패킷 (1KB) | 데이터 손실 최소 | 매우 느림 (SD 카드 부담) |
| 100 패킷 (100KB) | 데이터 손실 적음 | 느림 |
| 1000 패킷 (1MB) | **현재 설정** 균형 | 중단 시 최대 1MB 손실 |
| 전송 완료 시만 | 가장 빠름 | 중단 시 전체 손실 |

**사용자 피드백 기반:**
> "중간에 끊겨도 약간의 시간이 지난 뒤 재시작하고 끝까지 잘 써지는 것 같아"

→ **재시도 로직이 잘 작동하므로 f_sync() 빈도를 줄여도 됨**

**제안 옵션:**

**옵션 A: f_sync() 2MB마다 (속도 우선)**
```c
if (total_bytes % (2 * 1024 * 1024) == 0) {
    f_sync(&file);
}
```
- 전송 시간 5-10% 단축
- 중단 시 최대 2MB 재전송 (재시도 로직으로 복구)

**옵션 B: f_sync() 완전 제거 (최대 속도)**
```c
// f_sync()는 전송 완료 시에만 (ymodem.c:369)
```
- 전송 시간 10-15% 단축
- 중단 시 처음부터 재전송 (재시도 로직으로 복구)

**권장:** 옵션 A (2MB마다)
- 속도 향상과 안정성 균형
- 재시도 로직이 있으므로 안전

**구현 복잡도:** 매우 낮음 (숫자만 변경)

---

### 4. ACK 지연 최적화 (우선순위: 낮음)

**현재 설정:**
```c
// 추가 안정화 지연: 다음 패킷 수신 준비 시간
// transmit_byte(2ms) + 여기서 8ms = 총 10ms (안정성 우선)
HAL_Delay(8);
```

**분석:**
- 매 패킷마다 10ms 지연 (transmit_byte 2ms + HAL_Delay 8ms)
- 12,700 패킷 * 10ms = 127초 = 대부분의 전송 시간
- 이 지연은 YMODEM_UPLOAD_GUIDE.md에서 권장

**제안:**
- 8KB 버퍼링을 적용했으므로 8개 패킷 중 7개는 SD 쓰기 없음
- 빠른 패킷(1-7)은 지연 불필요
- 느린 패킷(8)만 지연 필요

```c
// ACK 전송
transmit_byte(huart, YMODEM_ACK);

// 8KB SD 쓰기 직후에만 안정화 지연 (다음 버퍼링 시작 준비)
if (write_buffer_offset == 0) {  // 방금 SD 쓰기 완료
    HAL_Delay(8);  // 다음 8개 패킷 수신 준비
}
// 버퍼링 중인 패킷은 지연 없음 (빠름)
```

**효과:**
- 패킷 1-7: 지연 0ms (빠름)
- 패킷 8: 지연 8ms (SD 쓰기 완료 후)
- 평균 지연: (7 * 0 + 1 * 8) / 8 = 1ms
- **전송 시간 약 80% 단축 가능!**

**주의 사항:**
- .doc\YMODEM_UPLOAD_GUIDE.md 확인 필요
- USB CDC 안정성 테스트 필요
- 재시도 로직이 있으므로 안전

**구현 복잡도:** 낮음 (조건 추가)

---

### 5. SD 카드 에러 복구 (우선순위: 중간)

**현재 동작:**
```c
if (fres != FR_OK || bytes_written != write_buffer_offset) {
    // 쓰기 에러
    printf("[ERROR] SD write failed: fres=%d\r\n", fres);
    transmit_byte(huart, YMODEM_CAN);
    uart_send_error(405, "SD write error");
    result = YMODEM_ERROR;
    break;  // 즉시 종료
}
```

**문제:**
- SD 카드 임시 에러 (busy, 전압 저하 등) 시 즉시 전송 취소
- 복구 가능한 에러도 재시도 없이 실패

**제안:**
```c
// SD 쓰기 재시도 (최대 3회)
uint8_t sd_write_retries = 0;
while (sd_write_retries < 3) {
    fres = f_write(&file, sdmmc1_buffer, write_buffer_offset, &bytes_written);

    if (fres == FR_OK && bytes_written == write_buffer_offset) {
        break;  // 성공
    }

    sd_write_retries++;
    printf("[WARN] SD write failed (retry %d/3): fres=%d\r\n",
           sd_write_retries, fres);

    // 짧은 지연 후 재시도
    HAL_Delay(100);
}

if (sd_write_retries >= 3) {
    // 3회 실패 시 전송 취소
    transmit_byte(huart, YMODEM_CAN);
    uart_send_error(405, "SD write error after retries");
    result = YMODEM_ERROR;
    break;
}
```

**장점:**
- 임시 SD 카드 에러 복구
- 전송 안정성 향상

**구현 복잡도:** 낮음 (while 루프 추가)

---

### 6. 파일 크기 검증 (우선순위: 낮음)

**현재 동작:**
- 파일 정보 패킷에서 파일 크기 수신
- 하지만 검증하지 않음

**제안:**
```c
// 파일 정보 패킷에서 파일 크기 추출 (ymodem.c:119-137)
uint32_t expected_file_size = 0;
// 파일 크기 파싱 (파일명\0 뒤에 숫자)

// EOT 수신 시 크기 검증
if (header == YMODEM_EOT) {
    if (expected_file_size > 0 && total_bytes != expected_file_size) {
        printf("[WARN] File size mismatch: expected=%lu, received=%lu\r\n",
               expected_file_size, total_bytes);
        // 경고만 출력, 전송은 계속
    }

    // 남은 버퍼 쓰기...
}
```

**장점:**
- 파일 무결성 검증
- 전송 중 패킷 손실 감지

**구현 복잡도:** 중간 (파일 크기 파싱 추가)

---

### 7. 전송 재개 기능 (우선순위: 매우 낮음)

**현재 동작:**
- 전송 실패 시 처음부터 재시작
- 12.7MB 파일도 처음부터

**제안:**
- Y-MODEM-G 또는 ZMODEM 프로토콜로 업그레이드
- 마지막 성공 패킷부터 재개

**문제:**
- 구현 복잡도 매우 높음
- Python 코드도 수정 필요
- 현재 재시도 로직으로 충분히 동작

**권장:** 구현 안 함 (현재 재시도 로직으로 충분)

---

## 추천 개선 우선순위

### Phase 1: 사용자 경험 개선 (즉시 적용 권장)

1. **진행률 표시 복원**
   - 1MB마다 진행률 출력
   - 사용자 불안 해소
   - 구현 시간: 5분

2. **전송 통계 추가**
   - 전송 완료 시 속도/시간 표시
   - 품질 모니터링
   - 구현 시간: 10분

### Phase 2: 성능 최적화 (테스트 후 적용)

3. **ACK 지연 최적화**
   - 버퍼링 중인 패킷은 지연 없음
   - 전송 시간 80% 단축 가능
   - 구현 시간: 5분
   - 테스트 필요: USB CDC 안정성

4. **f_sync() 빈도 조정**
   - 2MB마다로 변경
   - 전송 시간 5-10% 단축
   - 구현 시간: 1분

### Phase 3: 안정성 향상 (선택적)

5. **SD 카드 에러 복구**
   - SD 쓰기 재시도 3회
   - 임시 에러 복구
   - 구현 시간: 10분

6. **파일 크기 검증**
   - 전송 무결성 확인
   - 구현 시간: 20분

## 테스트 권장 순서

**Phase 1 구현 후:**
```bash
python upload_wav.py COM10 0 test.wav
```
- 진행률 출력 확인
- 전송 통계 확인

**Phase 2 구현 후:**
```bash
# 여러 번 테스트하여 안정성 확인
for i in {1..10}; do
    python upload_wav.py COM10 0 test.wav
    echo "Test $i completed"
done
```
- 전송 시간 측정
- 에러 발생 여부 확인
- USB 케이블 흔들기 테스트

**Phase 3 구현 후:**
- SD 카드 불량 섹터 테스트
- 전압 저하 시뮬레이션

## 결론

**즉시 적용 권장 (Phase 1):**
1. ✅ 진행률 표시 복원
2. ✅ 전송 통계 추가

**테스트 후 적용 (Phase 2):**
3. ⚠️ ACK 지연 최적화 (큰 성능 향상, 안정성 테스트 필요)
4. ⚠️ f_sync() 빈도 조정 (작은 성능 향상)

**선택적 (Phase 3):**
5. 📋 SD 카드 에러 복구
6. 📋 파일 크기 검증

현재 시스템이 잘 동작하고 있으므로, Phase 1만 적용해도 충분합니다. Phase 2는 전송 시간을 크게 단축할 수 있지만 안정성 테스트가 필요합니다.
