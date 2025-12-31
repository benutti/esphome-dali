# ESPHome DALI Component - AI Coding Agent Instructions

This document provides guidance for AI coding agents working on the `esphome-dali` codebase.

## Project Overview

This is an ESPHome custom component that provides a DALI (Digital Addressable Lighting Interface) master controller. It allows ESPHome devices to communicate with and control DALI-compliant lighting fixtures.

**All code lives in `components/dali/`** - there are no standalone builds, this is ESPHome-only.

## Architecture

### Component Hierarchy

```
DaliBusComponent (esphome_dali.h/cpp)
├── DaliMaster & DaliBusManager (dali.h)
│   └── DaliSerialBitBangPort (dali_port.cpp) - low-level bit-banged protocol
├── DaliLight[] (esphome_dali_light.h/cpp) - light platform instances
└── DaliOutput[] (esphome_dali_output.h/cpp) - output platform instances
```

**Key files and responsibilities:**

- **`dali.h`**: Type definitions (`short_addr_t`, `DaliCommand` enum), address constants (`ADDR_BROADCAST`, `ADDR_SHORT_MAX`), device queries (`isControlGearPresent()`, `isDevicePresent()`)
- **`dali_port.cpp`**: Low-level bit-banging protocol (1200 baud, Manchester encoding). Timing-critical - must not be interrupted.
- **`esphome_dali.cpp`**: Bus lifecycle (setup, loop, discovery, address initialization). Uses `run_discovery()` for device enumeration and `create_light_component()` for dynamic creation.
- **`esphome_dali_light.cpp`**: Implements ESPHome `light::LightOutput` interface. Handles brightness/color-temp commands via `write_state()`.
- **`light.py`, `output.py`**: Python schemas using `voluptuous` for YAML config validation. Custom validators: `validate_fade_time()`, `validate_fade_rate()` with allowable ranges defined as formula-based lists.

### Data Flow Example

1. User specifies `dali_bus` + `dali_light` in YAML (`poe_dali.yaml`)
2. ESPHome parses YAML → `light.py` validates schema → instantiates `DaliBusComponent` + `DaliLight`
3. `DaliBusComponent.setup()` → device discovery → dynamically creates additional lights
4. Home Assistant commands light entity → calls `DaliLight.write_state()` → issues DALI command via bus

## Developer Workflows

### ESPHome Development

```bash
# Compile and upload to ESP32 device
esphome run poe_dali.yaml

# Stream device logs (non-blocking)
esphome logs poe_dali.yaml
```

- Modify `poe_dali.yaml` to test config changes
- Component classes inherit from `esphome::Component` (must implement `setup()`, `loop()`, `dump_config()`)
- Use `DALI_LOGD()`, `DALI_LOGI()`, `DALI_LOGW()` for logging (defined in `dali.h`)

## Critical Patterns

### Boot State Preservation

**CRITICAL**: Lights must NOT change state during ESP32 boot/restart. The component uses a two-layer protection:

1. **Boot Guard**: `boot_state_sync_complete_` flag blocks all `write_state()` calls during first second
2. **Delayed State Sync**: Reads actual DALI device state, updates ESPHome, then allows commands

**Boot sequence**:
1. `setup_state()` queries device capabilities
2. Schedules 1-second delayed callback
3. Any ESPHome restore attempts are blocked by boot guard in `write_state()`
4. Callback executes at t+1000ms:
   - Reads device state via `getCurrentLevel(address_)`
   - Updates ESPHome UI without commanding device
   - Sends config commands (fade rate/time, brightness curve)
   - Sets `boot_state_sync_complete_ = true` to enable normal operation
5. Normal operation resumes - commands now flow through

