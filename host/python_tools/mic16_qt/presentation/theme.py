from __future__ import annotations


COLORS = {
    "navy": "#102A43",
    "navy_2": "#173F5F",
    "accent": "#007E9E",
    "accent_bright": "#00A7C7",
    "green": "#178C69",
    "amber": "#B77600",
    "red": "#C43D4B",
    "bg": "#EDF2F6",
    "panel": "#FFFFFF",
    "panel_alt": "#F6F9FB",
    "border": "#CFD9E2",
    "ink": "#17324D",
    "muted": "#61768A",
    "chart": "#0E1C29",
}


def application_stylesheet(font_family: str = "Microsoft YaHei UI") -> str:
    c = COLORS
    return f"""
    * {{
        font-family: "{font_family}";
        font-size: 14px;
        color: {c['ink']};
    }}
    QMainWindow, QWidget#AppRoot {{ background: {c['bg']}; }}
    QWidget#TopHeader {{ background: {c['navy']}; border: none; }}
    QLabel#ProductKicker {{ color: #6ED5E5; font-size: 12px; font-weight: 700; letter-spacing: 1px; }}
    QLabel#ProductTitle {{ color: white; font-size: 25px; font-weight: 700; }}
    QLabel#ProductSubtitle {{ color: #BCD0E0; font-size: 13px; }}
    QLabel#EvidenceBadge {{
        color: #083B47; background: #89E2EC; border-radius: 5px;
        font-family: Consolas; font-weight: 700; padding: 7px 11px;
    }}
    QWidget#StatusStrip, QFrame#Card, QFrame#Panel {{
        background: {c['panel']}; border: 1px solid {c['border']}; border-radius: 7px;
    }}
    QFrame#MetricCard {{
        background: {c['panel']}; border: 1px solid {c['border']};
        border-top: 3px solid {c['accent']}; border-radius: 7px;
    }}
    QLabel#MetricLabel, QLabel#SectionMeta {{ color: {c['muted']}; font-size: 12px; font-weight: 600; }}
    QLabel#MetricValue {{ color: {c['navy']}; font-size: 21px; font-weight: 700; }}
    QLabel#SectionTitle {{ color: {c['navy']}; font-size: 16px; font-weight: 700; }}
    QLabel#PageTitle {{ color: {c['navy']}; font-size: 22px; font-weight: 700; }}
    QLabel#PageSubtitle {{ color: {c['muted']}; font-size: 13px; }}
    QWidget#SideNav {{ background: {c['navy']}; }}
    QPushButton#NavButton {{
        color: #C9D9E6; background: transparent; border: none; border-radius: 6px;
        text-align: left; padding: 11px 14px; font-weight: 600;
    }}
    QPushButton#NavButton:hover {{ background: {c['navy_2']}; color: white; }}
    QPushButton#NavButton:checked {{ background: {c['accent']}; color: white; }}
    QPushButton#PrimaryButton {{
        background: {c['accent']}; color: white; border: 1px solid {c['accent']};
        border-radius: 5px; padding: 8px 16px; font-weight: 650;
    }}
    QPushButton#PrimaryButton:hover {{ background: {c['accent_bright']}; }}
    QPushButton#SecondaryButton {{
        background: {c['panel']}; color: {c['navy']}; border: 1px solid #AFC0CE;
        border-radius: 5px; padding: 8px 14px; font-weight: 600;
    }}
    QPushButton#SecondaryButton:hover {{ background: #E6F3F7; border-color: {c['accent']}; }}
    QPushButton:disabled {{ color: #91A2B1; background: #E3E9EE; border-color: #D3DDE5; }}
    QTableWidget, QPlainTextEdit, QLineEdit, QComboBox, QSpinBox {{
        background: white; border: 1px solid {c['border']}; border-radius: 4px;
        selection-background-color: #C9EEF3; padding: 5px;
    }}
    QHeaderView::section {{
        background: #E6EDF3; color: {c['navy']}; border: none; border-bottom: 1px solid #C5D1DB;
        padding: 7px; font-weight: 700;
    }}
    QTabWidget::pane {{ border: 1px solid {c['border']}; background: white; }}
    QTabBar::tab {{ background: #DFE7EE; padding: 8px 16px; margin-right: 2px; }}
    QTabBar::tab:selected {{ background: white; color: {c['accent']}; font-weight: 700; }}
    QStatusBar {{ background: {c['navy']}; color: #DCE8F1; }}
    QToolTip {{ background: {c['navy']}; color: white; border: 1px solid #6ED5E5; }}
    """
