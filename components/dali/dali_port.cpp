#include "dali.h"

// ESP-IDF implementation
#define QUARTER_BIT_PERIOD 208
#define HALF_BIT_PERIOD 416
#define BIT_PERIOD 833

void DaliSerialBitBangPort::writeBit(bool bit) {
    // NOTE: output is inverted - HIGH will pull the bus to 0V (logic low)
    bit = !bit;
    gpio_set_level((gpio_num_t)m_txPin, bit ? 0 : 1);
    esp_rom_delay_us(HALF_BIT_PERIOD-6);
    gpio_set_level((gpio_num_t)m_txPin, bit ? 1 : 0);
    esp_rom_delay_us(HALF_BIT_PERIOD-6);
}

void DaliSerialBitBangPort::writeByte(uint8_t b) {
    for (int i = 0; i < 8; i++) {
        writeBit(b & 0x80);
        b <<= 1;
    }
}

uint8_t DaliSerialBitBangPort::readByte() {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte <<= 1;
        byte |= gpio_get_level((gpio_num_t)m_rxPin);
        esp_rom_delay_us(BIT_PERIOD);
    }
    return byte;
}

void DaliSerialBitBangPort::sendForwardFrame(uint8_t address, uint8_t data) {
    // Start bit
    writeBit(1);
    writeByte(address);
    writeByte(data);
    // Set line to idle for stop bits
    gpio_set_level((gpio_num_t)m_txPin, 0);
    esp_rom_delay_us(BIT_PERIOD*4); // Stop bits and settling time
}

uint8_t DaliSerialBitBangPort::receiveBackwardFrame(unsigned long timeout_ms) {
    int64_t startTime = esp_timer_get_time();
    
    // Wait for bus to be idle (HIGH) first
    while (gpio_get_level((gpio_num_t)m_rxPin) == 0) {
        if ((esp_timer_get_time() - startTime) >= (timeout_ms * 1000)) {
            return 0;
        }
    }
    
    // Wait for start bit (HIGH to LOW transition)
    while (gpio_get_level((gpio_num_t)m_rxPin) != 0) {
        if ((esp_timer_get_time() - startTime) >= (timeout_ms * 1000)) {
            return 0;
        }
    }
    
    // Now at start of start bit (LOW), wait to middle of first data bit
    // Start bit = 1 TE low + 1 TE high = 833us total
    // First data bit starts at 833us, middle is at 833us + 416us = 1249us
    esp_rom_delay_us(BIT_PERIOD + HALF_BIT_PERIOD);
    
    // Read 8 data bits (Manchester encoded: read at bit midpoint)
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data <<= 1;
        // Manchester: bit value is the level in second half of bit period
        // We're reading at the transition point which gives us the data value
        data |= gpio_get_level((gpio_num_t)m_rxPin) ? 1 : 0;
        esp_rom_delay_us(BIT_PERIOD);
    }
    
    return data;
}