import sys
import tempfile
import unittest
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
import provision_db  # noqa: E402


class ProvisionDatabaseTests(unittest.TestCase):
    def test_current_four_slot_header_resolves_without_printing_values(self):
        passwords = provision_db.load_passwords(
            Path(__file__).parents[1] / "include" / "credentials.h.example"
        )
        self.assertEqual(len(passwords), 12)
        self.assertEqual(passwords[0], "REPLACE_ME")
        self.assertEqual(passwords[3], "REPLACE_ME")
        self.assertEqual(passwords[4:], [""] * 8)

    def test_database_round_trip_and_wrong_key(self):
        passwords = ["alpha", "b3ta!", "", "delta"] + [""] * 8
        key = bytes.fromhex("01FE48B701FEA05F01FE609F01FEE01F")
        iv = bytes.fromhex("00112233445566778899AABBCCDDEEFF")
        actual_iv, ciphertext, plaintext = provision_db.encrypt_database(passwords, key, iv)
        self.assertEqual(actual_iv, iv)
        self.assertNotIn(b"alpha", ciphertext)
        self.assertNotIn(b"b3ta!", ciphertext)

        decryptor = Cipher(algorithms.AES(key), modes.CTR(iv)).decryptor()
        self.assertEqual(decryptor.update(ciphertext) + decryptor.finalize(), plaintext)

        wrong = bytes.fromhex("01FE48B701FEA05F01FE609F01FEE01E")
        decryptor = Cipher(algorithms.AES(wrong), modes.CTR(iv)).decryptor()
        wrong_plaintext = decryptor.update(ciphertext) + decryptor.finalize()
        self.assertNotEqual(wrong_plaintext[:4], provision_db.MAGIC)

    def test_master_key_parser_accepts_variable_nec_values(self):
        self.assertEqual(
            provision_db.parse_master_key("A55A12ED 10EF55AA"),
            bytes.fromhex("A55A12ED10EF55AA"),
        )

        self.assertEqual(provision_db.parse_master_key(""), b"")

    def test_master_key_kdf_vectors_include_empty_and_variable_lengths(self):
        vectors = (
            ("", "102fa05ceae6677e136d8bd7c42aa72d"),
            ("01FE807F", "e1ef2f29c3429715b7586240a4b20904"),
            ("A55A12ED 10EF55AA", "640cd42085e7d95ee48a607273b160ec"),
            ("A55A12ED " * 32, "84a2e27517dca0f30089f9a92dca199d"),
        )
        for sequence, expected in vectors:
            frames = provision_db.parse_master_key(sequence)
            self.assertEqual(provision_db.derive_master_key(frames).hex(), expected)

    def test_master_key_parser_rejects_invalid_and_reserved_frames(self):
        for value in ("01FE807E", "01FE7887", "01FE48B7", "01FE58A7", "1234"):
            with self.subTest(value=value):
                with self.assertRaises(provision_db.ProvisionError):
                    provision_db.parse_master_key(value)

    def test_master_key_parser_enforces_maximum(self):
        maximum = " ".join(["A55A12ED"] * 32)
        self.assertEqual(len(provision_db.parse_master_key(maximum)), 128)
        with self.assertRaises(provision_db.ProvisionError):
            provision_db.parse_master_key(" ".join(["A55A12ED"] * 33))

    def test_master_key_header_loads_explicit_arbitrary_sequence(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "master_key.h"
            path.write_text(
                '#define AD00020_MASTER_KEY_COUNT 4\n'
                '#define AD00020_MASTER_KEY_01 "01FE807F"\n'
                '#define AD00020_MASTER_KEY_02 "01FE40BF"\n'
                '#define AD00020_MASTER_KEY_03 "A55A12ED"\n'
                '#define AD00020_MASTER_KEY_04 "10EF55AA"\n',
                encoding="ascii",
            )
            self.assertEqual(
                provision_db.load_master_key(path),
                provision_db.derive_master_key(
                    bytes.fromhex("01FE807F01FE40BFA55A12ED10EF55AA")
                ),
            )

            path.write_text(
                '#define AD00020_MASTER_KEY_COUNT 0\n',
                encoding="ascii",
            )
            self.assertEqual(
                provision_db.load_master_key(path),
                provision_db.derive_master_key(b""),
            )

            path.write_text(
                '#define AD00020_MASTER_KEY_COUNT 33\n',
                encoding="ascii",
            )
            with self.assertRaises(provision_db.ProvisionError):
                provision_db.load_master_key(path)

    def test_master_key_header_requires_only_the_declared_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "master_key.h"
            path.write_text(
                '#define AD00020_MASTER_KEY_COUNT 2\n'
                '#define AD00020_MASTER_KEY_01 "A55A12ED"\n'
                '#define AD00020_MASTER_KEY_02 "10EF55AA"\n',
                encoding="ascii",
            )
            self.assertEqual(
                provision_db.load_master_key(path),
                provision_db.derive_master_key(bytes.fromhex("A55A12ED10EF55AA")),
            )

    def test_legacy_four_frame_header_is_migrated(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "master_key.h"
            path.write_text(
                '#define AD00020_MASTER_KEY_01 "01FE807F"\n'
                '#define AD00020_MASTER_KEY_02 "01FE40BF"\n'
                '#define AD00020_MASTER_KEY_03 "A55A12ED"\n'
                '#define AD00020_MASTER_KEY_04 "10EF55AA"\n',
                encoding="ascii",
            )
            self.assertEqual(
                provision_db.load_master_key(path),
                provision_db.derive_master_key(
                    bytes.fromhex("01FE807F01FE40BFA55A12ED10EF55AA")
                ),
            )

    def test_generated_header_contains_no_plaintext(self):
        passwords = ["alpha", "b3ta!", "", "delta"] + [""] * 8
        key = bytes.fromhex("01FE48B701FEA05F01FE609F01FEE01F")
        iv, ciphertext, _ = provision_db.encrypt_database(passwords, key, bytes(16))
        rendered = provision_db.render_header(iv, ciphertext).encode("ascii")
        self.assertNotIn(b"alpha", rendered)
        self.assertNotIn(b"b3ta!", rendered)


if __name__ == "__main__":
    unittest.main()
