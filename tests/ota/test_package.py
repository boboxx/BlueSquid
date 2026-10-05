import hashlib
import importlib.util
import struct
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location('package_ota', 'tools/package_ota.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
image = b'\xe9' + bytes(range(256)) * 4
for environment, target in module.TARGETS.items():
    data = module.package_image(image, environment)
    magic, device, size, digest = struct.unpack('<8sB3xI32s16x', data[:64])
    assert magic == b'BSQOTA1\0' and device == target
    assert size == len(image) and data[64:] == image
    assert digest == hashlib.sha256(data[64:]).digest()
    corrupted = data[:-1] + bytes([data[-1] ^ 1])
    assert hashlib.sha256(corrupted[64:]).digest() != digest
    assert len(data[:-1]) - 64 != size
for invalid in (b'', b'\xe9', bytes(100)):
    try:
        module.package_image(invalid, 'main_controller')
        raise AssertionError('Invalid input accepted')
    except ValueError:
        pass
Path(sys.argv[1]).write_bytes(module.package_image(image, 'main_controller'))

# Both app slots fit 16 MB; NVS stays at the existing offset during migration.
rows = [line.split(',') for line in Path('partitions/touchscreen_ota.csv').read_text().splitlines()
        if line and not line.startswith('#')]
end = 0
for row in rows:
    name, _, subtype, offset, size, *_ = [field.strip() for field in row]
    offset, size = int(offset, 0), int(size, 0)
    assert offset >= end
    end = offset + size
    assert end <= 16 * 1024 * 1024
    if name == 'nvs':
        assert offset == 0x9000 and size == 0x5000
    if subtype in ('ota_0', 'ota_1'):
        assert offset % 0x10000 == 0 and size == 0x600000
assert sum(row[2].strip() in ('ota_0', 'ota_1') for row in rows) == 2
