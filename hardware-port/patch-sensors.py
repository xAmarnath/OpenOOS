"""Match Vivo's verified sensor payload ABI in copies of the GSI libraries.

Stock convertToSensorEvent routes types 3,5,6,8,11,36 to EventPayload::data.
Its data path also accepts standard types; AOSP restricts it to vendor types.
Keep the union tag check (data == 10) and every other conversion unchanged.
"""
from pathlib import Path
from io import BytesIO
import hashlib
import struct
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

BASE = Path(__file__).parent
TYPES = (3, 5, 6, 8, 11, 36)

def offset(e, address):
    s = next(s for s in e.iter_segments() if s['p_vaddr'] <= address < s['p_vaddr'] + s['p_filesz'])
    return s['p_offset'] + address - s['p_vaddr']

for name, table, base, default, guard in [
    ('libsensorservice.so', 0x161f4, 0x4be00, 0x4c10c, 0x4c118),
    ('libsensorserviceaidl.so', 0x602a, 0xc0d0, 0xc698, 0xc6a0),
]:
    data = bytearray((BASE/name).read_bytes())
    e = ELFFile(BytesIO(data))
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    g = offset(e, guard)
    ins = next(md.disasm(bytes(data[g:g+4]), guard))
    assert ins.mnemonic == 'b.lt', str(ins)
    # The unmodified tag test following this guard must still reject wrong tags.
    ins = list(md.disasm(bytes(data[g+4:g+16]), guard+4))
    assert [i.mnemonic for i in ins] == ['ldrb', 'cmp', 'b.ne']
    assert ins[1].op_str == 'w9, #0xa'
    data[g:g+4] = bytes.fromhex('1f2003d5')  # nop range check, as stock
    for sensor_type in TYPES:
        at = offset(e, table) + 2*sensor_type
        previous = base + struct.unpack_from('<H', data, at)[0]*4
        assert previous != default
        struct.pack_into('<H', data, at, (default-base)//4)
        print(f'{name}: type {sensor_type}: {previous:x} -> {default:x}')
    dest = BASE/name.replace('.so', '-vivo.so')
    dest.write_bytes(data)
    print(dest.name, hashlib.sha256(data).hexdigest())
