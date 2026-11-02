"""Inspect the actual PE imports; static MSVC flags alone are not evidence."""
import argparse
import hashlib
import json
import pathlib
import struct


def inspect(path):
    data = pathlib.Path(path).read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("Missing DOS header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Missing PE header")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    if machine != 0x8664 or struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise ValueError("Expected AMD64 PE32+")
    sections = []
    for i in range(count):
        virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data, optional + optional_size + 40 * i + 8)
        sections.append((rva, max(virtual_size, raw_size), raw))

    def offset(rva):
        for start, size, raw in sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise ValueError("Unmapped RVA")

    import_rva = struct.unpack_from("<I", data, optional + 120)[0]
    imports = []
    if import_rva:
        pos = offset(import_rva)
        for _ in range(1024):
            fields = struct.unpack_from("<IIIII", data, pos)
            if not any(fields):
                break
            name = offset(fields[3])
            end = data.index(b"\0", name, name + 260)
            imports.append(data[name:end].decode("ascii"))
            pos += 20
        else:
            raise ValueError("Import table exceeds bound")
    forbidden = ("vcruntime", "msvcp", "msvcrt", "ucrtbase", "api-ms-win-crt", "libc++", "libunwind", "libwinpthread")
    unsupported = [name for name in imports if name.lower().startswith(forbidden)]
    return {"schema_version": 1, "sha256": hashlib.sha256(data).hexdigest(), "architecture": "x64",
            "imports": imports, "dynamic_c_runtime": unsupported, "static_runtime_check_passed": not unsupported,
            "scope": "pe_imports_only"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--output")
    args = parser.parse_args()
    result = inspect(args.executable)
    content = json.dumps(result, indent=2)
    if args.output:
        pathlib.Path(args.output).write_text(content + "\n", encoding="utf-8")
    print(content)
    raise SystemExit(0 if result["static_runtime_check_passed"] else 1)
