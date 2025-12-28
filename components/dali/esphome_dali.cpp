#include <esphome.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include "esphome_dali.h"
#include "esphome_dali_light.h"

//static const char *const TAG = "dali";
static const bool DEBUG_LOG_RXTX = false; // NOTE: Will probably trigger WDT

using namespace esphome;
using namespace dali;

void DaliBusComponent::run_discovery() {
    if (!m_discovery) {
        DALI_LOGW("Discovery not enabled in config");
        return;
    }
    
    DALI_LOGI("Starting DALI bus discovery...");
        // Optional: reset devices on the bus so we are in a known-good state.
        // Can help if devices are not responding to anything.
        if (false) {
            this->resetBus();
            esp_task_wdt_reset();
        }

        if (dali.bus_manager.isControlGearPresent()) {
            DALI_LOGD("Detected control gear on bus");
        } else {
            DALI_LOGW("No control gear detected on bus!");
        }

        // for (int i = 0; i <= ADDR_SHORT_MAX; i++) {
        //     if (m_addresses[i] != 0) {
        //         DALI_LOGD("Static config addr: %.2x", i);
        //     }
        // }

        if (this->m_initialize_addresses != DaliInitMode::DiscoverOnly) {
            if (this->m_initialize_addresses == DaliInitMode::InitializeAll) {
                DALI_LOGI("Randomizing addresses for *all* DALI devices");
                dali.bus_manager.initialize(ASSIGN_ALL); 
            } 
            else if (this->m_initialize_addresses == DaliInitMode::InitializeUnassigned) {
                // Only randomize devices without an assigned short address
                DALI_LOGI("Randomizing addresses for unassigned DALI devices");
                dali.bus_manager.initialize(ASSIGN_UNINITIALIZED); 
            }

            dali.bus_manager.randomize();
            dali.bus_manager.terminate();

            // Seem to need a delay to allow time for devices to randomize...
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        DALI_LOGI("Begin device discovery...");
        
        uint8_t count = 0;
        
        // For DiscoverOnly mode with pre-configured devices, poll short addresses
        if (this->m_initialize_addresses == DaliInitMode::DiscoverOnly) {
            DALI_LOGI("Polling short addresses 0-63...");
            
            for (short_addr_t addr = 0; addr <= ADDR_SHORT_MAX; addr++) {
                vTaskDelay(pdMS_TO_TICKS(1)); // yield to ESP stack
                esp_task_wdt_reset();
                
                if (dali.isDevicePresent(addr)) {
                    DALI_LOGI("  Found device @ %.2x", addr);
                    
                    // Dynamic component creation (if not defined in YAML)
                    if (m_addresses[addr]) {
                        DALI_LOGD("  Ignoring, already defined");
                    }
                    else {
                        m_addresses[addr] = 0; // No long address for pre-configured devices
                        create_light_component(addr, 0);
                        count++;
                    }
                }
            }
            
            DALI_LOGI("Discovery complete, found %d device(s)", count);
            return;
        }
        
        // For initialization modes, use random-address scanning
        dali.bus_manager.startAddressScan(); // All devices

        // Keep track of short addresses to detect duplicates
        bool duplicate_detected = false;
        bool is_discovered[ADDR_SHORT_MAX+1];
        for (int i = 0; i <= ADDR_SHORT_MAX; i++) {
            is_discovered[i] = false;
        }

        short_addr_t short_addr = 0xFF;
        uint32_t long_addr = 0;
        while (dali.bus_manager.findNextAddress(short_addr, long_addr)) {
            count++;
            vTaskDelay(pdMS_TO_TICKS(1)); // yield to ESP stack
            esp_task_wdt_reset();

            // if (short_addr == 0xFF) {
            //     if (this->m_initialize_addresses) {
                    
            //         //dali.bus_manager.programShortAddress(count);
            //         // short_addr_t new_addr = 1;
            //         // programShortAddress(new_addr);
            
            //         // port.sendSpecialCommand(DaliSpecialCommand::QUERY_SHORT_ADDRESS, 0);
            //         // out_short_addr = port.receiveBackwardFrame();
            
            //         // if (out_short_addr != new_addr) {
            //         //     DALI_LOGE("Could not program short address");
            //         //     out_short_addr = 0xFF;
            //         // }

            //         short_addr_t new_addr = count;

            //         dali.bus_manager.programShortAddress(new_addr);

            //         dali.port.sendSpecialCommand(DaliSpecialCommand::QUERY_SHORT_ADDRESS, 0);
            //         short_addr = dali.port.receiveBackwardFrame();
            
            //         if (short_addr != new_addr) {
            //             DALI_LOGE("  Could not program short address");
            //             continue;
            //         }
            //     }
            //     else {
            //         // You'll need to assign a short address before the device will respond to commands.
            //         // However it will still respond to BROADCAST brightness updates...
            //         DALI_LOGW("  No short address assigned!");
            //         continue;
            //     }
            // }

            if (short_addr <= ADDR_SHORT_MAX) {
                DALI_LOGI("  Device %.6x @ %.2x", long_addr, short_addr);

                // Duplicate detection
                if (is_discovered[short_addr]) {
                    if (m_initialize_addresses == DaliInitMode::DiscoverOnly) {
                        DALI_LOGW("  WARNING: Duplicate short address detected!");
                        duplicate_detected = true;
                        // TODO: Maybe don't register the component in this case?
                        // Brightness control will work, but reported capabilities will not be correct.
                    }
                    else {
                        // Assign a new address for this
                        short_addr++;
                        DALI_LOGD("  Duplicate short address detected, assigning a new address: %.2x", short_addr);

                        if (!dali.bus_manager.programShortAddress(short_addr)) {
                            DALI_LOGE("  Could not program short address");
                            short_addr = 0xFF;
                            continue;
                        }
                    }
                }
                else {
                    is_discovered[short_addr] = true;
                }

                // Dynamic component creation (if not defined in YAML)
                if (m_addresses[short_addr]) {
                    DALI_LOGD("  Ignoring, already defined");
                }
                else {
                    m_addresses[short_addr] = long_addr;
                    create_light_component(short_addr, long_addr);
                }
            }
            else if (short_addr == 0xFF) {
                if (m_initialize_addresses == DaliInitMode::DiscoverOnly) {
                    DALI_LOGI("  Device %.6x @ --", long_addr);
                    // You'll need to assign a short address before the device will respond to commands.
                    // However it will still respond to BROADCAST brightness updates...
                    DALI_LOGW("  No short address assigned!");
                    continue;
                }
                else {
                    short_addr = 1;
                    DALI_LOGI("  Assigning short address: %.2x", short_addr);

                    if (!dali.bus_manager.programShortAddress(short_addr)) {
                        DALI_LOGE("  Could not program short address");
                        short_addr = 0xFF;
                        continue;
                    }

                    DALI_LOGI("  Device %.6x @ %.2x", long_addr, short_addr);
                }
            }
        }

        DALI_LOGD("No more devices found!");
        dali.bus_manager.endAddressScan();

        if (duplicate_detected) {
            DALI_LOGW("Duplicate short addresses detected on the bus!");
            DALI_LOGW("  Devices may report inconsistent capabilities.");
            DALI_LOGW("  You should fix your address assignments!");
        }
}

void DaliBusComponent::create_light_component(short_addr_t short_addr, uint32_t long_addr) {
#ifdef USE_LIGHT
    DaliLight* dali_light = new DaliLight { this };
    dali_light->set_address(short_addr);

    const int MAX_STR_LEN = 20;
    char* name = new char[MAX_STR_LEN];
    char* id = new char[MAX_STR_LEN];
    snprintf(name, MAX_STR_LEN, "DALI Light %d", short_addr);
    snprintf(id, MAX_STR_LEN, "dali_light_%.6x", long_addr);
    // NOTE: Not freeing these strings, they will be owned by LightState.

    auto* light_state = new light::LightState { dali_light };
    light_state->set_component_source(LOG_STR("light"));
    App.register_light(light_state);
    App.register_component(light_state);
    light_state->set_name(name);
    light_state->set_object_id(id);
    light_state->set_disabled_by_default(false);
    light_state->set_restore_mode(light::LIGHT_RESTORE_DEFAULT_ON);
    light_state->add_effects({});

    DALI_LOGI("Created light component '%s' (%s)", name, id);
#else
    // Make sure you set discovery: true, or specify a light component somewhere in your YAML!
    DALI_LOGE("Cannot add light component - not enabled");
#endif
}

void DaliBusComponent::setup() {
    m_txPin->pin_mode(gpio::Flags::FLAG_OUTPUT);
    m_rxPin->pin_mode(gpio::Flags::FLAG_INPUT);
    DALI_LOGI("DALI bus ready");

    if (m_discovery) {
        run_discovery();
    }
}

void DaliBusComponent::loop() {

}

void DaliBusComponent::dump_config() {

}

#define QUARTER_BIT_PERIOD 208
#define HALF_BIT_PERIOD 416
#define BIT_PERIOD 833

void DaliBusComponent::writeBit(bool bit) {
    // NOTE: output is inverted - HIGH will pull the bus to 0V (logic low)
    bit = !bit;
    m_txPin->digital_write(!bit);
    esp_rom_delay_us(HALF_BIT_PERIOD-6);
    m_txPin->digital_write(bit);
    esp_rom_delay_us(HALF_BIT_PERIOD-6);
}

void DaliBusComponent::writeByte(uint8_t b) {
    for (int i = 0; i < 8; i++) {
        writeBit(b & 0x80);
        b <<= 1;
    }
}

uint8_t DaliBusComponent::readByte() {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte <<= 1;
        byte |= m_rxPin->digital_read();
        esp_rom_delay_us(BIT_PERIOD);
    }
    return byte;
}

