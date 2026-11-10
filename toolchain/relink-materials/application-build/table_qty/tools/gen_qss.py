#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 GJG 原生 skin/style.ini 生成 Qt 样式表 (QSS)。

用法:
    python tools/gen_qss.py <style.ini> <输出目录>

说明:
    style.ini 是厂商资源包里的主题配色表, 只读取 key=value 配色值,
    不含任何二进制资源; 生成的两份 QSS 是本工程独立编写的 Qt 样式表。
    QSS 中的 __ASSETS__ 由运行期替换为 assets 目录绝对路径。
"""

import os
import sys
from string import Template


TEMPLATE = Template(r"""/* GJG 表格算量 — 主题: $theme_label
   由 tools/gen_qss.py 依据原生 skin/style.ini 配色生成, 请勿手工编辑 */

* { outline: 0; }

QWidget {
    color: $fg_normal;
    font-family: "Microsoft YaHei UI", "Microsoft YaHei", "SimHei", sans-serif;
    font-size: 9pt;
}

QMainWindow, QDialog, QMessageBox, QFileDialog { background: $bg_window; }
QFrame#headerBar { background: $bg_toolbar; border-bottom: 1px solid $border; }

/* ---------- 工具栏 ---------- */
QToolBar {
    background: $bg_toolbar;
    border: none;
    border-bottom: 1px solid $border;
    spacing: 2px;
    padding: 3px 4px;
}
QToolBar::separator { background: $border; width: 1px; margin: 4px 4px; }

QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 3px 7px;
    color: $fg_normal;
    min-height: 20px;
}
QToolButton:hover { background: $hover_bg; border-color: $hover_border; }
QToolButton:pressed { background: $pressed_bg; border-color: $accent; }
QToolButton:checked { background: $pressed_bg; border-color: $accent; }
QToolButton:disabled { color: $ro_fg; }
QToolButton::menu-indicator { image: none; }

/* ---------- 表格 / 树 ---------- */
QTreeView, QTableView, QListView {
    background: $table_bg;
    alternate-background-color: $alt_bg;
    color: $fg_normal;
    border: 1px solid $grid_edge;
    gridline-color: $grid_line;
    selection-background-color: $sel_bg;
    selection-color: $sel_fg;
}
QTreeView::item { padding: 2px 3px; border: 0; }
QTreeView::item:hover { background: $hover_bg; }
QTreeView::item:selected { background: $sel_bg; color: $sel_fg; }
QTableView::item { padding: 2px 5px; }
QTableView::item:selected { background: $sel_bg; color: $sel_fg; border: 1px solid $cell_sel_border; }

QHeaderView { background: $header_bg; border: none; }
QHeaderView::section {
    background: $header_bg;
    color: $header_fg;
    border: none;
    border-right: 1px solid $header_border;
    border-bottom: 1px solid $header_border;
    padding: 3px 6px;
    font-weight: normal;
}
QHeaderView::section:hover { background: $header_sel_bg; color: $header_sel_fg; }
QHeaderView::section:checked { background: $header_sel_bg; color: $header_sel_fg; }
QTableCornerButton::section { background: $header_bg; border: none; }

/* ---------- 选项卡 ---------- */
QTabWidget::pane { border: 1px solid $border; top: -1px; background: $table_bg; }
QTabBar::tab {
    background: $tab_bg;
    color: $fg_normal;
    border: 1px solid $border;
    border-bottom: none;
    padding: 5px 15px;
    margin-right: 2px;
    border-top-left-radius: 3px;
    border-top-right-radius: 3px;
}
QTabBar::tab:hover:!selected { background: $hover_bg; }
QTabBar::tab:selected {
    background: $tab_sel_bg;
    color: $tab_sel_fg;
    border-top: 2px solid $accent;
}

/* ---------- 输入控件 ---------- */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateTimeEdit,
QTextEdit, QPlainTextEdit {
    background: $edit_bg;
    color: $edit_fg;
    border: 1px solid $edit_border;
    border-radius: 2px;
    padding: 2px 5px;
    min-height: 19px;
    selection-background-color: $sel_bg;
    selection-color: $sel_fg;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QDateTimeEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: $accent; }
