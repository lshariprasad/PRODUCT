# Handheld NRF24 Long-Range Communicator: Parametric Enclosure Generator

A single Fusion 360 Python script that builds a complete, 3D-printable, two-piece handheld enclosure for an **ESP32 + NRF24L01+ PA+LNA** communicator. It also generates detailed visual models of every internal component, runs automatic fit and clearance checks, and exports DXF drawings, STL files and a Fusion archive.

> **Status:** the script's logic and fit checks have been run against a mocked Fusion API. It has **not yet been validated inside Fusion 360**. Run it once and report the first error or warning (see [Known limitations](#known-limitations)).

---

## Contents

1. [What you get](#what-you-get)
2. [Hardware (bill of materials)](#hardware-bill-of-materials)
3. [Verified dimensions and open items](#verified-dimensions-and-open-items)
4. [Design summary](#design-summary)
5. [Requirements](#requirements)
6. [Quick start](#quick-start)
7. [Output files](#output-files)
8. [Customising (parameters)](#customising-parameters)
9. [Automatic checks](#automatic-checks)
10. [3D printing guide](#3d-printing-guide)
11. [Assembly](#assembly)
12. [Wiring notes](#wiring-notes)
13. [Known limitations](#known-limitations)
14. [Troubleshooting](#troubleshooting)
15. [Roadmap](#roadmap)
16. [Sources and credits](#sources-and-credits)

---

## What you get

- **Two-piece enclosure** (front shell + rear shell) with a tongue-and-groove lip, 0.2 mm shell gap, and four M3 corner screws.
- **Front panel:** OLED window, 3 × 2 keypad (6 printed keycaps with raised T9-style labels), engraved nameplate.
- **Ports and openings:** ESP32 USB-C, TP4056 USB-C, KCD1-11 rocker-switch cutout, RP-SMA antenna opening (vertical slot for tolerance), buzzer grille (7 holes).
- **Internal structure:** screw bosses, PCB standoffs and rails, guide ribs, battery pocket with hold-down ribs, wire channel, OLED posts (M2), keypad-board posts (M2).
- **Detailed component models** (visual only, not printed): ESP32 DevKit, TP4056, AMS1117 module, NRF24 PA+LNA with antenna, 0.96" OLED, keypad perfboard with 6 tact switches, buzzer, 18650 holder with cell, rocker switch.
- **Automatic checks:** pre-build fit check, Fusion interference analysis, and a written `CLEARANCE_REPORT.txt`.
- **Exports:** 8 DXF sheets, 3 STL files, native `.f3d` archive.
- **User parameters:** every dimension is created in Fusion's parameter table.

## Hardware (bill of materials)

Two identical units are planned. Quantities below are **per unit**.

| Qty | Part | Notes |
|---|---|---|
| 1 | ESP32-WROOM-32 DevKit, 30-pin, USB Type-C | See open items: board length varies by vendor |
| 1 | NRF24L01+ PA+LNA module with **RP-SMA** jack | Plus a 2.4 GHz RP-SMA antenna |
| 1 | 0.96" I2C OLED (SSD1306), 4-pin | 27.3 × 27.8 mm PCB |
| 6 | 6 × 6 × 5 mm tactile switches | Mounted on a perfboard |
| 1 | Perfboard, cut to about 64 × 44 mm | Drill pattern is in `04_BUTTON_PATTERN.dxf` |
| 1 | TP4056 Type-C charger/protection module | 26 or 28 mm variants exist |
| 1 | AMS1117-3.3 V module | See the power warning below |
| 1 | Single 18650 holder with leads | About 76 × 21.5 × 20 mm |
| 1 | 18650 cell (flat-top) | Protected cell recommended |
| 1 | 12 mm buzzer | 12 × 9.5 mm |
| 1 | KCD1-11 mini rocker switch (SPST) | Panel hole about 19.2 × 12.9 mm |
| 4 | M3 × 35 mm self-tapping screws | Rear to front, counterbored |
| 10 | M2 × 6 mm self-tapping screws | 4 for OLED, 6 for keypad board |

> **Power warning:** an AMS1117 needs about 1.1 V of headroom. From a single Li-ion cell (3.0–4.2 V) it **cannot** hold a clean 3.3 V once the cell drops below about 4.4 V. Prefer a low-dropout or buck-boost 3.3 V regulator. Add a 10–100 µF capacitor at the NRF24 supply pins.

## Verified dimensions and open items

Dimensions were compared against more than one source where possible. Where sources disagreed, the script uses the larger, safe envelope.

| Component | Used in model | Confidence | Note |
|---|---|---|---|
| NRF24L01+ PA+LNA | PCB 41 × 15.5, about 48 mm with connector, 14 mm high | Medium-high | RP-SMA, not true SMA. Jack height above the PCB is **unverified** |
| ESP32 30-pin Type-C | 54.5 × 28 × 12.5 (upper bound) | **Low** | Vendors list about 51 to 55 mm. A photo of your board is needed |
| 0.96" OLED | PCB 27.3 × 27.8, active 21.74 × 10.86, holes Ø2.0 | Medium | Window set to 23.0 × 12.0. Active-area offset on the PCB is **not verified** |
| KCD1-11 switch | Panel hole 19.4 × 13.1 | Medium | The commonly quoted 15 × 10.5 is too small |
| TP4056 Type-C | 28 × 17.5 × 4.2 (envelope) | Medium | Conflict between 26 and 28 mm variants |
| 18650 holder | 76 × 21.5 × 20 | High | Real products range 74.5–76 × 21–21.5 × 17.7–22 |
| AMS1117 module | 25.5 × 11.5 × 4 | Medium-high | Common breakout is about 25 × 11 mm |
| Buzzer, tact switch | 12 × 9.5 / 6 × 6 × 5 | Low-medium | Standard parts, not re-verified |

**Please confirm with photos or measurements:** ESP32 board, TP4056 board, AMS1117 module, and the OLED active-area offset (`oledActiveOffsetY`, measured with calipers).

## Design summary

- **External size:** 126 × 80 × 45 mm. The length grew from the 120 mm starting target because the TP4056 and the 76 mm battery holder need about 125 mm of internal length when stacked. Keycaps stand 2 mm proud of the front face.
- **Internal size:** 121.2 × 75.2 × 39.8 mm. Wall 2.4 mm, floor 2.8 mm, front plate 2.4 mm. The rear shell is split at Z = 27 mm.
- **Layout (top view, device portrait):**
  - Antenna end (+Y): NRF24 on the right, rocker switch at the top wall.
  - USB end (−Y): TP4056 on the left and ESP32 in the middle-right, with 30 mm between the two USB-C centres.
  - Left column: 18650 holder, kept more than 25 mm from the NRF24.
  - Right wall: buzzer with side grille.
  - Front layer: OLED and keypad boards hang from the front plate above the floor components.
- **Coordinate system:** origin at the enclosure centre, X across the width, Y along the length, Z up from the bottom face.

## Requirements

- Autodesk Fusion 360 (a personal or education licence works).
- No extra Python packages. The script uses only Fusion's built-in `adsk` API.
- A 3D printer capable of about 130 × 85 × 50 mm (FDM).

## Quick start

1. In Fusion, open **Utilities → Add-Ins → Scripts and Add-Ins** (Shift+S).
2. Click **+** beside "My Scripts" and choose **Create new script** (Python). Name it `HANDHELD_COMMUNICATOR`.
3. Open the generated `.py` file, delete its contents, and paste the full script. Save.
4. Select the script and click **Run**. A new design opens and is built automatically.
5. Read the message box (fit check, interference result, warnings) and open `CLEARANCE_REPORT.txt`.

**Display switches at the top of the script**

| Switch | Default | Effect |
|---|---|---|
| `SHOW_COMPONENTS` | `True` | Keeps the detailed component models visible and makes the front shell 35% transparent |
| `SHOW_ANTENNA` | `True` | Models the antenna bent upward outside the top wall |

## Output files

Everything is written to `Documents/HANDHELD_COMMUNICATOR_EXPORT/`.

| File | Purpose |
|---|---|
| `HANDHELD_COMMUNICATOR_MODEL.f3d` | Native Fusion archive |
| `REAR_SHELL.stl`, `FRONT_SHELL.stl`, `KEYCAPS.stl` | Print files (components are never exported) |
| `01_FRONT_PANEL.dxf` | Front outline, OLED window, 6 key holes |
| `02_REAR_PANEL.dxf` | Rear outline and 4 screw holes |
| `03_OLED_PANEL.dxf` | OLED window, PCB outline, 4 mounting holes |
| `04_BUTTON_PATTERN.dxf` | Key holes, flanges, tact positions, perfboard drill pattern |
| `05_SWITCH_CUTOUT.dxf` | Rocker-switch cutout on the top wall |
| `06_USB_CUTOUTS.dxf` | Both USB-C cutouts on the bottom wall |
| `07_SMA_CUTOUT.dxf` | RP-SMA opening on the top wall |
| `08_FULL_FLAT_PROFILE.dxf` | Front, rear and wall panels laid out flat |
| `CLEARANCE_REPORT.txt` | Pass/fail checks, component positions, interference result, warnings |

In the Fusion browser, `HANDHELD_COMMUNICATOR_MODEL` contains `REAR_SHELL`, `FRONT_SHELL`, `KEYCAPS`, `COMPONENT_MODELS_VISUAL` (one sub-component per part), a hidden `REFERENCE_ENVELOPES_HIDDEN_FOR_CHECKS`, and a hidden `DXF_SKETCHES`.

**DXF scale check:** open `01_FRONT_PANEL.dxf` and confirm the outline measures 80 × 126 mm. Fusion's display units are set to mm by the script.

## Customising (parameters)

Two ways to change dimensions:

1. Edit the `PARAMS` dictionary at the top of the script and run it again.
2. In the generated design, edit values in **Modify → Change Parameters**, then run the script again while that document is active. The script reads the table and builds a fresh design.

The parameters are a mirror of the script's inputs. Changing them in the table does **not** live-update existing geometry.

Key parameters:

| Parameter | Default (mm) | Meaning |
|---|---|---|
| `enclosureLength` / `Width` / `Height` | 126 / 80 / 45 | External size |
| `wallThickness` / `baseThickness` | 2.4 / 2.8 | Walls and floor |
| `splitHeight`, `lipHeight`, `shellGap` | 27 / 3 / 0.2 | Shell joint |
| `cornerRadius` | 6 | Outer corner radius |
| `usbCutoutWidth` / `Height` | 13 / 7 | USB-C plug clearance (receptacle alone is about 9 × 3.3) |
| `switchCutoutWidth` / `Height` | 19.4 / 13.1 | Rocker panel hole |
| `smaHoleDiameter`, `smaSlotExtra` | 8.5 / 3 | Antenna hole and vertical tolerance slot |
| `oledWindowWidth` / `Height` | 23 / 12 | OLED window |
| `oledActiveOffsetY` | 0 | **Measure this** on your OLED |
| `buttonHoleDiameter`, `keycapDiameter` | 16.6 / 16 | Key holes and caps |
| `keyPitchX` / `keyPitchY` | 25 / 23 | Key spacing |
| `esp32Standoff`, `nrfStandoff` | 14 / 5 | Board heights above the floor |
| `screwBossDiameter`, `m3Pilot`, `m2Pilot` | 7 / 2.6 / 1.7 | Boss and pilot sizes |

## Automatic checks

Before building, the script checks:

- Pairwise clearance between all components (at least 2 mm).
- RF separation: NRF24 to battery holder at least 25 mm.
- Wall clearance for each floor component.
- USB-C centre spacing at least 30 mm.
- Dupont connector headroom above the NRF24 (15 mm).
- Battery lead buffer, USB cutout position, wall thickness, key web width, keypad edge clearance.

If any check fails, the script asks before building. After building, Fusion's interference analysis runs on the shells, keycaps and hidden envelope bodies.

## 3D printing guide

These are starting suggestions. Adjust for your printer and material.

- **Material:** PETG for toughness, or PLA for prototypes.
- **Layer height:** 0.2 mm. **Walls:** 3–4 perimeters. **Infill:** 20–30%.
- **Front shell:** print **face-down** (front plate on the bed), supports off in most cases.
- **Rear shell:** print floor-down.
- **Keycaps:** print flange-down. Raised labels are 0.4 mm, so use a fine layer height.
- **Orientation:** the STL files are exported in model orientation, so flip the front shell in your slicer.

## Assembly

1. Print all parts. Test-fit the TP4056, ESP32 and switch in the cutouts.
2. Solder the wiring (see below). Leave about 120 mm of service-loop wire from the front shell electronics to the floor electronics.
3. Place the ESP32, NRF24, TP4056 and AMS1117 on their standoffs. Snap the rocker switch into the top-wall cutout.
4. Fit the 18650 holder in its pocket and route the leads through the wire channel.
5. Screw the OLED to its four posts with M2 screws. Screw the keypad perfboard to its six posts with M2 screws.
6. Drop the six keycaps into the front-plate holes from inside, then lower the front shell onto the rear shell.
7. Insert four M3 × 35 mm screws from the bottom through the counterbored holes.

## Wiring notes

Typical ESP32 defaults, which you can change in firmware:

- **OLED (I2C):** SDA on GPIO 21, SCL on GPIO 22, powered from 3.3 V.
- **NRF24 (SPI):** SCK on GPIO 18, MISO on GPIO 19, MOSI on GPIO 23. CE and CSN go to two free GPIOs. Power from a stable 3.3 V with a bulk capacitor at the module.
- **Keys:** six free GPIOs with internal pull-ups, each button to ground.
- **Power path:** cell to TP4056 to rocker switch to regulator.

Always double-check module pinouts against your own boards before powering up.

## Known limitations

- Not yet validated inside Fusion 360. Some steps (fillets, text, colours) are wrapped so that a failure skips the step and lists a warning instead of stopping the script.
- Component dimensions marked low confidence need your confirmation.
- ESP32 headers are assumed to point down. NRF24 header is assumed to point up (change `nrfStandoff` to 9 mm or more if yours points down).
- The component models are procedural approximations, not manufacturer CAD. For exact shapes, insert STEP files from the maker, GrabCAD or SnapEDA and position them using the coordinates in `CLEARANCE_REPORT.txt`.
- Boards are held sideways by ribs and rest on standoffs. Only the battery has a hold-down. Use a dot of hot glue or Kapton tape for the PCBs.
- Battery access is by opening the front shell. There is no separate battery hatch.
- Geometry is regenerated by re-running the script. It is not a live history-parametric model.
- Raised key labels at about 1.9 mm text height are near the limit of a 0.4 mm nozzle.

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| Script fails immediately | Copy the error text from the message box. Check that the whole script was pasted with no cut lines |
| Parts are default grey | The appearance library name differs. This is cosmetic. The script lists a warning |
| Front shell is not transparent | Hide `FRONT_SHELL` manually with its eye icon |
| DXF has the wrong scale | Set Fusion document units to mm and run again |
| Fit check reports FAIL | Read `CLEARANCE_REPORT.txt`, then increase `enclosureLength` or `enclosureWidth` |
| USB plug does not fit | Increase `usbCutoutWidth` and `usbCutoutHeight` |
| SMA connector does not line up | Adjust `smaAxisAbovePcb` or `smaSlotExtra` after measuring your module |
| OLED window is off-centre | Measure the active-area offset and set `oledActiveOffsetY` |

## Roadmap

- Optional GSM module bay in a future revision.
- Battery hatch on the rear shell.
- Board hold-down clips.
- Re-verified dimensions after photos of the ESP32, TP4056 and AMS1117.
- Real vendor STEP models in place of procedural ones.

## Sources and credits

Dimensions were cross-checked from retailer and manufacturer pages including Grobotronics, Protosupplies, Canaduino (NRF24), Espressif and Grid Connect (ESP32 DevKitC), DFRobot, Easby, Lckfb and Zbotic (OLED), WE-Online and LCSC (KCD1 switch), Sigmanortec, Wald, Open-Electronics and Thingbits (TP4056), Tinytronics, Phipps, Makerlab and Circuit.rocks (AMS1117), and SparkFun, Core Electronics, Jaycar and Makers (18650 holder).

Licence: choose one (for example MIT) before publishing.
