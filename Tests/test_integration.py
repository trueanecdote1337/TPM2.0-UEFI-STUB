"""Regression checks for reversible OVMF integration; no real checkout is modified."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('vtpm_integrate', Path(__file__).resolve().parents[1] / 'scripts/integrate.py')
integrate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(integrate)

DSC = ('[LibraryClasses.common.DXE_SMM_DRIVER]\n'
       '  BaseCryptLib|CryptoPkg/Library/BaseCryptLib/SmmCryptLib.inf\n'
       '[Components]\n!if $(SMM_REQUIRE) == TRUE\n'
       '  OvmfPkg/SmmControl2Dxe/SmmControl2Dxe.inf\n'
       '  ' + integrate.CPU_ORIGINAL + '\n!endif\n')
FDF = '!if $(SMM_REQUIRE) == TRUE\nINF  OvmfPkg/SmmControl2Dxe/SmmControl2Dxe.inf\n!endif\n'


class IntegrationTests(unittest.TestCase):
    def test_roundtrip_and_idempotence(self):
        for kind, original in [('dsc', DSC), ('fdf', FDF)]:
            installed = integrate.transform(original, kind)
            self.assertEqual(integrate.transform(installed, kind), installed)
            self.assertEqual(integrate.transform(installed, kind, True), original)
            self.assertEqual(integrate.transform(original, kind, True), original)

    def test_unknown_or_duplicate_anchors_are_rejected(self):
        with self.assertRaises(ValueError):
            integrate.transform(DSC + '  ' + integrate.CPU_ORIGINAL, 'dsc')
        with self.assertRaises(ValueError):
            integrate.transform(DSC.replace('SmmCryptLib.inf', 'Unknown.inf'), 'dsc')
        with self.assertRaises(ValueError):
            integrate.transform(DSC + '# MyTpm2EMU/custom.inf\n', 'dsc')

    def fixture(self, directory):
        root = Path(directory)
        source = root / 'project'; source.mkdir()
        edk2 = root / 'edk2'; (edk2 / 'OvmfPkg').mkdir(parents=True)
        (edk2 / 'edksetup.sh').touch()
        for extension, text in [('dsc', DSC), ('fdf', FDF)]:
            (edk2 / ('OvmfPkg/OvmfPkgX64.' + extension)).write_bytes(text.replace('\n', '\r\n').encode())
        return source, edk2

    def cli(self, source, edk2, *arguments):
        with patch.object(integrate, 'ROOT', source), \
             patch.object(integrate.subprocess, 'run', return_value=SimpleNamespace(stdout=integrate.EDK2_REVISION)), \
             patch('sys.argv', ['integrate.py', '--edk2', str(edk2)] + list(arguments)):
            integrate.main()

    def test_cli_preserves_crlf_and_unrelated_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            source, edk2 = self.fixture(directory)
            self.cli(source, edk2, '--dry-run')
            self.assertFalse((edk2 / 'MyTpm2EMU').exists())
            self.cli(source, edk2)
            self.assertEqual((edk2 / 'MyTpm2EMU').resolve(), source)
            path = edk2 / 'OvmfPkg/OvmfPkgX64.dsc'
            self.assertNotIn(b'\n', path.read_bytes().replace(b'\r\n', b''))
            path.write_bytes(path.read_bytes() + b'# user setting\r\n')
            self.cli(source, edk2, '--remove')
            self.assertEqual(path.read_bytes(), DSC.replace('\n', '\r\n').encode() + b'# user setting\r\n')
            self.assertFalse((edk2 / 'MyTpm2EMU').is_symlink())

    def test_second_file_failure_rolls_back_first_and_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            source, edk2 = self.fixture(directory)
            before = (edk2 / 'OvmfPkg/OvmfPkgX64.dsc').read_bytes()
            original_write = integrate.atomic_write
            calls = []
            def fail_once(path, data):
                calls.append(path)
                if len(calls) == 2:
                    raise OSError('simulated second-file failure')
                original_write(path, data)
            with patch.object(integrate, 'atomic_write', side_effect=fail_once):
                with self.assertRaises(OSError):
                    self.cli(source, edk2)
            self.assertEqual((edk2 / 'OvmfPkg/OvmfPkgX64.dsc').read_bytes(), before)
            self.assertFalse((edk2 / 'MyTpm2EMU').is_symlink())


if __name__ == '__main__':
    unittest.main()
