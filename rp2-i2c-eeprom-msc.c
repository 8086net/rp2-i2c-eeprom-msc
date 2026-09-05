/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2022-2026 Chris Burton
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "bsp/board.h"
#include "tusb.h"

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/unique_id.h"

// I2C Port for EEPROM
#define I2C_PORT            i2c0
#define I2C_SDA_PIN         0
#define I2C_SCL_PIN         1
#define I2C_BAUDRATE        100000

// I2C EEPROM (24C32)
#define EEPROM_ADDR         0x50
#define EEPROM_SIZE_BYTES   4096 // 24C32 is 32,768 bits arranged as 4096x8 bits.
#define EEPROM_PAGE_SIZE    32

// MSC USB
#define MSC_VID             "8086-RP2"         // Max 8 characters
#define MSC_PID             "I2C 24C32 Bridge" // Max 16 characters
#define MSC_REV             "1.0"              // Max 4 characters
#define MSC_BLOCK_SIZE      512
#define MSC_BLOCK_COUNT     (EEPROM_SIZE_BYTES / MSC_BLOCK_SIZE) // 8 blocks

char usb_serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

int main(void) {

	// Setup USB serial number from EEPROMs unique ID
	pico_get_unique_board_id_string(usb_serial, sizeof(usb_serial));

	// Setup I2C
	i2c_init(I2C_PORT, I2C_BAUDRATE);
	gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
	gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);

	// Cable Tester boards will have pullups but if you're using this elsewhere you might need them
	// gpio_pull_up(I2C_SDA_PIN);
	// gpio_pull_up(I2C_SCL_PIN);


	// Init USB device stack
	board_init();
	tusb_init(BOARD_TUD_RHPORT);

	while (true) tud_task();

	return 0;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(__unused uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
	const char vid[] = MSC_VID;
	const char pid[] = MSC_PID;
	const char rev[] = MSC_REV;

	memcpy(vendor_id, vid, strlen(vid));
	memcpy(product_id, pid, strlen(pid));
	memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(__unused uint8_t lun) {
	return true; // Always ready
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(__unused uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
	*block_count = MSC_BLOCK_COUNT;
	*block_size  = MSC_BLOCK_SIZE;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(__unused uint8_t lun, __unused uint8_t power_condition, __unused bool start, __unused bool load_eject) {
	return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(__unused uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
	// Bounds check
	if (lba >= MSC_BLOCK_COUNT) return -1;
    
	uint16_t mem_addr = (lba * MSC_BLOCK_SIZE) + offset;
	uint8_t addr_buf[2];

	addr_buf[0] = (mem_addr >> 8) & 0xFF; // MSB
	addr_buf[1] = mem_addr & 0xFF;        // LSB
    
	// Write 2-byte address
	i2c_write_blocking(I2C_PORT, EEPROM_ADDR, addr_buf, 2, true);

	// Read the data payload
	i2c_read_blocking(I2C_PORT, EEPROM_ADDR, (uint8_t*)buffer, bufsize, false);
    
	return bufsize;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(__unused uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
	// Bounds check
	if (lba >= MSC_BLOCK_COUNT) return -1;
    
	uint16_t base_addr = (lba * MSC_BLOCK_SIZE) + offset;
	uint32_t bytes_written = 0;
    
	while (bytes_written < bufsize) {
		uint16_t current_addr = base_addr + bytes_written;

		// We can only write up to 32 byte pages
		uint32_t chunk_size = bufsize - bytes_written;
		uint32_t max_chunk = EEPROM_PAGE_SIZE - (current_addr % EEPROM_PAGE_SIZE);
		if (chunk_size > max_chunk) {
			chunk_size = max_chunk;
		}
        
		// 2 bytes for address + payload
		uint8_t tx_buf[2 + EEPROM_PAGE_SIZE]; 
		tx_buf[0] = (current_addr >> 8) & 0xFF;
		tx_buf[1] = current_addr & 0xFF;
		memcpy(&tx_buf[2], buffer + bytes_written, chunk_size);

		// Send address + chunk data
		i2c_write_blocking(I2C_PORT, EEPROM_ADDR, tx_buf, 2 + chunk_size, false);

		// RP2040 doesn't support 0 byte writes so wait for the write to complete by waiting until a read succeeds
		absolute_time_t timeout = make_timeout_time_ms(15); 
		while (absolute_time_diff_us(get_absolute_time(), timeout) > 0) {
			uint8_t dummy;

			// We get <0 for a NACK when it's still busy or >=0 ACK when finished
			int result = i2c_read_blocking(I2C_PORT, EEPROM_ADDR, &dummy, 1, false);

			if (result >= 0) {
				break; // Got ACK! EEPROM is ready for next page.
			}
			sleep_us(200); // Sleep 0.2ms before polling again
		}
      
		bytes_written += chunk_size;
	}
    
	return bufsize;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], __unused void* buffer, __unused uint16_t bufsize) {

	switch(scsi_cmd[0]) {
		default:
		// Set Sense = Invalid Command Operation
		tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

		// negative means error -> tinyusb could stall and/or response with failed status
		return -1;
	}
}
