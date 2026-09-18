GasDetector

A C++ Arduino project for real-time gas monitoring using an Arduino Uno R3, an MQ-2 gas sensor, a 16x2 LCD, and an active buzzer. 

Designed as an early-warning system, it alerts users to elevated gas concentrations before traditional fire alarms trigger. The software features non-blocking timing, automatic baseline drift recalibration, and a warm-up phase.

Features

- Non-Blocking Architecture: Built entirely using millis() timing patterns with no blocking delay() calls in the main loop.
- Warm-Up Phase: Features a non-blocking 20-second warm-up countdown on power-up.
- Gas Percentage Mapping: Converts raw ADC values into a percentage constrained between baseline clean air and maximum calibrated thresholds.
- Visual Display: Displays gas concentration percentage and operational status (Normal vs. WARNING!) on a 16x2 LCD screen with anti-ghosting line padding.
- Audible Alerts: Triggers a 3-chirp repeating buzzer burst when gas levels reach or exceed 80%.
- Auto-Baseline Recalibration: Automatically updates the minimum clean-air baseline if sensor readings remain stable within a set tolerance over an 8-hour period.

Hardware Requirements

- Microcontroller: Arduino Uno R3
- Sensor: MQ-2 Gas/Smoke Sensor
- Display: 16x2 Character LCD (HD44780 driven via parallel interface)
- Alert Output: Active Buzzer (5V)
- Potentiometer: 10k (LCD contrast control)
- Breadboard & Jumper Wires

Pin Mapping

Component | Pin Name | Arduino Uno R3 Pin
MQ-2 Sensor | Analog Out (AOUT) | A0
Buzzer | Signal (+) | Digital 9
LCD | Register Select (RS) | Digital 12
LCD | Enable (EN) | Digital 11
LCD | Data 4 (D4) | Digital 5
LCD | Data 5 (D5) | Digital 6
LCD | Data 6 (D6) | Digital 7
LCD | Data 7 (D7) | Digital 8

Project Structure

GasDetector/
GasDetector.ino    # Main Arduino source code file
README.md          # Project documentation

Getting Started

1. Clone the repository:
   git clone https://github.com/YOUR-USERNAME/GasDetector.git
   cd GasDetector

2. Open in VS Code or Arduino IDE:
   - Ensure you have the LiquidCrystal library installed (included by default in the Arduino AVR core).
   - Set board target to Arduino Uno.

3. Build and Upload:
   - Compile and upload GasDetector.ino to your Arduino Uno R3.
   - Open the Serial Monitor at 9600 baud to view real-time ADC logs and baseline updates.
