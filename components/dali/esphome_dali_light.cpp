
#include <esphome.h>
#include "esphome_dali_light.h"
#include "esphome/core/log.h"

using namespace esphome;
using namespace dali;

using namespace esphome::light;

static const char *const TAG = "dali.light";

#define DALI_MAX_BRIGHTNESS_F (254.0f)

void dali::DaliLight::setup_state(light::LightState *state) {
    // Initialization code for DaliLight
    this->light_state_ = state;

    // Exclude broadcast and group addresses
    if ((this->address_ != ADDR_BROADCAST) && ((this->address_ & ADDR_GROUP_MASK) == 0)) {
        ESP_LOGD(TAG, "Querying DALI device capabilities...");
        if (bus->dali.isDevicePresent(address_)) {
            ESP_LOGD(TAG, "DALI[%.2x] Is Present", address_);

            uint8_t query_min = bus->dali.lamp.getMinLevel(address_);
            uint8_t query_max = bus->dali.lamp.getMaxLevel(address_);
            
            // Validate query results (0 or 255 typically indicate timeout/error)
            if (query_min >= 1 && query_min <= 254 && query_max >= 1 && query_max <= 254 && query_max > query_min) {
                this->dali_level_min_ = query_min;
                this->dali_level_max_ = query_max;
                this->dali_level_range_ = (float)(dali_level_max_ - this->dali_level_min_ + 1);
                ESP_LOGD(TAG, "Reported min:%d max:%d", this->dali_level_min_, this->dali_level_max_);
            } else {
                ESP_LOGW(TAG, "DALI[%.2x] Invalid query response (min=%d max=%d), keeping defaults", address_, query_min, query_max);
            }

            // Color temperature support disabled - only brightness mode used
            // If you need color temp, uncomment and set color_mode: COLOR_TEMPERATURE in YAML
            // this->tc_supported_ = bus->dali.color.isTcCapable(address_);

            // Schedule a delayed task to:
            // 1. Read actual device state FIRST (without changing lights)
            // 2. Send configuration commands AFTER syncing state
            this->set_timeout("dali_state_sync", 1000, [this]() {
                if (this->light_state_ == nullptr) return;

                // Step 1: Query current state from device
                uint8_t current_level = this->bus->dali.lamp.getCurrentLevel(this->address_);
                ESP_LOGD(TAG, "DALI[%.2x] Delayed state query returned: %d", this->address_, current_level);

                // Accept 0..255 (255 = full brightness on some devices)
                float brightness = 0.0f;
                if (current_level == 0) {
                    brightness = 0.0f;
                } else if (current_level >= 255) {
                    brightness = 1.0f; // clamp
                } else {
                    brightness = (current_level / DALI_MAX_BRIGHTNESS_F);
                }

                this->light_state_->current_values.set_brightness(brightness);
                this->light_state_->current_values.set_state(current_level > 0);
                this->light_state_->remote_values.set_brightness(brightness);
                this->light_state_->remote_values.set_state(current_level > 0);
                this->light_state_->publish_state();

                ESP_LOGD(TAG, "DALI[%.2x] Synced from bus: level=%d brightness=%.2f", this->address_, current_level, brightness);

                // Step 2: NOW send configuration commands (after state is synced)
                ESP_LOGD(TAG, "DALI[%.2x] Sending configuration to device...", this->address_);

                if (this->brightness_curve_.has_value()) {
                    switch (this->brightness_curve_.value()) {
                        case DaliLedDimmingCurve::LOGARITHMIC: ESP_LOGD(TAG, "Setting brightness curve to LOGARITHMIC"); break;
                        case DaliLedDimmingCurve::LINEAR:      ESP_LOGD(TAG, "Setting brightness curve to LINEAR"); break;
                    }
                    this->bus->dali.led.setDimmingCurve(this->address_, this->brightness_curve_.value());
                }

                if (this->fade_rate_.has_value()) {
                    ESP_LOGD(TAG, "Setting fade rate: %d", this->fade_rate_.value());
                    this->bus->dali.lamp.setFadeRate(this->address_, this->fade_rate_.value());
                }
                if (this->fade_time_.has_value()) {
                    ESP_LOGD(TAG, "Setting fade time: %d", this->fade_time_.value());
                    this->bus->dali.lamp.setFadeTime(this->address_, this->fade_time_.value());
                }
            });
        }
        else {
            ESP_LOGW(TAG, "DALI device at addr %.2x not found!", address_);
        }

        //bus->dali.dumpStatusForDevice(address_);
    }
    else {
        // TODO: How do we detect color temperature support for broadcast and group addresses?
    }


    // if (this->color_mode_.has_value()) {
    //     if (this->color_mode_.value() == DaliColorMode::COLOR_TEMPERATURE) {
    //         tc_supported_ = true;
    //         ESP_LOGD(TAG, "Override: enable color temperature support");
    //     } else {
    //         tc_supported_ = false;
    //         ESP_LOGD(TAG, "Override: disable color temperature support");
    //     }
    // }
}

light::LightTraits dali::DaliLight::get_traits() {
    light::LightTraits traits;

    // NOTE: This is called repeatedly, do not perform any bus queries here...

    // Force a color mode irrespective of what the device itself says it supports
    // eg. you can convert a CT capable device to a plain brighness device,
    // or force colour temperature support and hope the device recognizes the command...
    if (this->color_mode_.has_value()) {
        switch (this->color_mode_.value()) {
            case DaliColorMode::COLOR_TEMPERATURE: 
                this->tc_supported_ = true;
                traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
                traits.set_min_mireds(this->cold_white_temperature_);
                traits.set_max_mireds(this->warm_white_temperature_);
                break;
            case DaliColorMode::BRIGHTNESS:
                this->tc_supported_ = false;
                traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
                break;
            case DaliColorMode::ON_OFF:
                this->tc_supported_ = false;
                traits.set_supported_color_modes({light::ColorMode::ON_OFF});
                break;
        }
    }
    else {
        // Device reports color temperature support
        if (this->tc_supported_) {
            traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
            traits.set_min_mireds(this->cold_white_temperature_);
            traits.set_max_mireds(this->warm_white_temperature_);
        }
        else {
            traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
        }
    }

    return traits;
}

void dali::DaliLight::write_state(light::LightState *state) {
    bool on;
    float brightness;

    state->current_values_as_binary(&on);
    if (!on) {
        // User turned light OFF - send with fade
        bus->dali.lamp.setBrightness(address_, 0);
        return;
    }

    // Brightness-only mode
    state->current_values_as_brightness(&brightness);

    // Safety: use defaults if member variables are corrupted
    float range = this->dali_level_range_;
    uint8_t min = this->dali_level_min_;
    uint8_t max = this->dali_level_max_;
    if (range <= 0.0f || min < 1 || min > 254 || max < 1 || max > 254 || max <= min) {
      ESP_LOGW(TAG, "DALI[%d] Corrupted values (range=%.0f min=%d max=%d), using defaults", address_, range, min, max);
      range = 254.0f;
      min = 1;
      max = 254;
    }

    int dali_brightness = static_cast<uint8_t>(brightness * range) + min - 1;
    if (dali_brightness < 1) dali_brightness = 1;
    if (dali_brightness > 254) dali_brightness = 254;

    ESP_LOGD(TAG, "DALI[%d] B=%.2f (%d) range=%.0f min=%d max=%d", address_, brightness, dali_brightness, range, min, max);
    bus->dali.lamp.setBrightness(address_, (uint8_t)dali_brightness);
}
