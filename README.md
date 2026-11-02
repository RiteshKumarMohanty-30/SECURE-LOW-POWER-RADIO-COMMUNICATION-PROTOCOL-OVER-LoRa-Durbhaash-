# Durbhaash – Secure Low-Power Radio Communication Protocol over LoRa

<p align="center">
  <strong>Secure. Offline. Long-Range. Communication When Networks Fail.</strong>
</p>

<p align="center">
  An ESP32-S3 and LoRa-based offline messaging system designed for emergency communication, remote locations and disaster-response scenarios.
</p>

---

## About the Project

**Durbhaash** is a secure, low-power, long-range wireless communication prototype developed to enable text communication without depending on cellular networks, mobile data or internet connectivity.

Built using the **Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)** platform, the system enables two-way communication between independent nodes through LoRa radio technology.

Each node provides a Wi-Fi-based local web interface, allowing users to connect their smartphones and exchange messages through a browser. The system incorporates AES-128-GCM encryption, message acknowledgements, automatic retries and replay protection to enhance communication security and reliability.

## Key Features

- **Offline Communication:** Direct node-to-node communication without cellular networks or internet.
- **Long-Range Connectivity:** Tested communication up to 3.5 km with obstacles and 4 km in clear line of sight.
- **AES-128-GCM Encryption:** Authenticated encryption for secure message transmission.
- **Acknowledgement System:** ACK-based message confirmation.
- **Automatic Retries:** Retransmission mechanism for unacknowledged messages.
- **Replay Protection:** Message identification and replay-window handling.
- **Web-Based User Interface:** Send and receive messages through a smartphone browser.
- **Wi-Fi SoftAP:** Each node creates its own local Wi-Fi access point.
- **OLED Display:** Displays communication status and message information.
- **Emergency Messaging:** Dedicated emergency message functionality.
- **Portable Design:** Custom-designed and completed 3D-printed enclosure.

## Hardware Components

| Component | Specification |
|---|---|
| Development Board | Heltec WiFi LoRa 32 V3 |
| Microcontroller | ESP32-S3 |
| LoRa Transceiver | SX1262 |
| Display | 128 × 64 OLED |
| Battery | 3.7 V, 700 mAh Li-ion |
| Communication | LoRa + Wi-Fi SoftAP |
| Enclosure | Custom 3D-printed case |

## System Architecture

The project consists of two independent communication nodes:

- **Node 1:** Transmits and receives messages to/from Node 2.
- **Node 2:** Transmits and receives messages to/from Node 1.

Each node handles message encryption, radio communication, acknowledgement processing and local smartphone connectivity.

The smartphone connects to the node's Wi-Fi access point and accesses the messaging interface through a web browser.

## Technology Stack

- **Embedded Programming:** Arduino IDE / C++
- **Microcontroller:** ESP32-S3
- **Wireless Communication:** LoRa (SX1262)
- **Security:** AES-128-GCM
- **Web Interface:** HTML, CSS and JavaScript
- **Local Connectivity:** Wi-Fi SoftAP
- **Display:** OLED
- **Hardware Design:** 3D Printing

## Performance and Testing

The prototype has undergone physical assembly, interface validation and field range testing.

| Parameter | Achieved Result |
|---|---|
| Communication Type | Two-way wireless messaging |
| Range with Obstacles | Up to 3.5 km |
| Clear Line-of-Sight Range | Up to 4 km |
| Web Interface | Successfully implemented and tested |
| 3D-Printed Enclosure | Completed |
| Encryption | AES-128-GCM |

*Note: The range figures represent the maximum distances observed during our tests under the tested conditions. Actual performance may vary depending on terrain, antenna placement, interference and environmental conditions.*

## Repository Structure

The repository includes the complete project resources, including firmware, hardware designs and demonstration materials.

```text
Durbhaash/
│
├── Code/
│   ├── Node_1/
│   │   └── Node_1.ino
│   └── Node_2/
│       └── Node_2.ino
│
├── 3D_Designs/
│   ├── CAD_Files/
│   ├── STL_Files/
│   └── Design_Preview/
│
├── Images/
│   ├── Prototype/
│   ├── Hardware/
│   └── Testing/
│
├── Videos/
│   ├── Demonstration/
│   └── Range_Testing/
│
├── Documentation/
│   ├── Block_Diagram/
│   └── Project_Report/
│
├── LICENSE
└── README.md
```

*Folder and file names may be adjusted to match the final uploaded repository.*

## Project Demonstrations

We are including visual documentation of the development and testing process.

- **Prototype Photographs:** Hardware assembly and completed devices.
- **Demonstration Videos:** Working communication between the two nodes and smartphone interface.
- **Range Testing Videos:** Field testing under obstructed and clear line-of-sight conditions.
- **3D Case Designs:** CAD and STL files of the custom enclosure.
- **Source Code:** Firmware for both communication nodes.

## How It Works

1. Power on both LoRa communication nodes.
2. Connect a smartphone to the Wi-Fi access point of either node.
3. Open the local web interface in a browser.
4. Compose and send a normal or emergency message.
5. The ESP32-S3 processes and encrypts the message.
6. The SX1262 transmits the encrypted packet over LoRa.
7. The receiving node authenticates and decrypts the message.
8. An acknowledgement is transmitted back to the sender.
9. The message is displayed on the web interface and OLED.

## Applications

- Disaster management and emergency communication.
- Remote and rural communication.
- Forest and wildlife field operations.
- Search-and-rescue coordination.
- Communication in areas with limited network connectivity.
- Field communication for disaster-response teams.

## Future Scope

- Integration of GPS-based location sharing.
- Expansion to a multi-node communication network.
- Improved battery optimisation and power management.
- Extended field testing and reliability analysis.
- Enhanced message delivery and network routing.
- Local-language interface support.

## Security and Regulatory Note

This repository is intended for educational, research and prototype development purposes.

The embedded development encryption key should be replaced with a properly managed key-distribution mechanism before any real-world deployment. Users should independently verify radio frequency, transmission power and applicable spectrum regulations before operating the hardware.

## Project Team

| Team Member | Contribution |
|---|---|
| **Chinmay Pandey** (2330023) | 3D Designing & Assembling |
| **Ritesh Kumar Mohanty** (2330107) | Arduino Coding & Testing |
| **Sambhav Pattnaik** (2330109) | Research & Documentation |
| **Nilay Pandey** (2330168) | Web Interface Development & Deployment |

## Acknowledgement

We sincerely acknowledge the guidance and support of our project supervisor and the School of Electronics Engineering, KIIT Deemed to be University, for providing us with the opportunity to explore and develop this project.

## License

This project is licensed under the **Apache License 2.0**. Refer to the [LICENSE](LICENSE) file for details.

---

<p align="center">
  <strong>Durbhaash – Bridging Communication Gaps When Conventional Networks Fail.</strong>
  <br>
  <em>Developed as an academic engineering project at KIIT Deemed to be University.</em>
</p>
