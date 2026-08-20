#!/usr/bin/env python3

import io
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from symbolize_log import RvaIndex, parse_dump, symbolize_lines


SAMPLE_DUMP = """
// Namespace:
public class GameConnect : MonoBehaviour
{
    // RVA: 0x118F728 Offset: 0x118F728 VA: 0x118F728
    public static void Disconnect() { }
}

// Namespace:
public static class PhotonNetwork // TypeDefIndex: 4860
{
    // RVA: 0xC78060 Offset: 0xC78060 VA: 0xC78060
    public static void Disconnect() { }

    // RVA: 0xC8002C Offset: 0xC8002C VA: 0xC8002C
    public static bool ConnectUsingSettings(string gameVersion) { }
}
"""


class SymbolizeLogTest(unittest.TestCase):
    def setUp(self) -> None:
        self.index = RvaIndex(parse_dump(io.StringIO(SAMPLE_DUMP)))

    def test_resolves_nearest_method(self) -> None:
        self.assertEqual(
            self.index.resolve(0x118F750, 0x1000),
            "GameConnect.Disconnect+0x28",
        )
        self.assertEqual(
            self.index.resolve(0xC80040, 0x1000),
            "PhotonNetwork.ConnectUsingSettings+0x14",
        )

    def test_respects_maximum_offset(self) -> None:
        self.assertIsNone(self.index.resolve(0x118F750, 0x10))

    def test_annotates_only_libil2cpp_callsite(self) -> None:
        source = [
            "[#000001 +000001ms tid=7 pc=libil2cpp.so+0x118f750] disconnect\n",
            "[#000002 +000002ms tid=7 pc=libopg3d.so+0x1234] helper\n",
        ]
        rendered = "".join(symbolize_lines(source, self.index, 0x1000))
        self.assertIn("[managed=GameConnect.Disconnect+0x28]", rendered)
        self.assertNotIn("helper [managed=", rendered)

    def test_does_not_duplicate_annotation(self) -> None:
        line = (
            "pc=libil2cpp.so+0x118f750 disconnect "
            "[managed=GameConnect.Disconnect+0x28]\n"
        )
        self.assertEqual("".join(symbolize_lines([line], self.index, 0x1000)), line)


if __name__ == "__main__":
    unittest.main()
