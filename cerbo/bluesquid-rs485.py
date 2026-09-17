#!/usr/bin/env python3
"""Send selected Venus OS D-Bus values to BlueSquid over RS-485."""

import glob
import os
import sys
import termios
import time

import dbus

BAUD = termios.B115200
INTERVAL_SECONDS = 1.0


def value(bus, service, path, default=None):
    if not service:
        return default
    try:
        obj = bus.get_object(service, path)
        return dbus.Interface(obj, "com.victronenergy.BusItem").GetValue()
    except dbus.DBusException:
        return default


def services(bus):
    names = bus.get_object("org.freedesktop.DBus", "/org/freedesktop/DBus")
    return dbus.Interface(names, "org.freedesktop.DBus").ListNames()


def identify_devices(bus):
    battery = None
    solar = None
    dc_dc = None
    for service in services(bus):
        if service.startswith("com.victronenergy.battery."):
            product = str(value(bus, service, "/ProductName", "")).lower()
            if battery is None or "smartshunt" in product:
                battery = service
        elif service.startswith("com.victronenergy.solarcharger."):
            product = str(value(bus, service, "/ProductName", "")).lower()
            if "orion" in product:
                dc_dc = service
            elif solar is None:
                solar = service
    return battery, solar, dc_dc


def number(bus, service, path, default=0.0):
    result = value(bus, service, path, default)
    try:
        return float(result)
    except (TypeError, ValueError):
        return default


def integer(bus, service, path, default=255):
    result = value(bus, service, path, default)
    try:
        return int(result)
    except (TypeError, ValueError):
        return default


def xor_checksum(payload):
    checksum = 0
    for byte in payload.encode("ascii"):
        checksum ^= byte
    return checksum


def serial_device():
    if len(sys.argv) > 1:
        return sys.argv[1]
    matches = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    if not matches:
        raise RuntimeError("No USB serial adapter found")
    return matches[-1]


def open_serial(path):
    descriptor = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_SYNC)
    attributes = termios.tcgetattr(descriptor)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    attributes[3] = 0
    attributes[4] = BAUD
    attributes[5] = BAUD
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 5
    termios.tcsetattr(descriptor, termios.TCSANOW, attributes)
    return descriptor


def main():
    bus = dbus.SystemBus()
    port = serial_device()
    output = open_serial(port)
    sequence = 0
    cached_devices = (None, None, None)
    next_discovery = 0.0

    while True:
        started = time.monotonic()
        if started >= next_discovery:
            cached_devices = identify_devices(bus)
            next_discovery = started + 10.0
        battery, solar, dc_dc = cached_devices

        mask = (1 if battery else 0) | (2 if solar else 0) | (4 if dc_dc else 0)
        voltage = number(bus, battery, "/Dc/0/Voltage")
        current = number(bus, battery, "/Dc/0/Current")
        power = value(bus, battery, "/Dc/0/Power")
        if power is None:
            power = voltage * current
        soc = number(bus, battery, "/Soc")
        consumed_ah = number(bus, battery, "/ConsumedAmphours")
        time_to_go = number(bus, battery, "/TimeToGo") / 60.0
        solar_power = number(bus, solar, "/Yield/Power")
        solar_state = integer(bus, solar, "/State")
        dc_voltage = number(bus, dc_dc, "/Dc/0/Voltage")
        dc_current = number(bus, dc_dc, "/Dc/0/Current")
        dc_power = dc_voltage * dc_current
        dc_state = integer(bus, dc_dc, "/State")

        payload = (
            "BSQ,1,{},{},{:.3f},{:.3f},{:.1f},{:.1f},{:.3f},{:.1f},"
            "{:.1f},{},{:.1f},{}"
        ).format(sequence, mask, voltage, current, float(power), soc,
                 consumed_ah, time_to_go, solar_power, solar_state,
                 dc_power, dc_state)
        frame = "${}*{:02X}\r\n".format(payload, xor_checksum(payload))
        os.write(output, frame.encode("ascii"))
        sequence = (sequence + 1) & 0xFFFFFFFF
        time.sleep(max(0.0, INTERVAL_SECONDS - (time.monotonic() - started)))


if __name__ == "__main__":
    main()
