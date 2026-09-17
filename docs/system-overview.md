Historical design reference: this document includes superseded CAN, RS-485 or PCA9685 designs. For the current implemented connections, see docs/wiring.md and docs/rvc-fa75.md.

# BlueSquid System Design and Feasibility Review

Revision: 2026-08-16

This is a provisional architecture and budgeting document, not a final
construction drawing. Circuit protection, conductor sizes, terminal ratings
and 120 V work must be finalized from actual appliance nameplates, cable
lengths and applicable Canadian electrical/RV requirements.

## 1. Project objective

BlueSquid is intended to provide one local control and monitoring system for:

- Victron battery, solar and DC/DC information from a Cerbo GX
- 12 V white and RGBW lighting
- switched 12 V feeds for local USB converters
- water-pump control and generic expansion outputs
- selected 120 V outlets and a 120 V water heater through contactors
- a Dometic fan or HVAC system, if its exact controller supports RV-C
- hard switches, motion sensing and future cabin sensors
- a dedicated seven-inch touchscreen plus an optional iOS/BLE interface
- local settings and history without relying on Wi-Fi

The rear controller keeps hardwired ownership of loads and safety logic. The
touchscreen uses a persistent bonded BLE control link to reduce front-to-rear
wiring, while both CAN networks remain isolated at the rear.

### 1.1 Electrical baseline

Assuming the three 12.8 V, 300 Ah LiFePO4 batteries are connected in parallel,
the bank setting is **900 Ah** and its nominal stored energy is approximately
**11.52 kWh**. The stated 3.84 kWh is the nominal energy of one battery. Usable
energy will be lower after the configured discharge limit and conversion losses.

The SmartShunt should be configured to the same 900 Ah capacity. It remains the
authoritative source for bank state of charge, consumed amp-hours and time to go;
BlueSquid should not attempt to derive SOC from voltage alone.

## 2. Recommended architecture

```text
Victron devices -> Cerbo GX -> isolated VE.Can channel 2 -> rear controller

Dometic RV-C equipment -- isolated RV-C CAN -- rear controller
                                                    |
                                   persistent bonded BLE
                                                    |
                                  Waveshare ESP32-S3 touchscreen

```

There are exactly two CAN networks:

| Network | Interface at rear | Function | Desired failure behavior |
| --- | --- | --- | --- |
| RV-C | Isolated MCP2515 CAN channel 1 | Expansion nodes and verified RV-C equipment | A BlueSquid failure must not hold the RV-C bus dominant |
| Victron VE.Can | Isolated MCP2515 CAN channel 2 | Cerbo GX energy data | Energy data becomes stale without affecting RV-C/control |

The rear controller remains the source of truth for outputs, settings, safety
rules and history. The touchscreen and iOS app are clients, not independent
power controllers.

### 2.1 What the hardwired Victron connection can provide

The Victron devices connect to the Cerbo GX. The rear controller receives the
Cerbo's published energy data through dedicated isolated VE.Can channel 2. This
bus must never be electrically joined to the RV-C bus.

Recommended Cerbo-side connections are:

| Equipment | Connection to Cerbo |
| --- | --- |
| SmartShunt 500 A | VE.Direct socket or genuine VE.Direct-to-USB cable |
| SmartSolar MPPT 150/45 | VE.Direct socket or genuine VE.Direct-to-USB cable |
| Orion XS 12/12-50 | VE.Direct socket or genuine VE.Direct-to-USB cable |
| Rear BlueSquid controller | Dedicated isolated CAN channel to Cerbo VE.Can |

The Cerbo has three VE.Direct ports and supports VE.Direct-to-USB expansion. If
additional USB sockets are required, Victron recommends a good powered USB hub;
the documented maximum is fifteen VE.Direct devices, with headroom advised for
complex systems.

