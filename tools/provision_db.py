#!/usr/bin/env python3
"""Generate the encrypted credential database consumed by the PIC firmware."""

from __future__ import annotations

import argparse
import ast
import os
import re
import struct
from pathlib import Path
from typing import Mapping

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


SLOT_COUNT = 12
MAX_PASSWORD_LENGTH = 63
HEADER_SIZE = 16
RECORD_SIZE = 64
CRC_SIZE = 4
MAGIC = b"ADPK"
VERSION = 1
PLAINTEXT_SIZE = HEADER_SIZE + SLOT_COUNT * RECORD_SIZE + CRC_SIZE

_DEFINE_RE = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*$")
_STRING_RE = re.compile(r'^"(?:[^"\\]|\\.)*"$')
_SLOT_RE = re.compile(r"^AD00020_PASSWORD_SLOT_(0[1-9]|1[0-2])$")
_MASTER_KEY_MACROS = tuple(f"AD00020_MASTER_KEY_{index:02d}" for index in range(1, 5))
_ALLOWED_MASTER_KEY_VALUES = {
    "01FE7887",
    "01FE807F",
    "01FE40BF",
    "01FEC03F",
    "01FE20DF",
    "01FEA05F",
    "01FE609F",
    "01FEE01F",
    "01FE10EF",
    "01FE906F",
    "01FE50AF",
    "01FED827",
    "01FEF807",
    "01FE30CF",
    "01FEB04F",
    "01FE708F",
}


class ProvisionError(ValueError):
    """Raised when local credential input is invalid."""


def parse_defines(path: Path) -> dict[str, str]:
    defines: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = _DEFINE_RE.match(line.split("//", 1)[0].strip())
        if match:
            name, value = match.groups()
            defines[name] = value.strip()
    return defines


def resolve_string(name: str, defines: Mapping[str, str], trail: tuple[str, ...] = ()) -> str:
    if name in trail:
        raise ProvisionError("credential macro cycle detected")
    try:
        value = defines[name]
    except KeyError as exc:
        raise ProvisionError(f"missing credential macro: {name}") from exc
    if _STRING_RE.fullmatch(value):
        parsed = ast.literal_eval(value)
        if not isinstance(parsed, str):
            raise ProvisionError(f"credential macro is not a string: {name}")
        return parsed
    if value in defines:
        return resolve_string(value, defines, trail + (name,))
    raise ProvisionError(f"credential macro must be a string or alias: {name}")


def load_passwords(path: Path) -> list[str]:
    defines = parse_defines(path)
    try:
        count = int(defines["AD00020_PASSWORD_SLOT_COUNT"], 10)
    except (KeyError, ValueError) as exc:
        raise ProvisionError("AD00020_PASSWORD_SLOT_COUNT must be an integer") from exc
    if not 0 <= count <= SLOT_COUNT:
        raise ProvisionError("AD00020_PASSWORD_SLOT_COUNT must be between 0 and 12")

    passwords: list[str] = []
    for index in range(1, SLOT_COUNT + 1):
        name = f"AD00020_PASSWORD_SLOT_{index:02d}"
        if index > count:
            passwords.append("")
            continue
        password = resolve_string(name, defines)
        encoded = password.encode("ascii")
        if len(encoded) > MAX_PASSWORD_LENGTH:
            raise ProvisionError(f"password slot {index:02d} exceeds 63 bytes")
        if any(byte < 0x20 or byte > 0x7E for byte in encoded):
            raise ProvisionError(f"password slot {index:02d} contains non-printable ASCII")
        passwords.append(password)
    return passwords


def parse_master_key(value: str) -> bytes:
    parts = [part for part in re.split(r"[,:;\s]+", value.strip()) if part]
    if len(parts) != 4 or any(not re.fullmatch(r"[0-9A-Fa-f]{8}", part) for part in parts):
        raise ProvisionError("master key must contain four 8-hex-digit NEC values")
    return b"".join(bytes.fromhex(part) for part in parts)


