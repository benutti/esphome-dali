#pragma once

#include <esphome.h>
#include "esphome/components/output/float_output.h"
#include "esphome_dali.h"

namespace esphome {
namespace dali {

/// @brief Map a float value to broadcasted DALI brightness
class DaliOutput : public Component, public output::FloatOutput {
public:
    DaliOutput(DaliBusComponent *bus)
    : bus(bus)
    { }

    void setup() override;
    void loop() override;
    void write_state(float state) override;

    void set_parent(DaliBusComponent *parent) {
        bus = parent;
    }

    float get_setup_priority() const override { return setup_priority::DATA; }

private:
    DaliBusComponent *bus;
};

}  // namespace dali
}  // namespace esphome
