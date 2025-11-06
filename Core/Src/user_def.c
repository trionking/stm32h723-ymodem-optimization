/*
 * user_def.c
 *
 *  Created on: Oct 1, 2025
 *      Author: SIDO
 */


#include "main.h"
#include "user_def.h"
#include "ff.h"
#include "fatfs.h"
#include "audio_stream.h"
#include "spi_protocol.h"
#include "uart_command.h"
#include "command_handler.h"
#include "ansi_colors.h"
#include "ring_buffer.h"

#include  <errno.h>
#include  <sys/unistd.h> // STDOUT_FILENO, STDERR_FILENO
#include "string.h"
#include "stdio.h"
#include "math.h"


extern UART_HandleTypeDef huart2;
extern SPI_HandleTypeDef hspi1;
extern SD_HandleTypeDef hsd1;

// in fatfs.c
extern uint8_t retSD;    /* Return value for SD */
extern char SDPath[4];   /* SD logical drive path */
extern FATFS SDFatFS;    /* File system object for SD logical drive */
extern FIL SDFile;       /* File object for SD */

// Debug counter for SD DMA read operations
int debug_read_count = 0;


// MDMA 버퍼: 32KB (512 Byte 단위로 정의)
__attribute__((section(".ram_d1_dma")))
__attribute__((aligned(4)))
uint8_t sdmmc1_buffer[32768]; // 32KB = 32 * 1024 = 32768


// ============================================================================
// UART2 DMA TX 구현 (논블럭 printf)
// ============================================================================

// UART2 TX Queue
Queue tx_UART2_queue;

// DMA TX state and buffer
volatile uint8_t g_uart2_tx_busy = 0;
volatile uint8_t g_ymodem_active = 0;  // Y-MODEM 처리 중 플래그

// DMA TX buffer - placed in DTCMRAM for DMA compatibility
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint8_t g_uart2_tx_dma_buffer[DMA_TX_BUFFER_SIZE];

/**
 * @brief Initialize UART2 DMA TX Queue
 */
void init_UART2_DMA_TX(void)
{
	InitQueue(&tx_UART2_queue, 8192);  // 8KB queue for printf output (increased for Y-MODEM)
	g_uart2_tx_busy = 0;

	// Verify queue initialization
	if (tx_UART2_queue.buf == NULL) {
		// Queue allocation failed - system will use blocking printf
		tx_UART2_queue.buf_size = 0;
	}
}

/**
 * @brief Process UART2 TX queue and start DMA transmission if not busy
 * @note Call this function periodically from main loop
 */
void UART2_Process_TX_Queue(void)
{
	// If DMA is busy, return immediately
	if (g_uart2_tx_busy)
	{
		return;
	}

	// Check if there's data in TX queue
	uint16_t q_len = Len_queue(&tx_UART2_queue);
	if (q_len == 0)
	{
		return;  // Nothing to send
	}

	// Limit chunk size to DMA buffer size
	if (q_len > DMA_TX_BUFFER_SIZE)
	{
		q_len = DMA_TX_BUFFER_SIZE;
	}

	// Copy data from queue to DMA buffer
	Dequeue_bytes(&tx_UART2_queue, g_uart2_tx_dma_buffer, q_len);

	// Start DMA transmission
	g_uart2_tx_busy = 1;
	HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(&huart2, g_uart2_tx_dma_buffer, q_len);

	if (status != HAL_OK)
	{
		// DMA start failed - mark as not busy so we can retry
		g_uart2_tx_busy = 0;
	}
}

/**
 * @brief UART2 TX DMA complete callback
 * @note Called from HAL_UART_TxCpltCallback in stm32h7xx_it.c
 */
void UART2_TX_Complete_Callback(void)
{
	// Mark as not busy - allows next transmission
	g_uart2_tx_busy = 0;

	// Immediately process next chunk if available (non-blocking)
	UART2_Process_TX_Queue();
}


#ifdef __GNUC__
 #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
 #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

PUTCHAR_PROTOTYPE
{
	// If queue not initialized, use blocking transmission
	if (tx_UART2_queue.buf_size == 0)
	{
		HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 10);
		return ch;
	}

	// Enqueue character if queue has space
	// Leave 10 bytes margin to prevent overflow
	if (Len_queue(&tx_UART2_queue) < (tx_UART2_queue.buf_size - 10))
	{
		Enqueue(&tx_UART2_queue, (uint8_t)ch);
	}

	return ch;
}

void led_out(uint8_t ld_val)
{
	uint32_t io_clr,io_set;

	io_set = (uint32_t)(ld_val^0xff) & 0x000000FF;
	io_clr = ((uint32_t)ld_val)<<16 & 0x00FF0000;

	GPIOF->BSRR = io_set + io_clr;
}

void rel_out(uint8_t rel_val)
{
	uint32_t io_clr,io_set;

	io_set = (((uint32_t)rel_val) << 10)&0x0000FC00;
	io_clr = ((((uint32_t)(rel_val^0xff))<<10) << 16)&0xFC000000;

	GPIOG->BSRR = io_set + io_clr;
}


uint8_t read_dip(void)
{
	uint8_t rtn_val = 0;

	rtn_val = (uint8_t)(GPIOG->IDR & 0x00FF);
	rtn_val ^= 0xFF;

	return rtn_val;
}

void test_gpio(void)
{
	uint32_t user_tick;
	uint8_t cnt_ld_out = 0;
	uint8_t cnt_rel_out = 0;

	user_tick = HAL_GetTick();
	HAL_GPIO_TogglePin(OT_SYS_GPIO_Port, OT_SYS_Pin);

	while(1)
	{
		if ((HAL_GetTick() - user_tick) > 500)
		{
			user_tick = HAL_GetTick();

			led_out(1<<cnt_ld_out);
			cnt_ld_out++;
			cnt_ld_out %= 8;

			rel_out(1<<cnt_rel_out);
			cnt_rel_out++;
			cnt_rel_out %= 6;

			printf("led out : %02X, rel_out : %02X, dip_stat : %02X\r\n",cnt_ld_out,cnt_rel_out,read_dip());

			HAL_GPIO_TogglePin(OT_REV_GPIO_Port, OT_REV_Pin);
			HAL_GPIO_TogglePin(OT_SYS_GPIO_Port, OT_SYS_Pin);
		}
	}
}

