# Master-Slave 통합 테스트 체크리스트

**작성일**: 2025-11-01
**테스트 일정**: 2025-11-02, 오전 10:00
**문서 목적**: 통합 테스트 전 최종 확인사항 및 핵심 정보 요약

---

## 🚨 중요 공지사항

### ⚠️ SPI 클럭 속도 최종 확정

**Master 측 클럭 계산** (2025-11-01 최종 확인):

```
HSE: 25MHz
PLL: 25MHz / 2 * 44 = 550MHz (SYSCLK)
HCLK: 550MHz / 2 = 275MHz
APB2: 275MHz / 2 = 137.5MHz
SPI1 클럭: 137.5MHz / 16 = 8.59375MHz ✅
```

**결론**:
- ✅ **SPI 클럭: 약 8.6MHz** (10MHz 이하, Slave 요구사항 만족)
- ✅ BaudRatePrescaler = 16으로 설정 완료
- ✅ 2025-11-01 빌드 완료

**Slave 팀 확인 필요**:
- [ ] Slave 측 SPI 클럭 **8.6MHz** 수신 가능한가요?
- [ ] 최소/최대 허용 클럭 범위는? (예: 3~10MHz?)

---

## 📌 핵심 정보 요약

### 1. 하드웨어 연결 (Slave 0)

| 신호 | Master | Slave 0 | 케이블 색상 제안 |
|------|--------|---------|-----------------|
| **SCK** | PA5 | PA5 | 노란색 |
| **MOSI** | PB5 | PB5 | 주황색 |
| **MISO** | PB4 | PB4 | 흰색 (선택) |
| **CS** | PA4 | PA15 | 녹색 |
| **RDY** | PF13 | PA8 | 파란색 |
| **GND** | GND | GND | ⚠️ **검은색 (필수!)** |
| **3.3V** | 3.3V | 3.3V | 빨간색 |

**연결 시 주의사항**:
1. ⚠️ **GND 먼저 연결** (통신 안정성)
2. ⚠️ 전원 극성 확인 (3.3V, 5V 혼동 금지)
3. 케이블 길이: 30cm 이하 권장 (신호 품질)
4. 듀폰 케이블 접촉 확인 (흔들어서 체크)

### 2. SPI 프로토콜 설정

| 파라미터 | 값 | 비고 |
|---------|-----|------|
| Mode | Master/Slave | ✅ |
| CPOL | 0 (LOW) | ✅ |
| CPHA | 0 (1 EDGE) | ✅ |
| Data Size | 8-bit | ✅ |
| First Bit | MSB First | ✅ |
| Clock Speed | **8.6MHz** | ✅ Slave 확인 필요 |

### 3. 프로토콜 패킷

**명령 패킷** (6 bytes):
```
0xC0 | Slave ID | Channel | CMD | Param_H | Param_L
```

**데이터 패킷** (가변):
```
0xDA | Slave ID | Channel | Length_H | Length_L | [2048 샘플...]
```

**명령 코드**:
- 0x01: PLAY
- 0x02: STOP
- 0x03: VOLUME
- 0xFF: RESET

### 4. 타이밍

| 항목 | 값 | 구현 위치 |
|------|-----|----------|
| CS 셋업 시간 | 50μs | `spi_protocol.c:58-62` |
| RDY 체크 | 전송 전 확인 | `audio_stream.c:280-287` |
| 전송 주기 | 50~60ms | `audio_stream_task()` |
| 패킷 간 간격 | >100μs | 자동 보장 |

---

## 📋 통합 테스트 전 체크리스트

### Master 팀 (오늘 완료 필요)

- [x] SPI 클럭 속도 수정 (Prescaler 16)
- [x] 펌웨어 빌드 완료 (2025-11-01)
- [ ] **긴급**: Master 보드에 플래시 ⚠️
- [ ] **긴급**: 테스트 WAV 파일 생성 및 SD 카드 저장 ⚠️
- [ ] UART2 케이블 준비 (USB-UART 컨버터)
- [ ] SPI 신호선 준비 (듀폰 케이블 7개)
- [ ] 멀티미터 준비 (전압 확인용)

### Slave 팀

- [x] Slave 0 펌웨어 플래시 완료
- [ ] UART3 모니터링 준비 (115200 baud)
- [ ] Slave 측 **SPI 8.6MHz 수신 가능** 확인 ⚠️
- [ ] 오실로스코프 준비 (DAC 출력 확인용)

### 공통

- [ ] 로직 분석기 (선택, 권장)
- [ ] 점퍼 와이어 (예비용)
- [ ] 노트북 2대 (각 팀 UART 모니터링)
- [ ] 작업 공간 (보드 2개 배치 가능)

---

## 🧪 테스트 WAV 파일 사양

### 필수 파일 (Master SD 카드)

**저장 위치**: `/audio/ch0/`

