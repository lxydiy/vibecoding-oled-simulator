"""Generator regression tests; build a tiny original font, no system font needed."""
import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import generate as gen


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(__file__).resolve().parents[2] / 'build' / 'font_generator_tests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = self.root / 'resources.yml'
        self.output = self.root / 'generated'

    def write(self, text):
        self.config.write_text(text, encoding='utf-8')
        return self.config

    def minimal(self, texts='        title: "A"\n', extra=''):
        return ('namespace: ui\nfonts:\n  tiny:\n    source: font.ttf\n'
                '    pixel_size: 16\n' + extra +
                'pages:\n  home:\n    - font: tiny\n      texts:\n' + texts)

    def make_font(self):
        builder = FontBuilder(1000, isTTF=True)
        builder.setupGlyphOrder(['.notdef', 'space', 'box'])
        builder.setupCharacterMap({32: 'space', 63: 'box', 65: 'box', 66: 'box',
                                   0x4e2d: 'box', 0x6d4b: 'box', 0x8bd5: 'box', 0x1f600: 'box'})
        glyphs = {}
        for name in ('.notdef', 'space', 'box'):
            pen = TTGlyphPen(None)
            if name != 'space':
                pen.moveTo((50, 0))
                pen.lineTo((450, 0))
                pen.lineTo((450, 700))
                pen.lineTo((50, 700))
                pen.closePath()
            glyphs[name] = pen.glyph()
        builder.setupGlyf(glyphs)
        builder.setupHorizontalMetrics({name: (500, 0) for name in glyphs})
        builder.setupHorizontalHeader(ascent=800, descent=-200)
        builder.setupNameTable({'familyName': 'TestFont', 'styleName': 'Regular',
                               'uniqueFontIdentifier': 'TestFont', 'fullName': 'TestFont',
                               'psName': 'TestFont'})
        builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
        builder.setupPost()
        builder.setupMaxp()
        builder.font['head'].created = builder.font['head'].modified = 3400000000
        builder.save(self.root / 'font.ttf')

    def make_pcf(self, endian='<', compressed=True, registry=b'ISO10646', charset_encoding=b'1'):
        """Original 5x9/9x9 bitmap fixture, including glyph zero and CJK."""
        fmt = 8 | (4 if endian == '>' else 0)  # MSB pixels, one-byte row padding.

        def pack(form, *values):
            return struct.pack(endian + form, *values)

        def table(data, flags=0):
            return struct.pack('<I', fmt | flags) + data

        strings = bytearray()

        def string(value):
            offset = len(strings)
            strings.extend(value + b'\0')
            return offset

        properties = [(b'PIXEL_SIZE', 9), (b'CHARSET_REGISTRY', registry),
                      (b'CHARSET_ENCODING', charset_encoding)]
        records = bytearray()
        for name, value in properties:
            name_offset = string(name)
            is_string = isinstance(value, bytes)
            records.extend(pack('IBI', name_offset, is_string, string(value) if is_string else value))
        props = table(pack('I', 3) + records + b'\0' + pack('I', len(strings)) + strings)
        metrics = [(0, 5, 5, 7, 2), (0, 0, 3, 0, 0), (0, 9, 9, 7, 2)]
        if compressed:
            metric_table = table(pack('H', 3) + bytes(v + 128 for m in metrics for v in m), 0x100)
        else:
            metric_table = table(pack('I', 3) + b''.join(pack('5hH', *m, 0) for m in metrics))
        accel = table(bytes([0, 0, 0, 0, 1, 0, 0, 0]) + pack('3i', 7, 2, 0)
                      + pack('5hH', *metrics[1], 0) + pack('5hH', *metrics[2], 0))
        # A/B share glyph zero; the last pixel row exercises the second OLED page.
        pixels = bytes([0x88, 0x50, 0x20, 0xF8, 0x88, 0x88, 0x88, 0x00, 0xF8])
        pixels += bytes([0xFF, 0x80] + [0x80, 0x80] * 7 + [0xFF, 0x80])
        bitmaps = table(pack('I3I4I', 3, 0, 9, 9, *([len(pixels)] * 4)) + pixels)
        indices = [0xffff] * (0x4f * 256)
        indices[32], indices[65], indices[66], indices[0x4e2d] = 1, 0, 0, 2
        encoding = table(pack('5H', 0, 255, 0, 0x4e, 65) + pack(f'{len(indices)}H', *indices))
        tables = [(1, props), (2, accel), (4, metric_table), (8, bitmaps), (32, encoding)]
        offset = 8 + 16 * len(tables)
        toc, body = bytearray(), bytearray()
        for tag, data in tables:
            data += bytes((-len(data)) % 4)
            toc.extend(struct.pack('<4I', tag, struct.unpack('<I', data[:4])[0], len(data), offset))
            body.extend(data)
            offset += len(data)
        path = self.root / 'font.pcf'
        path.write_bytes(b'\x01fcp' + struct.pack('<I', len(tables)) + toc + body)
        return path

    def test_pcf_native_pixels_unicode_and_byte_orders(self):
        for endian in ('<', '>'):
            for compressed in (True, False):
                with self.subTest(endian=endian, compressed=compressed):
                    path = self.make_pcf(endian, compressed)
                    spec = gen.FontSpec('tiny', path, 256, set(map(ord, ' AB中')))
                    mapping, glyphs, bitmap, height, baseline = gen.rasterize(spec, [])
                    self.assertEqual(mapping, [32, 65, 66, 0x4e2d])
                    self.assertEqual((height, baseline), (9, 7))
                    self.assertEqual(glyphs[0][-1], 3)  # Space still advances.
                    self.assertEqual(glyphs[1][1:], (5, 9, 0, -7, 5))
                    self.assertEqual(glyphs[1], glyphs[2])
                    offset = glyphs[1][0]
                    self.assertEqual(bitmap[offset:offset + 10], bytes([0x79, 0x0A, 0x0C, 0x0A, 0x79, 1, 1, 1, 1, 1]))
                    self.assertEqual(glyphs[3][1:], (9, 9, 0, -7, 9))
                    offset = glyphs[3][0]
                    self.assertEqual(bitmap[offset:offset + 18], bytes([255] + [1] * 7 + [255] + [1] * 9))

    def test_pcf_wenquanyi_legacy_charset_spelling(self):
        path = self.make_pcf(charset_encoding=b'-1')
        result = gen.rasterize(gen.FontSpec('tiny', path, None, {65, 0x4e2d}), [])
        self.assertEqual(result[0], [65, 0x4e2d])

    def test_pcf_pixel_size_ignored_or_omitted_and_determinism(self):
        self.make_pcf()
        base = self.minimal('        title: "AB中 "\n').replace('font.ttf', 'font.pcf')
        snapshot = None
        for size in ('1', '256', '0', 'false', 'null', '"ignored"', None):
            config = base.replace('    pixel_size: 16\n', '' if size is None else f'    pixel_size: {size}\n')
            paths, warnings = gen.generate(self.write(config), self.output)
            current = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in paths}
            if snapshot is not None:
                self.assertEqual(snapshot, current)
            snapshot = current
            self.assertEqual(any('pixel_size ignored' in w for w in warnings), size is not None)
        # Outline font validation remains strict.
        for size in ('0', 'false', 'null'):
            with self.assertRaisesRegex(gen.ResourceError, 'pixel_size'):
                gen.load_resources(self.write(self.minimal().replace('pixel_size: 16', 'pixel_size: ' + size)))

    def test_pcf_missing_glyphs_and_invalid_files_preserve_output(self):
        path = self.make_pcf()
        config = self.minimal().replace('font.ttf', 'font.pcf')
        paths, _ = gen.generate(self.write(config), self.output)
        snapshot = {p.name: p.read_bytes() for p in paths}
        spec = gen.FontSpec('tiny', path, None, {65}, {66, 0x1f600})
        warnings = []
        self.assertEqual(gen.rasterize(spec, warnings)[0], [65, 66])
        self.assertIn('U+1F600', warnings[0])
        spec.required.add(0x1f600)
        with self.assertRaisesRegex(gen.ResourceError, 'missing required glyphs'):
            gen.rasterize(spec, [])
        original = path.read_bytes()
        for data in (b'bad', original[:12], original[:-100]):
            path.write_bytes(data)
            with contextlib.redirect_stderr(io.StringIO()) as stream:
                self.assertEqual(gen.main([str(self.config), '-o', str(self.output)]), 1)
            self.assertIn('invalid PCF', stream.getvalue())
            self.assertEqual(snapshot, {p.name: p.read_bytes() for p in paths})
        self.make_pcf(registry=b'GB2312')
        with self.assertRaisesRegex(gen.ResourceError, 'ISO10646-1'):
            gen.generate(self.config, self.output)

    def test_duplicate_yaml_and_page_case(self):
        with self.assertRaisesRegex(gen.ResourceError, 'duplicate YAML key'):
            gen.load_resources(self.write(self.minimal('        title: "A"\n        title: "A"\n')))
        with self.assertRaisesRegex(gen.ResourceError, 'page names differ'):
            gen.load_resources(self.write(self.minimal() + '  HOME: []\n'))

    def test_merge_same_identity_and_union_fonts(self):
        text = self.minimal('        title: "中"\n        TITLE: "中"\n')
        text = text.replace('pages:', '  large:\n    source: font.ttf\n    pixel_size: 32\npages:')
        text += '    - font: large\n      texts:\n        Title: "中"\n'
        resources = gen.load_resources(self.write(text))
        self.assertEqual(resources.texts, {'UI_TEXT_HOME_TITLE': '中'})
        self.assertEqual(resources.fonts['tiny'].required, {0x4e2d})
        self.assertEqual(resources.fonts['large'].required, {0x4e2d})

    def test_conflicting_value_and_flattened_identity(self):
        with self.assertRaisesRegex(gen.ResourceError, 'conflicting text'):
            gen.load_resources(self.write(self.minimal('        title: "A"\n        TITLE: "B"\n')))
        text = self.minimal('        menu_title: "A"\n')
        text += '  home_menu:\n    - font: tiny\n      texts:\n        title: "A"\n'
        with self.assertRaisesRegex(gen.ResourceError, 'different identities'):
            gen.load_resources(self.write(text))

    def test_strict_types(self):
        for value in ('true', 'false', '123', 'null', '[]', '{}'):
            with self.subTest(value=value), self.assertRaisesRegex(gen.ResourceError, 'expected a string'):
                gen.load_resources(self.write(self.minimal(f'        title: {value}\n')))
        for extra in ('    extra_chars: [true]\n', '    extra_ranges: [{from: true, to: 65}]\n'):
            with self.assertRaises(gen.ResourceError):
                gen.load_resources(self.write(self.minimal(extra=extra)))
        # YAML 1.2-style key handling, no YAML 1.1 implicit on/off booleans.
        self.assertIn('UI_TEXT_HOME_ON', gen.load_resources(self.write(self.minimal('        on: "A"\n'))).texts)

    def test_ranges_chars_and_references(self):
        extra = ('    extra_ranges: [{from: 0x41, to: 0x42}]\n'
                 '    extra_chars: ["AA中", 0x1F600, "0x1234"]\n')
        resources = gen.load_resources(self.write(self.minimal(extra=extra)))
        self.assertEqual(resources.fonts['tiny'].required, set(map(ord, 'AB中😀0x1234')))
        self.assertEqual(resources.fonts['tiny'].source, self.root / 'font.ttf')
        for bounds in ('{from: 66, to: 65}', '{from: 0xD7FF, to: 0xE000}', '{from: 0, to: 0x110000}'):
            with self.assertRaises(gen.ResourceError):
                gen.load_resources(self.write(self.minimal(extra=f'    extra_ranges: [{bounds}]\n')))
        with self.assertRaisesRegex(gen.ResourceError, 'unknown font'):
            gen.load_resources(self.write(self.minimal().replace('font: tiny', 'font: TINY')))

    def test_unknown_fields_and_bpp_once(self):
        text = self.minimal(extra='    bpp: 2\n    foreign_option: {123: true}\n')
        text = text.replace('pages:', '  large:\n    source: font.ttf\n    pixel_size: 32\n    bpp: 1\npages:')
        resources = gen.load_resources(self.write(text))
        self.assertEqual(sum('bpp ignored' in warning for warning in resources.warnings), 1)
        self.assertTrue(any('foreign_option' in warning for warning in resources.warnings))

    def test_dictionary_literal_utf8_and_missing_policy(self):
        self.make_font()
        dictionary = self.root / 'dictionary.txt'
        dictionary.write_bytes('A\r\n中\nZ'.encode('utf-8'))
        self.write(self.minimal(extra='    extra_chars_files: [dictionary.txt]\n'))
        resources = gen.load_resources(self.config)
        self.assertEqual(resources.fonts['tiny'].dictionary, {65, 0x4e2d, 90})
        _, warnings = gen.generate(self.config, self.output)
        self.assertTrue(any('U+005A' in warning and 'skipped' in warning for warning in warnings))
        dictionary.write_text(r'\u4E2D', encoding='utf-8')
        self.assertEqual(gen.load_resources(self.config).fonts['tiny'].dictionary, set(map(ord, r'\u4E2D')))
        dictionary.write_bytes(b'\xef\xbb\xbfA')
        with self.assertRaisesRegex(gen.ResourceError, 'BOM'):
            gen.load_resources(self.config)
        dictionary.write_bytes(b'\xff')
        with self.assertRaisesRegex(gen.ResourceError, 'UTF-8'):
            gen.load_resources(self.config)

    def test_encoding_and_controls(self):
        self.assertEqual(gen.c_string('中A'), '"中A"')
        self.assertEqual(gen.c_string('A\n"\\'), '"A\\n\\"\\\\"')
        config = self.minimal('        title: "\\u6D4B\\u8BD5"\n        TITLE: "测试"\n')
        self.assertEqual(gen.load_resources(self.write(config)).texts['UI_TEXT_HOME_TITLE'], '测试')
        for value in ('"\\0"', '"\\uD800"'):
            with self.assertRaises(gen.ResourceError):
                gen.load_resources(self.write(self.minimal(f'        title: {value}\n')))

    def test_preserve_yaml_literal_and_escape_spelling(self):
        cases = [
            ('"测试"', '测试', '"测试"'),
            ('测试', '测试', '"测试"'),
            (r'"\u6D4B\u8BD5"', '测试', r'"\xE6\xB5\x8B\xE8\xAF\x95"'),
            (r'"中\u6D4BA"', '中测A', r'"中\xE6\xB5\x8B""A"'),
            (r'"中\U0001F600字"', '中😀字', r'"中\xF0\x9F\x98\x80字"'),
            (r'"\\u6D4B中"', r'\u6D4B中', r'"\\u6D4B中"'),
            (r"'\u6D4B中'", r'\u6D4B中', r'"\\u6D4B中"'),
            (r'"\u0041B"', 'AB', r'"\x41""B"'),
            (r'"中\n\u6D4B"', '中\n测', r'"中\n\xE6\xB5\x8B"'),
            ('"中\n          \\u6D4B"', '中 测', r'"中 \xE6\xB5\x8B"'),
        ]
        for raw, decoded, literal in cases:
            with self.subTest(raw=raw):
                resources = gen.load_resources(self.write(self.minimal(f'        title: {raw}\n')))
                self.assertEqual(resources.texts['UI_TEXT_HOME_TITLE'], decoded)
                self.assertEqual(gen.c_string(decoded, resources.text_escapes['UI_TEXT_HOME_TITLE']), literal)

    def test_duplicate_spellings_prefer_literal_independent_of_order(self):
        self.make_font()
        declarations = ['        title: "\\u4E2D"\n', '        TITLE: "中"\n']
        headers = []
        for entries in (declarations, declarations[::-1]):
            gen.generate(self.write(self.minimal(''.join(entries))), self.output)
            header = (self.output / 'ui_resources.h').read_text(encoding='utf-8')
            self.assertIn('#define UI_TEXT_HOME_TITLE "中"', header)
            headers.append(header)
        self.assertEqual(headers[0], headers[1])

    def test_determinism_dedup_and_preserve_existing_on_error(self):
        self.make_font()
        self.write(self.minimal('        title: "AB中"\n'))
        files, _ = gen.generate(self.config, self.output)
        snapshot = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in files}
        gen.generate(self.config, self.output)
        self.assertEqual(snapshot, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in files})
        generated = (self.output / 'ui_font_tiny.c').read_text(encoding='utf-8')
        spec = gen.load_resources(self.config).fonts['tiny']
        codepoints, glyphs, bitmap, _, _ = gen.rasterize(spec, [])
        self.assertEqual(codepoints, [ord('A'), ord('B'), ord('中')])
        self.assertEqual(len(glyphs), len(codepoints))
        self.assertEqual(glyphs[0], glyphs[1])  # Duplicate descriptions occupy separate indices.
        self.assertEqual(glyphs[1], glyphs[2])
        self.assertEqual(len(bitmap), glyphs[0][1] * ((glyphs[0][2] + 7) // 8))
        self.assertIn('static const uint32_t codepoints[]', generated)
        for obsolete in ('Font SHA256', 'Pillow', 'pixel_size=', 'ui_font_map_t'):
            self.assertNotIn(obsolete, generated)
        header = (self.output / 'ui_resources.h').read_text(encoding='utf-8')
        self.assertNotIn('#ifdef UI_TEXT_', header)
        self.assertNotIn('#error', header)
        self.assertIn('#include "ui/draw/text.h"', header)
        self.assertNotIn(str(self.root), generated)
        # Reordering text keys cannot change output or packing order.
        first = self.minimal('        a: "A"\n        b: "B"\n')
        self.write(first)
        gen.generate(self.config, self.output)
        ordered = {path.name: path.read_bytes() for path in files}
        self.write(self.minimal('        b: "B"\n        a: "A"\n'))
        gen.generate(self.config, self.output)
        self.assertEqual(ordered, {path.name: path.read_bytes() for path in files})
        self.write(self.minimal('        title: "Z"\n'))
        with self.assertRaisesRegex(gen.ResourceError, 'missing required glyphs'):
            gen.generate(self.config, self.output)
        self.assertEqual(ordered, {path.name: path.read_bytes() for path in files})

    def test_c_namespace_and_invalid_font_name(self):
        normalized = gen.load_resources(self.write(self.minimal().replace('namespace: ui', 'namespace: ß')))
        self.assertEqual(normalized.namespace, 'SS')
        for namespace in ('123', 'a-b', '_ui'):
            with self.assertRaises(gen.ResourceError):
                gen.load_resources(self.write(self.minimal().replace('namespace: ui', f'namespace: "{namespace}"')))
        with self.assertRaisesRegex(gen.ResourceError, 'font names'):
            gen.load_resources(self.write(self.minimal().replace('tiny:', 'Tiny:')))
        # ui_font_t is a runtime typedef and cannot also be a generated font variable.
        self.write(self.minimal().replace('tiny', 't'))
        with self.assertRaisesRegex(gen.ResourceError, 'C symbol collision'):
            gen.generate(self.config, self.output)

    def test_cli_error_exit_without_output(self):
        self.write(self.minimal('        title: true\n'))
        with contextlib.redirect_stderr(io.StringIO()) as stream:
            code = gen.main([str(self.config), '-o', str(self.output)])
        self.assertEqual(code, 1)
        self.assertIn('error:', stream.getvalue())
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