void init_sdmmc(void)
{
	extern SD_HandleTypeDef hsd1;

	// sdmmc power Off
	HAL_GPIO_WritePin(OT_SD_EN_GPIO_Port, OT_SD_EN_Pin, GPIO_PIN_RESET);
	HAL_Delay(100);
	// sdmmc power On
	HAL_GPIO_WritePin(OT_SD_EN_GPIO_Port, OT_SD_EN_Pin, GPIO_PIN_SET);
	HAL_Delay(100);

	/* Initialize SDMMC peripheral and SD card */
	MX_SDMMC1_SD_Init();

	/* Configure 4-bit bus mode */
	if (HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B) != HAL_OK)
	{
		printf("Warning: Failed to configure 4-bit bus mode\r\n");
	}

	/* CRITICAL: Wait for HAL to be READY after initialization */
	/* SD init leaves HAL in BUSY state, must wait before USB MSC access */
	uint32_t wait_start = HAL_GetTick();
	while (HAL_SD_GetState(&hsd1) != HAL_SD_STATE_READY)
	{
		if ((HAL_GetTick() - wait_start) > 5000)  /* 5 second timeout */
		{
			break;  /* Timeout - continue anyway */
		}
		HAL_Delay(10);
	}

	/* Pre-cache SD card capacity */
	HAL_SD_CardInfoTypeDef card_info;
	if (HAL_SD_GetCardInfo(&hsd1, &card_info) == HAL_OK)
	{
		printf("SD Card: %lu blocks, %lu bytes/block\r\n",
		       card_info.LogBlockNbr, card_info.LogBlockSize);

		/* SD 카드 상태 확인 */
		HAL_SD_CardStateTypeDef card_state = HAL_SD_GetCardState(&hsd1);
		printf("SD Card State: %d (1=READY, 2=IDENT, 3=STDBY, 4=TRANSFER, 5=SEND, 6=RCV, 7=PRG, 8=DIS, 0xFF=ERR)\r\n", card_state);

		/* Cache values in usbd_storage_if.c (USB 사용 시 필요) */
		/* 현재 USB는 사용하지 않으므로 주석 처리 */
		/*
		extern uint32_t cached_block_num;
		extern uint16_t cached_block_size;
		extern uint8_t capacity_cached;

		cached_block_num = card_info.LogBlockNbr - 1;
		cached_block_size = card_info.LogBlockSize;
		capacity_cached = 1;
		*/
	}

	/* Wait again for HAL to be READY after GetCardInfo */
	wait_start = HAL_GetTick();
	while (HAL_SD_GetState(&hsd1) != HAL_SD_STATE_READY)
	{
		if ((HAL_GetTick() - wait_start) > 5000)  /* 5 second timeout */
		{
			break;  /* Timeout - continue anyway */
		}
		HAL_Delay(10);
	}
}

/**
 * @brief  FatFs 파일시스템 마운트
 * @retval  0: 성공, -1: 실패
 */
int mount_fatfs(void)
{
	FRESULT res;

	printf("FatFs: Mounting SD card...\r\n");

	/* SD 카드 마운트 */
	res = f_mount(&SDFatFS, (TCHAR const*)SDPath, 1);
	if (res != FR_OK) {
		printf("FatFs: Mount failed (error %d)\r\n", res);
		return -1;
	}

	printf("FatFs: Mounted successfully\r\n");

	/* 루트 디렉토리 확인 (옵션) */
	DIR dir;
	res = f_opendir(&dir, "/");
	if (res == FR_OK) {
		f_closedir(&dir);
		printf("FatFs: Root directory accessible\r\n");
	} else {
		printf("FatFs: Warning - cannot access root directory (error %d)\r\n", res);
	}

	/* SD 카드 파일 I/O 테스트 - sd_rd_wr_test2()와 동일한 방식 */
	printf("\r\n=== SD Card File I/O Test ===\r\n");

	FSIZE_t file_sz;
	UINT r_byte, w_byte;

	// === 쓰기 테스트 ===
	// sd_diskio.c의 ENABLE_SD_DMA_CACHE_MAINTENANCE=1 설정으로
	// cache maintenance가 자동 처리되므로 명시적 호출 불필요
	printf("Test 1: Writing to test.txt...\r\n");
	res = f_open(&SDFile, "test.txt", FA_CREATE_ALWAYS | FA_WRITE);
	if (res != FR_OK) {
		printf("  Open for write failed (error %d)\r\n", res);
		return -1;
	}

	file_sz = sprintf((char*)sdmmc1_buffer, "Hello SD Card! IDMA works!\r\n");
	res = f_write(&SDFile, sdmmc1_buffer, file_sz, &w_byte);
	if (res != FR_OK) {
		printf("  Write failed (error %d)\r\n", res);
		f_close(&SDFile);
		return -1;
	}
	printf("  Written %u bytes successfully\r\n", w_byte);

	// f_close()가 내부적으로 f_sync()를 호출하므로 명시적 sync 불필요
	f_close(&SDFile);
	printf("  File closed (data auto-flushed)\r\n");

	// === 읽기 테스트 ===
	printf("Test 2: Reading from test.txt...\r\n");
	memset(sdmmc1_buffer, 0, sizeof(sdmmc1_buffer));
	res = f_open(&SDFile, "test.txt", FA_READ);
	if (res != FR_OK) {
		printf("  Open for read failed (error %d)\r\n", res);
		return -1;
	}

	file_sz = f_size(&SDFile);
	res = f_read(&SDFile, sdmmc1_buffer, file_sz, &r_byte);
	if (res != FR_OK) {
		printf("  Read failed (error %d)\r\n", res);
		f_close(&SDFile);
		return -1;
	}
	printf("  Read %u bytes: '%s'\r\n", r_byte, (char*)sdmmc1_buffer);
	f_close(&SDFile);

	printf("=== SD Card Test Complete ===\r\n\r\n");

	return 0;
}


