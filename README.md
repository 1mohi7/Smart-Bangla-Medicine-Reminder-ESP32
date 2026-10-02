# Smart Medicine Reminder System Using ESP32

An ESP32-based smart medicine reminder and management system designed to help users take medicines at the correct time and keep track of their medicine and body-weight history.

The system provides a touchscreen-based user interface with Bangla labels, scheduled medicine reminders, buzzer and voice notifications, three servo-controlled medicine compartments, medicine history, and body-weight measurement using load cells and an HX711 module.

The project uses bitmap-based Bangla interface labels stored in separate header files. The supplied headers include labels for the main medicine interface, weight measurement/history screens, and servo door status.  

## Main Features

- ESP32-based central controller
- Touchscreen graphical user interface
- Bangla user-interface labels
- Three separate user profiles
- Morning, afternoon, and night medicine schedules
- Automatic medicine reminders
- Buzzer melody before voice notification
- DFPlayer Mini based voice alerts
- Three servo-controlled medicine compartments
- Medicine Taken, Skipped, Missed, Early, and Late status tracking
- Early and late medicine-taking option
- Daily medicine history
- Persistent medicine history using ESP32 Preferences/NVS
- Body-weight measurement using load cells and HX711
- Separate weight history for each user
- Weight-history graph
- Persistent weight records after ESP32 restart
- DS3231 RTC for real-time scheduling
- Touch-based manual control and testing options

The interface includes separate Morning, Afternoon, and Night labels as well as medicine-status labels such as Taken and Missed. 

---

# Hardware Required

- ESP32 Development Board
- ILI9341 TFT Display
- XPT2046 Touch Controller
- DS3231 RTC Module
- DFPlayer Mini / MP3-TF-16P audio module
- Speaker
- Buzzer
- 3 × Servo Motors
- HX711 Load Cell Amplifier
- 4 × Load Cells / digital weighing-scale platform
- Medicine compartment enclosure
- Jumper wires
- Suitable power supply

---

# Pin Configuration

| Device | ESP32 Pin |
|---|---:|
| TFT CS | GPIO 5 |
| TFT DC | GPIO 2 |
| TFT RST | GPIO 4 |
| Touch CS | GPIO 15 |
| Touch IRQ | GPIO 27 |
| RTC SDA | GPIO 21 |
| RTC SCL | GPIO 22 |
| Buzzer | GPIO 26 |
| HX711 DT | GPIO 34 |
| HX711 SCK | GPIO 14 |
| DFPlayer RX | GPIO 33 |
| DFPlayer TX | GPIO 32 |
| Morning Servo | GPIO 13 |
| Afternoon Servo | GPIO 12 |
| Night Servo | GPIO 25 |

> Make sure all modules share a common GND with the ESP32.

---

# Required Arduino Libraries

Install the following libraries using **Arduino IDE → Library Manager**:

```text
Adafruit GFX Library
Adafruit ILI9341
XPT2046_Touchscreen
RTClib
DFRobotDFPlayerMini
ESP32Servo
HX711
```

The following libraries are provided by the ESP32/Arduino environment and normally do not require separate installation:

```text
SPI
Wire
Preferences
```

---

# Repository File Structure

Keep the main firmware and all three header files inside the same Arduino sketch folder.

```text
Smart-Medicine-Reminder-ESP32/
│
├── Smart_Medicine_Reminder.ino
├── BanglaLabels.h
├── WeightBanglaLabels_persistent_final.h
├── ServoBanglaLabels.h
└── README.md
```

The Bangla label files contain bitmap data stored in `PROGMEM`. For example, the weight interface provides labels for measuring weight, viewing weight history, saving measurements, and displaying a weight graph. 

### Important

The firmware includes the files using these names:

```cpp
#include "BanglaLabels.h"
#include "WeightBanglaLabels_persistent_final.h"
#include "ServoBanglaLabels.h"
```

Therefore, before uploading the project to GitHub or compiling it, rename the uploaded files to exactly these filenames if they currently contain suffixes such as `(1)`, `(2)`, or `(3)`.

---

# Arduino IDE Setup

## 1. Install ESP32 Board Support

Open:

```text
Arduino IDE → Tools → Board → Boards Manager
```

Search for:

```text
esp32
```

Install the ESP32 board package.

Then select the appropriate ESP32 development board from:

```text
Tools → Board
```

For a normal ESP32 DevKit, **ESP32 Dev Module** is usually appropriate.

---

# 2. Install Required Libraries

Open:

```text
Sketch → Include Library → Manage Libraries
```

Install all libraries listed in the **Required Arduino Libraries** section.

---

# 3. Prepare the Project Folder

Create one Arduino sketch folder and place these files together:

```text
Smart_Medicine_Reminder.ino
BanglaLabels.h
WeightBanglaLabels_persistent_final.h
ServoBanglaLabels.h
```

Do not place the header files in separate folders.

---

# 4. Connect the RTC

The project uses a **DS3231 RTC**.

Connect:

```text
DS3231 SDA → ESP32 GPIO21
DS3231 SCL → ESP32 GPIO22
VCC        → suitable supply
GND        → GND
```