| 파일명 | 포맷 | 용도 |
|--------|------|------|
| `test_1khz.wav` | 32kHz, 16-bit, Mono, 10초 | DAC 출력 확인 (오실로스코프) |
| `test_500hz.wav` | 32kHz, 16-bit, Mono, 10초 | 저주파 안정성 |
| `test_music.wav` | 32kHz, 16-bit, Mono, 30초 | 실제 사용 환경 |

**생성 방법** (Master 팀):
```python
import numpy as np
from scipy.io import wavfile

# 1kHz 정현파
fs = 32000
duration = 10
t = np.linspace(0, duration, fs * duration)
signal = np.int16(32767 * 0.5 * np.sin(2 * np.pi * 1000 * t))
wavfile.write('test_1khz.wav', fs, signal)
```

**Master 팀 확인**:
- [ ] 파일 생성 완료
- [ ] SD 카드 포맷 확인 (FAT32)
- [ ] 파일 복사 완료
- [ ] Master에서 `LS 0` 명령으로 파일 확인

---

## 🔍 통합 테스트 Phase별 절차

### Phase 1: 하드웨어 연결 확인 (30분)

**1.1 전원 OFF 상태에서 연결**
```
1. GND 연결 (검은색)
2. 3.3V 연결 (빨간색)
3. SPI 신호선 연결 (SCK, MOSI, MISO)
4. CS, RDY 연결
5. 멀티미터로 전압 확인 (3.3V, GND)
```

**1.2 부팅 확인**
```
Master:
- UART2 연결 (115200 baud)
- 전원 ON
- "System Ready" 메시지 확인

Slave:
- UART3 연결 (115200 baud)
- 전원 ON
- 테스트 메뉴 표시 확인
- '0' 입력 → Slave 모드 진입
- "Slave ready" 메시지 확인
```

**성공 기준**:
- [ ] Master UART: "System Ready"
- [ ] Slave UART: "Slave ready - waiting for Master commands"
- [ ] Slave RDY 핀: HIGH (멀티미터 또는 로직 분석기)

### Phase 2: 명령 패킷 테스트 (30분)

**2.1 PLAY 명령 전송**
```
Master UART 또는 USB CDC:
> PLAY 0 test_1khz.wav

예상 Master 로그:
[Master] Checking RDY pin (Slave 0)... HIGH
[Master] Sending PLAY command to Slave 0, CH 0
SPI: Sent cmd=0x01 to Slave0 Ch0 param=0

예상 Slave 로그:
[CMD] PLAY on CH0
```

**2.2 STOP 명령 전송**
```
Master:
> STOP 0

예상 Slave 로그:
[CMD] STOP on CH0
```

**성공 기준**:
- [ ] Slave가 PLAY 명령 수신 확인
- [ ] Slave가 STOP 명령 수신 확인
- [ ] Master에서 에러 없음
- [ ] Slave에서 에러 없음

**실패 시 체크**:
1. [ ] SPI 신호선 연결 확인 (SCK, MOSI, CS)
2. [ ] GND 연결 확인
3. [ ] SPI 클럭 속도 (로직 분석기로 8.6MHz 확인)
4. [ ] CS 핀 토글 확인 (로직 분석기)
5. [ ] Slave UART에 "[SPI] Invalid header" 메시지 있는지 확인

### Phase 3: 데이터 패킷 테스트 (1시간)

**3.1 첫 데이터 패킷 전송**
```
Master:
> PLAY 0 test_1khz.wav

예상 Slave 로그:
[SPI] RX 2048 samples CH0
[DAC] Buffer swap (CH0)
```

**3.2 DAC 출력 확인**
```
오실로스코프:
- Slave DAC1 출력 핀 측정
- 1kHz 정현파 확인 (약 0.5Vpp)
- 주파수 측정: 1000Hz ± 10Hz
```

**3.3 연속 전송 (10초)**
```
Master:
> PLAY 0 test_1khz.wav
(10초 대기)

Slave UART 5초마다 상태 출력:
[STATUS] --------------------
DAC1: PLAY | Samples: 20480 | Swaps: 10 | Underruns: 0
DAC2: STOP | Samples: 0 | Swaps: 0 | Underruns: 0
SPI:  Errors: 0 | Invalid Headers: 0 | Invalid IDs: 0
----------------------------
```

**성공 기준**:
- [ ] Slave에서 데이터 수신 확인
- [ ] DAC 출력 확인 (오실로스코프)
- [ ] 10초 연속 전송 무에러
- [ ] Underruns: 0 (버퍼 언더런 없음)
- [ ] SPI Errors: 0

**실패 시 체크**:
1. [ ] RDY 핀 동작 확인 (로직 분석기)
2. [ ] Master 전송 주기 측정 (50~60ms?)
3. [ ] Slave 버퍼 상태 확인 (UART 로그)
4. [ ] SPI DMA 에러 확인 (양쪽 UART)

