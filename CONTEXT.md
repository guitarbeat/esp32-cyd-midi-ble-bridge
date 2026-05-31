# Architecture Language

Use these terms exactly when discussing or implementing changes. This ensures consistency between agents and human developers.

## Core Concepts

- **Module** — A unit of code with a defined **Interface** and **Implementation**. Could be a class (e.g., `BridgeUi`), a file, or a group of files.
- **Interface** — The total surface area a caller must understand to use a module. Includes public methods, types, expected state transitions, and error modes.
- **Implementation** — The internal logic and private state hidden behind the interface.
- **Depth** — The ratio of "value provided" to "interface complexity." A **Deep** module hides a lot of work behind a simple call. A **Shallow** module requires the caller to manage its internals.
- **Seam** — An interface that allows behavior to be swapped or mocked (e.g., for testing). One adapter is a hypothetical seam; two adapters make it a real seam.
- **Adapter** — A concrete implementation of an interface at a seam.
- **Locality** — The principle of keeping related data and behavior in one place to reduce cognitive load and bugs.
- **Leverage** — The power a caller gains by using a deep module without needing to know its internals.

## Domain Specifics

- **MidiBridge** — The central coordinator that routes MIDI packets between transports.
- **Transport** — A physical or logical MIDI connection (USB, BLE, RTP-MIDI, UART-MIDI).
- **BridgeUi** — The module responsible for all visual feedback and user input handling.
- **ConnectivityManager** — (Planned) A deep module to manage all network-related state (WiFi, Provisioning, RTP-MIDI).