The RTC provides the actual date and time used for medicine scheduling and history records.

If the RTC has lost power, the firmware initially sets the RTC using the compilation date and time.

After setting the RTC correctly, the backup battery should preserve the time when the ESP32 is disconnected.

---

# 5. Connect the Weight Machine

The system uses an **HX711 module and load cells**.

Connect the HX711 digital interface as follows:

```text
HX711 DT  → ESP32 GPIO34
HX711 SCK → ESP32 GPIO14
```

The firmware uses the calibration factor:

```cpp
24240.0
```

The system takes multiple samples to obtain a more stable weight measurement.

### Important Startup Instruction

Keep the weighing platform **completely empty while the ESP32 starts**.

During startup, the system waits for the HX711 and automatically performs tare/zero calibration.

The weight interface contains dedicated Bangla messages for keeping the scale empty, preparing the scale, zeroing it, successful measurements, and unavailable scale conditions. 

---

# 6. Connect the Servo-Controlled Compartments

The system contains three medicine compartments.

```text
Morning Servo   → GPIO13
Afternoon Servo → GPIO12
Night Servo     → GPIO25
```

The current calibrated positions are:

| Compartment | Closed Angle | Open Angle |
|---|---:|---:|
| Morning | 70° | 140° |
| Afternoon | 70° | 150° |
| Night | 165° | 80° |

These angles depend on the physical mounting of the servo motors.

If your doors do not fully open or close, adjust the corresponding open and closed angles in the firmware.

The servo interface also uses Bangla bitmap labels representing door open, close, opened, and closed states. 

---

# 7. Connect the DFPlayer Mini

Connect the DFPlayer serial interface:

```text
DFPlayer TX → ESP32 GPIO33
DFPlayer RX → ESP32 GPIO32
GND         → ESP32 GND
VCC         → suitable supply
Speaker     → DFPlayer speaker output
```

The project communicates with the DFPlayer at:

```text
9600 baud
```

The firmware uses a high audio volume setting:

```cpp
player.volume(30);
```

---

# 8. Prepare the DFPlayer SD Card

Format a microSD card and create the folder:

```text
MP3
```

The reminder audio files should follow DFPlayer MP3-folder numbering.

The firmware currently uses tracks:

```text
0001.mp3
0002.mp3
0003.mp3
0004.mp3
0005.mp3
0006.mp3
0007.mp3
0008.mp3
0009.mp3
0010.mp3
0011.mp3
```

Tracks `1–9` are used for the different medicine reminder stages, while the final tracks are used for confirmation actions such as medicine taken or skipped.

Insert the microSD card into the DFPlayer before starting the ESP32.

---

# 9. Connect the Buzzer

Connect the buzzer control input to:

```text
GPIO26
```

The firmware plays a short melody before a voice notification so that the user notices the upcoming instruction.

For a larger buzzer or speaker requiring more current, use a transistor driver instead of powering it directly from an ESP32 GPIO.

---

# 10. TFT and Touchscreen Connections

The firmware uses an ILI9341 TFT with an XPT2046 touch controller.

Important control pins are:

```text
TFT CS    → GPIO5
TFT DC    → GPIO2
TFT RESET → GPIO4

Touch CS  → GPIO15
Touch IRQ → GPIO27
```

SPI clock/data connections should follow the ESP32 SPI configuration used by your display module.

The touchscreen calibration values currently used in the firmware are:

```cpp
TS_LEFT   = 3700
TS_RIGHT  = 600
TS_TOP    = 590
TS_BOTTOM = 3500
```

If touch locations do not match the displayed buttons, recalibrate these values for your touchscreen.

---

# Uploading the Firmware

After completing the connections:

1. Connect the ESP32 to the computer using USB.
2. Open the `.ino` file in Arduino IDE.
3. Select the correct ESP32 board.
4. Select the correct COM port.
5. Verify/Compile the sketch.
6. Fix any missing-library errors if shown.
7. Click **Upload**.
8. Open Serial Monitor at:

```text
115200 baud
```

The Serial Monitor can be used to check initialization messages from the RTC, servos, DFPlayer, and HX711.

---

# First Startup Procedure

For the first startup:

1. Keep the weighing platform empty.
2. Power on the ESP32.
3. Allow the system to initialize the display, touchscreen, RTC, servos, DFPlayer, and HX711.
4. Wait while the scale performs its startup zeroing procedure.
5. Check that all three servo doors begin in the closed position.
6. Check that the touchscreen displays the user-selection screen.
7. Test each user profile.
8. Test the weight machine.
9. Test the buzzer and voice notification.
10. Test each servo compartment.

---

# Using the System

## Select a User

The system provides three user profiles.

Select the required profile from the home screen.

The interface contains separate bitmap labels for User 1, User 2, and User 3.

Inside a profile, the user can access functions such as:

```text
Medicine Schedule
Medicine History
Weight
Manual/Early/Late Medicine
```

---

# Medicine Schedule

Each user has three medicine periods:

```text
Morning
Afternoon
Night
```

The current firmware contains predefined medicines and times for each user.

These can be changed from the medicine configuration section of the Arduino code.