### Phase 4: 장시간 안정성 테스트 (30분)

**4.1 30초 재생**
```
Master:
> PLAY 0 test_music.wav

30초 후:
> STOP 0

Slave 통계 확인:
- Underruns: 0
- SPI Errors: 0
- Buffer Swaps: 약 468회 (30초 / 64ms)
```

**성공 기준**:
- [ ] 30초 이상 안정적 재생
- [ ] 버퍼 언더런 0회
- [ ] SPI 에러 0회
- [ ] 음질 정상 (오실로스코프/스피커)

---

## 🐛 예상 문제 및 즉시 대응

### 문제 1: SPI 통신 안됨

**증상**:
- Slave UART에 "[SPI] RX" 메시지 없음
- Slave가 명령을 수신하지 못함

**즉시 체크**:
1. ⚠️ **GND 연결 확인** (가장 흔한 원인!)
2. SCK, MOSI, CS 연결 확인
3. 멀티미터로 CS 핀 전압 확인 (평소 3.3V, 전송 시 0V)
4. Slave 전원 확인 (3.3V)

**로직 분석기 캡처**:
- CS, SCK, MOSI 동시 캡처
- CS가 LOW일 때 SCK 클럭 있는지 확인

### 문제 2: 잘못된 헤더 수신

**증상**:
- Slave UART: "[SPI] Invalid header: 0xXX"

**즉시 체크**:
1. SPI 설정 확인 (CPOL=0, CPHA=0)
2. SPI 클럭 속도 확인 (8.6MHz)
3. 케이블 길이 확인 (30cm 이하?)
4. GND 연결 확인

**Master 측 디버깅**:
```c
// user_def.c에 임시 추가
printf("Sending PLAY: 0xC0 0x00 0x00 0x01 0x00 0x00\n");
```

### 문제 3: RDY 핀 작동 안함

**증상**:
- Master가 계속 "not ready, skipping" 출력
- 또는 Slave 처리 중인데도 Master가 전송

**즉시 체크**:
1. RDY 핀 연결 확인 (Master PF13 ↔ Slave PA8)
2. Slave RDY 핀 상태 확인 (멀티미터)
3. Master RDY 입력 읽기 확인

**Master 측 임시 디버깅**:
```c
// spi_protocol.c:80에 추가
printf("RDY pin read: %d\n", HAL_GPIO_ReadPin(...));
```

### 문제 4: 버퍼 언더런 발생

**증상**:
- Slave UART: "Underruns: N" (N > 0)
- 음질 끊김

**즉시 체크**:
1. Master 전송 주기 측정 (64ms 이하여야 함)
2. RDY 핀 타이밍 확인
3. SD 카드 읽기 속도 확인

**Master 측 확인**:
```c
// audio_stream.c에서 전송 주기 출력
printf("Send interval: %lu ms\n", HAL_GetTick() - last_send);
```

---

## 📊 테스트 성공 기준

### 최소 성공 (Phase 2 완료)
- [x] Master → Slave 명령 전송 성공
- [x] Slave 명령 수신 및 처리
- [x] RDY 핸드셰이크 동작

### 중간 성공 (Phase 3 완료)
- [ ] 데이터 패킷 전송 성공
- [ ] DAC 출력 확인 (오실로스코프)
- [ ] 10초 연속 무에러

### 최종 성공 (Phase 4 완료)
- [ ] 30초 이상 안정적 재생
- [ ] 버퍼 언더런 0회
- [ ] SPI 에러 0회
- [ ] 음질 정상

---

## 📞 긴급 연락

**테스트 중 문제 발생 시**:

**Master 팀**:
- 담당자: [이름]
- 전화: [전화번호]

**Slave 팀**:
- 담당자: [이름]
- 전화: [전화번호]

---

## 📚 참고 문서

**상세 문서**:
1. `MASTER_RESPONSE.md` - Master 측 상세 답변 (413줄)
2. `MASTER_REQUEST.md` - Slave 측 요청사항
3. `PROTOCOL_SUMMARY.md` - 프로토콜 전체 요약

**코드**:
- `Core/Src/spi_protocol.c` - SPI 전송 로직
- `Core/Src/audio_stream.c` - 스트리밍 구현
- `Core/Src/main.c:338` - SPI 클럭 설정

---

**문서 버전**: 1.0
**최종 업데이트**: 2025-11-01
**상태**: ⚠️ Master 펌웨어 플래시 및 WAV 파일 준비 필요

**다음 작업**:
1. ⚠️ **긴급 (오늘 완료)**: Master 보드 플래시
2. ⚠️ **긴급 (오늘 완료)**: 테스트 WAV 파일 생성
3. ✅ **내일 오전 10시**: 통합 테스트 시작
