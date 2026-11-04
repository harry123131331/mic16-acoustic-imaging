"""精简桌面仪器的低干扰样式；旧研究界面保留原主题。"""


def compact_stylesheet(font_family: str) -> str:
    return f"""
    * {{ font-family: "{font_family}"; font-size: 13px; color: #263342; }}
    QMainWindow, QWidget#CompactRoot {{ background: #f3f4f6; }}
    QDialog, QMessageBox {{ background: #f3f4f6; color: #263342; }}
    QMenuBar, QStatusBar {{ background: #f3f4f6; }}
    QMenu {{ background: #ffffff; color: #263342; border: 1px solid #bdc7d1; }}
    QMenu::item:selected {{ background: #e4eef7; color: #174468; }}
    QFrame#CompactToolbar {{ background: #ffffff; border-bottom: 1px solid #d5dbe1; }}
    QLabel#CompactTitle {{ font-size: 16px; font-weight: 600; }}
    QLabel#SourceBadge {{ background: #edf1f5; padding: 4px 9px; border-radius: 3px; }}
    QLabel#SourceBadge[demo="true"] {{ color: #825500; background: #fff1d2; }}
    QLabel#MutedLabel {{ color: #687582; font-size: 12px; }}
    QLabel#Notice {{ color: #566676; padding: 2px 0; }}
    QLabel#CountValue {{ font-size: 30px; font-weight: 600; color: #263342; }}
    QGroupBox {{ background: #ffffff; border: 1px solid #d5dbe1; border-radius: 3px;
        margin-top: 10px; padding: 12px 8px 8px 8px; font-weight: 600; }}
    QGroupBox::title {{ subcontrol-origin: margin; left: 10px; padding: 0 4px; }}
    QPushButton, QToolButton {{ background: #ffffff; border: 1px solid #bdc7d1;
        border-radius: 3px; padding: 6px 11px; }}
    QPushButton:hover, QToolButton:hover {{ background: #edf3f9; border-color: #7898b9; }}
    QPushButton#ConnectButton {{ background: #27669b; color: #ffffff; border-color: #27669b; }}
    QPushButton:disabled, QToolButton:disabled {{ color: #9aa3ad; background: #f0f2f4; border-color: #dce1e5; }}
    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {{ background: #ffffff; color: #263342; border: 1px solid #bdc7d1;
        border-radius: 3px; padding: 5px; }}
    QComboBox QAbstractItemView {{ background: #ffffff; color: #263342;
        selection-background-color: #dceaf7; selection-color: #174468;
        border: 1px solid #bdc7d1; outline: none; }}
    QTableWidget {{ background: #ffffff; border: none; gridline-color: #edf0f3;
        alternate-background-color: #f6f8fa; selection-background-color: #e4eef7; }}
    QHeaderView::section {{ background: #f1f4f6; border: none; padding: 7px 5px; font-weight: 500; }}
    QTabWidget::pane {{ border: 1px solid #d5dbe1; background: white; }}
    QTabBar::tab {{ background: #eef1f4; padding: 5px 16px; }}
    QTabBar::tab:selected {{ background: #ffffff; border-top: 2px solid #27669b; }}
    QPlainTextEdit {{ background: white; border: 1px solid #d5dbe1; }}
    QSplitter::handle {{ background: #f3f4f6; }}
    QCheckBox::indicator {{ width: 15px; height: 15px; background: #ffffff;
        border: 1px solid #788693; border-radius: 3px; }}
    QCheckBox::indicator:checked {{ background: #27669b; border-color: #174468; }}
    QToolTip {{ background: #ffffff; border: 1px solid #bdc7d1; color: #263342; }}
    """