QLineEdit:read-only, QLineEdit:disabled { color: $ro_fg; background: $table_ro_bg; }

QComboBox::drop-down { border: none; width: 18px; }
QComboBox::down-arrow {
    image: url(__ASSETS__/icons/$edir/icon/ArrowsDown.svg);
    width: 9px; height: 9px;
}
QComboBox QAbstractItemView {
    background: $combo_bg;
    color: $combo_fg;
    border: 1px solid $combo_border;
    selection-background-color: $combo_sel_bg;
    selection-color: $combo_sel_fg;
    outline: 0;
}
QComboBox QAbstractItemView::item { min-height: 20px; padding: 2px 6px; }

QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    background: $btn_bg;
    border: none;
    border-left: 1px solid $edit_border;
    width: 14px;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: $hover_bg; }

QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; }
QCheckBox::indicator:unchecked { image: url(__ASSETS__/icons/$edir/icon/checkBox-Uncheck.svg); }
QCheckBox::indicator:checked { image: url(__ASSETS__/icons/$edir/icon/checkBox-check.svg); }

/* ---------- 按钮 ---------- */
QPushButton {
    background: $btn_bg;
    color: $edit_fg;
    border: 1px solid $btn_border;
    border-radius: 3px;
    padding: 4px 18px;
    min-width: 62px;
    min-height: 20px;
}
QPushButton:hover { background: $hover_bg; border-color: $accent; }
QPushButton:pressed { background: $pressed_bg; }
QPushButton:default { border-color: $accent; }
QPushButton:disabled { color: $ro_fg; border-color: $border; }

/* ---------- 状态栏 / 分割条 ---------- */
QStatusBar { background: $status_bg; color: $fg_normal; border-top: 1px solid $border; }
QStatusBar::item { border: none; }
QStatusBar QLabel { padding-left: 6px; }

QSplitter::handle { background: $border; }
QSplitter::handle:horizontal { width: 3px; }
QSplitter::handle:vertical { height: 3px; }
QSplitter::handle:hover { background: $accent; }

