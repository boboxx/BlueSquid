# BlueSquid Protocol on the Shared RV-C CAN Network

In the current isolated bench configuration, the rear controller is the source
of truth and broadcasts status at 250 kbit/s using standard 11-bit CAN
identifiers. Multi-byte values are little-endian. Protocol version 1 is defined
in `include/BlueSquidCanProtocol.h`.

The standard 11-bit identifiers below are the existing BlueSquid bench
allocation. They must not be transmitted unchanged on a live RV-C network.
Before deployment, the protocol must move to an RV-C-compatible 29-bit
proprietary-message/PGN allocation with valid source addressing and verified
bus loading. A successful isolated bench test alone does not prove coexistence.

| CAN ID | Direction | Contents |
| ---: | --- | --- |
| `0x100` | Rear -> nodes | Heartbeat, protocol version and validity flags |
| `0x101` | Rear -> nodes | Rear-controller semantic firmware version |
| `0x110` | Rear -> nodes | Battery voltage, current, power and state of charge |
| `0x111` | Rear -> nodes | Solar, DC/DC and load power plus charger states |
| `0x112` | Rear -> nodes | Remaining/consumed Ah and time-to-go |
| `0x120` | Rear -> nodes | Cabin temperature and humidity |
| `0x121` | Rear -> nodes | Calibrated pitch, roll and level validity |
| `0x130` | Rear -> nodes | Light, fan and three generic accessory states plus saved light levels |
| `0x131` | Rear -> nodes | Front RGBW levels |
| `0x132` | Rear -> nodes | Rear RGBW levels |
| `0x133` | Rear -> nodes | Saved front RGB colour, brightness and channel selections |
| `0x134` | Rear -> nodes | Saved rear RGB colour, brightness and channel selections |
| `0x135` | Rear -> nodes | Battery capacity and pitch/roll calibration settings |
| `0x200` | Nodes -> rear | Accessory command and sequence number |
| `0x201` | Rear -> nodes | Command acknowledgement |

Commands cover the three dimmable light circuits, both RGBW zones, fan and
three generic accessory controls, all-off/all-on scenes, persistent level calibration and
an immediate status request. RGBW preset fields are sent separately from live
channel levels so colour and brightness remain available while a zone is off.
The command sender increments an 8-bit sequence number; the acknowledgement
returns that sequence and a result code. CAN transports controls and current
state only. Safety interlocks and physical output ownership remain in the rear
controller.

Output state is saved as one versioned, checksummed NVS record three seconds
after the last change. Lights, RGBW zones, fan and USB restore after a restart.
The pump and accessory 3 requests are recorded for diagnostics/configuration,
but both physical outputs always start off for safety.

Future expansion nodes should listen before transmitting and use identifiers
and source addresses proven not to conflict with installed RV-C equipment.
BlueSquid traffic must respect RV-C arbitration and bus-load requirements. The
second isolated CAN channel is dedicated to Victron VE.Can and must not be tied
to this shared network.
