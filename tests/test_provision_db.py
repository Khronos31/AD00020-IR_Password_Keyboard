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

    def test_master_key_requires_four_nec_values(self):
        self.assertEqual(
            provision_db.parse_master_key("01FE48B7 01FEA05F 01FE609F 01FEE01F"),
            bytes.fromhex("01FE48B701FEA05F01FE609F01FEE01F"),
        )
        with self.assertRaises(provision_db.ProvisionError):
            provision_db.parse_master_key("01FE48B7 01FEA05F")

    def test_master_key_header_loads_only_allowed_buttons(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "master_key.h"
            path.write_text(
                '#define AD00020_MASTER_KEY_01 "01FE7887"\n'
                '#define AD00020_MASTER_KEY_02 "01FE807F"\n'
                '#define AD00020_MASTER_KEY_03 "01FE40BF"\n'
                '#define AD00020_MASTER_KEY_04 "01FEC03F"\n',
                encoding="ascii",
            )
            self.assertEqual(
                provision_db.load_master_key(path),
                bytes.fromhex("01FE788701FE807F01FE40BF01FEC03F"),
            )

            path.write_text(
                '#define AD00020_MASTER_KEY_01 "01FE48B7"\n'
                '#define AD00020_MASTER_KEY_02 "01FE807F"\n'
                '#define AD00020_MASTER_KEY_03 "01FE40BF"\n'
                '#define AD00020_MASTER_KEY_04 "01FEC03F"\n',
                encoding="ascii",
            )
            with self.assertRaises(provision_db.ProvisionError):
                provision_db.load_master_key(path)

    def test_generated_header_contains_no_plaintext(self):
        passwords = ["alpha", "b3ta!", "", "delta"] + [""] * 8
        key = bytes.fromhex("01FE48B701FEA05F01FE609F01FEE01F")
        iv, ciphertext, _ = provision_db.encrypt_database(passwords, key, bytes(16))
        rendered = provision_db.render_header(iv, ciphertext).encode("ascii")
        self.assertNotIn(b"alpha", rendered)
        self.assertNotIn(b"b3ta!", rendered)


if __name__ == "__main__":
    unittest.main()