/* ---------- 滚动条 ---------- */
QScrollBar:vertical { background: $scroll_bg; width: 12px; margin: 0; border: none; }
QScrollBar::handle:vertical {
    background: $scroll_handle; min-height: 26px;
    border-radius: 5px; margin: 2px 3px;
}
QScrollBar::handle:vertical:hover { background: $scroll_handle_hover; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: $scroll_bg; height: 12px; margin: 0; border: none; }
QScrollBar::handle:horizontal {
    background: $scroll_handle; min-width: 26px;
    border-radius: 5px; margin: 3px 2px;
}
QScrollBar::handle:horizontal:hover { background: $scroll_handle_hover; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; border: none; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ---------- 菜单 / 提示 ---------- */
QMenu { background: $menu_bg; color: $menu_fg; border: 1px solid $border; padding: 3px; }
QMenu::item { padding: 5px 24px 5px 24px; border-radius: 2px; }
QMenu::item:selected { background: $menu_sel_bg; color: $menu_sel_fg; }
QMenu::separator { height: 1px; background: $border; margin: 4px 8px; }
QMenu::icon { padding-left: 6px; }

QToolTip {
    background: $menu_bg; color: $menu_fg;
    border: 1px solid $border; padding: 3px 6px;
}

/* ---------- 分组框 / 其它 ---------- */
QGroupBox {
    border: 1px solid $border;
    border-radius: 3px;
    margin-top: 12px;
    padding-top: 8px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 9px;
    padding: 0 4px;
    color: $fg_normal;
}
QLabel { background: transparent; color: $fg_normal; }
QScrollArea { background: $bg_window; border: none; }
QDialogButtonBox QPushButton { min-width: 74px; }
QPrintPreviewDialog QToolBar { background: $bg_toolbar; border-bottom: 1px solid $border; }
""")


def load_ini(path):
    """读取厂商 style.ini（ASCII 键值，注释为 GBK 中文）。"""
    data = {}
    cur = ""
    with open(path, "rb") as fh:
        raw = fh.read()
    text = raw.decode("gbk", errors="ignore")
    for line in text.splitlines():
        s = line.strip()
        if not s or s[0] in ";#":
            continue
        if s.startswith("[") and s.endswith("]"):
            cur = s[1:-1].strip()
            data.setdefault(cur, {})
            continue
        if "=" not in s:
            continue
        k, v = s.split("=", 1)
        data.setdefault(cur, {})[k.strip()] = v.strip()
    return data


def g(sec, key, fallback):
    v = sec.get(key)
    return v if v else fallback


def build_silverywhite(ini):
    s = ini.get("SilveryWhite", {})
    return {
        "theme_label": "SilveryWhite 银白",
        "edir": "light",
        "fg_normal": g(s, "Content/Foreground/Normal", "#333333"),
        "bg_window": "#FFFFFF",
        "bg_toolbar": g(s, "Header/Background/Normal", "#F0F0F0"),
        "border": g(s, "HeaderBorder/Background/Normal", "#D7D7D7"),
        "header_bg": g(s, "Header/Background/Normal", "#F0F0F0"),
        "header_fg": g(s, "Header/Foreground/Normal", "#333333"),
        "header_border": g(s, "HeaderBorder/Background/Normal", "#D7D7D7"),
        "header_sel_bg": g(s, "Header/Background/Selected", "#7FBCF9"),
        "header_sel_fg": g(s, "Header/Foreground/Selected", "#FFFFFF"),
        "table_bg": g(s, "Content/Background/Normal", "#FFFFFF"),
        "table_ro_bg": g(s, "Content/Background/ReadOnly", "#EBEBEB"),
        "alt_bg": "#F7F9FB",
        "grid_line": g(s, "GridLine", "#D7D7D7"),
        "grid_edge": g(s, "GridEdge", "#C5C5C5"),
        "sel_bg": g(s, "Content/Background/Selected", "#D6EBFF"),
        "sel_fg": g(s, "Content/Foreground/Normal", "#333333"),
        "cell_sel_border": g(s, "ContentBorder/Background/Selected", "#6DABE8"),
        "ro_fg": g(s, "Content/Foreground/ReadOnly", "#898989"),
        "edit_bg": g(s, "LineEdit/Background", "#FFFFFF"),
        "edit_fg": g(s, "LineEdit/Foreground", "#000000"),
        "edit_border": g(s, "LineEdit/BorderColor", "#668DDA"),
        "combo_bg": g(s, "ComboBox/Background", "#FFFFFF"),
        "combo_fg": g(s, "ComboBox/Foreground", "#000000"),
        "combo_border": g(s, "ComboBoxItemBorder", "#6DABE8"),
        "combo_sel_bg": g(s, "ComboBoxItem/Background/Selected", "#E2F0FF"),
        "combo_sel_fg": g(s, "ComboBoxItem/Foreground/Selected", "#000000"),
        "tab_bg": g(s, "Header/Background/Normal", "#F0F0F0"),
        "tab_sel_bg": "#FFFFFF",
        "tab_sel_fg": g(s, "Content/Foreground/Normal", "#333333"),
        "accent": g(s, "DrawBoderColor/Selected", "#4C84DD"),
        "hover_bg": g(s, "ComboBoxItem/Background/Selected", "#E2F0FF"),
        "hover_border": g(s, "Header/Background/Selected", "#7FBCF9"),
        "pressed_bg": g(s, "Content/Background/Selected", "#D6EBFF"),
        "btn_bg": "#F5F6F7",
        "btn_border": g(s, "DrawBoderColor/Normal", "#C0C0C0"),
        "status_bg": g(s, "Header/Background/Normal", "#F0F0F0"),
        "scroll_bg": "#F5F5F5",
        "scroll_handle": g(s, "GridEdge", "#C5C5C5"),
        "scroll_handle_hover": "#A8A8A8",
        "menu_bg": "#FFFFFF",
        "menu_fg": g(s, "Content/Foreground/Normal", "#333333"),
        "menu_sel_bg": g(s, "ComboBoxItem/Background/Selected", "#E2F0FF"),
        "menu_sel_fg": g(s, "ComboBoxItem/Foreground/Selected", "#000000"),
    }


def build_cooldark(ini):
    s = ini.get("CoolDark", {})
    return {
        "theme_label": "CoolDark 深色",
        "edir": "dark",
        "fg_normal": g(s, "Content/Foreground/Normal", "#d5d5d5"),
        "bg_window": g(s, "DrawBackgroundColor/Normal", "#363738"),
        "bg_toolbar": "#3E3F40",
        "border": g(s, "DrawBoderColor/Normal", "#48494A"),
        "header_bg": g(s, "Header/Background/Normal", "#666666"),
        "header_fg": g(s, "Header/Foreground/Normal", "#E5E5E5"),
        "header_border": g(s, "HeaderBorder/Background/Normal", "#525252"),
        "header_sel_bg": g(s, "Header/Background/Selected", "#4A4A4A"),
        "header_sel_fg": g(s, "Header/Foreground/Selected", "#FFFFFF"),
        "table_bg": g(s, "Content/Background/Normal", "#242526"),
        "table_ro_bg": g(s, "Content/Background/ReadOnly", "#242526"),
        "alt_bg": "#2A2B2C",
        "grid_line": g(s, "GridLine", "#525252"),
        "grid_edge": g(s, "GridEdge", "#333333"),
        "sel_bg": g(s, "Content/Background/Selected", "#61676e"),
        "sel_fg": "#FFFFFF",
        "cell_sel_border": g(s, "ContentBorder/Background/Selected", "#047bc9"),
        "ro_fg": g(s, "Content/Foreground/ReadOnly", "#999999"),
        "edit_bg": g(s, "LineEdit/Background", "#454647"),
        "edit_fg": g(s, "LineEdit/Foreground", "#d5d5d5"),
        "edit_border": g(s, "LineEdit/BorderColor", "#047BC9"),
        "combo_bg": g(s, "ComboBox/Background", "#5C5C5C"),
        "combo_fg": g(s, "ComboBox/Foreground", "#E5E5E5"),
        "combo_border": g(s, "ComboBoxItemBorder", "#2A2A2A"),
        "combo_sel_bg": g(s, "ComboBoxItem/Background/Selected", "#627EA8"),
        "combo_sel_fg": g(s, "ComboBoxItem/Foreground/Selected", "#E5E5E5"),
        "tab_bg": "#3E3F40",
        "tab_sel_bg": g(s, "Content/Background/Normal", "#242526"),
        "tab_sel_fg": "#FFFFFF",
        "accent": g(s, "ContentBorder/Background/Selected", "#047BC9"),
        "hover_bg": g(s, "DrawBackgroundColor/Selected", "#1F395C"),
        "hover_border": g(s, "DrawBoderColor/Selected", "#4C84DD"),
        "pressed_bg": g(s, "Content/Background/Selected", "#61676e"),
        "btn_bg": g(s, "LineEdit/Background", "#454647"),
        "btn_border": g(s, "DrawBoderColor/Normal", "#48494A"),
        "status_bg": "#2E2F30",
        "scroll_bg": "#2E2F30",
        "scroll_handle": "#5A5B5C",
        "scroll_handle_hover": "#757677",
        "menu_bg": "#3E3F40",
        "menu_fg": g(s, "Content/Foreground/Normal", "#d5d5d5"),
        "menu_sel_bg": g(s, "ComboBoxItem/Background/Selected", "#627EA8"),
        "menu_sel_fg": "#FFFFFF",
    }


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    ini_path, out_dir = sys.argv[1], sys.argv[2]
    ini = load_ini(ini_path)
    if "SilveryWhite" not in ini and "CoolDark" not in ini:
        print("ERROR: style.ini 中没有找到主题节")
        return 1
    os.makedirs(out_dir, exist_ok=True)
    jobs = [("silverywhite.qss", build_silverywhite(ini)),
            ("cooldark.qss", build_cooldark(ini))]
    for fname, tokens in jobs:
        text = TEMPLATE.substitute(tokens)
        path = os.path.join(out_dir, fname)
        with open(path, "w", encoding="utf-8", newline="\r\n") as fh:
            fh.write(text)
        print("%-18s %6d bytes  %2d rules-keys" % (fname, len(text.encode("utf-8")), len(tokens)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
