from pathlib import Path

def patch(src, dest, edits):
    data = bytearray(Path(src).read_bytes())
    for offset, original, words in edits:
        original = bytes.fromhex(original)
        assert data[offset:offset + len(original)] == original, hex(offset)
        replacement = b''.join(w.to_bytes(4, 'little') for w in words)
        assert len(original) == len(replacement)
        data[offset:offset + len(original)] = replacement
    Path(dest).write_bytes(data)

def branch(at, target):
    return 0x14000000 | (((target - at) // 4) & 0x3ffffff)

# Guest network namespace has no external interfaces. Keep map initialization
# and Binder netd services, omit cgroup attachments and unsupported diagnostics.
patch('libnetd_updatable17.so', 'libnetd_updatable17-guest.so', [
    (0xf670, '860e0094', [0xd2800000]),
    (0xc1ec, '500e0094', [0xd503201f]),
    (0xc4e4, 'a80357b8', [branch(0xc4e4, 0xc760)]),
    (0xc714, '88d24039', [branch(0xc714, 0xc760)]),
    (0xbf2c, 'c2200054', [0xd503201f]),
])

# WaitForService returns a unique_ptr through x8. Return its normal null
# representation instead of connecting to any boot-slot management HAL.
# Preserve PACIASP and authenticate the untouched LR before returning.
patch('libboot_control_client17.so', 'libboot_control_client17-guest.so', [
    (0x44bc, 'ffc302d1fd7b07a9f85f08a9',
     [0xf900011f, 0xd50323bf, 0xd65f03c0]),
])