At the scheduled time, the system starts the reminder process automatically.

---

# Reminder Operation

The basic reminder sequence is:

```text
Scheduled medicine time arrives
          ↓
Buzzer melody plays
          ↓
Voice reminder plays
          ↓
Corresponding medicine door opens
          ↓
User responds
          ↓
Taken / Skipped / Missed status recorded
```

The reminder interface includes first-reminder, second-reminder, and confirmation labels.

The normal scheduled response window is approximately:

```text
5 minutes
```

If the medicine is not confirmed within the allowed period, the system can mark the dose as missed according to the firmware logic.

---

# Early and Late Medicine

The project also allows a user to take medicine outside the exact scheduled time.

The firmware provides:

```text
Early medicine
Late medicine
```

with a manual confirmation process.

The manual medicine-taking window is approximately:

```text
5 minutes
```

The resulting state is stored separately as **Early** or **Late** instead of normal **Taken**.

---

# Medicine History

The medicine history stores the status of each scheduled dose.

Possible states include:

```text
Ready
Taken
Skipped
Missed
Early
Late
```

The system stores the current day's medicine state in ESP32 internal flash using **Preferences/NVS**.

Therefore, restarting the ESP32 during the same day does not immediately erase the current medicine history.

The daily state is refreshed when the date changes.

---

# Measuring Body Weight

From a user profile:

```text
User Profile
     ↓
Weight
     ↓
Measure Weight
```

Stand on the weighing platform and allow the reading to stabilize.

The system provides a live-weight screen and can save the measurement to the selected user's history. The Bangla weight interface explicitly contains live-weight, last-saved, save, and history labels. 

---

# Saving Weight

When a valid measurement is displayed, press the **Save** option.

The measurement is stored with its date/time.

Weight records are separated for each user.

The project can store multiple measurements and display previous values later.

---

# Weight History

Navigate to:

```text
Profile
   ↓
Weight
   ↓
Weight History
```

The system can display previously saved measurements and their associated time information.

The supplied interface also includes a dedicated weight-history bitmap.

---

# Weight Graph

The project can show stored weight records graphically so that changes in body weight can be observed over time.

Select the **Graph** option from the weight-history interface.

A dedicated Bangla weight-graph label is included in the firmware resources.

---

# Persistent Storage

ESP32 **Preferences/NVS** is used to preserve important information after power loss.

The system stores:

```text
Current-day medicine state/history
User 1 weight records
User 2 weight records
User 3 weight records
```

This means saved weight data remains available after restarting the ESP32.

---

# Changing Medicine Names and Times

Medicine schedules are defined in the main `.ino` file.

Each medicine entry contains:

```cpp
medicine name
hour
minute
```

To customize the system, change the medicine names and corresponding schedule values before uploading the program.

If a completely new medicine name is added, an appropriate display bitmap may also need to be added to `BanglaLabels.h`.

---

# Changing Servo Angles

If a medicine door does not properly open or close, modify:

```cpp
servoCloseAngle[]
servoOpenAngle[]
```

Adjust one servo at a time and test carefully to avoid forcing the servo against the mechanical end stop.

---

# HX711 Calibration

The current project calibration factor is:

```cpp
24240.0
```

If a different weighing platform or load-cell arrangement is used, recalibration may be required.

General calibration procedure:

1. Remove all weight from the platform.
2. Tare the scale.
3. Place a known reference weight.
4. Compare the displayed and actual weight.
5. Adjust the calibration factor.
6. Repeat until the reading is sufficiently accurate.

---

# Troubleshooting

### Display does not turn on

Check:

```text
TFT power
SPI wiring
CS
DC
RESET
Common GND
```

### Touch position is incorrect

Recalibrate:

```cpp
TS_LEFT
TS_RIGHT
TS_TOP
TS_BOTTOM
```

### RTC ERROR appears

Check:

```text
DS3231 power
GPIO21 SDA
GPIO22 SCL
Common GND
```

### Voice is not playing

Check:

```text
DFPlayer power
RX/TX wiring
microSD card
MP3 folder
audio filenames
speaker connection
```

### Servo does not move correctly

Check:

```text
servo power supply
common GND
GPIO pin
open/close angle
mechanical obstruction
```

Do not power several high-current servos directly from the ESP32 3.3 V pin.

### Weight stays at zero or shows an error

Check:

```text
HX711 power
GPIO34 DT
GPIO14 SCK
load-cell wiring
common GND
calibration factor
```

Also restart the device with the weighing platform completely empty.

### Wrong weight is displayed

Re-tare the scale and recalibrate the HX711 calibration factor using a known weight.

---

# Notes

This project is an academic prototype intended to demonstrate an embedded smart medicine-management system.

It should not be treated as a medical device or as a substitute for professional medical advice, prescription instructions, or medical supervision.

---

# Project Summary

The system combines:

```text
ESP32
   +
Touchscreen UI
   +
RTC-based scheduling
   +
Buzzer and voice reminders
   +
Servo medicine compartments
   +
Medicine status/history
   +
HX711 body-weight measurement
   +
Persistent user data
```

to create an integrated smart medicine reminder and monitoring platform..
