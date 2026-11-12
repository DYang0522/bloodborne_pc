"""Tests for the Linux launcher's Russian/English/Simplified Chinese UI."""
import os
import sys
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "launcher"))
import bbport_i18n as i18n


class LinuxLauncherLanguageTests(unittest.TestCase):
    def test_simplified_chinese_is_a_supported_choice(self):
        i18n.set_language("zh")
        self.assertEqual(i18n.language(), "zh")
        self.assertEqual(i18n.tr("Запустить"), i18n.ZH["Play"])
        self.assertNotEqual(i18n.tr("Запустить"), "Play")

    def test_chinese_system_locale_is_detected(self):
        with mock.patch.dict(os.environ, {
            "LC_ALL": "", "LC_MESSAGES": "", "LANG": "zh_CN.UTF-8", "LANGUAGE": ""
        }, clear=False):
            i18n.set_language("")
            self.assertEqual(i18n.language(), "zh")

    def test_english_and_russian_remain_available(self):
        i18n.set_language("en")
        self.assertEqual(i18n.tr("Запустить"), "Play")
        i18n.set_language("ru")
        self.assertEqual(i18n.tr("Запустить"), "Запустить")


if __name__ == "__main__":
    unittest.main()
