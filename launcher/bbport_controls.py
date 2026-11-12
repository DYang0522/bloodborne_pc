# SPDX-License-Identifier: GPL-2.0-or-later
"""Linux GTK controls page for keyboard, mouse and gamepad mappings."""
import bbport_input_config as input_config
from bbport_i18n import language, tr, translate_control_name

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
gi.require_version("Gdk", "4.0")
from gi.repository import Adw, Gdk, Gtk  # noqa: E402


def _combo_row(title, choices, current):
    row = Adw.ComboRow(title=title, model=Gtk.StringList.new([tr(label) for label, _ in choices]))
    row.values = [value for _label, value in choices]
    row.set_selected(row.values.index(current) if current in row.values else 0)
    return row


def _shown_binding(value, russian):
    if not value:
        return "—"
    return " + ".join(translate_control_name(input_config.display_name(part, russian))
                      for part in input_config.binding_parts(value))


class ControlsMixin:
    """Linux launcher's shared input.ini editor."""

    def init_controls(self, bbport_ini_path):
        self.input_path = input_config.input_ini_path(bbport_ini_path)
        (self.input_bindings, self.input_lines, self.input_toggle_key,
         self.input_reload_key, self.mouse_settings) = input_config.load_input_ini(self.input_path)
        self.controller_bindings = input_config.load_controller_bindings(self.input_lines)
        self.capture_target = None
        self.capture_modifiers = []

        self._capture_keys = Gtk.EventControllerKey.new()
        self._capture_keys.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)
        self._capture_keys.connect("key-pressed", self._capture_key_pressed)
        self.add_controller(self._capture_keys)

        self._capture_click = Gtk.GestureClick.new()
        self._capture_click.set_button(0)
        self._capture_click.set_propagation_phase(Gtk.PropagationPhase.BUBBLE)
        self._capture_click.connect("pressed", self._capture_mouse_pressed)
        self.add_controller(self._capture_click)

        self._capture_scroll = Gtk.EventControllerScroll.new(Gtk.EventControllerScrollFlags.BOTH_AXES)
        self._capture_scroll.set_propagation_phase(Gtk.PropagationPhase.BUBBLE)
        self._capture_scroll.connect("scroll", self._capture_mouse_scroll)
        self.add_controller(self._capture_scroll)

    def build_controls_page(self):
        page = Adw.PreferencesPage()
        russian = language() == "ru"

        info = Adw.PreferencesGroup(
            title=tr("Клавиатура, мышь и геймпад"),
            description=tr("Источники управления работают одновременно. Настройки сохраняются при запуске игры."))
        info.add(Adw.ActionRow(
            title=tr("Горячие клавиши"),
            subtitle=f"F7 — {tr('включить или выключить захват мыши')}; "
                     f"F8 — {tr('перезагрузить input.ini')}"))
        page.add(info)

        mouse = Adw.PreferencesGroup(
            title=tr("Управление мышью"),
            description=tr("Курсор захватывается в игре, когда режим мыши включён, окно активно и меню закрыто."))
        self.mouse_enabled_row = Adw.SwitchRow(
            title=tr("Включить управление камерой мышью"),
            subtitle=tr("После запуска можно временно переключать режим клавишей F7"),
            active=self.mouse_settings["enabled"])
        mouse.add(self.mouse_enabled_row)
        self.mouse_stick_row = _combo_row(
            tr("Какой стик управляет мышью"),
            [("Правый стик (камера)", "right"), ("Левый стик", "left")],
            self.mouse_settings["stick"])
        mouse.add(self.mouse_stick_row)
        self.mouse_camera_row = _combo_row(
            tr("Режим камеры"),
            [("Эмуляция стика", "stick"), ("Прямое управление камерой (1:1)", "direct")],
            self.mouse_settings["camera"])
        mouse.add(self.mouse_camera_row)
        self.mouse_speed_spin = self._add_spin(
            mouse, tr("Чувствительность мыши"), 0.01, 20.0, 0.05, 2, self.mouse_settings["speed"])
        self.mouse_offset_spin = self._add_spin(
            mouse, tr("Дополнительная чувствительность"), 0.0, 1.0, 0.01, 3,
            self.mouse_settings["speed_offset"])
        self.mouse_direct_sensitivity_spin = self._add_spin(
            mouse, tr("Чувствительность прямой камеры"), 0.05, 20.0, 0.05, 2,
            self.mouse_settings["camera_sensitivity"])
        self.mouse_vertical_sensitivity_spin = self._add_spin(
            mouse, tr("Вертикальная чувствительность"), 0.1, 10.0, 0.05, 2,
            self.mouse_settings["sensitivity_y"])
        self.mouse_invert_x_row = Adw.SwitchRow(
            title=tr("Инвертировать горизонталь"), active=self.mouse_settings["invert_x"])
        self.mouse_invert_y_row = Adw.SwitchRow(
            title=tr("Инвертировать вертикаль"), active=self.mouse_settings["invert_y"])
        mouse.add(self.mouse_invert_x_row)
        mouse.add(self.mouse_invert_y_row)
        page.add(mouse)

        self.input_slot_buttons = {}
        for group_title, actions in input_config.ACTION_GROUPS:
            group = Adw.PreferencesGroup(title=tr(group_title[1]))
            for output, labels in actions:
                row = Adw.ActionRow(title=tr(labels[1]))
                values = self.input_bindings.get(output, [])
                for slot in range(input_config.SLOTS):
                    value = values[slot] if slot < len(values) else None
                    button = Gtk.Button(label=_shown_binding(value, russian), valign=Gtk.Align.CENTER)
                    button.set_size_request(88, -1)
                    button.set_tooltip_text(
                        tr("Нажмите и введите клавишу, кнопку мыши или прокрутите колесо"))
                    button.connect("clicked", self._begin_binding_capture, output, slot)
                    row.add_suffix(button)
                    self.input_slot_buttons[(output, slot)] = button
                clear = Gtk.Button(icon_name="edit-clear-symbolic", valign=Gtk.Align.CENTER,
                                   tooltip_text=tr("Очистить назначения этой команды"))
                clear.add_css_class("flat")
                clear.connect("clicked", self._clear_keyboard_binding, output)
                row.add_suffix(clear)
                group.add(row)
            page.add(group)

        controller_choices = [("Не назначено", "")]
        controller_choices.extend(
            (translate_control_name(input_config.controller_display_name(name, russian)), name)
            for name in input_config.CONTROLLER_NAMES)
        action_labels = {
            output: labels for _group, actions in input_config.ACTION_GROUPS
            for output, labels in actions
        }
        gamepad = Adw.PreferencesGroup(
            title=tr("Назначения геймпада"),
            description=tr("Выберите физическую кнопку для каждого действия. Стики и триггеры остаются аналоговыми."))
        self.controller_rows = {}
        for output in input_config.CONTROLLER_OUTPUTS:
            labels = action_labels.get(output, input_config.OUTPUT_LABELS.get(output, (output, output)))
            row = _combo_row(tr(labels[1]), controller_choices,
                             self.controller_bindings.get(output) or "")
            gamepad.add(row)
            self.controller_rows[output] = row
        page.add(gamepad)

        status_group = Adw.PreferencesGroup()
        self.capture_status_row = Adw.ActionRow(
            title=tr("Переназначение кнопок"),
            subtitle=tr("Нажмите один из слотов, затем клавишу, кнопку мыши или прокрутите колесо. Shift/Ctrl/Alt можно сочетать."))
        self.capture_cancel = Gtk.Button(label=tr("Отменить переназначение"),
                                         valign=Gtk.Align.CENTER)
        self.capture_cancel.add_css_class("flat")
        self.capture_cancel.set_visible(False)
        self.capture_cancel.connect("clicked", self._cancel_binding_capture)
        self.capture_status_row.add_suffix(self.capture_cancel)
        status_group.add(self.capture_status_row)
        page.add(status_group)
        return page

    @staticmethod
    def _add_spin(group, title, lower, upper, step, digits, value):
        row = Adw.ActionRow(title=title)
        spin = Gtk.SpinButton.new_with_range(lower, upper, step)
        spin.set_digits(digits)
        spin.set_value(float(value))
        row.add_suffix(spin)
        group.add(row)
        return spin

    def _begin_binding_capture(self, button, output, slot):
        if self.process is not None:
            self.toasts.add_toast(Adw.Toast(title=tr("Сначала остановите игру, чтобы изменить управление")))
            return
        if self.capture_target is not None:
            old = self.capture_target["button"]
            old.set_label(self._binding_button_label(self.capture_target["output"],
                                                     self.capture_target["slot"]))
        self.capture_target = {"button": button, "output": output, "slot": slot}
        self.capture_modifiers = []
        button.set_label(tr("Нажмите ввод…"))
        self.capture_cancel.set_visible(True)
        self.capture_status_row.set_subtitle(
            tr("Ожидание клавиши, кнопки мыши или прокрутки колеса…"))

    def _binding_button_label(self, output, slot):
        values = self.input_bindings.get(output, [])
        value = values[slot] if slot < len(values) else None
        return _shown_binding(value, language() == "ru")

    def _cancel_binding_capture(self, _button=None):
        target = self.capture_target
        self.capture_target = None
        self.capture_modifiers = []
        self.capture_cancel.set_visible(False)
        if target:
            target["button"].set_label(self._binding_button_label(
                target["output"], target["slot"]))
        self.capture_status_row.set_subtitle(
            tr("Нажмите один из слотов, затем клавишу, кнопку мыши или прокрутите колесо. Shift/Ctrl/Alt можно сочетать."))

    def _capture_key_pressed(self, _controller, keyval, _keycode, _state):
        if self.capture_target is None:
            return False
        if getattr(self, "stack", None) and self.stack.get_visible_child_name() != "controls":
            return False
        keysym = Gdk.keyval_name(keyval) or ""
        name = input_config.keysym_to_name(keysym)
        if not name:
            self.capture_status_row.set_subtitle(tr("Эта клавиша не поддерживается; нажмите другую"))
            return True
        if name in input_config.MODIFIER_NAMES:
            if name not in self.capture_modifiers:
                self.capture_modifiers.append(name)
            shown = " + ".join(input_config.display_name(part, language() == "ru")
                               for part in self.capture_modifiers)
            self.capture_target["button"].set_label(shown + " + …")
            return True
        self._finish_binding_capture(", ".join(self.capture_modifiers + [name]))
        return True

    def _capture_mouse_pressed(self, gesture, _n_press, _x, _y):
        if self.capture_target is None:
            return
        if getattr(self, "stack", None) and self.stack.get_visible_child_name() != "controls":
            return
        name = input_config.button_num_to_name(gesture.get_current_button())
        if not name:
            self.capture_status_row.set_subtitle(tr("Эта кнопка мыши не поддерживается"))
            return
        self._finish_binding_capture(", ".join(self.capture_modifiers + [name]))
        gesture.set_state(Gtk.EventSequenceState.CLAIMED)

    def _capture_mouse_scroll(self, _controller, dx, dy):
        if self.capture_target is None:
            return False
        if getattr(self, "stack", None) and self.stack.get_visible_child_name() != "controls":
            return False
        name = (input_config.wheel_to_name(-dy) if abs(dy) >= abs(dx)
                else input_config.wheel_to_name(-dx, horizontal=True))
        if not name:
            return False
        self._finish_binding_capture(", ".join(self.capture_modifiers + [name]))
        return True

    def _finish_binding_capture(self, value):
        target = self.capture_target
        if target is None:
            return
        output, slot = target["output"], target["slot"]
        parts = input_config.binding_parts(value)
        if any(part in input_config.reserved_keys(self.input_toggle_key, self.input_reload_key)
               for part in parts):
            self.capture_status_row.set_subtitle(
                tr("Эта клавиша зарезервирована системным меню или горячей клавишей"))
            return
        if not input_config.is_bindable(output, value):
            self.capture_status_row.set_subtitle(tr("Такое сочетание нельзя назначить для этого действия"))
            return
        values = list(self.input_bindings.get(output, []))
        while len(values) <= slot:
            values.append(None)
        values[slot] = value
        self.input_bindings[output] = [part for part in values if part][:input_config.SLOTS]
        self.capture_target = None
        self.capture_modifiers = []
        self.capture_cancel.set_visible(False)
        self.capture_status_row.set_subtitle(
            tr("Назначение изменено; настройки сохраняются при запуске игры"))
        self._refresh_binding_buttons(output)
        self.store_controls()

    def _refresh_binding_buttons(self, output):
        for slot in range(input_config.SLOTS):
            button = self.input_slot_buttons.get((output, slot))
            if button:
                button.set_label(self._binding_button_label(output, slot))

    def _clear_keyboard_binding(self, _button, output):
        self._cancel_binding_capture()
        self.input_bindings[output] = []
        self._refresh_binding_buttons(output)
        self.store_controls()

    def store_controls(self):
        """Write keyboard/mouse, mouse-look and controller settings to the runtime input.ini."""
        if not hasattr(self, "input_path"):
            return
        try:
            self.mouse_settings.update({
                "enabled": self.mouse_enabled_row.get_active(),
                "stick": self.mouse_stick_row.values[self.mouse_stick_row.get_selected()],
                "camera": self.mouse_camera_row.values[self.mouse_camera_row.get_selected()],
                "speed": self.mouse_speed_spin.get_value(),
                "speed_offset": self.mouse_offset_spin.get_value(),
                "camera_sensitivity": self.mouse_direct_sensitivity_spin.get_value(),
                "sensitivity_y": self.mouse_vertical_sensitivity_spin.get_value(),
                "invert_x": self.mouse_invert_x_row.get_active(),
                "invert_y": self.mouse_invert_y_row.get_active(),
            })
            controller = {
                output: (row.values[row.get_selected()] or None)
                for output, row in self.controller_rows.items()
            }
            self.input_path.parent.mkdir(parents=True, exist_ok=True)
            input_config.save_input_ini(self.input_path, self.input_bindings, self.input_lines)
            _bindings, lines, _toggle, _reload, _mouse = input_config.load_input_ini(self.input_path)
            input_config.save_mouse_settings(self.input_path, self.mouse_settings, lines)
            _bindings, lines, _toggle, _reload, _mouse = input_config.load_input_ini(self.input_path)
            input_config.save_controller_bindings(self.input_path, controller, lines)
            (self.input_bindings, self.input_lines, self.input_toggle_key,
             self.input_reload_key, self.mouse_settings) = input_config.load_input_ini(self.input_path)
            self.controller_bindings = input_config.load_controller_bindings(self.input_lines)
        except (OSError, ValueError) as error:
            self.toasts.add_toast(Adw.Toast(title=tr("Не удалось сохранить управление: {}").format(error)))
