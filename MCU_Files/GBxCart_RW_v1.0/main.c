/*
 GBxCart RW
 Version: 1.0
 Author: Alex from insideGadgets (www.insidegadgets.com)
 Created: 7/11/2016
 Last Modified: 11/03/2017
 
 GBxCart RW allows you to dump your Gameboy/Gameboy Colour/Gameboy Advance games ROM, save the RAM and write to the RAM.
 
 The ATmega32A talks to the cartridge and interfaces with the CH340G serial to USB converter with the PC.
 
 Remember to change your fuse bits so the ATmega32A uses an external 8MHz crystal, divide clock by 8 is off, JTAG disabled
 avrdude -c usbtiny -p atmega32 -U lfuse:w:0xef:m -U hfuse:w:0xd9:m
 
 */

// ATmega32A Pin Map
//
// VCC (5, 17, 38)		GND (6, 18, 28, 39)		AVCC (27)				XTAL (7 ,8)		LED PD3 (12)		Switch sense PD2 (11)
// SCK PB7 (3)				MOSI PB5 (1)				MISO PB6 (2)			RST (4)			RXD PD0 (9)			TXD PD1 (10)
// 
// Gameboy / Gameboy Colour
// A0-A7 PB0-7 (40-44, 1-3) 		A8-A15 PA0-7 (37-30)			D0-D7 PC0-7 (19-26)
// 
// Gameboy Advance
// AD0-AD7 PB0-7 (40-44, 1-3) 	AD8-AD15 PA0-7 (37-30)		A16-23 / D0-D7 PC0-7 (19-26)
// *WR PD4 (13)						*RD PD5 (14)					*MREQ/CS PD6 (15)			CS2/RST PD7 (16)

#define F_CPU 8000000 // 8 MHz
#define BUILD 1

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <stdlib.h>
#include <string.h>
#include "setup.c" // See defines, variables, constants, functions here


