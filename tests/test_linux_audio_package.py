"""AppImage includes its own PipeWire ALSA config and warns about missing user sessions."""
from paths import ROOT
import unittest

NIX = (ROOT / "packaging/default.nix").read_text(encoding="utf-8")
APPRUN = (ROOT / "packaging/linux/AppRun.sh").read_text(encoding="utf-8")


class LinuxAudioPackageTests(unittest.TestCase):
    def test_pipewire_modules_and_alsa_config_are_bundled(self):
        self.assertIn("pkgs.pipewire", NIX)
        self.assertIn("libasound_module_pcm_pipewire.so", NIX)
        self.assertIn("49-pipewire-modules.conf", NIX)
        self.assertIn("50-pipewire.conf", NIX)
        self.assertIn("99-pipewire-default.conf", NIX)
        self.assertIn("pkgs.alsa-lib", NIX)
        self.assertIn("ALSA_CONFIG_PATH", APPRUN)
        self.assertIn("ALSA_PLUGIN_DIR", APPRUN)
        self.assertIn('cp -L "$plugin" "$d/lib/alsa-lib/"', NIX)
        self.assertNotIn("cp -aL ${pkgs.pipewire}/lib/alsa-lib", NIX)
        self.assertNotIn("cp -aL ${pkgs.alsa-plugins}/lib/alsa-lib", NIX)

    def test_appimage_explains_missing_desktop_audio_session(self):
        self.assertIn("XDG_RUNTIME_DIR", APPRUN)
        self.assertIn("launch without sudo", APPRUN)


if __name__ == "__main__":
    unittest.main()
