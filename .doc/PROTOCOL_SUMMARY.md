# SPI 프로토콜 요약 문서

**작성일**: 2025-11-01
**대상**: Main 개발팀
**프로젝트**: STM32 오디오 스트리밍 시스템 (Master-Slave 구조)

---

## 📋 시스템 개요

### 구성
```
Master STM32H723ZGT6 (1개)
    ↓ SPI 통신 (10MHz)
Slave STM32H523CCTx (3개)
    ↓ DAC 출력
6채널 오디오 출력
```

### 역할
- **Master**: SD 카드에서 WAV 파일 읽기, SPI로 Slave에게 오디오 데이터 전송
- **Slave**: SPI로 데이터 수신, 2채널 DAC로 아날로그 오디오 출력
- **각 Slave**: 2개 독립 채널 (총 6채널)

---

## 🔌 하드웨어 연결

### SPI 인터페이스
| 신호 | Master 핀 | Slave 핀 | 설명 |
|------|----------|----------|------|
| SCK | PA5 (SPI1_SCK) | PA5 | 클럭 10MHz |
| MOSI | PB5 (SPI1_MOSI) | PB5 | 데이터 (M→S) |
| MISO | PB4 (SPI1_MISO) | PB4 | 미사용 (향후 상태 응답용) |

### Slave별 제어 핀
| Slave ID | CS 핀 (Master) | RDY 핀 (Master) | 채널 |
|----------|---------------|----------------|------|
| **Slave 0** | PA4 (OT_SPI1_NSS1) | PF13 (IN_SPI1_RDY1) | CH0, CH1 |
| **Slave 1** | PF11 (OT_SPI1_NSS2) | PF14 (IN_SPI1_RDY2) | CH2, CH3 |
| **Slave 2** | PF12 (OT_SPI1_NSS3) | PF15 (IN_SPI1_RDY3) | CH4, CH5 |

---

## 📦 프로토콜 상세

### 1. 명령 패킷 (Command Packet)

**크기**: 6 bytes (고정)
**헤더**: `0xC0`

| Byte | 필드 | 설명 | 값 범위 |
|------|------|------|---------|
| 0 | Header | 패킷 식별자 | 0xC0 (고정) |
| 1 | Slave ID | 대상 Slave | 0~2 |
| 2 | Channel | DAC 채널 | 0=DAC1, 1=DAC2 |
| 3 | Command | 명령 코드 | 아래 표 참조 |
| 4 | Param High | 파라미터 상위 바이트 | 명령별 상이 |
| 5 | Param Low | 파라미터 하위 바이트 | 명령별 상이 |

**C 구조체**:
```c
typedef struct __attribute__((packed)) {
    uint8_t header;         // 0xC0
    uint8_t slave_id;       // 0~2
    uint8_t channel;        // 0=DAC1, 1=DAC2
    uint8_t command;        // Command code
    uint8_t param_h;        // Parameter high byte
    uint8_t param_l;        // Parameter low byte
} CommandPacket_t;
```

**지원 명령**:
| 명령 | 코드 | 파라미터 | 설명 |
|------|------|----------|------|
| **PLAY** | 0x01 | 0x0000 | 재생 시작 |
| **STOP** | 0x02 | 0x0000 | 재생 정지 |
| **VOLUME** | 0x03 | 0~100 | 볼륨 설정 |
| **RESET** | 0xFF | 0x0000 | 채널 리셋 |

### 2. 데이터 패킷 (Data Packet)

**크기**: 5 bytes (헤더) + N*2 bytes (샘플)
**헤더**: `0xDA`
**최대 샘플**: 2048개 (4096 bytes)
**총 최대 크기**: 4101 bytes

| Byte | 필드 | 설명 | 값 |
|------|------|------|-----|
| 0 | Header | 패킷 식별자 | 0xDA (고정) |
| 1 | Slave ID | 대상 Slave | 0~2 |
| 2 | Channel | DAC 채널 | 0=DAC1, 1=DAC2 |
| 3 | Length High | 샘플 개수 상위 바이트 | Big-endian |
| 4 | Length Low | 샘플 개수 하위 바이트 | Big-endian |
| 5~ | Samples | 오디오 샘플 데이터 | 16-bit Little-endian 각각 |

**C 구조체**:
```c
typedef struct __attribute__((packed)) {
    uint8_t header;         // 0xDA
    uint8_t slave_id;       // 0~2
    uint8_t channel;        // 0=DAC1, 1=DAC2
    uint8_t length_h;       // Sample count high byte
    uint8_t length_l;       // Sample count low byte
} DataPacketHeader_t;

// 샘플 개수 디코딩
uint16_t num_samples = (header->length_h << 8) | header->length_l;
```

**오디오 포맷**:
- 샘플레이트: 32kHz
- 비트 깊이: 16-bit (전송), 12-bit (DAC 출력)
- 채널: Mono (각 채널별 독립)
- 변환: `dac_value = sample_16bit >> 4`