def load_master_key(path: Path) -> bytes:
    defines = parse_defines(path)
    values: list[str] = []
    for name in _MASTER_KEY_MACROS:
        value = resolve_string(name, defines)
        normalized = value.upper()
        if not re.fullmatch(r"[0-9A-F]{8}", normalized):
            raise ProvisionError(f"master key macro is not an 8-hex-digit NEC value: {name}")
        if normalized not in _ALLOWED_MASTER_KEY_VALUES:
            raise ProvisionError(f"master key macro is not an allowed NEC command: {name}")
        values.append(normalized)
    return parse_master_key(" ".join(values))


def build_plaintext(passwords: list[str]) -> bytes:
    if len(passwords) != SLOT_COUNT:
        raise ProvisionError("internal error: wrong slot count")
    header = MAGIC + bytes((VERSION, SLOT_COUNT, MAX_PASSWORD_LENGTH, sum(bool(p) for p in passwords)))
    header += bytes(HEADER_SIZE - len(header))
    records = bytearray()
    for password in passwords:
        encoded = password.encode("ascii")
        records.append(len(encoded))
        records.extend(encoded)
        records.extend(bytes(MAX_PASSWORD_LENGTH - len(encoded)))
    body = header + bytes(records)
    crc = struct.pack("<I", crc32(body))
    plaintext = body + crc
    if len(plaintext) != PLAINTEXT_SIZE:
        raise ProvisionError("internal error: wrong database size")
    return plaintext


def crc32(data: bytes) -> int:
    value = 0xFFFFFFFF
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xEDB88320 if value & 1 else 0)
    return value ^ 0xFFFFFFFF


def encrypt_database(passwords: list[str], master_key: bytes, iv: bytes | None = None) -> tuple[bytes, bytes, bytes]:
    if len(master_key) != 16:
        raise ProvisionError("AES-128 key must be 16 bytes")
    iv = os.urandom(16) if iv is None else iv
    if len(iv) != 16:
        raise ProvisionError("CTR IV must be 16 bytes")
    plaintext = build_plaintext(passwords)
    encryptor = Cipher(algorithms.AES(master_key), modes.CTR(iv)).encryptor()
    ciphertext = encryptor.update(plaintext) + encryptor.finalize()
    return iv, ciphertext, plaintext


def c_array(name: str, data: bytes) -> str:
    values = ", ".join(f"0x{byte:02X}" for byte in data)
    return f"static const unsigned char {name}[{len(data)}] = {{{values}}};"


def render_header(iv: bytes, ciphertext: bytes) -> str:
    return """/* Generated by tools/provision_db.py. Do not edit or commit. */
#ifndef AD00020_GENERATED_DATABASE_H
#define AD00020_GENERATED_DATABASE_H

#define ADPK_DB_VERSION 1
#define ADPK_DB_SLOT_COUNT 12
#define ADPK_DB_MAX_PASSWORD_LENGTH 63
#define ADPK_DB_HEADER_SIZE 16
#define ADPK_DB_RECORD_SIZE 64
#define ADPK_DB_CRC_SIZE 4
#define ADPK_DB_PLAINTEXT_SIZE 788

{iv}
{ciphertext}

#endif /* AD00020_GENERATED_DATABASE_H */
""".format(
        iv=c_array("adpk_db_iv", iv),
        ciphertext=c_array("adpk_db_ciphertext", ciphertext),
    )


def write_database(credentials: Path, output: Path, master_key: bytes) -> None:
    passwords = load_passwords(credentials)
    iv, ciphertext, _ = encrypt_database(passwords, master_key)
    output.parent.mkdir(parents=True, exist_ok=True)
    old_umask = os.umask(0o077)
    try:
        temporary = output.with_suffix(output.suffix + ".tmp")
        temporary.write_text(render_header(iv, ciphertext), encoding="ascii")
        os.replace(temporary, output)
    finally:
        os.umask(old_umask)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--credentials", type=Path, default=Path("include/credentials.h"))
    parser.add_argument("--output", type=Path, default=Path("include/generated_database.h"))
    parser.add_argument(
        "--master-key-header",
        type=Path,
        default=Path("include/master_key.h"),
        help="local header containing the four NEC master-key values",
    )
    args = parser.parse_args()
    try:
        master_key = load_master_key(args.master_key_header)
        write_database(args.credentials, args.output, master_key)
    except (OSError, ProvisionError) as exc:
        parser.error(str(exc))
    print(f"generated encrypted database: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
