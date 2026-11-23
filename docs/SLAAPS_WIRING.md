# SLAAPS wiring diagram

Status: draft, 2026-11-22. Built from the connections table in `docs/SLAAPS_BOM.md`. Dashed lines or "?" mark connections that are not
yet documented. The diagram renders in GitHub and in any Mermaid viewer.

```mermaid
flowchart LR
    subgraph PWR["Power"]
        B1["Battery 5S 1300 mAh<br/>XT90"]
        B2["Battery 5S 1300 mAh<br/>XT90"]
        PM["Holybro PM08-CAN<br/>power module<br/>(POWER1, POWER2, CAN)"]
    end

    subgraph PROP["Propulsion"]
        ESC["TBS Lucid 12S 90A ESC"]
        MOT["Hacker E50-M 2.5D motor<br/>in EJets Jetfan 90"]
    end

    subgraph FC["Pixhawk 6X"]
        direction TB
        PIN1["POWER1"]
        PIN2["POWER2"]
        MAIN["MAIN1-4<br/>PX4IO, PWM 333 Hz"]
        AUX5["AUX5<br/>DShot600"]
        CAN["CAN bus<br/>DroneCAN"]
        T1["TELEM1"]
        T2["TELEM2"]
        USB["USB"]
        GPSP["GPS1"]
    end

    subgraph ACT["Actuation"]
        S1["Servo 1 ES3059MD"]
        S2["Servo 2 ES3059MD"]
        S3["Servo 3 ES3059MD"]
        S4["Servo 4 ES3059MD"]
        RAIL["Servo power rail"]
    end

    subgraph SENS["Sensors"]
        PIT["Holybro DLVR<br/>DroneCAN pitot"]
        GPS["Holybro M9N GPS"]
    end

    subgraph LINK["Links"]
        SIK["Holybro SiK 915 MHz"]
        GND["Ground station<br/>QGC and joystick"]
        PI["Raspberry Pi 4B<br/>(planned)"]
    end

    %% Main power: two 5S packs in series (10S), through the module to the ESC
    B1 ==>|"series, 10S"| B2
    B2 ==>|"10S main"| PM
    PM ==>|"pass-through, about 70 A"| ESC
    ESC ==>|"3-phase"| MOT

    %% Regulated outputs
    PM -->|"POWER1"| PIN1
    PM -->|"POWER2"| PIN2
    PIN2 -.->|"servo and Pi supply"| RAIL
    PIN2 -.->|"5 V supply"| PI
    RAIL --> S1
    RAIL --> S2
    RAIL --> S3
    RAIL --> S4

    %% Control signals
    MAIN -->|"PWM"| S1
    MAIN -->|"PWM"| S2
    MAIN -->|"PWM"| S3
    MAIN -->|"PWM"| S4
    AUX5 -->|"DShot600"| ESC

    %% DroneCAN
    PM <-->|"DroneCAN"| CAN
    PIT <-->|"DroneCAN"| CAN

    %% Serial and USB links
    SIK <-->|"MAVLink 57600"| T1
    PI <-->|"MAVLink"| T2
    GND <-->|"MAVLink over USB"| USB
    SIK -.->|"915 MHz radio"| GND
    GPS <-->|"serial"| GPSP
```

## Notes
- **Single point of failure.** One PM08-CAN carries the whole EDF current, the flight controller, the servo rail and the Pi. A fault or sag on it
  takes all of them out together.
- **POWER2 split.** Which POWER2 pins or leads reach the servo rail and the Pi is not recorded, so those two lines are dashed.
- **Not documented:** the M9N variant (it is on GPS1, a serial port), the CAN port (CAN1 or CAN2) and bus termination, the Pi's baud rate, and how the rail is distributed
  to the four servos.
- **Servo order:** servos 1 to 4 are on MAIN1 to MAIN4. The plugs for servos 3 and 4 were swapped and fixed on 2026-11-05.
- **MAIN versus AUX:** the servos are on the PX4IO rail (bank 0) and the EDF is on the FMU rail (bank 1). On 2026-11-22 the MAIN bank was the slow
  one to acknowledge activation (about 43 ms against 7 ms for AUX).
