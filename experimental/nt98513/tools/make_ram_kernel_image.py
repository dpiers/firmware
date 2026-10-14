#!/usr/bin/env python3
"""Package an owned NT98513 ELF/raw pair; never load or flash the image.

The candidate staging address is checked structurally, not certified as free
on hardware. See research/uboot-ram-image-contract.md (DAN-234).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import zlib

RAM_START = 0x00101000
RAM_END = 0x03400000
LOAD = 0x80101000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def uint32(value, name):
    require(type(value) is int and 0 <= value <= 0xffffffff, name+' must be uint32')
    return value


def digest(data):
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def physical(address):
    """Canonicalize only direct physical/KSEG0/KSEG1 address representations."""
    uint32(address, 'address')
    if address < 0x20000000:
        return address
    require(0x80000000 <= address < 0xc0000000, 'unsupported staging address alias')
    return address & 0x1fffffff


def package_kernel(elf, raw, timestamp, staging_address):
    uint32(timestamp, 'timestamp')
    uint32(staging_address, 'staging address')
    require(len(elf) >= 52 and elf[:7] == b'\x7fELF\x01\x01\x01',
            'ELF32 little-endian version 1 required')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', elf)
    require(h[1:4] == (2, 8, 1) and h[8] == 52, 'ELF executable/MIPS/header version')
    require(h[7] & 0xf000f020 == 0x70001000, 'MIPS32r2 O32 flags required')
    require(h[9] == 32 and h[10] > 0 and h[5] >= 52 and
            h[5] + h[9]*h[10] <= len(elf), 'program header bounds/stride')
    headers = [struct.unpack_from('<8I', elf, h[5]+32*i) for i in range(h[10])]
    require(all(p[0] not in (2, 3) for p in headers), 'dynamic/interpreter segment')
    loads = [p for p in headers if p[0] == 1]
    require(len(loads) == 1, 'exactly one LOAD segment required')
    _, offset, virtual, paddr, filesz, memsz, flags, _ = loads[0]
    require(virtual == paddr == LOAD, 'unexpected load address or physical alias')
    require(0 < filesz <= memsz and offset + filesz <= len(elf), 'LOAD file/memory bounds')
    require(len(raw) == filesz and raw == elf[offset:offset+filesz], 'raw bytes differ from complete LOAD')
    entry = h[4]
    require(entry % 4 == 0 and flags & 1 and virtual <= entry and entry+4 <= virtual+filesz,
            'entry outside aligned executable file bytes')
    require(RAM_START+memsz <= RAM_END, 'kernel/BSS reaches withheld RAM')

    # The original call at 0340e418 shifts the byte length in its delay slot.
    cache_bytes = 4*filesz
    cache_end = RAM_START+cache_bytes
    require(cache_bytes <= 0xffffffff and LOAD+cache_bytes <= 0xffffffff and
            ((cache_end+31) & ~31) <= RAM_END, 'fourfold cache range reaches withheld RAM')
    stage = physical(staging_address)
    stage_end = stage+64+filesz
    require(staging_address % 4 == 0 and staging_address+64+filesz <= 0xffffffff,
            'staging address alignment/wrap')
    require(RAM_START <= stage < stage_end <= RAM_END, 'complete staging span outside admitted RAM')
    require(stage_end <= RAM_START or RAM_START+memsz <= stage,
            'physical staging overlap with kernel/BSS')

    name = b'OpenIPC NT98513 RAM test'.ljust(32, b'\0')
    values = (0x27051956, 0, timestamp, filesz, LOAD, entry, zlib.crc32(raw),
              5, 5, 2, 0, name)
    header = bytearray(struct.pack('>7I4B32s', *values))
    struct.pack_into('>I', header, 4, zlib.crc32(header))
    image = bytes(header)+raw
    report = {
        'schema': 1,
        'format': 'legacy uImage; Linux/MIPS/single kernel/uncompressed',
        'elf': digest(elf), 'raw': digest(raw), 'image': digest(image),
        'timestamp': timestamp, 'load_address': LOAD, 'entry_address': entry,
        'staging_address': staging_address,
        'staging_physical_span': [stage, stage_end],
        'kernel_bss_physical_span': [RAM_START, RAM_START+memsz],
        'cache_physical_span': [RAM_START, (cache_end+31) & ~31],
        'admitted_ram': [RAM_START, RAM_END],
        'hardware_validated': False,
        'limitations': [
            'Candidate address arithmetic does not establish live staging ownership or a loading transport.',
            'ELF flags and LOAD checks do not audit every instruction, floating-point ABI, DT, initramfs or kernel configuration.',
            'Cache visibility, watchdog continuity, RTOS coexistence and camera boot remain untested.',
            'This container neither installs an RTOS capture module nor updates flash.',
        ],
    }
    return image, report


def publish_new(output, image):
    """Publish complete bytes with an exclusive hard link; never replace a path."""
    temp = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.nt98513-image-', dir=output.parent,
                                         delete=False) as stream:
            temp = Path(stream.name)
            stream.write(image)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temp, output)
    finally:
        if temp is not None:
            temp.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--timestamp', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--staging-address', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        image, report = package_kernel(args.elf.read_bytes(), args.raw.read_bytes(),
                                       args.timestamp, args.staging_address)
        publish_new(args.output, image)
        report['output'] = str(args.output.resolve())
        print(json.dumps(report, indent=2))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, 'RAM image refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
