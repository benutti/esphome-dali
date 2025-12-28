
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

            // NOTE: Some DALI controllers report their device type is LED(6) even though they do also support color temperature,
            // so let's explicitly check if they respond to this:
            this->tc_supported_ = bus->dali.color.isTcCapable(address_);
            if (tc_supported_) {
                ESP_LOGD(TAG, "DALI[%.2x] Supports color temperature", address_);

                // TODO: Don't seem to be getting the full range here?
                // Tc(cool)=153, Tc(warm)=370
                uint16_t coolest = bus->dali.color.queryParameter(address_, DaliColorParam::ColourTemperatureTcCoolest);
                uint16_t warmest = bus->dali.color.queryParameter(address_, DaliColorParam::ColourTemperatureTcWarmest);

                ESP_LOGD(TAG, "Tc(cool)=%d, Tc(warm)=%d", coolest, warmest);
                if (coolest > COLOR_MIREK_WARMEST || warmest > COLOR_MIREK_WARMEST) {
                    ESP_LOGW(TAG, "Tc min/max is out of range!");
                } else {
                    // Store reported coolest/warmest mired values for mapping.
                    // NOTE: Not updating the configuration-provided warm/cool values, those are for UI only.
                    // Ultimately we don't really want to trust the mired range reported by the dimmer
                    // as it depends on the LED strip attached. So we map the UI range into the reported range.
                    this->dali_tc_coolest_ = (float)coolest;
                    //this->dali_tc_warmest_ = (float)warmest;
                }
            }
            else {
                ESP_LOGD(TAG, "Does not support color temperature");
            }

            ESP_LOGD(TAG, "Sending configuration to device...");

            if (this->brightness_curve_.has_value()) {
                switch (this->brightness_curve_.value()) {
                    case DaliLedDimmingCurve::LOGARITHMIC: ESP_LOGD(TAG, "Setting brightness curve to LOGARITHMIC"); break;
                    case DaliLedDimmingCurve::LINEAR:      ESP_LOGD(TAG, "Setting brightness curve to LINEAR"); break;
                }
                bus->dali.led.setDimmingCurve(address_, this->brightness_curve_.value());
            }

            if (this->fade_rate_.has_value()) {
                ESP_LOGD(TAG, "Setting fade rate: %d", this->fade_rate_.value());
                bus->dali.lamp.setFadeRate(0, this->fade_rate_.value());
            }
            if (this->fade_time_.has_value()) {
                ESP_LOGD(TAG, "Setting fade time: %d", this->fade_time_.value());
                bus->dali.lamp.setFadeTime(0, this->fade_time_.value());
            }

            // bus->dali.lamp.setMinLevel(address_, 1);
            // bus->dali.lamp.setMaxLevel(address_, 254);

            // Schedule a delayed query to read the actual device state after boot.
            // We update the light state **without** sending a DALI command, so we don't
            // accidentally turn lights off during boot. Once synced, normal writes resume.
            this->set_timeout("dali_state_sync", 1000, [this]() {
                if (this->light_state_ == nullptr) return;

                uint8_t current_level = this->bus->dali.lamp.getCurrentLevel(this->address_);
                ESP_LOGD(TAG, "DALI[%.2x] Delayed state query returned: %d", this->address_, current_level);

                if (current_level <= 254) {
                    float brightness = (current_level > 0) ? (current_level / DALI_MAX_BRIGHTNESS_F) : 0.0f;

                    this->light_state_->current_values.set_brightness(brightness);
                    this->light_state_->current_values.set_state(current_level > 0);
                    this->light_state_->publish_state();

                    ESP_LOGD(TAG, "DALI[%.2x] Synced from bus: level=%d brightness=%.2f", this->address_, current_level, brightness);
                } else {
                    ESP_LOGW(TAG, "DALI[%.2x] Delayed query returned invalid value (%d)", this->address_, current_level);
                }

                this->state_synced_ = true;
            });
        }
        else {
            ESP_LOGW(TAG, "DALI device at addr %.2x not found!", address_);
            this->state_synced_ = true; // Allow user commands even if the device wasn't detected
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
    // Skip sending commands until we've synced once, to avoid turning lights off at boot.
    if (!this->state_synced_) {
        ESP_LOGD(TAG, "DALI[%d] Ignoring command until initial sync", address_);
        return;
    }

    bool on;
    float brightness;
    float color_temperature;

    static uint16_t last_temperature = 0;

    state->current_values_as_binary(&on);
    if (!on) {
        // Short cut: send power off command
        //bus->dali.lamp.turnOff(address_); // no fade
        bus->dali.lamp.setBrightness(address_, 0); // fade
        return;
    }

    if (tc_supported_) {
        state->current_values_as_ct(&color_temperature, &brightness);

        // Map temperature 0..1 to reported TC coolest/warmest mireds
        // NOTE: Not using the configuration warm/cool colours - these may not match the reported range of the DALI device.
        float color_temperature_mired = (color_temperature * (dali_tc_warmest_ - dali_tc_coolest_)) + dali_tc_coolest_;

        uint16_t dali_color_temperature = static_cast<uint16_t>(color_temperature_mired);

        // Only update if temperature has changed, to allow faster brightness changes
        if (dali_color_temperature != last_temperature) {
            last_temperature = dali_color_temperature;

            ESP_LOGD(TAG, "DALI[%d] Tc=%d", address_, dali_color_temperature);

            // IMPORTANT: Do not set start_fade (activate), or the color temperature fade will
            // be cancelled when we next call setBrightness, and no color change will occur.
            bus->dali.color.setColorTemperature(address_, dali_color_temperature, false);
        }
    } else {
        state->current_values_as_brightness(&brightness);
    }

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
