# ESPHome DALI Component - AI Coding Agent Instructions

This document provides guidance for AI coding agents working on the `esphome-dali` codebase.

## Project Overview

This project is an ESPHome custom component that provides a DALI (Digital Addressable Lighting Interface) master controller. It allows ESPHome devices to communicate with and control DALI-compliant lighting fixtures.

The codebase has a dual nature:
1.  **ESPHome Custom Component**: The primary use case. The C++ code in `components/dali/` is used to create a custom component that can be configured in ESPHome YAML files.
2.  **PlatformIO Library**: The core DALI logic is also structured as a PlatformIO-compatible Arduino library.

## Architecture

The project is centered around the `components/dali/` directory, which contains all the C++ source code, Python definitions for ESPHome, and the PlatformIO library manifest.

### Key Directories and Files

-   `components/dali/`: The main component directory.
    -   `dali.h`, `dali_port.cpp`: Implements the low-level, bit-banged DALI communication protocol. This is the core logic.
    -   `esphome_dali.h`, `esphome_dali.cpp`: The main ESPHome component that manages the DALI bus. It handles device discovery and address initialization.
    -   `esphome_dali_light.h`, `esphome_dali_light.cpp`: Implements the ESPHome `light` platform for DALI devices, allowing control over brightness and color temperature.
    -   `esphome_dali_output.h`, `esphome_dali_output.cpp`: Implements a simple ESPHome `output` component for broadcasting brightness levels.
    -   `light.py`, `output.py`: These Python files define the configuration schema for the `dali` light and output components within ESPHome. When adding new YAML configuration options, these files must be updated.
    -   `library.json`: The PlatformIO library manifest. It defines the library's properties and dependencies.
-   `poe_dali.yaml`: An example ESPHome configuration file demonstrating how to use the component.
-   `src/main.cpp`: The entry point for a standalone PlatformIO application, separate from the ESPHome integration.

### Data Flow

1.  **Configuration**: The user configures the DALI bus and devices in their ESPHome YAML file (e.g., `poe_dali.yaml`).
2.  **Initialization**: ESPHome parses the YAML and uses the schemas from `light.py` and `output.py` to instantiate the C++ components.
3.  **Bus Communication**: The `DaliBusManager` (`esphome_dali.cpp`) initializes the DALI bus (`dali_port.cpp`).
4.  **Control**: Home Assistant (or other clients) interacts with the ESPHome `light` or `output` entities. These calls are translated into DALI commands and sent over the bus via the bit-banging implementation in `dali_port.cpp`.

## Developer Workflows

### ESPHome Development

The primary workflow is developing the ESPHome component.

-   **Configuration**: Modify `poe_dali.yaml` to test different component settings.
-   **Build & Upload**: Use the ESPHome CLI to build and upload the firmware to a device.
    ```bash
    # To compile and upload to the device defined in poe_dali.yaml
    esphome run poe_dali.yaml
    ```
-   **Logs**: View device logs to debug runtime behavior.
    ```bash
    # To view logs from the device
    esphome logs poe_dali.yaml
    ```

### PlatformIO Library Development

To work on the core DALI logic as a standalone library:

-   **Build**: Use the PlatformIO CLI. The environment is defined in `platformio.ini`.
    ```bash
    # Build the project
    pio run

    # Clean build files
    pio run --target clean
    ```
-   **Entry Point**: The code in `src/main.cpp` is used when building with PlatformIO directly.

## Conventions

-   **ESPHome Integration**: C++ classes for ESPHome components inherit from `esphome::Component` and specific device classes (e.g., `esphome::light::LightOutput`). They are registered with macros (e.g., `REGISTER_LIGHT`).
-   **Python Schemas**: YAML configuration options are defined using the `voluptuous` schema validation library in the `.py` files within the component directory. Any new user-configurable parameter in YAML needs a corresponding entry here.
-   **Logging**: Use `ESP_LOGD`, `ESP_LOGI`, etc., from the ESPHome logging framework for debugging.
-   **DALI Commands**: DALI command constants are defined in `src/dali_commands.h`.
