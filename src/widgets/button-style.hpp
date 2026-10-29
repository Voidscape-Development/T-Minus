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
// QPushButton a min-height (Yami: 18px), which Qt turns into the widget's
// minimum size when it polishes the button, so setFixedSize() alone is
// undone and the layout squeezes the button down to the theme's minimum.
// Themes also add margins (Yami: "OBSDock QPushButton { margin: 1px 2px; }",
// which matches our dock too) that would shrink the visible button. A
// widget's own style sheet wins over the theme's, while colours, borders
// and icons still come from the theme.

// An icon-only button of exactly size, themed through the "themeID"
// (legacy themes) and "class" (OBS 30+ themes) properties.
inline void StyleIconButton(QPushButton *btn, QSize size, const char *themeID = nullptr, const char *cls = nullptr)
{
	btn->setStyleSheet(QString("margin: 0px; padding: 0px; min-width: %1px; max-width: %1px; "
				   "min-height: %2px; max-height: %2px;")
				   .arg(size.width())
				   .arg(size.height()));
	btn->setFixedSize(size);
	btn->setIconSize(QSize(16, 16));
	if (themeID)
		btn->setProperty("themeID", themeID);
	if (cls)
		btn->setProperty("class", cls);
	btn->style()->unpolish(btn);
	btn->style()->polish(btn);
}

// A text button of exactly height, keeping horizontal padding for its text.
// Vertical padding is dropped so the height is the whole button (Qt's
// min-height applies to the content box) and the text stays centred.
inline void StyleTextButton(QPushButton *btn, int height)
{
	btn->setStyleSheet(QString("margin: 0px; padding: 0px 14px; min-height: %1px; max-height: %1px;").arg(height));
	btn->setFixedHeight(height);
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
