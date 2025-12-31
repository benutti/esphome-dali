
#include <esphome.h>
#include "esphome_dali_output.h"

using namespace esphome;
using namespace dali;

static const char *const TAG = "dali.output";

void DaliOutput::setup() {

}

void DaliOutput::loop() {

}

void DaliOutput::write_state(float state) {
    if (bus == nullptr) {
        return;
    }

    // Convert ESPHome 0.0-1.0 state to DALI brightness 0-254
    uint8_t level = static_cast<uint8_t>(state * 255.0f);
    if (level > 254) {
        level = 254;
    }
    if (level < 0) {
        level = 0;
    }

    // BROADCAST to ALL devices on the bus (ADDR_BROADCAST = 0x7F)
    // This component controls all lights simultaneously
    // Use dali_light instead if you want individual control
    bus->dali.lamp.setBrightness(ADDR_BROADCAST, level);
}
