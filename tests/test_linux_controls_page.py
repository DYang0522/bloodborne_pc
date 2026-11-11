"""The Linux launcher exposes input remapping and passes real SDL window modes."""
from paths import ROOT
import unittest

LAUNCHER = (ROOT / "launcher/bbport_launcher.py").read_text(encoding="utf-8")
CONTROLS = (ROOT / "launcher/bbport_controls.py").read_text(encoding="utf-8")
WINDOW = (ROOT / "gpu/shim/window.cpp").read_text(encoding="utf-8")


class LinuxControlsPageTests(unittest.TestCase):
    def test_linux_launcher_has_a_controls_tab_and_persists_both_input_sources(self):
        self.assertIn("build_controls_page()", LAUNCHER)
        self.assertIn("ControlsMixin", LAUNCHER)
        self.assertIn("input_config.save_input_ini", CONTROLS)
        self.assertIn("input_config.save_controller_bindings", CONTROLS)

    def test_mouse_look_can_be_enabled_and_mapped(self):
        self.assertIn("mouse_enabled_row", CONTROLS)
        self.assertIn("save_mouse_settings", CONTROLS)
        self.assertIn("hotkey_toggle_mouse_to_joystick", (ROOT / "src/runtime_input_config.c").read_text(encoding="utf-8"))

    def test_window_mode_is_passed_to_sdl(self):
        self.assertIn('env["BB_WINDOW_MODE"]', LAUNCHER)
        self.assertIn('SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN', WINDOW)
        self.assertIn('strcmp(mode, "fullscreen")', WINDOW)
        self.assertIn('strcmp(mode, "borderless")', WINDOW)


if __name__ == "__main__":
    unittest.main()
