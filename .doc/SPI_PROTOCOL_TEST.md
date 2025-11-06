# SPI 오디오 슬레이브 프로토콜 테스트 가이드

## 프로젝트 개요

STM32H523 기반 듀얼 DAC 오디오 슬레이브 장치
- SPI 슬레이브 모드로 동작
- 32kHz 샘플레이트, 듀얼 채널 12비트 DAC
- EXTI 인터럽트 기반 패킷 수신

---

## SPI 프로토콜 명세

### 1. 패킷 타입

#### 명령 패킷 (Command Packet) - 6 bytes
```
+--------+----------+---------+---------+----------+----------+
| Header | SlaveID  | Channel | Command | Param_H  | Param_L  |
| 1 byte | 1 byte   | 1 byte  | 1 byte  | 1 byte   | 1 byte   |
+--------+----------+---------+---------+----------+----------+
  0xC0     0x00-0xFF  0x00-0x01 0x01-0x04 0x00-0xFF  0x00-0xFF
```

#### 데이터 패킷 (Data Packet) - 5 + N bytes
```
+--------+----------+---------+----------+----------+-----------+
| Header | SlaveID  | Channel | Count_H  | Count_L  | Samples[] |
| 1 byte | 1 byte   | 1 byte  | 1 byte   | 1 byte   | N bytes   |
+--------+----------+---------+----------+----------+-----------+
  0xDA     0x00-0xFF  0x00-0x01 0x00-0xFF  0x00-0xFF  16-bit data
```

---

## 2. 명령어 정의

### CMD_PLAY (0x01) - 재생 시작
```
바이트:  C0 00 00 01 00 00
설명:    Header=0xC0, Slave=0, Ch=0, Cmd=PLAY, Param=0
기능:    DAC1 채널 재생 시작, DMA/TIM7 활성화
응답:    [CMD] PLAY CH0
```

### CMD_STOP (0x02) - 재생 중지
```
바이트:  C0 00 00 02 00 00
설명:    Header=0xC0, Slave=0, Ch=0, Cmd=STOP, Param=0
기능:    DAC1 채널 재생 중지, DMA/TIM7 비활성화
응답:    [CMD] STOP CH0
```

### CMD_VOLUME (0x03) - 볼륨 설정 ✅ 테스트 완료
```
바이트:  C0 00 00 03 00 32
설명:    Header=0xC0, Slave=0, Ch=0, Cmd=VOLUME, Param=50
기능:    볼륨을 50%로 설정 (0-100)
응답:    [CMD] VOLUME=50 CH0
상태:    정상 동작 확인됨
```

### CMD_RESET (0x04) - 리셋
```
바이트:  C0 00 00 04 00 00
설명:    Header=0xC0, Slave=0, Ch=0, Cmd=RESET, Param=0
기능:    채널 리셋, 버퍼 클리어, 상태 초기화
응답:    [CMD] RESET CH0
```

---

## 3. 데이터 패킷 전송 (오디오 샘플)

### 구조
```
Header:   0xDA (데이터 패킷)
SlaveID:  0x00 (슬레이브 ID)
Channel:  0x00 (DAC1) 또는 0x01 (DAC2)
Count:    전송할 샘플 수 (16비트, Big Endian)
Samples:  16비트 오디오 샘플 배열 (최대 512 샘플)
```

### 예시: 4개 샘플 전송
```
DA 00 00 00 04 80 00 90 00 A0 00 B0 00
│  │  │  └─┴─ Count = 4 samples
│  │  └────── Channel 0 (DAC1)
│  └───────── SlaveID 0
└──────────── Header (DATA)

샘플 데이터:
  0x8000 (32768) → 중간값
  0x9000 (36864)
  0xA000 (40960)
  0xB000 (45056)
```

**중요:** 샘플은 16비트 부호 없는 정수, Big Endian 형식

---

## 4. 현재 테스트 상태

### ✅ 완료된 테스트
- [x] SPI 슬레이브 통신 - 정상
- [x] EXTI 인터럽트 (CS falling edge) - 정상
- [x] CMD_VOLUME 수신 및 처리 - 정상
- [x] 패킷 파싱 - 정상

### 🔄 진행 중인 테스트
- [ ] **CMD_PLAY 명령** ← 다음 테스트 필요
- [ ] DAC DMA 재생 동작 확인
- [ ] 데이터 패킷 수신 및 버퍼링
- [ ] Half/Complete 콜백 동작

### ⏳ 예정된 테스트
- [ ] CMD_STOP 동작
- [ ] 연속 데이터 스트리밍
- [ ] 버퍼 언더런 처리
- [ ] 듀얼 채널 동시 재생

---

## 5. 테스트 절차

