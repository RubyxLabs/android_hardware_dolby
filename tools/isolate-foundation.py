#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Aeron-Aeron
# SPDX-License-Identifier: Apache-2.0
"""Reproduce the private Dolby foundation ABI without rebuilding native code.

Keep symbol indices, code addresses and Android packed relocations intact. Only
symbol names, foundation dependency names and ELF lookup metadata change. Append
read-only metadata and a new program-header table instead of moving old segments.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align(value, boundary):
    return (value + boundary - 1) & -boundary


class Elf:
    def __init__(self, data):
        self.data = bytearray(data)
        if data[:6] != b"\x7fELF\x02\x01":
            raise ValueError("Expected a little-endian ELF64 file")
        header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
        if header[2] != 183 or header[9] != 56 or header[11] != 64:
            raise ValueError("Expected standard AArch64 ELF headers")
        self.phoff, self.shoff = header[5:7]
        self.phdrs = [list(struct.unpack_from("<IIQQQQQQ", data, self.phoff + i * 56))
                      for i in range(header[10])]
        self.sections = [list(struct.unpack_from("<IIQQQQIIQQ", data, self.shoff + i * 64))
                         for i in range(header[12])]
        self.sym_index = next(i for i, section in enumerate(self.sections) if section[1] == 11)
        self.symtab = self.sections[self.sym_index]
        self.str_index = self.symtab[6]
        strings = self.sections[self.str_index]
        self.strings = bytes(data[strings[4]:strings[4] + strings[5]])
        self.symbols = [list(struct.unpack_from("<IBBHQQ", data, offset))
                        for offset in range(self.symtab[4], self.symtab[4] + self.symtab[5], 24)]
        self.names = [self.string(symbol[0]) for symbol in self.symbols]

    def string(self, offset):
        return self.strings[offset:self.strings.index(0, offset)].decode("utf-8")

    def exports(self):
        return {name for name, symbol in zip(self.names, self.symbols)
                if symbol[3] != 0 and symbol[1] >> 4 in (1, 2)}

    @staticmethod
    def gnu_hash(name):
        result = 5381
        for byte in name.encode():
            result = (result * 33 + byte) & 0xffffffff
        return result

    def isolate(self, mapping, soname=None):
        strings = bytearray(self.strings)
        offsets = {}

        def intern(name):
            if name not in offsets:
                offsets[name] = len(strings)
                strings.extend(name.encode() + b"\0")
            return offsets[name]

        names = [mapping.get(name, name) for name in self.names]
        for i, (old, new) in enumerate(zip(self.names, names)):
            if old != new:
                struct.pack_into("<I", self.data, self.symtab[4] + i * 24, intern(new))

        dynamic = next(section for section in self.sections if section[1] == 6)
        entries = []
        for offset in range(dynamic[4], dynamic[4] + dynamic[5], 16):
            tag, value = struct.unpack_from("<QQ", self.data, offset)
            if tag == 0:
                break
            if tag == 1 and self.string(value) in (
                    "libstagefright_foundation.so", "libstagefright_foundation-v33.so"):
                value = intern("libstagefright_foundation_dolby.so")
            if tag == 14 and soname is not None:
                value = intern(soname)
            entries.append([offset, tag, value])
        if soname is not None and not any(tag == 14 for _, tag, _ in entries):
            raise ValueError("Foundation must already have a SONAME")

        # One GNU bucket preserves symbol ordering, so packed relocation symbol
        # indices and .gnu.version need no changes. Recompute the bloom filter.
        hashes = {}
        for i, section in enumerate(self.sections):
            if section[1] == 0x6ffffff6:  # SHT_GNU_HASH
                _, first, _, shift = struct.unpack_from("<IIII", self.data, section[4])
                if names[first:] == self.names[first:]:
                    continue
                values = [self.gnu_hash(name) for name in names[first:]]
                bloom = 0
                for value in values:
                    bloom |= (1 << (value % 64)) | (1 << ((value >> shift) % 64))
                chain = [value & ~1 for value in values]
                if chain:
                    chain[-1] |= 1
                hashes[i] = (struct.pack("<IIIIQI", 1, first, 1, shift, bloom,
                                         first if chain else 0)
                             + b"".join(struct.pack("<I", value) for value in chain))
            elif section[1] == 5:  # SHT_HASH
                if names == self.names:
                    continue
                count = len(names)
                chain = [0] + list(range(2, count)) + ([0] if count > 1 else [])
                hashes[i] = (struct.pack("<III", 1, count, 1 if count > 1 else 0)
                             + b"".join(struct.pack("<I", value) for value in chain))

        # Put PHDR first and keep the original PT_LOAD mappings in place. This
        # also handles inputs whose string table was relocated by an importer.
        headers = [header[:] for header in self.phdrs if header[0] != 6]
        count = len(headers) + 2  # PT_PHDR and the appended PT_LOAD
        loads = [header for header in headers if header[0] == 1]
        page = max(4096, *(header[7] for header in loads))
        file_offset = align(len(self.data), page)
        address = align(max(header[3] + header[6] for header in loads), page)
        payload = bytearray(count * 56)
        locations = {}

        def append(section_index, contents):
            offset = align(len(payload), 8)
            payload.extend(b"\0" * (offset - len(payload)))
            locations[section_index] = address + offset
            section = self.sections[section_index]
            section[3:6] = [address + offset, file_offset + offset, len(contents)]
            payload.extend(contents)

        append(self.str_index, strings)
        for section_index, contents in hashes.items():
            append(section_index, contents)
        headers.insert(0, [6, 4, file_offset, address, address, count * 56, count * 56, 8])
        headers.append([1, 4, file_offset, address, address, len(payload), len(payload), page])
        for i, header in enumerate(headers):
            struct.pack_into("<IIQQQQQQ", payload, i * 56, *header)

        pointers = {5: locations[self.str_index], 10: len(strings)}  # STRTAB, STRSZ
        for i in hashes:
            pointers[0x6ffffef5 if self.sections[i][1] == 0x6ffffff6 else 4] = locations[i]
        for offset, tag, value in entries:
            struct.pack_into("<QQ", self.data, offset, tag, pointers.get(tag, value))
        for i, section in enumerate(self.sections):
            struct.pack_into("<IIQQQQIIQQ", self.data, self.shoff + i * 64, *section)
        struct.pack_into("<Q", self.data, 32, file_offset)  # e_phoff
        struct.pack_into("<H", self.data, 56, count)  # e_phnum
        self.data.extend(b"\0" * (file_offset - len(self.data)))
        self.data.extend(payload)
        return bytes(self.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    tree = Path(__file__).resolve().parents[1]
    parser.add_argument("--android-root", type=Path, default=tree.parents[1])
    args = parser.parse_args()
    manifest = json.loads((tree / "proprietary/provenance.json").read_text())
    compat = manifest["foundation_compat"]
    source = (args.android_root / compat["source"]).read_bytes()
    if digest(source) != compat["source_sha256"]:
        raise ValueError("Unexpected v33 foundation input; refusing to patch")
    exports = Elf(source).exports()
    mapping = {name: compat["symbol_prefix"] + name for name in sorted(exports)}
    map_text = "".join(f"{old} {new}\n" for old, new in mapping.items()).encode()
    if len(exports) != compat["symbol_count"] or digest(map_text) != compat["symbol_map_sha256"]:
        raise ValueError("Foundation export map does not match the pinned recipe")

    pending = {}
    for item in manifest["files"]:
        path = tree / item["path"]
        data = path.read_bytes()
        if digest(data) == item["sha256"]:
            continue
        if "upstream_sha256" not in item or digest(data) != item["upstream_sha256"]:
            raise ValueError(f"Unexpected input: {path}")
        patched = Elf(data).isolate(mapping)
        if digest(patched) != item["sha256"]:
            raise ValueError(f"Output differs from the pinned recipe: {path}")
        pending[path] = patched

    private = tree / compat["path"]
    if private.exists() and digest(private.read_bytes()) != compat["sha256"]:
        raise ValueError("Unexpected private foundation; refusing to overwrite it")
    if not private.exists():
        patched = Elf(source).isolate(mapping, private.name)
        if digest(patched) != compat["sha256"]:
            raise ValueError("Private foundation differs from the pinned recipe")
        pending[private] = patched

    # Validate every input/output before replacing any file in the tree.
    with tempfile.TemporaryDirectory(dir=tree) as temporary:
        for i, (path, data) in enumerate(pending.items()):
            staged = Path(temporary) / str(i)
            staged.write_bytes(data)
            staged.chmod(path.stat().st_mode if path.exists() else 0o644)
        for i, path in enumerate(pending):
            (Path(temporary) / str(i)).replace(path)
    print(f"Foundation isolation verified; updated {len(pending)} files")


if __name__ == "__main__":
    main()
