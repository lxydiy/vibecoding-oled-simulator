"""Read PCF strike size and Unicode coverage; Pillow/FreeType decodes pixels.

Format reference: https://fontforge.org/docs/techref/pcf-format.html
"""
import struct
from pathlib import Path


class Reader:
    def __init__(self, data, endian='<'):
        self.data = data
        self.offset = 0
        self.endian = endian

    def take(self, size):
        if size < 0 or self.offset + size > len(self.data):
            raise ValueError('truncated PCF table')
        result = self.data[self.offset:self.offset + size]
        self.offset += size
        return result

    def unpack(self, fmt):
        return struct.unpack(self.endian + fmt, self.take(struct.calcsize(self.endian + fmt)))


def read_pcf_metadata(path: Path) -> tuple[int, set[int]]:
    """Return the native pixel size and explicitly encoded Unicode scalars.

    Do not use Pillow's PcfFontFile encoding loader: it exposes only 256
    characters. PCF glyph index zero is valid; 0xffff alone means missing.
    """
    reader = Reader(path.read_bytes())
    if reader.take(4) != b'\x01fcp':
        raise ValueError('invalid PCF signature')
    count, = reader.unpack('I')
    directory = Reader(reader.take(count * 16))
    tables = {}
    for _ in range(count):
        tag, fmt, size, offset = directory.unpack('4I')
        if tag in tables or offset < 8 + count * 16:
            raise ValueError('invalid PCF table directory')
        tables[tag] = (fmt, size, offset)

    def table(tag):
        if tag not in tables:
            raise ValueError(f'missing PCF table {tag}')
        fmt, size, offset = tables[tag]
        if size < 4 or offset + size > len(reader.data):
            raise ValueError(f'truncated PCF table {tag}')
        result = Reader(reader.data[offset:offset + size])
        if result.unpack('I')[0] != fmt:
            raise ValueError(f'inconsistent PCF table format {tag}')
        result.endian = '>' if fmt & 4 else '<'
        return result, fmt

    properties, fmt = table(1)
    if fmt & ~0x3f:
        raise ValueError('unsupported PCF properties format')
    count, = properties.unpack('I')
    records = Reader(properties.take(count * 9), properties.endian)
    properties.take((-count) % 4)
    strings = properties.take(properties.unpack('I')[0])

    def string(offset):
        if offset >= len(strings):
            raise ValueError('invalid PCF property string offset')
        end = strings.find(b'\0', offset)
        if end < 0:
            raise ValueError('unterminated PCF property string')
        return strings[offset:end]

    info = {}
    for _ in range(count):
        name, is_string, value = records.unpack('IBI')
        info[string(name)] = string(value) if is_string else value
    registry = info.get(b'CHARSET_REGISTRY')
    # WenQuanYi 0.9.9's 10/11/12pt strikes spell the encoding as "-1".
    if (not isinstance(registry, bytes) or registry.upper() != b'ISO10646'
            or info.get(b'CHARSET_ENCODING') not in (b'1', b'-1')):
        raise ValueError('PCF requires Unicode charset ISO10646-1')
    size = info.get(b'PIXEL_SIZE')
    if type(size) is not int or not 1 <= size <= 32767:
        raise ValueError('PCF requires a positive integer PIXEL_SIZE property (1..32767)')

    metrics, fmt = table(4)
    if fmt & ~0x3f == 0x100:
        glyph_count, = metrics.unpack('H')
        metrics.take(glyph_count * 5)
    elif fmt & ~0x3f == 0:
        glyph_count, = metrics.unpack('I')
        metrics.take(glyph_count * 12)
    else:
        raise ValueError('unsupported PCF metrics format')

    encoding, fmt = table(32)
    if fmt & ~0x3f:
        raise ValueError('unsupported PCF encoding format')
    first_col, last_col, first_row, last_row, _ = encoding.unpack('5H')
    if not (0 <= first_col <= last_col <= 255 and 0 <= first_row <= last_row <= 255):
        raise ValueError('invalid PCF encoding range')
    supported = set()
    for row in range(first_row, last_row + 1):
        for col in range(first_col, last_col + 1):
            index, = encoding.unpack('H')
            if index == 0xffff:
                continue
            if index >= glyph_count:
                raise ValueError('invalid PCF encoding glyph index')
            cp = row * 256 + col
            if not 0xd800 <= cp <= 0xdfff:
                supported.add(cp)
    return size, supported
