"""Compare ELF32 runtime contents while allowing removal of debug metadata."""
import struct


def runtime_contents(data):
    assert data[:6] == b'\x7fELF\x01\x01', 'Expected little-endian ELF32'
    header = bytearray(data[:52])
    # Section-table location/count/index are the only permitted header changes.
    header[32:36] = bytes(4)
    header[48:52] = bytes(4)
    phoff, shoff = struct.unpack_from('<II', data, 28)
    phsize, phnum, shsize, shnum, names_index = struct.unpack_from('<HHHHH', data, 42)
    program_headers = data[phoff:phoff + phsize * phnum]
    sections = [struct.unpack_from('<10I', data, shoff + i * shsize) for i in range(shnum)]
    names = sections[names_index]
    names = data[names[4]:names[4] + names[5]]
    allocated = {}
    for name, kind, flags, address, offset, size, link, info, align, entsize in sections:
        if not flags & 2:
            continue
        name = names[name:].split(b'\0', 1)[0]
        contents = b'' if kind == 8 else data[offset:offset + size]
        allocated[name] = (kind, flags, address, size, align, entsize, contents)
    return bytes(header), program_headers, allocated


def assert_same_runtime(before, after):
    assert runtime_contents(before) == runtime_contents(after), 'ELF runtime contents changed'
