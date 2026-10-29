#!/usr/bin/env python3
"""Compile YAML resources into deterministic, monochrome C font tables."""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import fontTools
import yaml
from fontTools.ttLib import TTFont
from PIL import ImageFont

from pcf import read_pcf_metadata


class ResourceError(ValueError):
    pass


class MarkedMap(dict):
    def __init__(self):
        super().__init__()
        self.marks = {}
        self.unicode_escapes = {}


class ResourceLoader(yaml.SafeLoader):
    """Keep locations and reject duplicate keys before dict construction."""


# YAML 1.2-style booleans: keys such as `on` and `off` remain strings.
ResourceLoader.yaml_implicit_resolvers = {
    key: [(tag, rx) for tag, rx in entries
          if tag not in ('tag:yaml.org,2002:bool', 'tag:yaml.org,2002:timestamp')]
    for key, entries in yaml.SafeLoader.yaml_implicit_resolvers.items()
}
ResourceLoader.add_implicit_resolver(
    'tag:yaml.org,2002:bool', re.compile(r'^(?:true|True|TRUE|false|False|FALSE)$'), list('tTfF'))


def unicode_escape_positions(source, value):
    """Let YAML handle folding/quoting, marking only explicit numeric escapes."""
    marker_codepoint = 0xe000
    while chr(marker_codepoint) in value:
        marker_codepoint += 1
    marker = chr(marker_codepoint)
    marked = re.sub(
        r'\\(?:x[0-9a-fA-F]{2}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|.)',
        lambda match: marker if match[0][1] in 'xuU' else match[0],
        source, flags=re.DOTALL)
    if marked == source:
        return set()
    # Substituting one codepoint keeps decoded positions, including folded lines.
    return {index for index, ch in enumerate(yaml.safe_load(marked)) if ch == marker}


def construct_mapping(loader, node):
    result = MarkedMap()
    for key_node, value_node in node.value:
        if key_node.tag == 'tag:yaml.org,2002:merge':
            raise ResourceError(f'{key_node.start_mark}: YAML merge keys are not supported')
        key = loader.construct_object(key_node, deep=True)
        try:
            hash(key)
        except TypeError as exc:
            raise ResourceError(f'{key_node.start_mark}: complex YAML mapping keys are not supported') from exc
        if key in result:
            raise ResourceError(f'{key_node.start_mark}: duplicate YAML key {key!r}; first at {result.marks[key]}')
        result[key] = loader.construct_object(value_node, deep=True)
        result.marks[key] = key_node.start_mark
        if isinstance(value_node, yaml.ScalarNode) and value_node.style == '"' and type(result[key]) is str:
            raw = loader.source[value_node.start_mark.index:value_node.end_mark.index]
            result.unicode_escapes[key] = unicode_escape_positions(raw, result[key])
    return result


ResourceLoader.add_constructor('tag:yaml.org,2002:map', construct_mapping)


def location(mapping, key):
    mark = getattr(mapping, 'marks', {}).get(key)
    return f'{mark.name}:{mark.line + 1}:{mark.column + 1}' if mark else str(key)


def require_mapping(value, where):
    if not isinstance(value, dict):
        raise ResourceError(f'{where}: expected a mapping')
    if any(type(key) is not str for key in value):
        raise ResourceError(f'{where}: schema mapping keys must be strings')
    return value


def require_list(value, where):
    if not isinstance(value, list):
        raise ResourceError(f'{where}: expected a list')
    return value


def required(mapping, key, where):
    if key not in mapping:
        raise ResourceError(f'{where}: missing required field {key!r}')
    return mapping[key]


def identifier(value, where):
    if type(value) is not str or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', value, re.ASCII):
        raise ResourceError(f'{where}: expected an ASCII identifier')
    return value.upper()


def scalar(cp, where):
    if type(cp) is not int or not 0 <= cp <= 0x10ffff or 0xd800 <= cp <= 0xdfff:
        raise ResourceError(f'{where}: invalid Unicode scalar {cp!r}')
    return cp


def text_codepoints(value, where):
    if type(value) is not str:
        raise ResourceError(f'{where}: expected a string (quote numbers/booleans explicitly)')
    points = set()
    for ch in value:
        cp = scalar(ord(ch), where)
        if cp in (9, 10, 13):
            continue
        if cp < 32 or 0x7f <= cp <= 0x9f:
            raise ResourceError(f'{where}: unsupported control character U+{cp:04X}')
        points.add(cp)
    return points


def resolve_path(value, base, where):
    if type(value) is not str or not value:
        raise ResourceError(f'{where}: expected a nonempty path string')
    return (base / value).resolve()


@dataclass
class FontSpec:
    name: str
    source: Path
    size: int | None
    required: set[int] = field(default_factory=set)
    dictionary: set[int] = field(default_factory=set)


