# 🚗 ESP32 RC Car

### A DIY wireless RC car powered by ESP32

A custom-built 4WD RC car using an **ESP32**, an **L298N motor driver**, and a custom web-based controller. Drive wirelessly, control the speed, and explore DIY robotics!

---

## ✨ Features

* 🎮 **Web-Based Control** — Drive the car using a browser on your phone or computer.
* 📶 **Wireless Connectivity** — Connect directly to the ESP32 using its Wi-Fi access point.
* ⚡ **Adjustable Speed** — Change the motor speed using an on-screen slider.
* 🛞 **4-Wheel Drive** — Control four DC motors for movement.
* ↗️ **Multiple Directions** — Move forward, backward, left, right, or stop.
* 📱 **Browser Interface** — No dedicated mobile app required.

## 🧰 Hardware

| Component          | Purpose                |
| ------------------ | ---------------------- |
| ESP32 Dev Board    | Main controller        |
| L298N Motor Driver | Controls the DC motors |
| 4× DC Motors       | Drive the wheels       |
| RC Car Chassis     | Holds the components   |
| Battery Pack       | Powers the car         |
| Jumper Wires       | Electrical connections |

## 💻 Software & Technologies

* **C++** — Firmware and motor control
* **Arduino IDE** — Code development and uploading
* **HTML, CSS & JavaScript** — Web controller interface
* **ESP32 Wi-Fi** — Wireless communication
* **PWM** — Motor speed control

## 🚀 Getting Started

### 1. Upload the Firmware

Open the project in the Arduino IDE, select your ESP32 board, choose the correct port, and upload the code.

### 2. Power On

Connect the battery pack and power on the RC car.

### 3. Connect to Wi-Fi

Connect your phone or computer to the ESP32's Wi-Fi network.

### 4. Open the Controller

Open your browser and navigate to:

```text
http://192.168.4.1
```

### 5. Drive!

Use the directional buttons to move the car and the speed slider to adjust its speed.

## 🗺️ Pin Configuration

| Function      | ESP32 Pin |
| ------------- | --------: |
| Motor A — IN1 |   GPIO 27 |
| Motor A — IN2 |   GPIO 26 |
| Motor A — ENA |   GPIO 14 |
| Motor B — IN3 |   GPIO 33 |
| Motor B — IN4 |   GPIO 32 |
| Motor B — ENB |   GPIO 25 |

*Note: Each motor driver output controls one side of the car's drivetrain. Check your wiring and power connections before use.*

---