**Implementation**: [esphome_dali_light.cpp](components/dali/esphome_dali_light.cpp#L183-L186)
```cpp
void DaliLight::write_state(light::LightState *state) {
    // Boot guard - blocks ALL commands during sync window
    if (!this->boot_state_sync_complete_) {
        ESP_LOGD(TAG, "DALI[%.2x] Ignoring write_state during boot sync window", address_);
        return;  // Early exit, no DALI traffic
    }
    // ... normal command processing
}
```

**YAML Configuration**:
```yaml
light:
  - platform: dali
    name: "My Light"
    address: 0x00
    restore_mode: RESTORE_DEFAULT_OFF  # Cleanest option - restore if possible, default OFF
```

**Why RESTORE_DEFAULT_OFF works**: The boot guard blocks ALL commands during sync window. After sync, the actual device state is read and published, so ESPHome UI matches reality regardless of what restore_mode tried to do.

**When adding config commands**: Ensure they run inside the `set_timeout` callback AFTER state sync.

### Group Address Handling

DALI supports 16 group addresses (0x40-0x4F). Groups must be pre-configured in devices using DALI commissioning tools.

**YAML Example**:
```yaml
# Individual lights
- platform: dali
  id: light_1
  address: 0x08  # Short address
  
- platform: dali
  id: light_2
  address: 0x09

# Group (devices must be programmed to respond to group 0)
- platform: dali
  id: kitchen_group
  name: "Kitchen All"
  address: 0x40  # Group 0 = 0x40
  # No restore_mode for groups - they inherit state from members
```

**Important**: The component does NOT auto-detect group membership. Groups must be configured in DALI devices first using external tools. The group address simply broadcasts commands to all devices programmed for that group.

### DALI Address Handling

- **Short address** (0–63): Direct device reference. Format in `dali.h`: `0AAAAAA` (7 bits)
- **Broadcast** (0x7F): All devices. Format: `1111111`
- **Group address** (0–15): Format: `100AAAA` (requires group setup in device)
- Use `short_addr_t` typedef (uint8_t) for all address variables
- Constants: `ADDR_BROADCAST`, `ADDR_SHORT_MAX`, `ASSIGN_UNINITIALIZED`, `ASSIGN_ALL`

### Device Discovery & Initialization

`DaliBusComponent::run_discovery()` has three modes (set via `do_initialize_addresses()`):

1. **DiscoverOnly** (no initialization): Polls short addresses 0–63, skips unassigned devices
2. **InitializeUnassigned** (default): Randomizes addresses only for devices without short address, then scans
3. **InitializeAll**: Randomizes all devices, then performs random-address scanning

Each mode requires different DALI procedures:
- `randomize()` + `terminate()` for initial setup (requires 50ms delay per `vTaskDelay(pdMS_TO_TICKS(50))`)
- `startAddressScan()` + `findNextAddress()` loop for enumeration
- Dynamic light creation: `create_light_component(short_addr, long_addr)`

### Configuration Schema Pattern (Python)

```python
# light.py pattern for custom config options
def validate_fade_time(value):
    if isinstance(value, int):
        cv.int_range(0, 15)(value)  # Raw 0-15 values
        return value
    # Parse "100ms", "1s", "2m" format
    time = cv.positive_time_period_milliseconds(value)
    # Find matching ALLOWABLE_FADE_TIMES entry
    
CONFIG_SCHEMA = light.LIGHT_SCHEMA.extend({
    cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(DaliLight),
    cv.GenerateID(CONF_DALI_BUS): cv.use_id(DaliBusComponent),
    cv.Optional(CONF_ADDRESS): cv.int_,  # Device short address
    cv.Optional(CONF_FADE_TIME): validate_fade_time,  # Custom validator
}).extend(cv.COMPONENT_SCHEMA)
```

**When adding YAML options**: Update both `.py` schema AND C++ component class with matching getter/setter.

### Logging & Debugging

- **Protocol-level**: `DALI_LOGD(...)` from `dali.h` (conditional on `ESPHOME_LOG_LEVEL`)
- **Component-level**: `ESP_LOGD(TAG, ...)`, `ESP_LOGI()` etc. in `esphome_dali.cpp`
- **Warning**: `DEBUG_LOG_RXTX = false` in `esphome_dali.cpp` - enabling triggers watchdog timeout
- Use `esp_task_wdt_reset()` in long loops (discovery) to prevent ESP32 watchdog reset

## Integration Points & Dependencies

- **ESPHome framework**: Components inherit `esphome::Component`, platform classes from `esphome::light`, `esphome::output`
- **esp-idf** (ESP32 SDK): FreeRTOS, GPIO, timer APIs used in `dali.h` and `dali_port.cpp`
- **Python voluptuous**: YAML schema validation in `.py` files
- **External component loading**: Users load via GitHub in ESPHome YAML (see `README.md`)

## Common Tasks Reference

| Task | Key Files | Implementation Notes |
|------|-----------|---|
| Add DALI command | `dali.h` | Define enum value in `DaliCommand`, update command implementation |
| Add brightness curve | `esphome_dali_light.cpp`, `light.py` | Extend `DALI_BRIGHTNESS_CURVES` dict in `light.py` |
| Fix device discovery | `esphome_dali.cpp` | Check `run_discovery()` mode logic, verify address polling vs random-address scanning |
| Debug DALI protocol | `dali_port.cpp` | Enable `DEBUG_LOG_RXTX`, use serial monitor, watch for timing issues |
| Add new YAML option | `light.py`, `esphome_dali_light.h/cpp` | Add schema in `.py`, add class member + getter/setter in `.h/.cpp` |
| Fix boot state issues | `esphome_dali_light.cpp` | Check `setup_state()` for commands using broadcast address or causing visible changes |
| Broadcast control | `esphome_dali_output.cpp` | Uses `ADDR_BROADCAST` (0x7F) to control ALL lights simultaneously |