@dataclass
class Resources:
    namespace: str
    fonts: dict[str, FontSpec]
    texts: dict[str, str]
    warnings: list[str]
    text_escapes: dict[str, set[int]]


def load_resources(path: Path) -> Resources:
    path = path.resolve()
    try:
        source = path.read_text(encoding='utf-8')
        # Reject alias graphs, so schema walking cannot encounter recursive data.
        for event in yaml.parse(source):
            if isinstance(event, yaml.AliasEvent):
                raise ResourceError(f'{path}:{event.start_mark.line + 1}: YAML aliases are not supported')
        with path.open(encoding='utf-8') as stream:
            loader = ResourceLoader(stream)
            loader.source = source
            try:
                root = require_mapping(loader.get_single_data(), str(path))
            finally:
                loader.dispose()
    except (UnicodeError, yaml.YAMLError) as exc:
        raise ResourceError(f'{path}: invalid UTF-8/YAML: {exc}') from exc
    warnings = []

    def unknown(mapping, known, where):
        for key in sorted(mapping.keys() - set(known)):
            warnings.append(f'{location(mapping, key)}: unknown field {where}.{key}; ignored')

    unknown(root, ('namespace', 'fonts', 'pages'), 'root')
    raw_namespace = required(root, 'namespace', 'root')
    namespace = identifier(raw_namespace.upper() if type(raw_namespace) is str else raw_namespace, 'namespace')
    # Global C names beginning with underscore are reserved by C.
    if namespace.startswith('_'):
        raise ResourceError('namespace: leading underscore would produce reserved C identifiers')
    raw_fonts = require_mapping(required(root, 'fonts', 'root'), 'fonts')
    fonts = {}
    bpp_ignored = False
    for name, raw in sorted(raw_fonts.items()):
        if not re.fullmatch(r'[a-z0-9_]+', name, re.ASCII):
            raise ResourceError(f'{location(raw_fonts, name)}: font names allow only lowercase letters, digits and underscores')
        raw = require_mapping(raw, f'fonts.{name}')
        unknown(raw, ('source', 'pixel_size', 'bpp', 'extra_ranges', 'extra_chars', 'extra_chars_files'), f'fonts.{name}')
        source = resolve_path(required(raw, 'source', name), path.parent, name + '.source')
        if source.suffix.lower() == '.pcf':
            size = None
            if 'pixel_size' in raw:
                warnings.append(f'{name}: pixel_size ignored for PCF; using the font native bitmap size')
        else:
            size = required(raw, 'pixel_size', name)
            if type(size) is not int or not 1 <= size <= 256:
                raise ResourceError(f'{name}.pixel_size: expected an integer from 1 to 256')
        spec = FontSpec(name, source, size)
        bpp_ignored |= 'bpp' in raw
        for item in require_list(raw.get('extra_ranges', []), name + '.extra_ranges'):
            item = require_mapping(item, name + '.extra_ranges[]')
            unknown(item, ('from', 'to'), name + '.extra_ranges[]')
            start = scalar(required(item, 'from', name), name + '.from')
            end = scalar(required(item, 'to', name), name + '.to')
            if start > end or (start <= 0xdfff and end >= 0xd800):
                raise ResourceError(f'{name}: reversed range or range crossing Unicode surrogates')
            if end - start + 1 > 65535:
                raise ResourceError(f'{name}: one range may contain at most 65535 codepoints')
            spec.required.update(text_codepoints(''.join(map(chr, range(start, end + 1))), name + '.extra_ranges'))
        extra = raw.get('extra_chars', [])
        if isinstance(extra, str):
            extra = [extra]
        for item in require_list(extra, name + '.extra_chars'):
            if type(item) is int:
                item = chr(scalar(item, name + '.extra_chars'))
            spec.required.update(text_codepoints(item, name + '.extra_chars'))
        for entry in require_list(raw.get('extra_chars_files', []), name + '.extra_chars_files'):
            dictionary = resolve_path(entry, path.parent, name + '.extra_chars_files')
            try:
                contents = dictionary.read_bytes().decode('utf-8')
            except UnicodeError as exc:
                raise ResourceError(f'{dictionary}: dictionary must be UTF-8') from exc
            if contents.startswith('\ufeff'):
                raise ResourceError(f'{dictionary}: dictionary must not have a UTF-8 BOM')
            spec.dictionary.update(text_codepoints(contents, str(dictionary)))
        fonts[name] = spec
    if bpp_ignored:
        warnings.append('bpp ignored by the OLED backend; all output uses 1 bpp')
    pages = require_mapping(required(root, 'pages', 'root'), 'pages')
    identities = {}
    texts = {}
    text_escapes = {}
    locations = {}
    page_names = {}
    for page, groups in pages.items():
        canonical_page = identifier(page, location(pages, page))
        if canonical_page in page_names:
            raise ResourceError(f'{location(pages, page)}: page names differ only by case; first at {page_names[canonical_page]}')
        page_names[canonical_page] = location(pages, page)
        for group in require_list(groups, 'pages.' + page):
            group = require_mapping(group, 'pages.' + page + '[]')
            unknown(group, ('font', 'texts'), 'pages.' + page + '[]')
            name = required(group, 'font', page)
            if type(name) is not str or name not in fonts:
                raise ResourceError(f'{location(group, "font")}: unknown font {name!r}; references are case-sensitive')
            entries = require_mapping(required(group, 'texts', page), page + '.texts')
            for key, value in entries.items():
                canonical_key = identifier(key, location(entries, key))
                where = location(entries, key)
                chars = text_codepoints(value, where)
                identity = (canonical_page, canonical_key)
                macro = f'{namespace}_TEXT_{canonical_page}_{canonical_key}'
                if macro in identities and identities[macro] != identity:
                    raise ResourceError(f'{where}: macro collision {macro} from different identities; first at {locations[macro]}')
                if macro in texts and texts[macro] != value:
                    raise ResourceError(f'{where}: conflicting text for {macro}; first at {locations[macro]}')
                identities[macro] = identity
                texts[macro] = value
                positions = entries.unicode_escapes.get(key, set())
                # Prefer a literal spelling at each position when equal declarations merge.
                text_escapes[macro] = text_escapes[macro] & positions if macro in text_escapes else positions
                locations.setdefault(macro, where)
                fonts[name].required.update(chars)
    for spec in fonts.values():
        if len(spec.required | spec.dictionary) > 65535:
            raise ResourceError(f'{spec.name}: at most 65535 unique codepoints per font')
    return Resources(namespace, fonts, texts, warnings, text_escapes)