### Step 1: CMD_PLAY 전송
```c
// SPI 마스터 코드 예시
uint8_t cmd_play[] = {0xC0, 0x00, 0x00, 0x01, 0x00, 0x00};
SPI_Transmit(cmd_play, 6);
```

**예상 슬레이브 응답 (UART 디버그):**
```
[RX] Raw bytes: C0 00 00 01 00 00
[PARSE] Header=0xC0, SlaveID=0, Ch=0, Cmd=0x01, Param=0
[CMD] PLAY CH0 (Slave 0)
[DAC] Starting DMA playback on CH1
[DAC] TIM7 started at 32kHz

[STATUS] --------------------
DAC1: PLAYING | Samples: xxxx | Swaps: x | Underruns: 0
```

### Step 2: 데이터 패킷 전송 (PLAY 후)
```c
// 512 샘플 (1024 bytes) 전송
uint8_t data_packet[1029];
data_packet[0] = 0xDA;           // Header
data_packet[1] = 0x00;           // SlaveID
data_packet[2] = 0x00;           // Channel 0
data_packet[3] = 0x02;           // Count_H (512 >> 8)
data_packet[4] = 0x00;           // Count_L (512 & 0xFF)

// 샘플 데이터 (16-bit, Big Endian)
for (int i = 0; i < 512; i++) {
    uint16_t sample = audio_samples[i];
    data_packet[5 + i*2] = (sample >> 8) & 0xFF;   // High byte
    data_packet[6 + i*2] = sample & 0xFF;          // Low byte
}

SPI_Transmit(data_packet, 1029);
```

**예상 슬레이브 응답:**
```
[RX] DATA packet: Ch=0, Count=512
[AUDIO] Filled 512 samples to buffer
[DMA] Half Complete - Filled 256 samples
[DMA] Complete - Filled 256 samples
```

### Step 3: CMD_STOP 전송
```c
uint8_t cmd_stop[] = {0xC0, 0x00, 0x00, 0x02, 0x00, 0x00};
SPI_Transmit(cmd_stop, 6);
```

**예상 슬레이브 응답:**
```
[CMD] STOP CH0 (Slave 0)
[DAC] Stopping DMA playback

[STATUS] --------------------
DAC1: STOP | Samples: xxxx | Swaps: x | Underruns: 0
```

---

## 6. SPI 통신 타이밍

### CS (NSS) 제어
1. **CS LOW** → EXTI 인터럽트 발생
2. 슬레이브가 **첫 바이트(헤더) 수신 대기**
3. 마스터가 **패킷 전체 전송** (6 bytes 또는 5+N bytes)
4. **CS HIGH** → 전송 완료

### 권장 SPI 설정
- **클럭**: 최대 64MHz (슬레이브는 64MHz 지원)
- **모드**: Mode 0 (CPOL=0, CPHA=0) 또는 Mode 3
- **비트 순서**: MSB First
- **데이터 크기**: 8비트

---

## 7. 에러 처리

### 슬레이브 에러 응답
```
[SPI] ERROR: Invalid header 0xXX (expected 0xC0 or 0xDA)
[SPI] ERROR: Invalid slave ID 0xXX (expected 0x00)
[SPI] ERROR: Overflow - too many samples
```

### 디버그 출력 모니터링
- **포트**: COM9 (또는 사용자 환경에 따라 다름)
- **보드레이트**: 115200
- **형식**: 8N1

---

## 8. 추가 요청사항

### 마스터 프로그램에 추가 필요한 명령:

1. **CMD_PLAY 지원**
   - 명령: `SPITEST PLAY [채널]`
   - 패킷: `C0 00 [CH] 01 00 00`

2. **데이터 패킷 전송**
   - 명령: `SPITEST DATA [채널] [파일명]`
   - 오디오 파일에서 샘플 읽어 전송
   - 512 샘플씩 분할 전송

3. **CMD_STOP 지원**
   - 명령: `SPITEST STOP [채널]`
   - 패킷: `C0 00 [CH] 02 00 00`

4. **스트리밍 모드**
   - PLAY → 데이터 연속 전송 → STOP
   - DMA Half/Complete 콜백에 맞춰 타이밍 조절

---

## 9. 현재 슬레이브 설정

- **샘플레이트**: 32kHz
- **버퍼 크기**: 512 samples per buffer
- **듀얼 버퍼링**: 활성화 (ping-pong)
- **DMA 모드**: Circular
- **비트 깊이**: 12-bit DAC (16-bit 입력 → 12-bit 변환)

---

## 10. 연락처

슬레이브 개발 완료 상태:
- SPI 수신: ✅ 정상
- 명령 파싱: ✅ 정상
- DAC DMA: ✅ 설정 완료
- **테스트 대기**: CMD_PLAY 전송 필요

**다음 단계**: 마스터에서 CMD_PLAY 명령 구현 및 전송
