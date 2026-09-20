#pragma once

#include <QString>

namespace brick2::theme {

inline const char* styleSheet()
{
    return R"css(
* { font-family: "Segoe UI", "Inter", "Helvetica Neue", sans-serif; }
QMainWindow, QDialog {
  background: #0b0d10;
  color: #c8d0da;
}
QWidget { background: transparent; color: #c8d0da; font-size: 12px; }
QMenuBar {
  background: #0b0d10;
  color: #9aa3ad;
  border-bottom: 1px solid #1e2530;
  padding: 2px 8px;
}
QMenuBar::item { padding: 6px 10px; background: transparent; }
QMenuBar::item:selected { background: #1a2230; color: #e8eef5; border-radius: 4px; }
QMenu {
  background: #141a22;
  color: #e8eef5;
  border: 1px solid #2a3340;
}
QMenu::item:selected { background: #243044; }
QStatusBar {
  background: #08090c;
  color: #7d8793;
  border-top: 1px solid #1e2530;
}
QStatusBar QLabel { color: #8b95a1; font-size: 11px; }

QFrame#panel {
  background: #12161c;
  border: 1px solid #242c36;
  border-radius: 8px;
}
QFrame#bezel {
  background: #07080a;
  border: 1px solid #2a3340;
  border-radius: 8px;
}
QFrame#header {
  background: #10141a;
  border: 1px solid #242c36;
  border-radius: 8px;
}

QLabel#brand {
  color: #e8eef5;
  font-size: 15px;
  font-weight: 700;
  letter-spacing: 1.4px;
}
QLabel#brandSub {
  color: #6f7b88;
  font-size: 10px;
  letter-spacing: 1.6px;
}
QLabel#section {
  color: #6f7b88;
  font-size: 9px;
  font-weight: 700;
  letter-spacing: 1.4px;
}
QLabel#value {
  color: #d5dde6;
  font-size: 11px;
}
QLabel#bwReadout {
  color: #7ee6e6;
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0.4px;
  padding: 2px 8px;
}

QPushButton {
  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2a3140, stop:1 #1a1f28);
  color: #d5dde6;
  border: 1px solid #323b48;
  border-radius: 4px;
  padding: 5px 8px;
  min-height: 24px;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.4px;
}
QPushButton:hover {
  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #343c4e, stop:1 #222836);
  border-color: #4a5668;
}
QPushButton:pressed { background: #151920; }
QPushButton:checked {
  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1f5a4a, stop:1 #12362c);
  color: #b6f3d8;
  border: 1px solid #3dd68c;
}
QPushButton[role="tx"]:checked, QPushButton[tx="true"]:checked {
  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8a2222, stop:1 #4a1010);
  color: #ffd4d4;
  border: 1px solid #e23d3d;
}
QPushButton[role="power"] {
  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2a6a8a, stop:1 #16384a);
  color: #d7f4ff;
  border: 1px solid #3ec8c8;
  min-width: 84px;
  font-weight: 700;
  letter-spacing: 1px;
}
QPushButton[role="power"]:hover { border-color: #7ee6e6; }
QPushButton[role="ghost"] {
  background: transparent;
  border: 1px solid #323b48;
  color: #9aa3ad;
}

QSlider::groove:horizontal {
  height: 3px;
  background: #2a3340;
  border-radius: 2px;
}
QSlider::sub-page:horizontal {
  background: #3ec8c8;
  border-radius: 2px;
}
QSlider::handle:horizontal {
  width: 12px;
  height: 12px;
  margin: -6px 0;
  background: #e8eef5;
  border-radius: 6px;
  border: 1px solid #3ec8c8;
}
QSlider::groove:vertical {
  width: 4px;
  background: #2a3340;
  border-radius: 2px;
}
QSlider::sub-page:vertical { background: #3ec8c8; border-radius: 2px; }
QSlider::handle:vertical {
  height: 12px; width: 12px; margin: 0 -5px;
  background: #e8eef5; border-radius: 6px; border: 1px solid #3ec8c8;
}

QSpinBox#filterHz {
  min-width: 108px;
  max-width: 128px;
  padding: 2px 6px;
  font-size: 11px;
}
QComboBox, QSpinBox, QLineEdit, QPlainTextEdit {
  background: #0b0d10;
  border: 1px solid #2a3340;
  color: #e8eef5;
  padding: 4px 8px;
  border-radius: 4px;
  min-height: 22px;
}
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
  background: #141a22;
  color: #e8eef5;
  selection-background-color: #243044;
  border: 1px solid #2a3340;
}
QTabWidget::pane { border: 1px solid #242c36; background: #12161c; border-radius: 6px; }
QTabBar::tab {
  background: #10141a;
  color: #8b95a1;
  padding: 8px 14px;
  border: 1px solid #242c36;
  border-bottom: none;
}
QTabBar::tab:selected { background: #1a2230; color: #e8eef5; }
QGroupBox {
  border: 1px solid #242c36;
  margin-top: 14px;
  padding: 10px;
  border-radius: 6px;
  color: #8b95a1;
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; }
QScrollBar:vertical { background: #0b0d10; width: 10px; }
QScrollBar::handle:vertical { background: #2a3340; border-radius: 4px; min-height: 24px; }
)css";
}

} // namespace brick2::theme