int main(void) {
	setup();
	
	uint32_t address = 0;
	uint8_t eepromSize = EEPROM_4KBIT;
	uint8_t cartMode = GB_MODE;
	uint8_t resetCommonLines = 1;
	
	while(1) {
		if (resetCommonLines == 1) {
			rd_wr_csmreq_cs2_reset();
		}
		receivedChar = USART_Receive(); // Wait for 1 byte of data
		
		// Read the pin to find out which mode we are in, if high (3.3V), we are powered by 5V
		if (PIND & (1<<SWITCH_DETECT)) {
			cartMode = GB_MODE;
		}
		else {
			cartMode = GBA_MODE;
		}
		
		// Return the cart mode in use
		if (receivedChar == CART_MODE) {
			USART_Transmit(cartMode);
		}
		
		// Change to GB mode or GBA mode if requested
		else if (receivedChar == GB_CART_MODE) {
			gb_mode();
		}
		else if (receivedChar == GBA_CART_MODE) {
			gba_mode();
		}
		
		// Set address
		else if (receivedChar == SET_START_ADDRESS) {
			usart_read_chars(); // Read start address
			address = strtol(receivedBuffer, NULL, 16); // Convert address string in hex to dec
		}
		
		
		// ****** Gameboy / Gameboy Colour ******
		
		// Read 64 bytes of ROM/RAM from address (and increment) until anything but 1 is received
		else if (receivedChar == READ_ROM_RAM) {
			gb_mode();
			receivedChar = '1';
			while (receivedChar == '1') {
				PORTD |= (1<<LED);
				for (uint8_t x = 0; x < 64; x++) {
					USART_Transmit(read_8bit_data(address));
					address++;
				}
				
				PORTD &= ~(1<<LED);
				receivedChar = USART_Receive();
			}
		}
		
		// Write 64 bytes to RAM on address (and increment)
		else if (receivedChar == WRITE_RAM) {
			gb_mode();
			
			// Read 64 bytes first as CH340G sends them all at once
			usart_read_bytes(64);
			
			PORTD |= (1<<LED);
			for (uint8_t x = 0; x < 64; x++) {
				write_8bit_data(address, receivedBuffer[x], MEMORY_WRITE);
				address++;
			}
			
			PORTD &= ~(1<<LED);
			USART_Transmit('1'); // Send back acknowledgement
		}
		
		// Set bank address and write a byte
		else if (receivedChar == SET_BANK) {
			gb_mode();
			
			usart_read_chars(); // Read start address
			uint16_t bankaddress = strtol(receivedBuffer, NULL, 16); // Convert address string in hex to dec
			
			receivedChar = USART_Receive(); // Wait for bank number
			if (receivedChar == 'B') {
				usart_read_chars(); // Read data
				uint8_t data = atoi(receivedBuffer); // Convert data string to dec
				
				write_8bit_data(bankaddress, data, BANK_WRITE);
			}
		}
		
		
		// ****** Gameboy Advance ******
		
		// ---------- ROM ----------
		// Read one 16bit byte from ROM using address (and increment) until anything but 1 is received
		else if (receivedChar == GBA_READ_ROM) {
			gba_mode(); 
			
			receivedChar = '1';
			while (receivedChar == '1') {
				PORTD |= (1<<LED);
				for (uint8_t x = 0; x < 32; x++) {
					uint16_t dataRead = gba_read_16bit_data(address);
					
					// Low byte & High byte
					USART_Transmit(dataRead & 0xFF);
					USART_Transmit(dataRead >> 8);
					
					address++;
				}
				
				PORTD &= ~(1<<LED);
				receivedChar = USART_Receive();
			}
		}
		
		
		// ---------- SRAM ----------
		// Read RAM from address (and increment) until anything but 1 is received
		else if (receivedChar == GBA_READ_SRAM) {
			gb_mode(); // Set GB mode as it uses 16 bit address with 8 bit data
			
			receivedChar = '1';
			while (receivedChar == '1') {
				PORTD |= (1<<LED);
				for (uint8_t x = 0; x < 64; x++) {
					USART_Transmit(gba_read_ram_8bit_data(address));
					address++;
				}
				
				PORTD &= ~(1<<LED);
				receivedChar = USART_Receive();
			}
			
			gba_mode(); // Set back
		}
		
		// Write to RAM on address (and increment) with 64 bytes of data
		else if (receivedChar == GBA_WRITE_SRAM) {
			gb_mode();
			
			usart_read_bytes(64);
			
			PORTD |= (1<<LED);
			for (uint8_t x = 0; x < 64; x++) {
				gba_write_ram_8bit_data(address, receivedBuffer[x]);
				address++;
			}
			USART_Transmit('1'); // Send back acknowledgement
			
			PORTD &= ~(1<<LED);
			gba_mode(); // Set back
		}
		
		// 'o' - Write 1 byte to SRAM address
		else if (receivedChar == GBA_WRITE_ONE_BYTE_SRAM) {
			gb_mode();
			
			uint8_t data = USART_Receive();
			gba_write_ram_8bit_data(address, data);
			USART_Transmit('1'); // Send back acknowledgement
			
			gba_mode(); // Set back
		}
		
		
		// ---------- FLASH ----------
		// Read the Flash Manufacturer and Device ID
		else if (receivedChar == GBA_FLASH_READ_ID) {
			gb_mode();
			
			flash_read_chip_id();
			USART_Transmit(flashChipIdBuffer[0]);
			USART_Transmit(flashChipIdBuffer[1]);
			
			gba_mode(); // Set back
		}
		
		// Change bank
		else if (receivedChar == GBA_FLASH_SET_BANK) {
			usart_read_chars(); // Read data
			uint8_t bank = atoi(receivedBuffer); // Convert data string to dec
			
			gb_mode();
			flash_switch_bank(bank);
			
			gba_mode(); // Set back
		}
		
		// Erase 4K sector on Flash (sector 0 to 15 for 512Kbit)
		else if (receivedChar == GBA_FLASH_4K_SECTOR_ERASE) {
			gb_mode();
			
			usart_read_chars(); // Read sector
			uint8_t sectorAddress = strtol(receivedBuffer, NULL, 16); // Convert address string in hex to dec
			
			flash_erase_4k_sector(sectorAddress);
			USART_Transmit('1'); // Send back acknowledgement
			
			gba_mode(); // Set back
		}
		
		// Write 64 bytes to Flash address one byte write at a time (and increment)
		else if (receivedChar == GBA_FLASH_WRITE_BYTE) {
			gb_mode();
			
			usart_read_bytes(64);
			
			PORTD |= (1<<LED);
			for (uint8_t x = 0; x < 64; x++) {
				flash_write_byte(address, receivedBuffer[x]);
				address++;
			}
			USART_Transmit('1'); // Send back acknowledgement
			
			PORTD &= ~(1<<LED);
			gba_mode(); // Set back
		}
		
		// Write 128 bytes to flash sector for Atmel flash (and increment)
		else if (receivedChar == GBA_FLASH_WRITE_ATMEL) {
			gb_mode();
			
			usart_read_bytes(128);
			
			PORTD |= (1<<LED);
			flash_write_sector(address); // Address used as sector number
			address++;
			USART_Transmit('1'); // Send back acknowledgement
			
			PORTD &= ~(1<<LED);
			gba_mode(); // Set back
		}
		
		
		// ---------- EEPROM ----------
		// Set EEPROM size
		if (receivedChar == GBA_SET_EEPROM_SIZE) {
			usart_read_chars(); // Read size
			eepromSize = strtol(receivedBuffer, NULL, 16); // Convert size to dec
		}
		
		// Read the EERPOM on address (and increment) until anything but 1 is received
		else if (receivedChar == GBA_READ_EEPROM) {
			gba_eeprom_mode();
			
			receivedChar = '1';
			while (receivedChar == '1') {
				PORTD |= (1<<LED);
				gba_eeprom_read(address, eepromSize);
				
				// Send back the 8 bytes of data
				for (uint8_t c = 0; c < 8; c++) {
					USART_Transmit(eepromBuffer[c]);
				}
				address++; // Increment to next 8 bytes
				
				PORTD &= ~(1<<LED);
				receivedChar = USART_Receive();
			}
			
			gba_mode(); // Set back
		}
		
		// Write 8 bytes to the EEPROM address (and increment)
		else if (receivedChar == GBA_WRITE_EEPROM) {
			gba_eeprom_mode();
			
			// Read 8 bytes from USART and place in buffer
			for (uint8_t x = 0; x < 8; x++) {
				eepromBuffer[x] = USART_Receive();
			}
			PORTD |= (1<<LED);
			
			gba_eeprom_write(address, eepromSize);
			address++;
			
			_delay_ms(8); // Wait for EEPROM to write data (8ms)
			USART_Transmit('1'); // Send back acknowledgement
			
			PORTD &= ~(1<<LED);
			gba_mode(); // Set back
		}
		
		
		// ---------- General commands ----------
		// Set any pin as input/output
		// Reads the DDR/PORT (e.g. DDRB/PORTB is 'B') and the hex value that represents pins to set to an input (e.g. PB7 is 0x80)
		else if (receivedChar == SET_INPUT || receivedChar == SET_OUTPUT) {
			char portChar = USART_Receive();
			usart_read_chars();
			uint8_t setValue = strtol(receivedBuffer, NULL, 16);
			
			PORTD |= (1<<LED);
			if (receivedChar == SET_INPUT) {
				if (portChar == 'A') {
					DDRA &= ~(setValue);
				}
				else if (portChar == 'B') {
					DDRB &= ~(setValue);
				}
				else if (portChar == 'C') {
					DDRC &= ~(setValue);
				}
				else if (portChar == 'D') {
					DDRD &= ~(setValue);
				}
			}
			else if (receivedChar == SET_OUTPUT) {
				if (portChar == 'A') {
					DDRA |= (setValue);
				}
				else if (portChar == 'B') {
					DDRB |= (setValue);
				}
				else if (portChar == 'C') {
					DDRC |= (setValue);
				}
				else if (portChar == 'D') {
					DDRD |= (setValue);
				}
			}
			PORTD &= ~(1<<LED);
		}
		
		// Set pin output as low
		else if (receivedChar == SET_OUTPUT_LOW) {
			char portChar = USART_Receive();			
			usart_read_chars();
			uint8_t setValue = strtol(receivedBuffer, NULL, 16);
			
			PORTD |= (1<<LED);
			if (portChar == 'A') {
				PORTA &= ~(setValue);
			}
			else if (portChar == 'B') {
				PORTB &= ~(setValue);
			}
			else if (portChar == 'C') {
				PORTC &= ~(setValue);
			}
			else if (portChar == 'D') {
				PORTD &= ~(setValue);
			}
			PORTD &= ~(1<<LED);
		}
		
		// Set pin output as high
		else if (receivedChar == SET_OUTPUT_HIGH) {
			char portChar = USART_Receive();			
			usart_read_chars();
			uint8_t setValue = strtol(receivedBuffer, NULL, 16);
			
			PORTD |= (1<<LED);
			if (portChar == 'A') {
				PORTA |= (setValue);
			}
			else if (portChar == 'B') {
				PORTB |= (setValue);
			}
			else if (portChar == 'C') {
				PORTC |= (setValue);
			}
			else if (portChar == 'D') {
				PORTD |= (setValue);
			}
			PORTD &= ~(1<<LED);
		}
		
		// Read all pins of a PORT and return the value
		else if (receivedChar == READ_INPUT) {
			char portChar = USART_Receive();			
			
			PORTD |= (1<<LED);
			if (portChar == 'A') {
				USART_Transmit(PINA);
			}
			else if (portChar == 'B') {
				USART_Transmit(PINB);
			}
			else if (portChar == 'C') {
				USART_Transmit(PINC);
			}
			else if (portChar == 'D') {
				USART_Transmit(PIND);
			}
			PORTD &= ~(1<<LED);
		}
		
		// Set the reset common lines variable on or off, useful if you are controlling all the pins directly
		else if (receivedChar == RESET_COMMON_LINES) {
			char commonChar = USART_Receive();
			if (commonChar == '1') {
				resetCommonLines = 1;
			}
			else if (commonChar == '0') {
				resetCommonLines = 0;
			}
		}
		
		// Send back the build version number
		else if (receivedChar == READ_BUILD_VERSION) {
			USART_Transmit(BUILD);
		}
	}
}