[Victron: connecting VE.Direct products to a Cerbo GX](https://www.victronenergy.com/media/pg/Cerbo_GX/en/connecting-victron-products.html)

| Requested value | Primary source | Expected result |
| --- | --- | --- |
| Battery voltage/current/power | SmartShunt through Cerbo | Direct measurement |
| SOC, consumed Ah, time to go | SmartShunt through Cerbo | Direct measurement; use 900 Ah bank capacity |
| Remaining Wh | BlueSquid calculation | SOC x configured nominal Wh, labelled as an estimate |
| Solar power and charge state | SmartSolar MPPT through Cerbo | Separate real-time source |
| DC/DC power and charge state | Orion XS through Cerbo | Separate real-time source |
| Whole-bank net charge/use | SmartShunt | Accurate when every load and charger is on the load side of the shunt |
| DC load estimate | Known sources plus shunt | Accurate only while all active charge sources are known |
| Per-circuit usage | Additional sensors | Not available from one SmartShunt |
| Historical energy | Rear BlueSquid logger | Calculated from timestamped samples and counters |

Charge stage should be displayed per source because the MPPT and Orion may be
in different stages at the same time. BlueSquid may derive a
plain-language bank summary, but it should not replace those individual states.

## 3. Hardware already selected or owned

The following costs are excluded from the incremental estimates because they
are already owned, ordered, or part of the Victron installation:

- ordered ESP32-S3 touchscreen; the current firmware and wiring target the
  Waveshare ESP32-S3-Touch-LCD-7, so confirm the exact label if the smaller
  AliExpress display replaced it
- Cerbo GX
- SmartShunt 500 A
- SmartSolar MPPT 150/45
- Orion XS 12/12-50 A
- battery bank and primary battery protection
- previously ordered ADuM1201 modules

The existing isolated RS-485 hardware may be retained only as a development or
service fallback; production Victron communication uses VE.Can channel 2.

## 4. Recommended provisional bill of materials

Prices are planning ranges in Canadian dollars as of 2026-08-16, before tax
and shipping. Marketplace modules vary widely; model numbers matter more than
the appearance of the board.

### 4.1 Core communications and power

| Qty | Item | Planning cost CAD | Notes |
| ---: | --- | ---: | --- |
| 1 | Espressif ESP32-S3-DevKitC-1-N8R8 | $22-30 | Rear controller; current firmware target |
| 1 | Waveshare 2-CH Isolated CAN HAT, SKU 17912 | $31-55 | Two isolated MCP2515 channels over SPI |
| 2 | Mean Well RSD-30G-5 or DDR-30G-5 | $78-105 total | Separate isolated 5 V supplies front and rear |
| 2 | fused transient/reverse-polarity input stages | $30-100 total | One ahead of each front/rear DC/DC supply |
| 1 | Passive verified VE.Can RJ45 pigtail/terminal adapter | $15-60 | Cerbo GX connection; not Ethernet |
| 1 lot | CAN/DeviceNet cable and connectors | $50-150 | Depends strongly on route and connector style |

Reference Canadian prices found during this review include approximately
$23.80 for the ESP32-S3 DevKitC at
[DigiKey Canada](https://www.digikey.ca/en/products/detail/espressif-systems/ESP32-S3-DEVKITC-1-N8R8/15295894),
$31.41 for the dual CAN HAT at
[RobotShop Canada](https://ca.robotshop.com/products/waveshare-2-ch-isolated-can-expansion-hat-raspberry-pi-dual-chips-solution),
$50.11 for an RSD-30G-5 at
[DigiKey Canada](https://www.digikey.ca/en/products/detail/mean-well-usa-inc/RSD-30G-5/7706229).

The DC/DC modules still need close source fusing, reverse-polarity protection,
transient suppression and input filtering appropriate to a 12 V vehicle. A
regulated converter alone is not the complete vehicle power-entry circuit.

### 4.2 Front touchscreen inputs

| Qty | Item | Planning cost CAD | Function |
| ---: | --- | ---: | --- |
| 1 | ADS1115 16-bit module | $8-25 | Four analog channels at I2C address `0x48` |
| 1 | AM312 PIR or better 3.3 V-output PIR | $4-15 | Motion sensor |
| 3-6 | Momentary SPST switches | $10-40 total | Scenes and accessory commands |
| 1 | LDR plus resistors, optional | $2-5 | Ambient-light measurement |
| 1 lot | JST connectors, terminal strip and filtering parts | $10-30 | Strain relief and serviceability |

Suggested ADS1115 allocation:

| Channel | Initial use |
| --- | --- |
| A0 | Resistor ladder for multiple momentary switches |
| A1 | PIR output |
| A2 | Ambient-light divider |
| A3 | Spare analog input |

The ADS1115 and sensors must be powered from 3.3 V, and all analog inputs must
stay between ground and 3.3 V. Keep the shared I2C wiring behind the screen and
preferably below 30 cm.

At least one entry/emergency light should retain a direct manual fallback. The
other convenience switches can depend on the touchscreen, but rear safety and
manual fallback must not depend on its BLE connection.

### 4.3 Rear low-voltage I/O

| Qty | Item | Planning cost CAD | Function |
| ---: | --- | ---: | --- |
| 1 | MCP23017 module | $8-20 | Sixteen slow digital inputs/outputs |
| 1 | ULN2803A driver module | $8-32 | Eight protected coil-sink outputs |
| 1 | SHT31 module | $10-25 | Cabin temperature and humidity |
| 1 | LIS3DH module | $15-35 | Digital camper leveling sensor |
| 1 | DS3231 RTC module, optional but recommended | $8-20 | Time without Wi-Fi or Cerbo |
| 1 | Quality 3.3 V microSD breakout and high-endurance card | $25-60 | Rear history logging |
| 1 lot | Automotive relays, sockets, suppressors and feedback wiring | $40-150 | Pumps, USB feeds and dry contacts |

The rear I2C address plan avoids conflicts:

| Device | Address |
| --- | ---: |
| LIS3DH | `0x18` |
| MCP23017 | `0x20` |
| PWM lighting controller | `0x40` |
| SHT31 | `0x44` |
| Optional rear ADS1115 | `0x48` |
| DS3231 | `0x68` |

Replacing the current analog GY-61 and separate HTU21D bus with LIS3DH and
SHT31 reduces pin usage and eliminates the present `0x40` address conflict.
The cabin sensor should be placed away from the warm electronics enclosure.

Optional measurement additions should be driven by a specific need rather than
fitted everywhere:

| Measurement | Suitable hardware class | Planning allowance |
| --- | --- | ---: |
| One high-value DC branch | Rated Hall sensor or external-shunt monitor | $25-100 per branch |
| 120 V total/branch energy | Approved enclosed CT or DIN energy meter | $80-250 per metered point |

Common low-current INA219 boards are not suitable for a high-current charger or
pump unless the complete external shunt, voltage range, PCB clearances
and protection are deliberately engineered for that circuit.

### 4.4 Lighting output choices

Approximately eleven independently dimmed channels are currently planned:

- ceiling, reading and exterior white-light circuits
- front RGBW, four channels
- bed RGBW, four channels

The ordinary PCA9685 module only creates logic-level PWM and cannot power
these circuits directly.

#### Option A: compact premium prototype

Use one NCD PCA9685 16-channel open-collector FET controller. It integrates
the PWM generator, sixteen low-side MOSFETs and terminal connections. The
published design uses 55 V, 5.5 A FETs, but enclosure thermal limits and total
board current must still be respected. Plan approximately **$270-350 CAD**
after currency conversion and shipping.

[NCD 16-channel FET controller](https://store.ncd.io/product/pca9685-16-channel-8w-open-collector-12-bit-pwm-fet-driver-with-i2c-interface/)

Advantages: compact, documented and close to the eventual custom-PCB layout.
Disadvantages: expensive and not stocked everywhere.

#### Option B: economical modular prototype

Use the existing PCA9685 with reputable 3.3 V-compatible MOSFET modules, such
as DFRobot DFR0457, only for the channels installed during each phase. Budget
**$80-200 CAD** for a full eleven-channel implementation.

Advantages: inexpensive to start and easy to replace one channel.
Disadvantages: more wiring and substantially more panel space.

Avoid generic IRF520/IRF540 boards and undocumented multi-channel boards. Many
do not fully turn on from 3.3 V and can overheat while appearing to work.

For either option:

- fuse every lighting branch separately
- use the board for LED/resistive loads, not the water pump or large motors
- run both positive and switched return conductors
- do not use chassis ground as the switched RGBW return
- size an RGBW common-positive conductor for the sum of all four channels
- initially limit channels to about 2 A until thermal performance is measured

### 4.5 DC distribution

| Qty | Item | Planning cost CAD |
| ---: | --- | ---: |
| 1 | Blue Sea 5026 fuse block with negative bus | $66-100 |
| 1 | Main controller-feed fuse/breaker and holder | $25-80 |
| 1 set | Positive/negative busbars | $35-100 |
| 1 lot | DIN terminal blocks, ferrules, labels and end stops | $50-150 |
| 1 | Low-voltage enclosure and mounting plate | $75-250 |

The Blue Sea 5026 is rated for twelve branches, 30 A per circuit and 100 A per
block when correctly wired and fused. A current Canadian price found during
this review was $65.94 from
[Sigma Safety](https://shop.sigmasafety.ca/products/blue-sea-5026-12-position-fuse-block).

The fuse protects the conductor. The main fuse belongs close to the source,
and every outgoing load conductor needs protection appropriate to its gauge
and connected equipment.

### 4.6 USB outlets

Run switched 12 V to each outlet location and convert to USB voltage locally.
Do not distribute 5 V from the rear cabinet.

```text
rear fuse -> protected relay/high-side switch -> 12 V cable
          -> local automotive USB-C/USB-A converter
```

Budget **$25-60 per outlet location** for a credible USB-C PD converter,
connector and local protection. A nominal 60 W outlet can draw approximately
5 A from 12 V before conversion losses, so its branch is not a small signal
circuit.

### 4.7 120 V switching

No Arduino relay module should carry branch-circuit mains power. Use a
code-appropriate breaker/GFCI and DIN-rail contactor. The ESP32 controls only
the isolated 12 V contactor coil through the MCP23017 and ULN2803.

```text
AC source -> breaker/GFCI -> contactor -> outlet or water heater

ESP32 -> MCP23017 -> ULN2803 -> 12 V contactor coil
```

For planning, allow **$60-150 CAD per controlled AC circuit** for a quality
two-pole contactor, selector/override, terminals and coil suppression, not
including the breaker, GFCI or labour. Finder 22-series contactors offer 25 A
two-pole versions, protected 12 V AC/DC coil options and reinforced separation.

[Finder 22.32 specifications](https://www.findernet.com/en/uk/series/22-series-modular-contactors-25-40-63a/type/type-2232-modular-contactor-25a/)

Water-heater controls must retain the appliance thermostat and thermal cutoff.
BlueSquid may enable or inhibit the circuit but must not replace those safety
devices. The safe reset state is off. Include a physical `OFF/AUTO`, or a
suitably designed `OFF/AUTO/ON`, selector.

The AC panel requires a separate enclosure or properly divided listed assembly.
Final selection and installation should be reviewed by someone qualified for
Canadian RV and mains wiring.

### 4.8 Dometic RV-C dependency

Do not make an RV-C appliance connection from the brand name alone. Dometic
fans and HVAC systems may use RV-C, RJ-11 wall controls, infrared remotes or a
separate Dometic gateway/load box. The complete fan, control-board and gateway
model numbers must be checked first.

The RV-C channel should initially observe installed traffic
before RV-C control is enabled. The second isolated channel is dedicated to
Victron VE.Can and remains electrically separate.

## 5. Proposed rear-controller pin allocation

Moving lighting away from direct GPIO frees enough pins for the dual CAN board.
This map is provisional until the exact ESP32-S3 board and all modules are on
the bench.

| ESP32-S3 pin | Function |
| ---: | --- |
| GPIO8 | Rear I2C SDA |
| GPIO9 | Rear I2C SCL |
| GPIO10 | External CAN SPI SCK |
| GPIO11 | External CAN SPI MISO |
| GPIO12 | External CAN SPI MOSI |
| GPIO13 | RV-C MCP2515 CS |
| GPIO14 | Victron VE.Can MCP2515 CS |
| GPIO15 | RV-C MCP2515 interrupt |
| GPIO16 | Victron VE.Can MCP2515 interrupt |
| GPIO4 | Rear microSD SCK, separate SPI bus |
| GPIO5 | Rear microSD MISO |
| GPIO6 | Rear microSD MOSI |
| GPIO7 | Rear microSD CS |
| Remaining GPIO | output enable, relay feedback and service inputs |

Both MCP2515 controllers share SCK/MISO/MOSI but require separate chip-select
and interrupt pins. Set the Waveshare board logic jumper to 3.3 V and supply
the board with regulated 5 V. The microSD is placed on a separate SPI bus so a
long card write cannot needlessly block servicing the MCP2515 receive buffers.

## 6. Cable and routing plan

Wire sizes below are starting points, not final selections. Calculate each run
from its maximum continuous current, starting/inrush current, route temperature,
bundle derating and round-trip length. Target no more than roughly 3% voltage
drop for electronics and lighting.

| Connection | Recommended cable |
| --- | --- |
| Front screen power | Fused supply sized for the local protected 5 V converter; no rear communications pair required |
| Cerbo VE.Can | Verified Victron RJ45 pigtail to 120-ohm CAN cabling; not Ethernet |
| RV-C | OEM RV-C cable/connector or 120-ohm CAN cable after model confirmation |
| Small white lights | 18/2 short/light loads; 16/2 longer or higher-current loads |
| RGBW strips | 18/5 or 16/5 stranded copper, with common sized for total current |
| USB outlet 12 V feed | 16/2; consider 14/2 for high-power PD or long runs |
| Water pump | Typically 14/2 or 12/2 after nameplate/length calculation |
| Dometic fan power | Follow the exact Dometic installation manual |
| Contactor coils | 18/2 or 20/2 |
| Local hard switches | 22/2 |
| PIR | 22/3 |
| Local I2C | 22-24 AWG four-conductor, short and away from power switching |
| 120 V, 15 A | Approved 14/2 copper cable |
| 120 V, 20 A | Approved 12/2 copper cable |

Keep 120 V wiring in separate routing from CAN, I2C and sensor
cables. Separate high-current DC motor and charger conductors where practical.
Cross noisy power wiring at approximately 90 degrees when separation is not
possible.

The RV-C route should be a line, not a star. Install exactly
two 120-ohm terminators at the physical ends of the complete bus and keep all
device stubs short.
## 7. Software architecture

### 7.1 Rear firmware responsibilities

The production rear firmware should be divided into these services:

| Service | Responsibility |
| --- | --- |
| `VictronCanManager` | Isolated VE.Can channel 2, Cerbo source discovery and stale-data detection |
| `CanManager` | Shared BlueSquid/RV-C CAN status, commands, RV-C discovery and coexistence rules |
| `InputManager` | MCP23017, local overrides, feedback and alarms |
| `OutputController` | Desired accessory state independent of physical driver |
| `LightingDriver` | Sixteen-channel PWM/FET output and all-off hardware enable |
| `SafetyManager` | SOC limits, timeouts, interlocks, stale-data and restart behavior |
| `HistoryManager` | Timestamped rear microSD logging and summaries |
| `SettingsManager` | NVS configuration retained across normal OTA updates |
| `BleManager` | Existing iOS fallback transport |

The output state machine must keep working when Cerbo data is stale. Victron
loss should disable only rules that require valid energy data; it must not
crash or reset lights and accessories.

The shared CAN firmware should initially observe the installed RV-C traffic
before transmitting BlueSquid messages. Its identifiers, source addressing and
bus load must be verified not to conflict with RV-C equipment.

### 7.2 Touchscreen firmware responsibilities

- LVGL monitoring and control interface
- persistent bonded BLE client with automatic reconnection and offline indication
- ADS1115 sampling and switch debounce
- motion and ambient-light event publication
- local display settings and screen dimming
- no direct ownership of rear power outputs

Motion logic should be enforced by the rear controller. The touchscreen sends
a motion event; the rear decides whether a light may turn on, its brightness,
and the timeout based on sleep mode, ambient light and current system state.

### 7.3 iOS application

The current iOS application remains a BLE secondary interface. It already has
power, lighting, RGBW, accessories, battery-capacity configuration and seven
days of history stored locally on the phone. It is not required for normal
operation.

### 7.4 History strategy

Current history is stored only in the iOS app, so it stops when the phone is
not connected. Production history should be recorded at the rear controller:

- raw or one-minute samples on high-endurance microSD
- rolling hourly/daily summaries for quick screen display
- DS3231 time when Cerbo/network time is absent
- controlled file rotation and recovery after power loss
- optional export over BLE or the screen's service USB connection

NVS remains appropriate for settings but not continuous time-series writes.

## 8. Current implementation status

### Implemented and compiling

- separate rear ESP32-S3 and Waveshare touchscreen PlatformIO targets
- rear accessory/output abstraction
- existing PCA9685 lighting abstraction
- persistent BLE touchscreen control protocol with complete state snapshots
- touchscreen power, light and accessory pages
- validated SD configuration export/import, including touchscreen labels and
  icon associations, display timeout and persisted rear-controller presets
- BLE transport and iOS interface
- optimized BLE touchscreen snapshots, sequence-numbered commands,
  acknowledgements and atomic RGBW updates
- editable battery capacity stored in ESP32 NVS
- current RS-485 Cerbo D-Bus bridge as a temporary development path
- battery, solar, DC/DC, load and charger-stage status models

### Partially implemented or hardware-unverified

- physical MOSFET/relay outputs
- RGBW output mapping, currently using direct ESP32 GPIO rather than the
  proposed sixteen-channel driver
- physical all-lights switch
- climate and level sensors
- touchscreen state synchronization and complete RGBW/settings UI
- energy history, currently phone-only

### Not yet implemented

- production Cerbo VE.Can source discovery, parsing and recovery handling
- dual MCP2515 driver integration
- Dometic RV-C discovery/control
- ADS1115 switch, PIR and ambient-light input
- MCP23017/ULN2803 driver layer
- rear RTC/microSD history service
- AC contactor interlocks and feedback
- hardware watchdog/output-enable strategy
- production fault logging and installation commissioning tools

The current repository must not be represented as ready to energize physical
120 V or high-current DC loads.

## 9. Safety and offline behavior

| Event | Required behavior |
| --- | --- |
| Rear ESP32 reboot | 120 V contactors, pump and optional high-power loads default off |
| Screen disconnected | Rear continues operating; hardwired fallback still works |
| Cerbo/VE.Can lost | Energy values marked stale; BlueSquid control remains operational |
| Shared BlueSquid/RV-C lost | Rear retains safe output state; Dometic retains its native/local fallback |
| Low SOC | Optional inhibit of water-heater and other discretionary loads |
| Output-driver fault | Hardware output enable can shut down power stages |
| SD failure | Controls continue; history reports unavailable |

High-risk functions should have physical feedback rather than assuming that a
command succeeded. Examples include water-pump current or pressure, contactor
auxiliary contacts and water-heater current.

## 10. Space estimate

The control electronics are compact, but safe wiring space is larger than the
Arduino modules themselves.

Suggested starting allocations:

- front screen cavity: approximately 220 x 140 x 45 mm, with ventilation and
  connector strain relief
- rear low-voltage panel/enclosure: approximately 300 x 400 mm minimum once
  fuse block, terminal blocks and service loops are included
- separate AC DIN enclosure: approximately 200 x 300 mm, adjusted for breaker,
  GFCI and contactor count

Do not shrink the enclosure by eliminating terminal space or bending high-current
conductors tightly. Removable connectors, labels and service loops are a large
part of making a prototype maintainable.

## 11. Budget summary

All totals exclude already-owned Victron equipment, Cerbo GX, touchscreen,
loads/appliances and labour.

| Build level | Estimated CAD | Included |
| --- | ---: | --- |
| Communications bench prototype | $250-550 | Rear ESP32, isolated dual-CAN interface, protected power and basic cabling |
| Full low-voltage bench prototype | $350-800 | Adds inputs, sensors and a prototype lighting driver |
| Installable DC control system | $700-1,600 | Adds protected power, fuse block, relays, terminals, enclosure and vehicle wiring |
| DC plus two controlled AC circuits | $1,000-2,300 | Adds contactors, AC enclosure and related materials |
| Premium/full first installation | $1,300-2,800 | Premium lighting board, history, several USB zones, extensive connectors and contingency |

The largest variables are:

1. premium integrated lighting board versus modular MOSFET channels
2. number of controlled 120 V circuits
3. number and power rating of USB-C outlets
4. total cable lengths and conductor sizes
5. whether the Dometic product needs an additional OEM RV-C gateway
6. enclosure, connector and professional AC-labour choices

A sensible contingency is 15-20% because prototype wiring frequently needs
additional terminals, fuses, connectors and replacement modules.

## 12. Feasibility assessment

| Area | Assessment | Main concern |
| --- | --- | --- |
| Persistent touchscreen BLE control | Good feasibility | Bonding, reconnect testing and radio coexistence |
| 12 V lighting | High feasibility | Correct MOSFET stage, cooling and branch fusing |
| Victron monitoring over Cerbo VE.Can | Good feasibility | NMEA 2000 parsing and source/instance discovery |
| Hard switches and PIR | High feasibility | Electrical filtering and motion policy |
| USB outlet control | High feasibility | 12 V current and converter quality |
| Water-pump control | High feasibility | Feedback and fail-safe output design |
| 120 V outlet/heater control | Feasible with qualified installation | Code compliance, enclosure and contactor selection |
| Dometic RV-C | Undetermined until model is known | Product may not expose RV-C directly |
| Long-term history | High feasibility | Power-loss-safe microSD logging |
| Future distributed nodes | High feasibility | CAN addressing, connectors and update strategy |

Overall, the project is technically feasible. The software is substantial but
well suited to the two ESP32-S3 processors. The difficult portions are not CPU
or memory capacity; they are appliance-protocol confirmation, protected power
hardware, commissioning and safe 120 V construction.

### 12.1 Software effort and maintenance

These are planning ranges for one person who is comfortable with Arduino/C++
and can test on the actual hardware. They are not quotations.

| Work package | Approximate effort |
| --- | ---: |
| Cerbo VE.Can receive path, source discovery and validation | 25-50 hours |
| Shared BlueSquid/RV-C reliability, identifier allocation and diagnostics | 20-45 hours |
| Touchscreen hard switches, motion, dimming and remaining UI | 20-45 hours |
| Rear I/O, lighting and accessory state machines | 30-70 hours |
| History, RTC, SD recovery and export | 20-40 hours |
| iOS synchronization and regression work | 15-35 hours |
| Safety behavior, fault injection, commissioning tools and documentation | 30-70 hours |
| Dometic RV-C | 20-100+ hours after the hardware is confirmed |

A dependable low-voltage MVP is roughly **140-260 hours** of implementation,
bench work and testing from the repository's current state. The full system,
including RV-C, polished history, iOS parity and commissioned AC control, is
more realistically **220-440+ hours**. Appliance protocol surprises can move
those numbers considerably.

The current toolchain is appropriate:

- PlatformIO and Arduino C++ for both ESP32-S3 targets
- LVGL 9.5, BlueSquid's LVGL 9 display/touch port and Espressif's
  display-panel hardware stack on the touchscreen
- SwiftUI and CoreBluetooth for the optional iOS client
- a private versioned BLE protocol between the touchscreen and rear controller
- ESP32 NVS for settings and microSD for high-write history
- USB serial as the initial recovery/update path

The most maintainable scope is to make the touchscreen the primary interface
and keep iOS as a secondary BLE client. Requiring pixel-for-pixel feature parity
across both interfaces adds recurring development and test cost. CarPlay should
remain outside this project scope.

### 12.2 What to purchase now and what to defer

Buy for the communications/low-voltage bench first:

- rear ESP32-S3 DevKitC with the exact memory variant documented
- dual isolated MCP2515 board and M5Stack isolated CAN unit
- two protected 5 V power paths
- ADS1115, MCP23017 and ULN2803 modules
- one or two representative MOSFET channels, relays and dummy loads
- CAN cable, terminators, fuses, terminals and a bench emergency disconnect

Defer until measurements and model numbers are available:

- the expensive sixteen-channel lighting controller
- all production lighting MOSFET channels
- AC contactors, enclosure and energy meters
- any Dometic gateway or RV-C-specific connector
- per-circuit DC current sensors
- final fuse values, cable gauge and production enclosure size

This staged approach caps the first incremental spend at approximately
**$350-800 CAD** while proving the riskiest integrations: touchscreen BLE
reconnection, RV-C coexistence, Cerbo VE.Can data and protected output hardware.

## 13. Recommended implementation phases

1. Record every load's model, voltage, running current, starting current and
   cable distance. Confirm the Dometic model and control board.
2. Bench-test the rear ESP32, dual isolated CAN HAT and M5Stack Unit CAN with no
   vehicle loads connected.
3. Validate bonded BLE connection/reconnection between rear and touchscreen,
then independently observe and validate the RV-C bus and add ADS1115 switches
and PIR.
4. Implement VE.Can as an application-level receive-only client and compare
   every energy reading to the Cerbo Remote Console.
5. Build and test four low-current lighting channels, including shorts,
   disconnects, reboot and brownout behavior. Expand only after validation.
6. Add MCP23017/ULN2803 and low-voltage relay loads with dummy lamps before
   connecting pumps or USB zones.
7. Capture RV-C traffic in listen-only mode and validate the exact appliance
   protocol. Retain the native Dometic control.
8. Add rear RTC/microSD history and test abrupt power loss repeatedly.
9. Finalize the DC panel, wire schedule, fuses, terminals and enclosure.
10. Have the 120 V contactor/breaker/GFCI design reviewed and installed or
    inspected by an appropriately qualified person.
11. After a stable modular season, use the proven pinout, loads and thermal
    measurements as the requirements for a custom PCB.

## 14. Decisions required before final ordering

- exact rear ESP32-S3 development-board model
- exact touchscreen model/PCB revision and whether it is the Waveshare board
- Dometic fan/HVAC model, controller and gateway part numbers
- number, wattage and length of every white/RGBW lighting circuit
- water-pump running and stall current
- water-heater nameplate current and existing safety/control circuit
- number and power rating of controlled 120 V outlets
- number and wattage of USB-C/USB-A outlet zones
- approximate cable lengths from rear cabinet to every load
- desired front hard-switch functions
- premium integrated lighting board versus staged modular MOSFET prototype

Those details turn this architecture into a final BOM, fuse schedule, connector
schedule and point-to-point wiring diagram.
