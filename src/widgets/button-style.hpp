#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>

// Sizes for icon-only buttons, matched to the native OBS dock toolbars
// (Scenes, Sources, Scene Transitions), whose buttons are 32x26.
namespace ButtonSize {
inline const QSize Toolbar(32, 26);
inline const QSize Inline(30, 26);
// Smaller, for the menu button in a card header
inline const QSize Header(26, 22);
} // namespace ButtonSize

// Height shared by every button in a timer card's control row, so text
// and icon buttons line up
constexpr int kControlRowHeight = 30;

// Sizing has to go through the button's own style sheet. Themes give every
// QPushButton a height, max-height and vertical margins (Yami: "height:
// var(--input_height); margin-top: var(--spacing_input)"), which Qt turns
// into the widget's minimum and maximum size when it polishes the button,
// so setFixedSize() alone is undone. Qt derives the minimum from the larger
// of height and min-height and the maximum from the smaller of height and
// max-height, so all three are pinned here; leaving the theme's height in
// place lets buttons in the same row come out at different sizes. A
// widget's own style sheet wins over the theme's, while colours, borders
// and icons still come from the theme.
//
// The rules are scoped to QPushButton on purpose: a tooltip is styled with
// the style sheet of the widget showing it, so a bare "max-width: 32px"
// would squeeze the button's tooltip into a 32x26 box as well.

// The explicit size goes through polish so it holds even if the button
// is already visible (e.g. buttons rebuilt after editing a timer)
inline void ApplyButtonGeometry(QPushButton *btn, const QString &rules)
{
	btn->setStyleSheet(QString("QPushButton { %1 }").arg(rules));
	btn->style()->unpolish(btn);
	btn->style()->polish(btn);
}

// An icon-only button of exactly size, themed through the "themeID"
// (legacy themes) and "class" (OBS 30+ themes) properties.
inline void StyleIconButton(QPushButton *btn, QSize size, const char *themeID = nullptr, const char *cls = nullptr)
{
	if (themeID)
		btn->setProperty("themeID", themeID);
	if (cls)
		btn->setProperty("class", cls);
	btn->setIconSize(QSize(16, 16));
	btn->setFixedSize(size);
	ApplyButtonGeometry(btn, QString("margin: 0px; padding: 0px; width: %1px; min-width: %1px; max-width: %1px; "
					 "height: %2px; min-height: %2px; max-height: %2px;")
					 .arg(size.width())
					 .arg(size.height()));
}

// A text button of exactly height, keeping horizontal padding for its text.
// Vertical padding is dropped so the height is the whole button (Qt's
// min-height applies to the content box) and the text stays centred.
inline void StyleTextButton(QPushButton *btn, int height, int hPadding = 14)
{
	btn->setFixedHeight(height);
	ApplyButtonGeometry(btn, QString("margin: 0px; padding: 0px %2px; height: %1px; min-height: %1px; "
					 "max-height: %1px;")
					 .arg(height)
					 .arg(hPadding));
}

// Three-line "list" glyph for the compact-view toggle; themes have no such
// icon, so it's drawn in the button's text colour.
inline QIcon ListIcon(const QColor &color)
{
	QPixmap pixmap(32, 32);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(color);
	for (int i = 0; i < 3; i++)
		painter.drawRoundedRect(QRectF(5, 7 + i * 8, 22, 3), 1.5, 1.5);
	return QIcon(pixmap);
}
