#include "dali.h"

/// @brief Automatically assign sequential short addresses to all devices on the DALI bus
/// @param assign ASSIGN_ALL, ASSIGN_UNINITIALIZED, or the short address for a specific device
/// @return The number of devices found on the bus
uint8_t DaliBusManager::autoAssignShortAddresses(uint8_t assign, bool reset) {
    if (reset) {
        DALI_LOGI("BEGIN AUTO ADDRESS ASSIGNMENT");
    } else {
        DALI_LOGI("BEGIN AUTO ADDRESS QUERY");
    }

    // for (int i = 0; i < 64; i++) {
    //     m_addresses[i] = 0;
    // }

    // Put all devices on the bus into initialization mode, where they will accept special commands
    initialize(assign);

    // Tell all devices to randomize their addresses
    if (reset) {
        DALI_LOGI("Randomizing addresses");
        randomize();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Binary search through address space to find lowest address
    uint8_t count = 0;
    while (true) {
        uint32_t addr = 0x000000; // Start with the lowest address

        //NOTE: This doesn't work with one of my controllers!!
        // // Shortcut: test if we are done
        // if (!compareSearchAddress(0xFFFFFF)) {
        //     Serial.println("No more devices found");
        //     break;
        // }

        for (uint32_t i = 0; i < 24; i++) {
            uint32_t bit = 1ul << (uint32_t)(23ul - i);
            uint32_t search_addr = addr | bit;
            // Serial.print("Searching for addr 0x");
            // Serial.println(search_addr, HEX);

            // True if actual address <= search_address
            bool compare_result = compareSearchAddress(search_addr);
            if (compare_result) {
                addr &= ~bit; // Clear the bit (already clear)
            } else {
                addr |= bit;  // Set the bit
            }
        }

        if (addr == 0xFFFFFF) {
            break; // No more devices found
        }

        // Need to increment by one to get the actual address
        addr++;

        DALI_LOGD("Found address: 0x%.6x", addr);

        // Sanity check: Address should still return true for comparison
        if (!compareSearchAddress(addr)) {
            DALI_LOGE("Address did not match?");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // Remove this device from the search
        withdraw(addr);

        // Sanity check: Address should no longer respond to comparison
        if (compareSearchAddress(addr)) {
            DALI_LOGE("Device did not withdraw");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }


        uint8_t short_addr = (count << 1);

        if (reset) {
            // Program short address
            // NOTE: an address of 0xFF will delete the short address
            port.sendSpecialCommand(
                DaliSpecialCommand::PROGRAM_SHORT_ADDRESS, 
                short_addr | DALI_COMMAND);
        }

        // Verify
        port.sendSpecialCommand(DaliSpecialCommand::VERIFY_SHORT_ADDRESS, short_addr | DALI_COMMAND);
        if (port.receiveBackwardFrame() == 0xFF) {
            DALI_LOGD("Short address: %.2x", short_addr);

            //m_addresses[count] = addr;
        } else {
            DALI_LOGE("Short address verification failed!");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        count++;
    }

    if (count == 0) {
        DALI_LOGE("No devices found");
    }

    // Exit initialization mode
    // Devices will respond to regular commands again
    terminate();

    //m_device_count = count;

    return count;
}


void DaliBusManager::startAddressScan() {
    if (!this->_is_scanning) {
        this->_is_scanning = true;
        // Put all devices on the bus into initialization mode, where they will accept special commands
        initialize(0);
    }
}

bool DaliBusManager::findNextAddress(short_addr_t& out_short_addr, uint32_t& out_long_addr) {
    if (!this->_is_scanning) {
        DALI_LOGE("Scan not started!");
        return false;
    }

    uint32_t addr = 0x000000; // Start with the lowest address

    // Shortcut: test if we are done
    if (!compareSearchAddress(0xFFFFFF)) {
        return false;
    }

    for (uint32_t i = 0; i < 24; i++) {
        uint32_t bit = 1ul << (uint32_t)(23ul - i);
        //uint32_t search_addr = addr | bit;
        addr |= bit;

        // True if actual address <= search_address
        bool compare_result = compareSearchAddress(addr);
        //DALI_LOGD("Test addr %.6x %.2x", addr, compare_result);
        if (compare_result) {
            addr &= ~bit; // Clear the bit
        } else {
            addr |= bit;  // Set the bit
        }
    }

    if (addr == 0xFFFFFF) {
        return false; // No more devices found
    }

    // Final step in the search, set last bit if no longer matching
    if (!compareSearchAddress(addr)) {
        addr++;
    }

    // Sanity check: Address should still return true for comparison
    if (!compareSearchAddress(addr)) {
        DALI_LOGE("ERROR: Address did not match?");
        return false;
    }

    // Remove this device from the search
    withdraw(addr);

    out_long_addr = addr;

    // Get short address
    port.sendSpecialCommand(DaliSpecialCommand::QUERY_SHORT_ADDRESS, 0);
    out_short_addr = port.receiveBackwardFrame();
    if (out_short_addr == 0) {
        DALI_LOGW("Short address not found for %.6x", addr);
        out_short_addr = 0xFF;
    }
    else if (out_short_addr <= ADDR_SHORT_MAX) {
        out_short_addr >>= 1; // remove command bit
    }

    return true;
}


void DaliBusManager::endAddressScan() {
    if (this->_is_scanning) {
        this->_is_scanning = false;
        // Exit initialization mode
        // Devices will respond to regular commands again
        terminate();
    }
}

#if 0
/// @brief 
/// @return The number of devices found on the bus
uint8_t DaliBusManager::scanAddresses(std::vector<uint32_t>& addresses) {
    uint8_t count = 0;

    const int MAX_SHORT_ADDRESS = 64;
    uint32_t m_addresses[MAX_SHORT_ADDRESS];
    for (int i = 0; i < MAX_SHORT_ADDRESS; i++) {
        m_addresses[i] = 0;
    }

    // TODO: Scanning entire space causes watchdog
    // [I][dali:136]: DALI[00] = 0x6721a8
    // E (11344) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
    // E (11344) task_wdt:  - loopTask (CPU 1)
    // E (11344) task_wdt: Tasks currently running:
    // E (11344) task_wdt: CPU 0: IDLE
    // E (11344) task_wdt: CPU 1: loopTask
    // E (11344) task_wdt: Aborting.
    // abort() was called at PC 0x400f2808 on core 0
    for (int i = 0; i < 4; i++) {
        yield();
        // TODO: reset WDT??
        if (isControlGearPresent(i)) {
            uint32_t addr = queryAddress(i);
            DALI_LOGI("DALI[%.2x] = 0x%.6x", i, addr);
            //count++;
        }
    }

    DALI_LOGI("Beginning full device scan...");



    // Binary search through address space to find lowest address
    // TODO: When two devices are connected, I am only seeing one.
    // Maybe the test for withdraw isn't right?
    
    while (true) {
        yield();
        getNextAddress(...);
 
        count++;
    }

    //m_device_count = count;

    DALI_LOGI("Scanning complete. %d devices found", count);

    return count;
}
#endif

void DaliMaster::dumpStatusForDevice(uint8_t addr) {
    // Not implemented for ESP-IDF build - use ESPHome logging instead
}