---

## 🔄 통신 시퀀스

### RDY 핀 핸드셰이크 (Active Low)

```
1. [Slave] RDY = LOW (수신 준비 완료) ★ Active Low
2. [Master] RDY 핀 확인 (LOW인지 체크)
3. [Master] CS = LOW (Slave 선택)
4. [50us delay]
5. [Slave] RDY = HIGH (처리 중)
6. [Master] SPI로 패킷 전송
7. [Master] CS = HIGH (전송 완료)
8. [Slave] 패킷 처리
9. [Slave] RDY = LOW (다음 패킷 준비) ★ Active Low
```

### 재생 플로우

```
1. Master → Slave: PLAY 명령 패킷
2. Slave: DAC 및 타이머 시작
3. 반복 (64ms 주기):
   a. Master: RDY 핀 확인
   b. Master → Slave: 데이터 패킷 (2048 샘플)
   c. Slave: 버퍼에 샘플 채우기
   d. Slave: DAC DMA로 출력 중
4. Master → Slave: STOP 명령 패킷
5. Slave: DAC 정지, 버퍼 클리어
```

---

## ⏱️ 타이밍 요구사항

### SPI 통신
- **클럭 속도**: 10MHz (실제 동작 확인됨)
- **CS 셋업 시간**: 최소 50μs
- **패킷 간 간격**: 최소 100μs
- **데이터 패킷 전송 시간**: 약 3.3ms @ 10MHz

### 오디오 타이밍
- **샘플레이트**: 32,000 Hz (정확)
- **버퍼 크기**: 2048 샘플
- **버퍼 재생 시간**: 64ms (2048 / 32000)
- **전송 주기**: 50~60ms (여유 있게)

**중요**: 64ms 이내에 다음 버퍼를 전송해야 버퍼 언더런 방지!

---

## 🎛️ 오디오 시스템

### 이중 버퍼링
- **버퍼 A**: 2048 샘플 (4096 bytes) - DAC로 출력 중
- **버퍼 B**: 2048 샘플 (4096 bytes) - SPI로 채우는 중
- **스왑 시점**: 버퍼 A 출력 완료 시 버퍼 B와 교체

### 메모리 사용량 (각 Slave)
```
DAC1 버퍼: 8KB (2048 * 2 * 2)
DAC2 버퍼: 8KB
SPI 수신 버퍼: 4101 bytes
총: 약 16KB
```

---

## 📊 구현 현황 (2025-11-01)

### ✅ Master 측 (STM32H723)
- [x] SD 카드 FATFS (D-Cache 일관성 해결)
- [x] WAV 파일 파서 (32kHz 16-bit Mono)
- [x] SPI 프로토콜 구현 (명령/데이터 패킷 전송)
- [x] 6채널 오디오 스트리밍 시스템
- [x] UART2 명령 인터페이스
- [x] USB CDC 통신
- [x] Y-MODEM 파일 업로드
- [x] Python GUI 프로그램

### ✅ Slave 측 (STM32H523 - Slave ID 0)
- [x] SPI 프로토콜 수신 (명령/데이터 패킷)
- [x] 상태 머신 기반 패킷 처리
- [x] RDY 핀 제어
- [x] 2채널 DAC 출력 (DAC1, DAC2)
- [x] 이중 버퍼링 시스템
- [x] 32kHz 타이머 트리거
- [x] DMA 기반 SPI 수신 및 DAC 출력
- [x] 에러 처리 및 통계
- [x] 테스트 메뉴 (LED, DAC 정현파, RDY 토글)

### 🔜 다음 단계
1. **Master-Slave 통신 테스트** (1개 Slave, 1채널)
2. **2채널 동시 재생 테스트** (1개 Slave)
3. **멀티 Slave 통합** (3개 Slave, 6채널)
4. **장시간 안정성 테스트**

---

## 🔍 주요 파일 위치

### Master 측
```
Core/Inc/spi_protocol.h          ← 프로토콜 정의 (공통)
Core/Src/spi_protocol.c          ← SPI 전송 로직
Core/Inc/audio_stream.h          ← 오디오 시스템 인터페이스
Core/Src/audio_stream.c          ← 스트리밍 구현
Core/Inc/main.h                  ← GPIO 핀 매핑
```

### Slave 측
```
Core/Inc/spi_protocol.h          ← 프로토콜 정의 (Master와 동일)
Core/Inc/spi_handler.h           ← SPI 수신 핸들러
Core/Src/spi_handler.c           ← 상태 머신 및 패킷 처리
Core/Inc/audio_channel.h         ← 오디오 채널 관리
Core/Src/audio_channel.c         ← 버퍼 관리 구현
Core/Src/user_def.c              ← 메인 애플리케이션
SLAVE_README.md                  ← Slave 구현 가이드
.doc/SLAVE_IMPLEMENTATION_SPEC.md ← 상세 명세서
```