void DaliBusComponent::resetBus() {
    DALI_LOGD("Resetting bus");
    m_txPin->digital_write(true);
    vTaskDelay(pdMS_TO_TICKS(1000));
    m_txPin->digital_write(false);
}

void DaliBusComponent::sendForwardFrame(uint8_t address, uint8_t data) {
    if (DEBUG_LOG_RXTX) {
        DALI_LOGD("TX: %02x %02x", address, data);
        delayMicroseconds(BIT_PERIOD*8);
        //Serial.print("TX: "); Serial.print(address, HEX); Serial.print(" "); Serial.println(data, HEX);
    }

    {
        // This is timing critical
        InterruptLock lock;

        writeBit(1); // START bit
        writeByte(address);
        writeByte(data);
        m_txPin->digital_write(false);
    }

    // Non critical delay
    esp_rom_delay_us(HALF_BIT_PERIOD*2);
    esp_rom_delay_us(BIT_PERIOD*4);
}

uint8_t DaliBusComponent::receiveBackwardFrame(unsigned long timeout_ms) {
    uint8_t data;

    int64_t startTime = esp_timer_get_time();

    // Wait for START bit (timing critical)
    while (m_rxPin->digital_read() == false) {
        if ((esp_timer_get_time() - startTime) / 1000 >= timeout_ms) {
            //Serial.println("No reply");
            if (DEBUG_LOG_RXTX) {
                DALI_LOGD("RX: 00 (NACK)");
            }
            return 0;
        }
    }

    {
        // This is timing critical
        InterruptLock lock;

        esp_rom_delay_us(BIT_PERIOD);
        esp_rom_delay_us(QUARTER_BIT_PERIOD);
        data = readByte();
        esp_rom_delay_us(BIT_PERIOD*2);
    }

    //Serial.print("RX: "); Serial.println(data, HEX);
    if (DEBUG_LOG_RXTX) {
        DALI_LOGD("RX: %02x", data);
    }

    // Minimum time before we can send another forward frame
    esp_rom_delay_us(BIT_PERIOD*8);
    return data;
}