def c_string(value: str, unicode_escapes=()) -> str:
    escaped = {'\\': '\\\\', '"': '\\"', '\n': '\\n', '\r': '\\r', '\t': '\\t', '?': '\\?'}
    result = []
    previous_hex = False
    for index, ch in enumerate(value):
        if index in unicode_escapes:
            result.append(''.join(f'\\x{byte:02X}' for byte in ch.encode('utf-8')))
            previous_hex = True
        else:
            if previous_hex and ch in '0123456789abcdefABCDEF':
                result.append('""')  # End C's variable-length hex escape before literal text.
            result.append(escaped.get(ch, ch))
            previous_hex = False
    return '"' + ''.join(result) + '"'


def codes_summary(points):
    points = sorted(points)
    sample = ', '.join(f'U+{cp:04X}' for cp in points[:16])
    return sample + (f', ... ({len(points)} total)' if len(points) > 16 else '')


def rasterize(spec: FontSpec, warnings: list[str]):
    if not spec.source.is_file():
        raise ResourceError(f'{spec.name}: font file not found: {spec.source}')
    size = spec.size
    if spec.source.suffix.lower() == '.pcf':
        try:
            size, supported = read_pcf_metadata(spec.source)
        except ValueError as exc:
            raise ResourceError(f'{spec.name}: invalid PCF font: {exc}') from exc
    else:
        with TTFont(spec.source, fontNumber=0, lazy=True) as face:
            cmap = face.getBestCmap() or {}
            supported = {cp for cp, glyph in cmap.items() if face.getGlyphID(glyph) != 0}
    missing = spec.required - supported
    if missing:
        raise ResourceError(f'{spec.name}: missing required glyphs: {codes_summary(missing)}')
    optional_missing = spec.dictionary - supported
    if optional_missing:
        warnings.append(f'{spec.name}: dictionary glyphs missing and skipped: {codes_summary(optional_missing)}')
    points = sorted((spec.required | spec.dictionary) & supported)
    font = ImageFont.truetype(str(spec.source), size, index=0, layout_engine=ImageFont.Layout.BASIC)
    ascent, descent = font.getmetrics()
    top, bottom = -ascent, descent
    bitmap = bytearray()
    glyphs = []
    mapping = []
    bitmap_cache = {}
    for cp in points:
        mask, (offset_x, offset_y) = font.getmask2(chr(cp), mode='1', anchor='ls')
        width, height = mask.size
        if width > 65535 or height > 65535 or not -32768 <= offset_x <= 32767 or not -32768 <= offset_y <= 32767:
            raise ResourceError(f'{spec.name}: glyph metrics exceed the C data format for U+{cp:04X}')
        top = min(top, offset_y)
        bottom = max(bottom, offset_y + height)
        advance = int(font.getlength(chr(cp)) + 0.5)
        if not 0 <= advance <= 65535:
            raise ResourceError(f'{spec.name}: advance exceeds uint16_t for U+{cp:04X}')
        packed = bytearray(width * ((height + 7) // 8))
        for y in range(height):
            for x in range(width):
                if mask[y * width + x]:
                    packed[(y // 8) * width + x] |= 1 << (y % 8)
        bits_key = (width, height, bytes(packed))
        if bits_key not in bitmap_cache:
            bitmap_cache[bits_key] = len(bitmap)
            bitmap.extend(packed)
        glyph = (bitmap_cache[bits_key], width, height, offset_x, offset_y, advance)
        glyphs.append(glyph)
        mapping.append(cp)
    if len(bitmap) > 0xffffffff or bottom - top > 65535 or -top > 65535:
        raise ResourceError(f'{spec.name}: font exceeds the C data format')
    return mapping, glyphs, bytes(bitmap), bottom - top, -top


def render_font(spec, symbol, warnings):
    mapping, glyphs, bitmap, line_height, baseline = rasterize(spec, warnings)
    lines = [
        '/* Generated by tools/font_generator/generate.py; do not edit. */',
        '#include "ui_resources.h"', '',
        'static const uint8_t bitmap[] = {',
    ]
    if not bitmap:
        lines.append('    0x00,')
    for offset in range(0, len(bitmap), 16):
        lines.append('    ' + ', '.join(f'0x{b:02X}' for b in bitmap[offset:offset + 16]) + ',')
    lines += ['};', '', 'static const ui_glyph_t glyphs[] = {']
    for glyph in glyphs:
        lines.append('    {' + ', '.join(map(str, glyph)) + '},')
    if not glyphs:
        lines.append('    {0, 0, 0, 0, 0, 0},')
    lines += ['};', '', 'static const uint32_t codepoints[] = {']
    for cp in mapping:
        lines.append(f'    0x{cp:06X}u,')
    if not mapping:
        lines.append('    0,')
    lines += ['};', '', f'const ui_font_t {symbol} = {{',
              f'    codepoints, glyphs, bitmap, {len(mapping)}u,',
              f'    {line_height}, {baseline}', '};', '']
    return '\n'.join(lines)


def generate(config: Path, output: Path) -> tuple[list[Path], list[str]]:
    resources = load_resources(config)
    prefix = resources.namespace.lower()
    guard = resources.namespace + '_RESOURCES_H_INCLUDED'
    public_names = {guard: 'header guard'}
    # A text macro can also collide with an internal type/function used by the C renderer.
    for name in ('ui_font_t', 'ui_glyph_t', 'ui_text_size_t',
                 'ui_text_draw', 'ui_text_measure', 'ui_font_find_glyph'):
        public_names[name] = 'runtime API'
    symbols = {}
    for name in sorted(resources.fonts):
        symbol = f'{prefix}_font_{name}'
        if symbol in public_names:
            raise ResourceError(f'C symbol collision: {symbol} ({public_names[symbol]})')
        public_names[symbol] = 'font ' + name
        symbols[name] = symbol
    for macro in sorted(resources.texts):
        if macro in public_names:
            raise ResourceError(f'C symbol collision: {macro}')
        public_names[macro] = 'text'
    # Prepare every file in memory first: schema/font errors never publish partial output.
    files = {}
    for name, spec in sorted(resources.fonts.items()):
        files[symbols[name] + '.c'] = render_font(spec, symbols[name], resources.warnings)
    header = ['/* Generated by tools/font_generator/generate.py; do not edit. */',
              f'#ifndef {guard}', f'#define {guard}', '', '#include "ui/draw/text.h"', '']
    for name in sorted(symbols):
        header.append(f'extern const ui_font_t {symbols[name]};')
    header.append('')
    for macro, value in sorted(resources.texts.items()):
        header.append(f'#define {macro} {c_string(value, resources.text_escapes[macro])}')
    header += [f'#endif /* {guard} */', '']
    files['ui_resources.h'] = '\n'.join(header)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    paths = []
    for name, content in sorted(files.items()):
        target = output / name
        encoded = content.encode('utf-8')
        if not target.exists() or target.read_bytes() != encoded:
            temporary = output / (name + '.tmp')
            temporary.write_bytes(encoded)
            temporary.replace(target)
        paths.append(target)
    return paths, sorted(resources.warnings)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('yaml', type=Path, help='YAML resource configuration')
    parser.add_argument('-o', '--output-dir', type=Path, default=Path('out'),
                        help='output directory, relative to the current directory (default: ./out)')
    args = parser.parse_args(argv)
    try:
        paths, warnings = generate(args.yaml, args.output_dir)
    except (ResourceError, OSError, ValueError, yaml.YAMLError, fontTools.ttLib.TTLibError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 1
    for warning in warnings:
        print(f'warning: {warning}', file=sys.stderr)
    for path in paths:
        print(path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
