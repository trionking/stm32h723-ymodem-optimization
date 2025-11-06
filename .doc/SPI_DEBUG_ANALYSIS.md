# SPI 수신 문제 상세 분석

## 날짜: 2025-11-04

## 문제 현상
- 슬레이브가 6바이트 명령 패킷 중 **1바이트(Header)만 수신**
- 나머지 5바이트는 수신되지 않음
- 간헐적으로 동작 (3번째 또는 4번째 시도에 성공)

## 슬레이브 상태 분석 (디버그 로그)

```
[SPI] RX Start: expecting 6 bytes (header + 5 body)
[SPI] Byte[0] = 0xC0 (header)
[DEBUG] Calling HAL_SPI_Receive_IT for 5 bytes...
[DEBUG] SPI State before: 0x01    ← HAL_SPI_STATE_READY (정상)
[DEBUG] HAL_SPI_Receive_IT returned: 0    ← HAL_OK (성공)
[DEBUG] SPI State after: 0x04     ← HAL_SPI_STATE_BUSY_RX (수신 대기)
[DEBUG] SPI ErrorCode: 0x00000000 ← 에러 없음

... (이후 Byte[1]~[5] 출력 없음 = 수신 완료 인터럽트 미발생)
```

## 결론

### ✅ 슬레이브는 정상 동작 중
1. SPI 인터럽트: 정상 활성화 (우선순위 1)
2. Header 바이트: 정상 수신 (0xC0)
3. `HAL_SPI_Receive_IT(5바이트)` 호출: 성공 (반환값 HAL_OK)
4. SPI 상태: READY → BUSY_RX 정상 전환
5. SPI 에러: 없음 (0x00000000)

### ❌ 문제: 나머지 5바이트 수신 불가

**SPI 슬레이브 모드 동작 원리:**
- 슬레이브는 **마스터가 생성하는 SCLK**에 동기화하여 데이터 수신
- 마스터가 SCLK를 보내지 않으면 슬레이브는 데이터를 받을 수 없음
- `HAL_SPI_Receive_IT(5바이트)` 호출 후 **40클럭(5바이트×8비트)을 기다리는 중**

## 마스터 측 확인 요청 사항

### 1. 오실로스코프로 신호 확인 (필수)

다음 신호를 측정해주세요:

#### CS (Chip Select) 신호
- [ ] CS가 **6바이트 전송 동안 LOW 유지**되는지?
- [ ] CS가 몇 바이트 후에 HIGH로 올라가는지?

#### SCLK (Serial Clock) 신호
- [ ] SCLK가 **48클럭 (6바이트 × 8비트)** 생성되는지?
- [ ] 실제 클럭 수를 세어주세요
- [ ] 몇 클럭 후에 SCLK가 멈추는지?

#### MOSI (Master Out Slave In) 신호
- [ ] MOSI에 실제로 6바이트가 출력되는지?
- [ ] 육안으로 보이는 비트 패턴 수는?

### 2. 마스터 코드 확인

#### SPI 전송 코드 예상:
```c
// 잘못된 예 (1바이트씩 전송)
HAL_SPI_Transmit(&hspi, &header, 1, timeout);  // CS HIGH로 복귀?
HAL_SPI_Transmit(&hspi, &body, 5, timeout);    // CS 다시 LOW?

// 올바른 예 (6바이트 한번에)
uint8_t packet[6] = {0xC0, 0x00, 0x00, 0x01, 0x00, 0x00};
HAL_SPI_Transmit(&hspi, packet, 6, timeout);   // CS LOW 유지
```

#### 확인 사항:
- [ ] CS를 GPIO로 제어하는지, 하드웨어 NSS를 사용하는지?
- [ ] 6바이트를 한 번에 전송하는지, 나누어 전송하는지?
- [ ] CS를 LOW로 한 후 정확히 몇 바이트를 전송하는지?
- [ ] CS를 언제 HIGH로 올리는지?

### 3. 마스터 SPI 설정 확인

```
현재 슬레이브 설정:
- Mode: Slave
- CPOL: 0 (Low)
- CPHA: 0 (1st Edge)
- DataSize: 8-bit
- FirstBit: MSB
- NSS: Software (GPIO EXTI로 제어)

마스터 설정과 일치하는지 확인:
- [ ] CPOL/CPHA 일치
- [ ] DataSize 일치
- [ ] FirstBit 일치
```

## 슬레이브 측 추가 정보

### EXTI 인터럽트 (CS falling edge)
- ✅ 정상 동작
- CS falling edge 감지 시 `HAL_SPI_Receive_IT(&header, 1)` 호출
- Header 바이트 수신 성공

### SPI 인터럽트
- ✅ 정상 활성화 (NVIC 우선순위 1)
- Header 수신 완료 시 콜백 발생
- **5바이트 수신 완료 인터럽트는 발생하지 않음** ← SCLK 없음

### 간헐적 성공의 원인
- 3번째 또는 4번째 시도에서 간헐적으로 Header가 2번 수신됨
- 이는 타이밍 또는 노이즈 문제일 수 있음
- 하지만 여전히 5바이트 수신은 실패

## 권장 조치

1. **오실로스코프로 CS, SCLK, MOSI 신호 측정** (가장 중요!)
2. 마스터 코드에서 6바이트를 **한 번의 SPI 전송**으로 보내는지 확인
3. CS가 6바이트 전송 **전체 동안 LOW 유지**되는지 확인
4. 하드웨어 NSS vs GPIO NSS 제어 방식 확인

## 예상 결과

오실로스코프로 측정 시 다음을 확인할 수 있을 것:
- CS가 1바이트(8클럭) 후 HIGH로 올라가는 것을 발견
- 또는 SCLK가 8클럭만 생성되고 멈추는 것을 발견

---

**슬레이브는 정상 동작 중입니다. 문제는 마스터가 충분한 SCLK를 생성하지 않는 것으로 판단됩니다.**
