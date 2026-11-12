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

    def test_controls_page_labels_are_simplified_chinese(self):
        import bbport_input_config as inputs
        i18n.set_language("zh")
        for group, actions in inputs.ACTION_GROUPS:
            self.assertNotEqual(i18n.translate_control_name(group[0]), group[0])
            for _output, labels in actions:
                self.assertNotEqual(i18n.translate_control_name(labels[0]), labels[0])

        self.assertEqual(i18n.translate_control_name("Left click"), "鼠标左键")
        self.assertEqual(i18n.translate_control_name("Right Shift"), "右 Shift")


if __name__ == "__main__":
    unittest.main()
