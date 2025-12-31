# ESPHome DALI Master Component

A production-ready DALI master controller for ESP32 that provides digital addressable lighting interface support. Implements automatic device discovery, address assignment, and brightness control for DALI-compliant lighting fixtures.

## Features

- **Automatic Device Discovery**: Scan DALI bus and detect all connected devices
- **Smart Address Assignment**: Automatically assign short addresses to uninitialized devices
- **Brightness Control**: Full dimming support with configurable fade times and curves
- **Group Support**: Control multiple lights via DALI group addressing
- **Capability Detection**: Query device capabilities and configure parameters
- **State Synchronization**: Reliable state sync on boot to prevent unwanted state changes
- **Diagnostic Tools**: Built-in buttons for bus scanning and state synchronization

## Installation

Add the external component to your ESPHome configuration:

```yaml
external_components:
  - source: github://benutti/espdali@main
    components: [dali]
```

## Quick Start

### Basic Configuration

```yaml
# Define the DALI bus
dali:
  id: dali_bus
  tx_pin: 14        # GPIO transmit
  rx_pin: 5         # GPIO receive
  discovery: true   # Auto-detect devices
  initialize_addresses: true  # Auto-assign addresses

# Individual light (specific address)
light:
  - platform: dali
    id: kitchen_light
    name: "Kitchen Dimmer"
    address: 0x00    # Short address 0-63
    restore_mode: RESTORE_DEFAULT_OFF
    fade_time: 1s
    fade_rate: 44724  # steps/second

  # Group (multiple devices, requires DALI group setup)
  - platform: dali
    id: kitchen_group
    name: "Kitchen All"
    address: 0x40    # Group 0
    restore_mode: RESTORE_DEFAULT_OFF
```

### Bus Scanning

Add diagnostic buttons to discover devices and group membership:

```yaml
button:
  - platform: template
    name: "Scan DALI Bus"
    on_press:
      - lambda: |-
          # Scans all addresses, detects devices, and group membership
          # Output includes ready-to-copy YAML configurations
```

## Configuration Options

### dali Component

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `tx_pin` | int | required | GPIO pin for DALI transmit |
| `rx_pin` | int | required | GPIO pin for DALI receive |
| `discovery` | bool | true | Automatically create lights for discovered devices |
| `initialize_addresses` | bool | true | Assign addresses to uninitialized devices |

### dali.light Platform

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `address` | int | required | Short address (0-63), group (0x40-0x4F), or broadcast (0x7F) |
| `restore_mode` | enum | RESTORE_DEFAULT_OFF | State restore on boot |
| `brightness_curve` | enum | LOGARITHMIC | LOGARITHMIC or LINEAR |
| `fade_time` | time | 1s | Transition duration (0-15 mapped values) |
| `fade_rate` | int | 44724 | Fade speed in steps/second |

## Boot State Protection

The component implements **two-layer protection** to prevent lights from changing state during ESP32 boot:

1. **Delayed State Sync**: Queries actual device brightness after boot
2. **Restore Mode**: Respects ESPHome `restore_mode` setting

Always use `restore_mode: RESTORE_DEFAULT_OFF` for safest operation.

## DALI Addressing

- **Short Address** (0–63): Individual device control. Format: `0AAAAAA` (7 bits)
- **Broadcast** (0x7F): All devices simultaneously
- **Group Address** (0x40–0x4F): Pre-configured groups (must be set in devices first)

## Hardware Wiring

Connect ESP32 GPIO pins to DALI bus via appropriate level-shifting circuit:

```
ESP32 TX (GPIO 14) → DALI TX
ESP32 RX (GPIO 5)  → DALI RX
ESP32 GND          → DALI GND
```

For production setups, use proper DALI line drivers with current limiting and isolation.

## Testing

Build and upload to ESP32:

```bash
esphome run poe_dali.yaml
```

Stream device logs:

```bash
esphome logs poe_dali.yaml
```

## Supported Devices

Tested with various DALI LED drivers and ballasts including:
- EOKE BK-DWL060 (63W CCT Driver)
- LTECH LM-75 (75W CCT Driver)
- LTECH MT-100 (48VDC LED Module)

## Project Structure

```
components/dali/
├── dali.h                      # Protocol definitions and DALI master class
├── dali_port.cpp              # Low-level bit-banged protocol (1200 baud)
├── dali_bus_manager.cpp       # Bus lifecycle and discovery
├── esphome_dali.cpp/.h        # ESPHome component integration
├── esphome_dali_light.cpp/.h  # Light platform implementation
├── light.py                   # YAML configuration schema
└── README.md                  # Component documentation
```

## Troubleshooting

**Lights changing state on boot**: Ensure all lights use `restore_mode: RESTORE_DEFAULT_OFF`

**Devices not discovered**: Verify TX/RX pin connections, check device DALI compliance

**State sync not working**: Check device responds to brightness queries (QUERY_ACTUAL_LEVEL)

## License

GPL-3.0
