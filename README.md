# AutoSort — Autonomous Trash Sorting System

**After seeing how hard it is for humans to memorize how to sort trash correctly, and how people are often way too lazy to do it correctly I decided to make this device. 
It's meant to be simple, requiring the user to do absolutely nothing but dropping in the trash. Also, unlike existing smart trashcans, it can sort 4 items at once instead of just one.**
**NONE OF THE CAD OR MECHANICS WAS AI GENERATED, ONLY THE CODE WHICH I HAVE NOT LOGGED TIME FOR.**
---
<img width="538" height="547" alt="Screenshot 2026-06-20 at 10 06 29 PM" src="https://github.com/user-attachments/assets/a5820709-77ae-47dc-b0ab-983227915157" />
(I dont have render access sorry in onshape anymore 😭 hope this suffices)



A 4-DOF robotic trash sorting system built. Items are dropped into the sorter's base, which has 4 slots for four seperate items, and the machine opens each slot and tilts to eject/discard that item.

---
ONSHAPE LINK: https://cad.onshape.com/documents/374a3ea9606b4332d25f084a/w/b8f0205f0cfaa6895c93c91e/e/6ee7d91d01f9acebc8ed9dcf?renderMode=0&uiState=6a35b72990a847a7145c244e

## How It Works

1. User drops trash through the input flap on top
2. An upward-facing(or possible downwards facing based on how it works out) **Logitech C270** camera captures the item
3. A **YOLOv8** model classifies the item (plastic, paper, landfill, etc.)
4. The bowl **rotates** via a gear-reduced DC motor to face the correct bin sector
5. The **Stewart platform tilts** to eject the item into the target bin
6. The platform re-levels and waits for the next item

---

<img width="538" height="547" alt="Screenshot 2026-06-20 at 10 07 12 PM" src="https://github.com/user-attachments/assets/e5b846e3-629b-4858-8be5-9025f8faa827" />

## Mechanical Design

### Stewart Platform (3 DOF)
- 3-leg parallel manipulator providing tilt in any direction
- **Arm length:** 38mm | **Pushrod length:** 53mm
- **Footprint:** 300×300mm base
- Actuated by **3× MG996R servos**
- Inverse kinematics solved on-device in C++

### Rotation Axis (4th DOF)
- DC motor with gear reduction drives bowl spin
- **Lazy Susan bearing** supports the rotating platform
- Rotary encoder at center tracks absolute bowl position relative to bins
- Enables full continuous rotation or homing

### Bowl & Bins
- **Bowl diameter:** 220mm rotating platform
- **3 bins:** 180×180×180mm, arranged at 120° intervals
- Trash ejected off bowl edge directly into bin below
- **4 servo-actuated input flaps** on the static outer ring for user access
- TPU rim on flap edges to seal the bowl-to-bin transition

### Camera Pod
- Logitech C270 mounted upward-facing below the bowl
- Manual focus mod for close-range item capture
- Fixed position on static base — not affected by tilt or rotation

---

## Electronics

| Component | Purpose |
|---|---|
| Arduino (main) | IK solver, servo control, system coordination |
| PCA9685 | 16-channel PWM driver — mounted on rotating platform |
| MG996R ×3 | Stewart platform actuation |
| Servo ×4 | Input flap actuation |
| Rotary encoder | Bowl position tracking |
| DC motor | Bowl rotation drive |
| Logitech C270 | Item classification camera |
| Slip ring | Power + I2C across rotating joint |

---

## Software

### Vision Pipeline
- **YOLOv8** object classification model
- Trained on custom dataset via Kaggle/Roboflow by Dhruv!!
- Runs on host machine (MPU) which is the linux side of things, and then communicates classification result to Arduino (MCU) that controls the servos

### Arduino Firmware
Three iterative firmware versions developed:
1. **Calibration tool** — servo homing and geometry verification so that the servos dont tweak out when we send commads 😭
2. **IK solver** — used to calculate servo angles

---

## CAD

- Designed in **Onshape**
- Base plate: polycarbonate pieces that need to be CNCd from sendcutsent
- Bowl and flaps: FDM printed in PETG
- Bins: modular, screw-together panels

---

## Status

- [x] Mechanical design (Onshape)
- [x] IK solver
- [x] YOLOv8 classification
- [x] Multi-object queue firmware
- [ ] Full system integration test
- [ ] Camera mount finalized(I have a provision to either put it at the top or bottom, but I'm not sure yet of what one to use.
