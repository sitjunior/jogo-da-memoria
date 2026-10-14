#!/usr/bin/env python3
"""Confere se um .nro do Switch traz ícone, metadados (NACP) e RomFS embutidos.

Uso: python3 tools/check_nro.py jogo-da-memoria.nro
"""
import struct
import sys


def main(path):
    data = open(path, "rb").read()
    print(f"{path}: {len(data)} bytes")
    if len(data) < 0x70 or data[0x10:0x14] != b"NRO0":
        print("ERRO: não é um .nro válido (faltou a assinatura NRO0)")
        return 1
    size = struct.unpack_from("<I", data, 0x18)[0]
    print(f"Executável (NRO0): {size} bytes")
    if len(data) < size + 0x38 or data[size:size + 4] != b"ASET":
        print("PROBLEMA: não há seção de assets (ASET). O .nro não tem ícone, nome nem RomFS.")
        print("Compile de novo com `make clean && make` e confira se icon.jpg existe.")
        return 2
    (_ver, io, isz, no, nsz, ro, rsz) = struct.unpack_from("<IQQQQQQ", data, size + 4)
    print("ASET encontrado:")
    print(f"  ícone : {isz} bytes" + ("" if isz else "  <-- FALTA"))
    print(f"  NACP  : {nsz} bytes" + ("" if nsz else "  <-- FALTA"))
    print(f"  RomFS : {rsz} bytes" + ("" if rsz else "  <-- FALTA (jogo ficará sem imagens)"))
    if isz:
        icon = data[size + io: size + io + isz]
        print(f"  ícone é JPEG: {icon[:2] == b'\xff\xd8'}")
    if nsz:
        nacp = data[size + no: size + no + nsz]
        name = nacp[0:0x200].split(b"\0")[0].decode("utf-8", "replace")
        author = nacp[0x200:0x300].split(b"\0")[0].decode("utf-8", "replace")
        version = nacp[0x3060:0x3070].split(b"\0")[0].decode("utf-8", "replace")
        print(f"  nome  : {name!r}\n  autor : {author!r}\n  versão: {version!r}")
    return 0 if isz and nsz and rsz else 2


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