void sound_mon_test(void)
{
	uint8_t f_snd = 0;
	while(1)
	{
		f_snd ^= 1;
		HAL_GPIO_TogglePin(OT_SYS_GPIO_Port, OT_SYS_Pin);
		HAL_GPIO_WritePin(OT_MON_SEL0_GPIO_Port, OT_MON_SEL0_Pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(OT_MON_SEL1_GPIO_Port, OT_MON_SEL1_Pin, GPIO_PIN_RESET);
		if (f_snd)
		{
		HAL_GPIO_WritePin(OT_MON_SEL2_GPIO_Port, OT_MON_SEL2_Pin, GPIO_PIN_SET);
		}
		else{
		HAL_GPIO_WritePin(OT_MON_SEL2_GPIO_Port, OT_MON_SEL2_Pin, GPIO_PIN_RESET);
		}
		HAL_Delay(100);  // Reduced from 500ms for faster response
	}

}

void sd_scan_dir(void)
{
	TCHAR path[200] = "0:/"; // 명시적으로 루트 디렉토리 설정
	DIR root_dir;
	FILINFO file_info;

	// SD 카드 마운트
	retSD = f_mount(&SDFatFS, (TCHAR const*)SDPath, 1);
	//printf("SD Mount : res f_mount : %02X\n\r", retSD);

	if (retSD == FR_OK)
	{
		// 디렉토리 열기
		retSD = f_opendir(&root_dir, path);
		//printf("res f_opendir : %02X\n\r", retSD);

		if (retSD == FR_OK)
		{
			while (1)
			{
				// 디렉토리 내 파일 읽기
				retSD = f_readdir(&root_dir, &file_info);
				if (retSD != FR_OK || file_info.fname[0] == 0)
						break;

				TCHAR *fn;
				fn = file_info.fname;


				printf("%c%c%c%c ",
										 ((file_info.fattrib & AM_DIR) ? 'D' : '-'),
										 ((file_info.fattrib & AM_RDO) ? 'R' : '-'),
										 ((file_info.fattrib & AM_SYS) ? 'S' : '-'),
										 ((file_info.fattrib & AM_HID) ? 'H' : '-'));

				printf("%10ld ", file_info.fsize);
				printf("%s\r\n", fn);
				HAL_Delay(2);

			}
		}
		else
		{
			printf("f_opendir 실패: res = %02X\n\r", retSD);
		}

		// SD 카드 언마운트
		retSD = f_mount(0, SDPath, 0);
		//printf("SD Unmount : res f_mount : %02X\n\r", retSD);
	}
	else
	{
		printf("SD Mount 실패: res f_mount : %02X\n\r", retSD);
	}
}


void sd_rd_wr_test2(void)
{
  FRESULT res;
  FSIZE_t file_sz;
  UINT r_byte,w_byte;

  printf("\r\n=== SD Card Write/Read Test ===\r\n");

  // SD 카드 마운트
  res = f_mount(&SDFatFS, (TCHAR const*)SDPath, 1);
  if (res != FR_OK) {
      printf("SD mount failed: %d\r\n", res);
      return;
  }

  // === 쓰기 테스트 ===
  // sd_diskio.c의 ENABLE_SD_DMA_CACHE_MAINTENANCE=1 설정으로
  // cache maintenance가 자동 처리되므로 명시적 호출 불필요
  printf("Test 1: Writing to test.txt...\r\n");

  res = f_open(&SDFile, "test.txt", FA_CREATE_ALWAYS | FA_WRITE);
  if (res != FR_OK) {
      printf("  f_open (write) failed: %d\r\n", res);
      return;
  }

  file_sz = sprintf((char*)sdmmc1_buffer,"test `12345\n");

  res = f_write(&SDFile, sdmmc1_buffer, file_sz, &w_byte);
  printf("  f_write: wrote %u bytes, res=%d\r\n", w_byte, res);

  // f_close()가 내부적으로 sync 수행
  f_close(&SDFile);
  printf("  f_close: file closed (auto-synced)\r\n");

  // === 읽기 테스트 ===
  printf("\r\nTest 2: Reading from test.txt...\r\n");

  // 버퍼 초기화
  memset(sdmmc1_buffer, 0, sizeof(sdmmc1_buffer));

  res = f_open(&SDFile, "test.txt", FA_READ);
  if (res != FR_OK) {
      printf("  f_open (read) failed: %d\r\n", res);
      return;
  }

  file_sz = f_size(&SDFile);
  printf("  File size: %lu bytes\r\n", file_sz);

  res = f_read(&SDFile, sdmmc1_buffer, file_sz, &r_byte);

  printf("  f_read: read %u bytes, res=%d\r\n", r_byte, res);

  if (res == FR_OK && r_byte > 0) {
      sdmmc1_buffer[r_byte] = '\0';  // null-terminate
      printf("  Content: '%s'\r\n", (char*)sdmmc1_buffer);
  }

  f_close(&SDFile);

  // SD 카드 언마운트
  f_mount(0, (TCHAR const*)SDPath, 0);

  printf("=== Test Complete ===\r\n\r\n");
}



void sdmmc1_test(void)
{
	init_sdmmc();
	//sd_scan_dir();
	sd_rd_wr_test2();
	while(1);
}


/* ========================================
 * SPI 송수신 테스트 함수들
 * ======================================== */

/**
 * @brief  Phase 1: 명령 패킷 전송 테스트
 */
void test_spi_command_packets(void)
{
	printf("\r\n=== Phase 1: SPI Command Packet Test ===\r\n");
	printf("Testing communication with Slave 0...\r\n\r\n");

	// 1. RDY 핀 상태 확인
	printf("1. Checking RDY pin status...\r\n");
	uint8_t rdy_state = spi_check_ready(0);
	printf("   Slave 0 RDY pin: %s\r\n", rdy_state ? "HIGH (Ready)" : "LOW (Busy)");

	if (!rdy_state) {
		printf("   WARNING: Slave not ready! Check hardware connection.\r\n");
	}
	printf("\r\n");
	HAL_Delay(1000);

	// 2. PLAY 명령 전송 (Slave 0, Channel 0)
	printf("2. Sending PLAY command (Slave 0, CH 0)...\r\n");
	printf("   Expected on Slave UART: '[CMD] PLAY on CH0'\r\n");
	HAL_StatusTypeDef status = spi_send_command(0, 0, SPI_CMD_PLAY, 0);
	if (status == HAL_OK) {
		printf("   -> Command sent successfully!\r\n");
	} else {
		printf("   -> ERROR: Command send failed (status=%d)\r\n", status);
	}
	printf("\r\n");
	HAL_Delay(2000);

	// 3. VOLUME 명령 전송 (볼륨 75)
	printf("3. Sending VOLUME command (Slave 0, CH 0, Volume=75)...\r\n");
	printf("   Expected on Slave UART: '[CMD] VOLUME on CH0: 75'\r\n");
	status = spi_send_command(0, 0, SPI_CMD_VOLUME, 75);
	if (status == HAL_OK) {
		printf("   -> Command sent successfully!\r\n");
	} else {
		printf("   -> ERROR: Command send failed (status=%d)\r\n", status);
	}
	printf("\r\n");
	HAL_Delay(2000);

	// 4. STOP 명령 전송
	printf("4. Sending STOP command (Slave 0, CH 0)...\r\n");
	printf("   Expected on Slave UART: '[CMD] STOP on CH0'\r\n");
	status = spi_send_command(0, 0, SPI_CMD_STOP, 0);
	if (status == HAL_OK) {
		printf("   -> Command sent successfully!\r\n");
	} else {
		printf("   -> ERROR: Command send failed (status=%d)\r\n", status);
	}
	printf("\r\n");
	HAL_Delay(2000);

	printf("=== Phase 1 Test Complete ===\r\n");
	printf("Check Slave UART output to verify command reception.\r\n\r\n");
}

/**
 * @brief  Phase 2: 데이터 패킷 전송 테스트 (더미 데이터)
 */
void test_spi_data_packets(void)
{
	printf("\r\n=== Phase 2: SPI Data Packet Test ===\r\n");
	printf("Sending dummy audio data to Slave 0, CH 0...\r\n\r\n");

	// 1kHz 사인파 생성 (2048 샘플, 32kHz)
	static uint16_t test_samples[2048];
	printf("1. Generating 1kHz sine wave (2048 samples @ 32kHz)...\r\n");

	for (int i = 0; i < 2048; i++) {
		// 1kHz: omega = 2*pi*1000/32000 = pi/16
		// amplitude = 16384 (50% of 16-bit range), offset = 32768
		float angle = 3.14159f * i / 16.0f;
		float value = 32768.0f + 16384.0f * sinf(angle);
		test_samples[i] = (uint16_t)value;
	}
	printf("   -> Sine wave generated\r\n");
	printf("   First 5 samples: %u, %u, %u, %u, %u\r\n",
	       test_samples[0], test_samples[1], test_samples[2],
	       test_samples[3], test_samples[4]);
	printf("\r\n");
	HAL_Delay(1000);

	// 2. PLAY 명령 전송
	printf("2. Sending PLAY command first...\r\n");
	spi_send_command(0, 0, SPI_CMD_PLAY, 0);
	printf("   -> PLAY command sent\r\n\r\n");
	HAL_Delay(500);

	// 3. RDY 핀 확인
	printf("3. Checking RDY pin before data transmission...\r\n");
	uint8_t rdy_state = spi_check_ready(0);
	printf("   Slave 0 RDY: %s\r\n", rdy_state ? "HIGH" : "LOW");

	if (!rdy_state) {
		printf("   WARNING: Slave not ready! Waiting...\r\n");
		for (int i = 0; i < 10; i++) {
			HAL_Delay(10);
			if (spi_check_ready(0)) {
				printf("   Slave is ready now!\r\n");
				break;
			}
		}
	}
	printf("\r\n");
	HAL_Delay(500);

	// 4. 데이터 패킷 전송
	printf("4. Sending data packet (2048 samples, 4096 bytes)...\r\n");
	printf("   Expected on Slave UART: '[SPI] RX 2048 samples CH0'\r\n");

	HAL_StatusTypeDef status = spi_send_data_dma(0, 0, test_samples, 2048);
	if (status == HAL_OK) {
		printf("   -> DMA transmission started...\r\n");

		// DMA 완료 대기
		status = spi_wait_dma_complete(100);
		if (status == HAL_OK) {
			printf("   -> Data sent successfully!\r\n");
		} else {
			printf("   -> ERROR: DMA timeout\r\n");
		}
	} else {
		printf("   -> ERROR: Failed to start DMA (status=%d)\r\n", status);
	}
	printf("\r\n");
	HAL_Delay(2000);

	// 5. STOP 명령 전송
	printf("5. Sending STOP command...\r\n");
	spi_send_command(0, 0, SPI_CMD_STOP, 0);
	printf("   -> STOP command sent\r\n\r\n");

	printf("=== Phase 2 Test Complete ===\r\n");
	printf("Check Slave DAC output on oscilloscope:\r\n");
	printf("  - Expected: 1kHz sine wave\r\n");
	printf("  - Amplitude: ~0.5Vpp (50%% of DAC range)\r\n\r\n");
}

/**
 * @brief  Phase 3: 연속 데이터 전송 테스트 (10초)
 */
void test_spi_continuous_transmission(void)
{
	printf("\r\n=== Phase 3: Continuous Transmission Test ===\r\n");
	printf("Sending data continuously for 10 seconds...\r\n\r\n");

	// 1kHz 사인파 생성
	static uint16_t test_samples[2048];
	for (int i = 0; i < 2048; i++) {
		float angle = 3.14159f * i / 16.0f;
		float value = 32768.0f + 16384.0f * sinf(angle);
		test_samples[i] = (uint16_t)value;
	}

	// PLAY 명령 전송
	printf("1. Sending PLAY command...\r\n");
	spi_send_command(0, 0, SPI_CMD_PLAY, 0);
	HAL_Delay(500);

	printf("2. Starting continuous transmission...\r\n");
	printf("   Press any key to stop (not implemented, will auto-stop after 10s)\r\n\r\n");

	uint32_t start_time = HAL_GetTick();
	uint32_t packet_count = 0;
	uint32_t error_count = 0;

	while ((HAL_GetTick() - start_time) < 10000) {  // 10초간 전송
		// RDY 핀 확인
		if (spi_check_ready(0)) {
			// 데이터 전송
			HAL_StatusTypeDef status = spi_send_data_dma(0, 0, test_samples, 2048);
			if (status == HAL_OK) {
				status = spi_wait_dma_complete(100);
				if (status == HAL_OK) {
					packet_count++;

					// 1초마다 진행 상황 출력
					if (packet_count % 16 == 0) {  // 약 1초 (16 * 64ms)
						printf("   [%lu s] Packets sent: %lu, Errors: %lu\r\n",
						       (HAL_GetTick() - start_time) / 1000,
						       packet_count, error_count);
					}
				} else {
					error_count++;
				}
			} else {
				error_count++;
			}
		}

		HAL_Delay(10);  // 작은 딜레이 (실제는 64ms 주기로 전송됨)
	}

	// STOP 명령 전송
	printf("\r\n3. Sending STOP command...\r\n");
	spi_send_command(0, 0, SPI_CMD_STOP, 0);

	printf("\r\n=== Phase 3 Test Complete ===\r\n");
	printf("Total packets sent: %lu\r\n", packet_count);
	printf("Total errors: %lu\r\n", error_count);
	printf("Expected packets: ~156 (10s / 64ms)\r\n\r\n");

	printf("Check Slave UART for:\r\n");
	printf("  - Buffer swaps: ~%lu\r\n", packet_count);
	printf("  - Underruns: 0 (should be zero!)\r\n");
	printf("  - SPI errors: 0\r\n\r\n");
}

/**
 * @brief  SPI 테스트 메뉴
 */
void spi_test_menu(void)
{
	printf("\r\n");
	printf("╔════════════════════════════════════════╗\r\n");
	printf("║   SPI Communication Test Menu          ║\r\n");
	printf("╚════════════════════════════════════════╝\r\n");
	printf("\r\n");
	printf("  [1] Phase 1: Command Packet Test\r\n");
	printf("      - Test PLAY/STOP/VOLUME commands\r\n");
	printf("      - Check RDY pin status\r\n");
	printf("\r\n");
	printf("  [2] Phase 2: Data Packet Test (Single)\r\n");
	printf("      - Send 1kHz sine wave (2048 samples)\r\n");
	printf("      - Verify DAC output on oscilloscope\r\n");
	printf("\r\n");
	printf("  [3] Phase 3: Continuous Transmission\r\n");
	printf("      - Send data continuously for 10 seconds\r\n");
	printf("      - Check stability and error count\r\n");
	printf("\r\n");
	printf("  [9] Skip tests and run normal operation\r\n");
	printf("\r\n");
	printf("Select test (1-3, 9): ");
}


void sound_mon_select(uint8_t channel)
{
	if (channel >= 7) {
		return;
	}

	HAL_GPIO_WritePin(OT_MON_SEL0_GPIO_Port, OT_MON_SEL0_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(OT_MON_SEL1_GPIO_Port, OT_MON_SEL1_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(OT_MON_SEL2_GPIO_Port, OT_MON_SEL2_Pin, GPIO_PIN_RESET);


	switch(channel) {
	case 0:
		break;
	case 1:
		HAL_GPIO_WritePin(OT_MON_SEL0_GPIO_Port, OT_MON_SEL0_Pin, GPIO_PIN_SET);
		break;
	case 2:
		HAL_GPIO_WritePin(OT_MON_SEL1_GPIO_Port, OT_MON_SEL1_Pin, GPIO_PIN_SET);
		break;
	case 3:
		HAL_GPIO_WritePin(OT_MON_SEL0_GPIO_Port, OT_MON_SEL0_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(OT_MON_SEL1_GPIO_Port, OT_MON_SEL1_Pin, GPIO_PIN_SET);
		break;
	case 4:
		HAL_GPIO_WritePin(OT_MON_SEL2_GPIO_Port, OT_MON_SEL2_Pin, GPIO_PIN_SET);
		break;
	case 5:
		HAL_GPIO_WritePin(OT_MON_SEL0_GPIO_Port, OT_MON_SEL0_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(OT_MON_SEL2_GPIO_Port, OT_MON_SEL2_Pin, GPIO_PIN_SET);
		break;
	case 6:
		HAL_GPIO_WritePin(OT_MON_SEL1_GPIO_Port, OT_MON_SEL0_Pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(OT_MON_SEL2_GPIO_Port, OT_MON_SEL2_Pin, GPIO_PIN_SET);
		break;
	}
}

void init_proc(void)
{
	// 시스템 초기화
	sound_mon_select(0);

	// UART2 DMA TX 초기화 (논블럭 printf)
	init_UART2_DMA_TX();
}

void run_proc(void)
{
	init_proc();

	//sdmmc1_test();

	printf("\r\n========================================\r\n");
	printf("  Audio Mux v1.00 - Multi-Channel Audio Streaming\r\n");
	printf("========================================\r\n\r\n");

	/* SD 카드 초기화 */
	printf("Initializing SD card...\r\n");
	init_sdmmc();

	/* FatFs 드라이버 링크 (disk I/O 레이어 초기화) */
	printf("Linking FatFs driver...\r\n");
	//MX_FATFS_Init();

	/* FatFs 마운트 */
	if (mount_fatfs() != 0) {
		printf("ERROR: Failed to mount FatFs\r\n");
		printf("System halted.\r\n");
		while(1);
	}

	/* 오디오 스트리밍 시스템 초기화 */
	if (audio_stream_init(&hspi1) != 0) {
		printf("ERROR: Failed to initialize audio stream\r\n");
		printf("System halted.\r\n");
		while(1);
	}

	/* USB CDC 명령 시스템 초기화 */
	usb_cdc_command_init();
	printf(ANSI_GREEN "USB CDC command interface ready\r\n" ANSI_RESET);

	/* UART 명령 시스템 초기화 (UART RX 하드웨어 문제로 비활성화) */
	// uart_command_init(&huart2);
	// printf(ANSI_GREEN "UART command interface ready\r\n" ANSI_RESET);

	printf("\r\n========================================\r\n");
	printf("  System Ready\r\n");
	printf("========================================\r\n");
	printf("Use TEST1, TEST2, TEST3 commands for SPI testing\r\n");
	printf("Running normal operation...\r\n\r\n");

	/* 테스트: 채널 0에 WAV 파일 로드 및 재생 */
	/* 실제 사용 시 아래 코드를 수정하세요 */
	// 주의: 먼저 SD 카드에 /audio/ch0/test.wav 파일을 업로드해야 합니다
	// PC 프로그램에서 UPLOAD 0 test.wav 명령 사용
	/*
	if (audio_load_file(0, "/audio/ch0/test.wav", 1) == 0) {
		audio_play(0);
		printf("Playing test.wav on channel 0 (loop enabled)\r\n");
	}
	*/

	/* 메인 루프 */
	uint32_t last_status_print = 0;
	while(1)
	{
		/* UART2 DMA TX 큐 처리 (논블럭 printf) */
		UART2_Process_TX_Queue();

		/* Y-MODEM 업로드 요청 처리 (인터럽트가 아닌 메인 컨텍스트) */
		process_upload_request();

		/* 오디오 스트리밍 태스크 실행 */
		audio_stream_task();

		/* 5초마다 상태 출력 (디버그용, 필요시 주석 처리) */
		if ((HAL_GetTick() - last_status_print) > 5000) {
			last_status_print = HAL_GetTick();
			audio_print_status();  // 오디오 시스템 상태 출력
		}

		/* LED 토글 (동작 확인용) */
		static uint32_t last_led_toggle = 0;
		if ((HAL_GetTick() - last_led_toggle) > 500) {
			last_led_toggle = HAL_GetTick();
			HAL_GPIO_TogglePin(OT_SYS_GPIO_Port, OT_SYS_Pin);
		}
	}
}

/* ============================================================================
 * SPI 테스트 함수 구현 (SPI_TEST_GUIDE.md 참조)
 * ============================================================================ */

/**
 * @brief  기본 SPI 연결 테스트
 * @param  slave_id: Slave ID (0~2)
 * @note   PLAY, STOP, VOLUME 명령을 순차 전송하여 연결 확인
 */
void spi_test_basic(uint8_t slave_id)
{
	printf(ANSI_CYAN "\r\n=== SPI Basic Test (Slave %d) ===\r\n" ANSI_RESET, slave_id);

	if (slave_id >= SPI_SLAVE_COUNT) {
		printf(ANSI_RED "ERROR: Invalid slave_id %d (max %d)\r\n" ANSI_RESET, slave_id, SPI_SLAVE_COUNT - 1);
		return;
	}

	/* 1. PLAY 명령 전송 (채널 0) */
	printf(ANSI_YELLOW "Test 1: Sending PLAY command...\r\n" ANSI_RESET);
	if (spi_send_command(slave_id, 0, SPI_CMD_PLAY, 0) == HAL_OK) {
		printf(ANSI_GREEN "  -> PLAY command sent successfully\r\n" ANSI_RESET);
	} else {
		printf(ANSI_RED "  -> PLAY command FAILED\r\n" ANSI_RESET);
	}
	HAL_Delay(200);

	/* 2. STOP 명령 전송 (채널 0) */
	printf(ANSI_YELLOW "Test 2: Sending STOP command...\r\n" ANSI_RESET);
	if (spi_send_command(slave_id, 0, SPI_CMD_STOP, 0) == HAL_OK) {
		printf(ANSI_GREEN "  -> STOP command sent successfully\r\n" ANSI_RESET);
	} else {
		printf(ANSI_RED "  -> STOP command FAILED\r\n" ANSI_RESET);
	}
	HAL_Delay(200);

	/* 3. VOLUME 명령 전송 (볼륨 50) */
	printf(ANSI_YELLOW "Test 3: Sending VOLUME command (vol=50)...\r\n" ANSI_RESET);
	if (spi_send_command(slave_id, 0, SPI_CMD_VOLUME, 50) == HAL_OK) {
		printf(ANSI_GREEN "  -> VOLUME command sent successfully\r\n" ANSI_RESET);
	} else {
		printf(ANSI_RED "  -> VOLUME command FAILED\r\n" ANSI_RESET);
	}
	HAL_Delay(200);

	/* 4. VOLUME 명령 전송 (볼륨 80, 채널 1) */
	printf(ANSI_YELLOW "Test 4: Sending VOLUME command (ch=1, vol=80)...\r\n" ANSI_RESET);
	if (spi_send_command(slave_id, 1, SPI_CMD_VOLUME, 80) == HAL_OK) {
		printf(ANSI_GREEN "  -> VOLUME command sent successfully\r\n" ANSI_RESET);
	} else {
		printf(ANSI_RED "  -> VOLUME command FAILED\r\n" ANSI_RESET);
	}
	HAL_Delay(200);

	printf(ANSI_CYAN "=== Basic Test Completed ===\r\n" ANSI_RESET);
	printf("Check Slave terminal for received packets.\r\n\r\n");
}

/**
 * @brief  SPI 데이터 전송 테스트
 * @param  slave_id: Slave ID (0~2)
 * @note   작은/큰 오디오 샘플 데이터를 전송하여 데이터 패킷 확인
 */
void spi_test_data(uint8_t slave_id)
{
	printf(ANSI_CYAN "\r\n=== SPI Data Transfer Test (Slave %d) ===\r\n" ANSI_RESET, slave_id);

	if (slave_id >= SPI_SLAVE_COUNT) {
		printf(ANSI_RED "ERROR: Invalid slave_id %d (max %d)\r\n" ANSI_RESET, slave_id, SPI_SLAVE_COUNT - 1);
		return;
	}

	/* 1. 작은 샘플 데이터 전송 (10개) */
	printf(ANSI_YELLOW "Test 1: Sending 10 samples to CH0...\r\n" ANSI_RESET);

	/* RDY 신호 확인 */
	if (!spi_check_ready(slave_id)) {
		printf(ANSI_RED "  -> WARNING: Slave RDY pin is LOW! Slave may not be ready.\r\n" ANSI_RESET);
	} else {
		printf("  -> Slave RDY pin is HIGH (ready)\r\n");
	}

	uint16_t small_samples[10];
	for (int i = 0; i < 10; i++) {
		small_samples[i] = 2048;  // 중간값
	}

	if (spi_send_data_dma(slave_id, 0, small_samples, 10) == HAL_OK) {
		printf("  -> DMA started, waiting for completion...\r\n");
		if (spi_wait_dma_complete(5000) == HAL_OK) {  // 타임아웃 5초로 증가
			printf(ANSI_GREEN "  -> 10 samples sent successfully\r\n" ANSI_RESET);
		} else {
			printf(ANSI_RED "  -> DMA timeout\r\n" ANSI_RESET);
		}
	} else {
		printf(ANSI_RED "  -> Failed to start DMA\r\n" ANSI_RESET);
	}
	HAL_Delay(300);

	/* 2. 중간 크기 샘플 데이터 전송 (100개) */
	printf(ANSI_YELLOW "Test 2: Sending 100 samples to CH1...\r\n" ANSI_RESET);
	uint16_t medium_samples[100];
	for (int i = 0; i < 100; i++) {
		medium_samples[i] = 2048 + (i * 10);  // 증가하는 값
	}

	if (spi_send_data_dma(slave_id, 1, medium_samples, 100) == HAL_OK) {
		printf("  -> DMA started, waiting for completion...\r\n");
		if (spi_wait_dma_complete(1000) == HAL_OK) {
			printf(ANSI_GREEN "  -> 100 samples sent successfully\r\n" ANSI_RESET);
		} else {
			printf(ANSI_RED "  -> DMA timeout\r\n" ANSI_RESET);
		}
	} else {
		printf(ANSI_RED "  -> Failed to start DMA\r\n" ANSI_RESET);
	}
	HAL_Delay(300);

	/* 3. 큰 샘플 데이터 전송 (2048개 - 최대) */
	printf(ANSI_YELLOW "Test 3: Sending 2048 samples to CH0 (max size)...\r\n" ANSI_RESET);
	static uint16_t large_samples[2048];  // 스택 오버플로우 방지를 위해 static
	for (int i = 0; i < 2048; i++) {
		large_samples[i] = 2048 + (i % 1000);  // 변화하는 값
	}

	if (spi_send_data_dma(slave_id, 0, large_samples, 2048) == HAL_OK) {
		printf("  -> DMA started, waiting for completion...\r\n");
		if (spi_wait_dma_complete(5000) == HAL_OK) {
			printf(ANSI_GREEN "  -> 2048 samples sent successfully\r\n" ANSI_RESET);
		} else {
			printf(ANSI_RED "  -> DMA timeout\r\n" ANSI_RESET);
		}
	} else {
		printf(ANSI_RED "  -> Failed to start DMA\r\n" ANSI_RESET);
	}
	HAL_Delay(300);

	printf(ANSI_CYAN "=== Data Transfer Test Completed ===\r\n" ANSI_RESET);
	printf("Check Slave terminal for received data packets.\r\n\r\n");
}

/**
 * @brief  멀티 Slave 테스트
 * @note   3개 Slave에 각각 다른 명령을 전송하여 ID 필터링 확인
 */
void spi_test_multi_slave(void)
{
	printf(ANSI_CYAN "\r\n=== SPI Multi-Slave Test ===\r\n" ANSI_RESET);
	printf("Testing Slave ID filtering...\r\n");

	/* 각 Slave에게 순차적으로 명령 전송 */
	for (uint8_t i = 0; i < SPI_SLAVE_COUNT; i++) {
		printf(ANSI_YELLOW "Sending PLAY command to Slave %d...\r\n" ANSI_RESET, i);
		if (spi_send_command(i, 0, SPI_CMD_PLAY, 0) == HAL_OK) {
			printf(ANSI_GREEN "  -> Command sent to Slave %d\r\n" ANSI_RESET, i);
		} else {
			printf(ANSI_RED "  -> Failed to send to Slave %d\r\n" ANSI_RESET, i);
		}
		HAL_Delay(300);
	}

	/* 역순으로 STOP 명령 전송 */
	for (int i = SPI_SLAVE_COUNT - 1; i >= 0; i--) {
		printf(ANSI_YELLOW "Sending STOP command to Slave %d...\r\n" ANSI_RESET, i);
		if (spi_send_command(i, 0, SPI_CMD_STOP, 0) == HAL_OK) {
			printf(ANSI_GREEN "  -> Command sent to Slave %d\r\n" ANSI_RESET, i);
		} else {
			printf(ANSI_RED "  -> Failed to send to Slave %d\r\n" ANSI_RESET, i);
		}
		HAL_Delay(300);
	}

	printf(ANSI_CYAN "=== Multi-Slave Test Completed ===\r\n" ANSI_RESET);
	printf("Each slave should only respond to its own ID.\r\n\r\n");
}

/**
 * @brief  SPI 에러 테스트
 * @param  slave_id: Slave ID (0~2)
 * @note   잘못된 패킷을 전송하여 에러 처리 확인
 */
void spi_test_error(uint8_t slave_id)
{
	printf(ANSI_CYAN "\r\n=== SPI Error Test (Slave %d) ===\r\n" ANSI_RESET, slave_id);
	printf(ANSI_YELLOW "WARNING: This test intentionally sends invalid packets\r\n" ANSI_RESET);

	if (slave_id >= SPI_SLAVE_COUNT) {
		printf(ANSI_RED "ERROR: Invalid slave_id %d (max %d)\r\n" ANSI_RESET, slave_id, SPI_SLAVE_COUNT - 1);
		return;
	}

	extern SPI_HandleTypeDef hspi1;  // main.c에서 선언

	/* 1. 잘못된 헤더 전송 */
	printf(ANSI_YELLOW "Test 1: Sending invalid header (0xFF)...\r\n" ANSI_RESET);
	uint8_t invalid_header[] = {0xFF, slave_id, 0x00, 0x01, 0x00, 0x00};
	spi_select_slave(slave_id);
	HAL_SPI_Transmit(&hspi1, invalid_header, 6, 100);
	spi_deselect_slave(slave_id);
	printf("  -> Invalid header packet sent\r\n");
	HAL_Delay(300);

	/* 2. 잘못된 Slave ID (범위 초과) */
	printf(ANSI_YELLOW "Test 2: Sending invalid slave ID (5)...\r\n" ANSI_RESET);
	uint8_t invalid_id[] = {SPI_CMD_HEADER, 5, 0x00, 0x01, 0x00, 0x00};
	spi_select_slave(slave_id);
	HAL_SPI_Transmit(&hspi1, invalid_id, 6, 100);
	spi_deselect_slave(slave_id);
	printf("  -> Invalid slave ID packet sent\r\n");
	HAL_Delay(300);

	/* 3. 잘못된 명령 코드 */
	printf(ANSI_YELLOW "Test 3: Sending unknown command (0x99)...\r\n" ANSI_RESET);
	if (spi_send_command(slave_id, 0, 0x99, 0) == HAL_OK) {
		printf("  -> Unknown command sent\r\n");
	}
	HAL_Delay(300);

	printf(ANSI_CYAN "=== Error Test Completed ===\r\n" ANSI_RESET);
	printf("Check Slave terminal for error messages.\r\n\r\n");
}

/**
 * @brief  전체 SPI 테스트 실행
 * @param  slave_id: Slave ID (0~2)
 * @note   위의 모든 테스트를 순차 실행
 */
void spi_test_all(uint8_t slave_id)
{
	printf(ANSI_CYAN "\r\n");
	printf("╔══════════════════════════════════════════════╗\r\n");
	printf("║  SPI Communication Full Test Suite          ║\r\n");
	printf("║  Target: Slave %d                            ║\r\n", slave_id);
	printf("╚══════════════════════════════════════════════╝\r\n");
	printf(ANSI_RESET);

	if (slave_id >= SPI_SLAVE_COUNT) {
		printf(ANSI_RED "ERROR: Invalid slave_id %d (max %d)\r\n" ANSI_RESET, slave_id, SPI_SLAVE_COUNT - 1);
		return;
	}

	printf("\r\n" ANSI_YELLOW "Make sure Slave board is in SPI Test Mode (Test 5)\r\n" ANSI_RESET);
	printf("Press any key to start, or 'q' to cancel...\r\n");

	// 간단한 대기 (실제로는 UART 입력 대기)
	HAL_Delay(2000);

	/* 1. 기본 연결 테스트 */
	spi_test_basic(slave_id);
	HAL_Delay(1000);

	/* 2. 데이터 전송 테스트 */
	spi_test_data(slave_id);
	HAL_Delay(1000);

	/* 3. 멀티 Slave 테스트 (모든 Slave) */
	spi_test_multi_slave();
	HAL_Delay(1000);

	/* 4. 에러 테스트 */
	spi_test_error(slave_id);

	printf(ANSI_GREEN "\r\n");
	printf("╔══════════════════════════════════════════════╗\r\n");
	printf("║  All Tests Completed!                        ║\r\n");
	printf("╚══════════════════════════════════════════════╝\r\n");
	printf(ANSI_RESET);
	printf("\r\nReview Slave terminal output for detailed results.\r\n");
	printf("Expected statistics: ~15 total packets, some errors in error test.\r\n\r\n");
}

/**
 * @brief  SPI PLAY 명령 테스트
 * @param  slave_id: Slave ID (0~2)
 * @param  channel: DAC 채널 (0=DAC1, 1=DAC2)
 */
void spi_test_play(uint8_t slave_id, uint8_t channel)
{
	printf("\r\n" ANSI_CYAN "=== SPI PLAY Command Test (Slave %d, Channel %d) ===" ANSI_RESET "\r\n", slave_id, channel);

	if (slave_id >= SPI_SLAVE_COUNT) {
		printf(ANSI_RED "ERROR: Invalid slave_id %d (max %d)\r\n" ANSI_RESET, slave_id, SPI_SLAVE_COUNT - 1);
		return;
	}

	if (channel > 1) {
		printf(ANSI_RED "ERROR: Invalid channel %d (must be 0 or 1)\r\n" ANSI_RESET, channel);
		return;
	}

	printf("Sending PLAY command to Slave %d, Channel %d...\r\n", slave_id, channel);

	HAL_StatusTypeDef status = spi_send_command(slave_id, channel, SPI_CMD_PLAY, 0);

	if (status == HAL_OK) {
		printf(ANSI_GREEN "  -> PLAY command sent successfully" ANSI_RESET "\r\n");
		printf("Expected Slave response: [CMD] PLAY CH%d (Slave %d)\r\n", channel, slave_id);
	} else {
		printf(ANSI_RED "  -> Failed to send PLAY command (error %d)" ANSI_RESET "\r\n", status);
	}

	printf("=== PLAY Test Completed ===" ANSI_RESET "\r\n");
	printf("Check Slave terminal for response.\r\n\r\n");
}

/**
 * @brief  SPI STOP 명령 테스트
 * @param  slave_id: Slave ID (0~2)
 * @param  channel: DAC 채널 (0=DAC1, 1=DAC2)
 */
void spi_test_stop(uint8_t slave_id, uint8_t channel)
{
	printf("\r\n" ANSI_CYAN "=== SPI STOP Command Test (Slave %d, Channel %d) ===" ANSI_RESET "\r\n", slave_id, channel);

	if (slave_id >= SPI_SLAVE_COUNT) {
		printf(ANSI_RED "ERROR: Invalid slave_id %d (max %d)\r\n" ANSI_RESET, slave_id, SPI_SLAVE_COUNT - 1);
		return;
	}

	if (channel > 1) {
		printf(ANSI_RED "ERROR: Invalid channel %d (must be 0 or 1)\r\n" ANSI_RESET, channel);
		return;
	}

	printf("Sending STOP command to Slave %d, Channel %d...\r\n", slave_id, channel);

	HAL_StatusTypeDef status = spi_send_command(slave_id, channel, SPI_CMD_STOP, 0);

	if (status == HAL_OK) {
		printf(ANSI_GREEN "  -> STOP command sent successfully" ANSI_RESET "\r\n");
		printf("Expected Slave response: [CMD] STOP CH%d (Slave %d)\r\n", channel, slave_id);
	} else {
		printf(ANSI_RED "  -> Failed to send STOP command (error %d)" ANSI_RESET "\r\n", status);
	}

	printf("=== STOP Test Completed ===" ANSI_RESET "\r\n");
	printf("Check Slave terminal for response.\r\n\r\n");
}