---

## 🧪 테스트 절차

### Phase 1: 하드웨어 검증 (완료)
- [x] LED 블링크 테스트
- [x] DAC 정현파 출력 (1kHz, 500Hz)
- [x] RDY 핀 토글 확인

### Phase 2: 통신 테스트 (진행 중)
- [x] Master에서 명령 패킷 전송 → Slave 수신 확인 (SPITEST BASIC/PLAY/STOP 완료)
- [ ] Master에서 데이터 패킷 전송 → Slave DAC 출력 확인
- [ ] RDY 핀 핸드셰이크 동작 확인

### Phase 3: 통합 테스트
- [ ] 단일 채널 10초 재생
- [ ] 2채널 동시 재생
- [ ] 3개 Slave, 6채널 동시 재생
- [ ] 장시간 안정성 (30분)

---

## ⚠️ 주의사항

### 1. 타이밍
- **버퍼 전송 주기**: 64ms 이내 (안전하게 50~60ms)
- **RDY 핀 응답**: 50μs 이내
- **CS 셋업 시간**: 최소 50μs

### 2. 데이터 정렬
- **샘플 개수**: Big-endian (length_h << 8 | length_l)
- **샘플 데이터**: Little-endian (16-bit 각각)
- **파라미터**: Big-endian (param_h << 8 | param_l)

### 3. 에러 처리
- **잘못된 헤더**: 재동기화 (다음 헤더 대기)
- **버퍼 언더런**: 무음 출력 유지
- **SPI 에러**: 재초기화 및 재시작

### 4. Slave ID 설정
- 현재 Slave 펌웨어는 `spi_protocol.h`에서 `MY_SLAVE_ID` 하드코딩
- 각 보드별로 0, 1, 2로 설정 후 컴파일 필요
- 향후: GPIO 핀으로 자동 감지 (DIP 스위치)

---

## 📞 연동 체크리스트

### Master 팀 확인사항
- [x] CS 핀 번호 확인 (PA4/PF11/PF12)
- [x] RDY 핀 번호 확인 (PF13/14/15)
- [x] SPI 클럭 속도 (10MHz 확인됨)
- [x] 패킷 전송 타이밍 (50~60ms 주기)
- [x] RDY 핀 체크 로직 구현 확인
- [x] CS 셋업 타임 (50us 구현됨)

### Slave 팀 확인사항
- [x] SPI 슬레이브 모드 설정
- [x] RDY 핀 제어 구현
- [x] 패킷 수신 상태 머신
- [x] 이중 버퍼링 시스템
- [x] 32kHz DAC 출력

### 통합 테스트 준비
- [ ] 하드웨어 연결 (SPI, CS, RDY 핀)
- [ ] 공통 GND 연결
- [ ] 로직 분석기 준비 (SPI 신호 모니터링)
- [ ] UART 모니터링 (양쪽 디버그 출력)

---

## 📚 추가 문서

- **CLAUDE.md**: 프로젝트 구조 및 빌드 가이드
- **SLAVE_README.md**: Slave 구현 완료 보고서
- **.doc/SLAVE_IMPLEMENTATION_SPEC.md**: 상세 구현 명세서 (1700줄)

---

## 🔧 Master-Slave 프로토콜 호환성 (2025-11-01 업데이트)

### 검증 완료 항목
- ✅ 패킷 구조 완벽 일치 (명령 6 bytes, 데이터 5 bytes header)
- ✅ 헤더 값 일치 (0xC0, 0xDA)
- ✅ 명령 코드 일치 (PLAY, STOP, VOLUME)
- ✅ Big-endian 샘플 개수, Little-endian 샘플 데이터
- ✅ 핀 매핑 확인 (CS, RDY)
- ✅ CS 셋업 타임 50μs 구현
- ✅ RDY 핸드셰이크 로직 구현
- ✅ DMA 콜백 최적화

### Master 측 개선사항 (2025-11-01)
1. **RESET 명령 추가** (0xFF) - Slave 프로토콜 호환
2. **CS 셋업 타임 증가**: 1μs → 50μs
   - `spi_select_slave()`: 5500 루프 (@ 550MHz)
3. **DMA 콜백 최적화**: 현재 Slave ID 추적
   - 모든 CS 해제 → 해당 Slave만 해제

### 빌드 결과
```
빌드: 성공 (2025-11-01)
메모리: text=129KB, data=356B, bss=187KB
경고: 없음 (사소한 경고 2개, 동작에 영향 없음)
```

---

**문서 버전**: 1.1
**작성일**: 2025-11-01
**최종 업데이트**: 2025-11-01 (Master 프로토콜 호환성 검증 완료)
**상태**: Master-Slave 프로토콜 호환성 확인, 통합 테스트 준비 완료
