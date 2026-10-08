"""Package the application binary with device identity, length and SHA-256."""
import hashlib
import struct
from pathlib import Path

TARGETS = {"main_controller": 1, "touchscreen_controller": 2,
           "touchscreen_10in": 3}


def package_image(image, environment):
    if len(image) < 32 or image[0] != 0xE9:
        raise ValueError("Expected an ESP application firmware.bin")
    return (struct.pack("<8sB3xI32s16x", b"BSQOTA1\0", TARGETS[environment],
                        len(image), hashlib.sha256(image).digest()) + image)


def after_build(source, target, env):
    binary = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin"))
    destination = binary.with_name("BlueSquid-" + env["PIOENV"] + ".bsfw")
    destination.write_bytes(package_image(binary.read_bytes(), env["PIOENV"]))
    print("OTA package: " + str(destination))


# PlatformIO loads this file as an extra script; ordinary imports support tests.
if "Import" in globals():
    Import("env")
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)
