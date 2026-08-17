# LARC (Aircraft Launch  Control) HMI - FW-3011 Hammer Head

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![Qt Framework](https://img.shields.io/badge/Qt-UI_%26_Networking-41CD52.svg)
![Protocol](https://img.shields.io/badge/Protocol-OPC__UA_%7C_MQTT-orange.svg)
![Database](https://img.shields.io/badge/Database-SQLite-003B57.svg)

## Overview
The Launch Aircraft Control (LARC) Human-Machine Interface (HMI) is the primary Ground Control Station (GCS) software designed for the **FW-3011 Hammer Head UAV** (Canard-configuration). 

Developed in C++ utilizing the Qt Framework, this system bridges software ground control with physical launch hardware. It provides real-time telemetry monitoring, strict hardware interlock management, and secure launch protocols via direct PLC integration.

## Core Architecture & Avionics Integration
This HMI is built with strict aerospace fail-safes and real-time machine-to-machine (M2M) communication standards to ensure operator and hardware safety during the UAV launch sequence.

### Key Systems & Features
* **Real-Time PLC Telemetry (OPC UA):** Zero-latency monitoring of critical launch parameters including Rail Position, Ram Pressure, and System Interlocks.
* **Aerospace-Grade Fail-Safes:** 
  * **Dual Watchdog Timers:** Continuous software health checks and HMI-to-PLC network heartbeats. If the heartbeat drops, the PLC automatically safe-states the launcher.
  * **Zero-Latency Abort:** Hardware-level emergency abort triggers that bypass standard logic queues to instantly halt ordnance and actuation paths.
* **Multi-Factor Maintenance Authorization:** System diagnostic modes are protected by a dual-lock security framework requiring both a physical encrypted USB service drive and a remote MQTT cryptographic token.
* **PKI Certificate Validation:** Secures the OPC UA connection between the HMI and the SIMATIC PLC, rejecting unauthorized hardware nodes.
* **Embedded "Black Box" Telemetry:** A localized SQLite database maintains an immutable audit log of system states, operator authentication, and dynamic PLC mapping.

## Dependencies & Toolchain
To build and run the LARC HMI, the following toolchain is required:
* **C++17** standard compiler (GCC/Clang/MSVC)
* **Qt Framework** (Core, Gui, Widgets, Sql)
* **Qt OPC UA** module (with `open62541` backend)
* **Qt MQTT** module
* **SQLite3**

<img width="1334" height="699" alt="Screenshot From 2026-08-10 13-54-07" src="https://github.com/user-attachments/assets/43269219-f2c3-4aba-9729-47572526705c" />
<img width="1193" height="600" alt="Screenshot From 2026-08-17 18-49-35" src="https://github.com/user-attachments/assets/4808cac4-d63c-43c3-9628-5beac880dd19" />



## Build Instructions
```bash
# Clone the repository
git clone [https://github.com/yourusername/fw3011-larc-hmi.git](https://github.com/yourusername/fw3011-larc-hmi.git)
cd fw3011-larc-hmi

# Build using CMake
mkdir build && cd build
cmake ..
make -j4

# Execute the GCS kernel
./LARC_HMI
