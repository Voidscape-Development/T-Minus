#include "timer-widget.hpp"
#include "timer-settings-dialog.hpp"
#include "drag-reorder.hpp"
#include "button-style.hpp"
#include "../core/action-executor.hpp"
#include "../utils/obs-utils.hpp"
#include "../utils/format-utils.hpp"
#include "../plugin-support.h"

#include <obs.h>
#include <obs-module.h>

#include <QColor>
#include <QHBoxLayout>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>

namespace {

const QColor kRunningColor(0x4f, 0x7c, 0xf0);
const QColor kPausedColor(0xd9, 0xa4, 0x41);
const QColor kFinishedColor(0xe8, 0x60, 0x4f);
const QColor kReadyColor(0x8b, 0x91, 0xa1);

// QLabel that elides its text instead of forcing the dock wider.
class ElidedLabel : public QLabel {
public:
	using QLabel::QLabel;

	QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		const QRect r = contentsRect();
		const QString elided = fontMetrics().elidedText(text(), Qt::ElideRight, r.width());
		style()->drawItemText(&painter, r, int(alignment()), palette(), isEnabled(), elided, foregroundRole());
	}
};

const char *TimerTypeToString(TimerType t)
{
	switch (t) {
	case TimerType::Countdown:
		return "Countdown";
	case TimerType::Stopwatch:
		return "Stopwatch";
	case TimerType::TimeUntil:
		return "Time Until";
	}
	return "Unknown";
}

QString Rgba(const QColor &c, int alphaPercent)
{
	return QString("rgba(%1, %2, %3, %4%)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alphaPercent);
}

// "+30s", "−1m", "+1m30s", "+2h"
QString FormatDelta(int seconds)
{
	const QString sign = seconds < 0 ? QString::fromUtf8("−") : QStringLiteral("+");
	int s = qAbs(seconds);
	if (s >= 3600 && s % 3600 == 0)
		return sign + QString("%1h").arg(s / 3600);
	if (s >= 60 && s % 60 == 0)
		return sign + QString("%1m").arg(s / 60);
	if (s > 60)
		return sign + QString("%1m%2s").arg(s / 60).arg(s % 60);
	return sign + QString("%1s").arg(s);
}

QFont TimeFont(const QFont &base, int pixelSize)
{
	QFont f = base;
	f.setPixelSize(pixelSize);
	f.setBold(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	// Tabular figures keep the digits from shifting as the time changes
	f.setFeature(QFont::Tag("tnum"), 1);
#endif
	return f;
}

void SetIconClass(QPushButton *btn, const char *themeID, const char *cls)
{
	btn->setProperty("themeID", themeID);
	btn->setProperty("class", cls);
	btn->style()->unpolish(btn);
	btn->style()->polish(btn);
}

} // namespace

TimerWidget::TimerWidget(QWidget *parent, obs_data_t *savedData) : QWidget(parent)
{
	m_engine = new TimerEngine(this);
	m_actionExecutor = new ActionExecutor(this);

	if (savedData) {
		LoadData(savedData);
	} else {
		m_data.timerId = GenerateUniqueID();
		m_data.countdownDuration = {0, 0, 5, 0};
		m_data.display.autoUnits = true;
	}

	SetupUI();

	connect(m_engine, &TimerEngine::tick, this, &TimerWidget::OnTick);
	connect(m_engine, &TimerEngine::finished, this, &TimerWidget::OnFinished);
	connect(m_engine, &TimerEngine::stateChanged, this, &TimerWidget::OnStateChanged);

	m_engine->configure(m_data.timerType, m_data.countdownDuration, m_data.targetDateTime);
	UpdateSubLabel();
	UpdateDisplay(m_engine->currentTimeMs());
	UpdateStateVisuals();

	// Register hotkeys immediately so timers added at runtime get them
	// without an OBS restart. savedData restores saved key bindings.
	RegisterHotkeys(savedData);
}

TimerWidget::~TimerWidget()
{
	UnregisterHotkeys();
}

void TimerWidget::SetupUI()
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);

	m_card = new QFrame(this);
	m_card->setObjectName("tminusCard");
	outer->addWidget(m_card);

	auto *cardLayout = new QVBoxLayout(m_card);
	cardLayout->setContentsMargins(0, 0, 0, 0);
	cardLayout->setSpacing(0);

	// --- Comfortable layout ---
	m_full = new QWidget(m_card);
	auto *full = new QVBoxLayout(m_full);
	full->setContentsMargins(10, 8, 8, 10);
	full->setSpacing(6);

	auto *header = new QHBoxLayout();
	header->setSpacing(6);
	m_dragHandle = new DragHandle(m_full);
	header->addWidget(m_dragHandle);
	m_statusPill = new QLabel(m_full);
	m_statusPill->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	m_statusPill->setFixedHeight(18);
	header->addWidget(m_statusPill);
	m_nameLabel = new ElidedLabel(m_full);
	m_nameLabel->setStyleSheet("font-weight: bold;");
	header->addWidget(m_nameLabel, 1);
	m_typeLabel = new QLabel(m_full);
	m_typeLabel->setStyleSheet("color: rgba(127, 127, 127, 100%);");
	header->addWidget(m_typeLabel);
	m_menuBtn = new QPushButton(m_full);
	m_menuBtn->setFlat(true);
	StyleIconButton(m_menuBtn, ButtonSize::Header, "menuIconSmall", "icon-dots-vert");
	m_menuBtn->setToolTip("More actions");
	m_menuBtn->setAccessibleName("More actions");
	header->addWidget(m_menuBtn);
	full->addLayout(header);

	auto *timeRow = new QHBoxLayout();
	m_timeDisplay = new QLabel(m_full);
	m_timeDisplay->setFont(TimeFont(font(), 32));
	timeRow->addWidget(m_timeDisplay);
	timeRow->addStretch();
	m_subLabel = new QLabel(m_full);
	m_subLabel->setStyleSheet("color: rgba(127, 127, 127, 100%);");
	timeRow->addWidget(m_subLabel, 0, Qt::AlignBottom);
	full->addLayout(timeRow);

	m_progress = new QProgressBar(m_full);
	m_progress->setRange(0, 1000);
	m_progress->setTextVisible(false);
	m_progress->setFixedHeight(4);
	full->addWidget(m_progress);

	m_sourceLabel = new ElidedLabel(m_full);
	full->addWidget(m_sourceLabel);
	m_infoLabel = new ElidedLabel(m_full);
	m_infoLabel->setStyleSheet("color: rgba(127, 127, 127, 100%);");
	full->addWidget(m_infoLabel);

	auto *controls = new QHBoxLayout();
	controls->setSpacing(4);
	m_primaryBtn = new QPushButton(m_full);
	StyleTextButton(m_primaryBtn, kControlRowHeight);
	controls->addWidget(m_primaryBtn, 1);
	m_nudgeBox = new QWidget(m_full);
	auto *nudgeLayout = new QHBoxLayout(m_nudgeBox);
	nudgeLayout->setContentsMargins(0, 0, 0, 0);
	nudgeLayout->setSpacing(4);
	controls->addWidget(m_nudgeBox);
	m_restartBtn = new QPushButton(m_full);
	m_restartBtn->setToolTip("Reset");
	m_restartBtn->setAccessibleName("Reset");
	StyleIconButton(m_restartBtn, QSize(36, kControlRowHeight), "restartIcon", "icon-media-restart");
	controls->addWidget(m_restartBtn);
	full->addLayout(controls);

	cardLayout->addWidget(m_full);

	// --- Compact layout: one row per timer ---
	m_compactRow = new QWidget(m_card);
	auto *row = new QHBoxLayout(m_compactRow);
	row->setContentsMargins(4, 4, 4, 4);
	m_compactDragHandle = new DragHandle(m_compactRow);
	row->addWidget(m_compactDragHandle);
	row->setSpacing(8);
	m_compactDot = new QLabel(m_compactRow);
	m_compactDot->setFixedSize(8, 8);
	row->addWidget(m_compactDot);
	m_compactName = new ElidedLabel(m_compactRow);
	row->addWidget(m_compactName, 1);
	m_compactTime = new QLabel(m_compactRow);
	m_compactTime->setFont(TimeFont(font(), 16));
	row->addWidget(m_compactTime);
	m_compactBtn = new QPushButton(m_compactRow);
	StyleIconButton(m_compactBtn, ButtonSize::Inline);
	row->addWidget(m_compactBtn, 0, Qt::AlignVCenter);
	cardLayout->addWidget(m_compactRow);

	m_compactProgress = new QProgressBar(m_card);
	m_compactProgress->setRange(0, 1000);
	m_compactProgress->setTextVisible(false);
	m_compactProgress->setFixedHeight(2);
	cardLayout->addWidget(m_compactProgress);

	m_compactRow->hide();
	m_compactProgress->hide();

	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this,
		[this](const QPoint &pos) { ShowMenu(mapToGlobal(pos)); });
	connect(m_menuBtn, &QPushButton::clicked, this,
		[this]() { ShowMenu(m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height()))); });

	auto primaryAction = [this]() {
		if (m_engine->state() == TimerState::Finished)
			RestartTimer();
		else
			ToggleStartPause();
	};
	connect(m_primaryBtn, &QPushButton::clicked, this, primaryAction);
	connect(m_compactBtn, &QPushButton::clicked, this, primaryAction);
	connect(m_restartBtn, &QPushButton::clicked, this, &TimerWidget::RestartTimer);

	UpdateHeaderLabels();
	RebuildNudgeButtons();
	UpdateMeta();
	UpdateSubLabel();
}

void TimerWidget::RebuildNudgeButtons()
{
	qDeleteAll(m_nudgeButtons);
	m_nudgeButtons.clear();

	const auto entries = NudgeEntries();
	for (int i = 0; i < entries.size() && i < 3; i++) {
		const int deltaMs = entries[i].deltaSeconds * 1000;
		auto *btn = new QPushButton(FormatDelta(entries[i].deltaSeconds), m_nudgeBox);
		StyleTextButton(btn, kControlRowHeight);
		btn->setToolTip(entries[i].label);
		connect(btn, &QPushButton::clicked, this, [this, deltaMs]() { m_engine->addTime(deltaMs); });
		m_nudgeBox->layout()->addWidget(btn);
		m_nudgeButtons.append(btn);
	}

	UpdateStateVisuals();
}

QList<AddTimeHotkeyEntry> TimerWidget::NudgeEntries() const
{
	if (!m_data.addTimeHotkeys.isEmpty())
		return m_data.addTimeHotkeys;

	AddTimeHotkeyEntry minus;
	minus.label = "Subtract 30 seconds";
	minus.deltaSeconds = -30;
	AddTimeHotkeyEntry plus;
	plus.label = "Add 1 minute";
	plus.deltaSeconds = 60;
	return {minus, plus};
}

QString TimerWidget::DisplayLabel() const
{
	return m_data.displayName.isEmpty() ? QString(TimerTypeToString(m_data.timerType)) : m_data.displayName;
}

void TimerWidget::UpdateHeaderLabels()
{
	m_nameLabel->setText(DisplayLabel());
	m_compactName->setText(DisplayLabel());
	m_typeLabel->setText(TimerTypeToString(m_data.timerType));
	m_typeLabel->setVisible(!m_data.displayName.isEmpty());
	m_nameLabel->update();
	m_compactName->update();
}

void TimerWidget::UpdateMeta()
{
	if (m_data.selectedTextSource.isEmpty()) {
		m_sourceLabel->setText("No text source");
		m_sourceLabel->setStyleSheet("color: rgba(127, 127, 127, 100%);");
		m_sourceLabel->setToolTip("Link a text source in the timer's settings to show the time on stream.");
	} else if (m_sourceMissing) {
		m_sourceLabel->setText(
			QString::fromUtf8("⚠ Text source “%1” not found").arg(m_data.selectedTextSource));
		m_sourceLabel->setStyleSheet(QString("color: %1;").arg(kPausedColor.name()));
		m_sourceLabel->setToolTip("The source was renamed or removed. Pick it again in the timer's settings.");
	} else {
		m_sourceLabel->setText(QString("Text: %1").arg(m_data.selectedTextSource));
		m_sourceLabel->setStyleSheet("color: rgba(127, 127, 127, 100%);");
		m_sourceLabel->setToolTip(QString());
	}
	m_sourceLabel->update();

	QStringList info;
	if (!m_data.endActions.isEmpty())
		info << (m_data.endActions.size() == 1 ? QString("1 end action")
						       : QString("%1 end actions").arg(m_data.endActions.size()));
	if (m_data.resetOnStreamStart && m_data.startOnStreamStart)
		info << "Restarts with stream";
	else if (m_data.startOnStreamStart)
		info << "Starts with stream";
	else if (m_data.resetOnStreamStart)
		info << "Resets with stream";

	m_infoLabel->setText(info.join(QString::fromUtf8(" · ")));
	m_infoLabel->setVisible(!info.isEmpty());
}

void TimerWidget::UpdateSubLabel()
{
	QString text;
	switch (m_engine->timerType()) {
	case TimerType::Countdown: {
		DisplayFormat timeOnly = m_data.display;
		timeOnly.useFormatString = false;
		text = QString("of %1").arg(FormatTimerDisplay(timeOnly, m_engine->initialMs()));
		break;
	}
	case TimerType::TimeUntil: {
		const QDateTime target = m_engine->targetDateTime();
		if (!target.isValid())
			break;
		const QLocale locale;
		if (target.date() == QDate::currentDate())
			text = QString("until %1").arg(locale.toString(target.time(), QLocale::ShortFormat));
		else
			text = QString("until %1").arg(locale.toString(target, QLocale::ShortFormat));
		break;
	}
	case TimerType::Stopwatch:
		break;
	}
	m_subLabel->setText(text);
	m_subLabel->setVisible(!text.isEmpty());
}

void TimerWidget::UpdateStateVisuals()
{
	const TimerState state = m_engine->state();

	QColor color = kReadyColor;
	QString status = "Ready";
	switch (state) {
	case TimerState::Running:
		color = kRunningColor;
		status = "Running";
		break;
	case TimerState::Paused:
		color = kPausedColor;
		status = "Paused";
		break;
	case TimerState::Finished:
		color = kFinishedColor;
		status = "Done";
		break;
	case TimerState::Stopped:
		break;
	}

	const bool highlighted = state == TimerState::Running || state == TimerState::Finished;
	m_card->setStyleSheet(QString("QFrame#tminusCard { background-color: rgba(127, 127, 127, 12%); "
				      "border: 1px solid %1; border-radius: 6px; }")
				      .arg(highlighted ? color.name() : QString("rgba(127, 127, 127, 25%)")));

	m_statusPill->setText(status.toUpper());
	m_statusPill->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 3px; "
					    "padding: 0px 5px; margin: 0px; font-size: 10px; font-weight: bold;")
					    .arg(Rgba(color, 30), Rgba(color, 70)));

	m_compactDot->setStyleSheet(state == TimerState::Stopped
					    ? QString("border: 1px solid %1; border-radius: 4px;").arg(color.name())
					    : QString("background-color: %1; border-radius: 4px;").arg(color.name()));
	m_compactDot->setToolTip(status);
	m_compactName->setToolTip(QString("%1 (%2)").arg(DisplayLabel(), status));

	// Size goes in the stylesheet too: OBS themes style QLabel fonts, which
	// overrides setFont()
	const QString timeColor = (state == TimerState::Paused || state == TimerState::Finished)
					  ? QString("color: %1;").arg(color.name())
					  : QString();
	m_timeDisplay->setStyleSheet("font-size: 32px; font-weight: bold;" + timeColor);
	m_compactTime->setStyleSheet("font-size: 16px; font-weight: bold;" + timeColor);

	const QString barStyle = QString("QProgressBar { border: none; background-color: rgba(127, 127, 127, 25%); "
					 "border-radius: 1px; } QProgressBar::chunk { background-color: %1; "
					 "border-radius: 1px; }")
					 .arg(color.name());
	m_progress->setStyleSheet(barStyle);
	m_compactProgress->setStyleSheet(barStyle);

	switch (state) {
	case TimerState::Running:
		m_primaryBtn->setText("Pause");
		SetIconClass(m_compactBtn, "pauseIcon", "icon-media-pause");
		m_compactBtn->setToolTip("Pause");
		break;
	case TimerState::Paused:
		m_primaryBtn->setText("Resume");
		SetIconClass(m_compactBtn, "playIcon", "icon-media-play");
		m_compactBtn->setToolTip("Resume");
		break;
	case TimerState::Finished:
		m_primaryBtn->setText("Reset");
		SetIconClass(m_compactBtn, "restartIcon", "icon-media-restart");
		m_compactBtn->setToolTip("Reset");
		break;
	case TimerState::Stopped:
		m_primaryBtn->setText("Start");
		SetIconClass(m_compactBtn, "playIcon", "icon-media-play");
		m_compactBtn->setToolTip("Start");
		break;
	}
	m_compactBtn->setAccessibleName(m_compactBtn->toolTip());

	// Once finished, the primary button is the reset; nudging a finished
	// timer would be discarded by the next run anyway.
	m_restartBtn->setVisible(state != TimerState::Finished);
	m_nudgeBox->setVisible(state != TimerState::Finished);
}

void TimerWidget::SetCompact(bool compact)
{
	m_compact = compact;
	m_full->setVisible(!compact);
	m_compactRow->setVisible(compact);
	// Progress visibility depends on the layout as well as the timer type
	UpdateDisplay(m_engine->currentTimeMs());
}

void TimerWidget::SetMoveEnabled(bool canMoveUp, bool canMoveDown)
{
	m_canMoveUp = canMoveUp;
	m_canMoveDown = canMoveDown;
}

void TimerWidget::InitAsType(TimerType type)
{
	m_data.timerType = type;
	if (type == TimerType::TimeUntil) {
		const QDateTime now = QDateTime::currentDateTime();
		m_data.targetDateTime = QDateTime(now.date(), QTime(now.time().hour(), 0)).addSecs(2 * 3600);
	}

	m_engine->configure(m_data.timerType, m_data.countdownDuration, m_data.targetDateTime);
	UpdateHeaderLabels();
	UpdateSubLabel();
	UpdateDisplay(m_engine->currentTimeMs());
}

void TimerWidget::StartTimer()
{
	// A fresh run supersedes whatever the previous run's end actions are
	// still doing (e.g. waiting on a Delay step before a scene switch).
	if (m_engine->state() != TimerState::Running && m_engine->state() != TimerState::Paused)
		m_actionExecutor->cancel();
	m_engine->start();
}

void TimerWidget::PauseTimer()
{
	m_engine->pause();
}

void TimerWidget::ToggleStartPause()
{
	if (m_engine->state() == TimerState::Running)
		m_engine->pause();
	else
		StartTimer();
}

void TimerWidget::RestartTimer()
{
	m_actionExecutor->cancel();
	m_engine->restart();
	UpdateSubLabel();
}

void TimerWidget::OnTick(long long currentMs)
{
	m_data.timeLeftMs = currentMs;
	UpdateDisplay(currentMs);
	// Add-time moves a time-until target
	if (m_engine->timerType() == TimerType::TimeUntil)
		UpdateSubLabel();
}

void TimerWidget::OnFinished()
{
	obs_log(LOG_INFO, "Timer %s finished", m_data.timerId.toUtf8().constData());

	if (!m_data.endActions.isEmpty()) {
		// %time% in Set Text steps is the bare time value, without the
		// timer's own format string wrapped around it.
		DisplayFormat timeOnly = m_data.display;
		timeOnly.useFormatString = false;
		m_actionExecutor->execute(m_data.endActions, m_data.selectedTextSource,
					  FormatTimerDisplay(timeOnly, m_engine->currentTimeMs()));
	}
}

void TimerWidget::OnStateChanged(TimerState newState)
{
	m_data.state = newState;
	// A new run may have picked up a pending configuration
	UpdateSubLabel();
	UpdateStateVisuals();
	// Swaps the finished text in or out of the source
	UpdateDisplay(m_engine->currentTimeMs());
	emit stateChanged(newState);
}

void TimerWidget::UpdateDisplay(long long timeMs)
{
	QString displayText = FormatTimerDisplay(m_data.display, timeMs);

	// The dock shows the bare time; the format string is for the source
	DisplayFormat timeOnly = m_data.display;
	timeOnly.useFormatString = false;
	const QString timeText = FormatTimerDisplay(timeOnly, timeMs);
	m_timeDisplay->setText(timeText);
	m_compactTime->setText(timeText);

	const long long initial = m_engine->initialMs();
	const bool showProgress = m_engine->timerType() == TimerType::Countdown && initial > 0;
	if (showProgress) {
		const int value = static_cast<int>(qBound(0LL, timeMs * 1000 / initial, 1000LL));
		m_progress->setValue(value);
		m_compactProgress->setValue(value);
	}
	m_progress->setVisible(showProgress);
	m_compactProgress->setVisible(showProgress && m_compact);

	// Once finished, the source shows the timer's finished text if it has
	// one. End actions run after this, so a Set Text step still wins.
	if (m_engine->state() == TimerState::Finished && !m_data.finishedText.isEmpty())
		UpdateSourceText(m_data.finishedText);
	else
		UpdateSourceText(displayText);
}

void TimerWidget::UpdateSourceText(const QString &text)
{
	bool missing = false;

	if (!m_data.selectedTextSource.isEmpty()) {
		obs_source_t *source = obs_get_source_by_name(m_data.selectedTextSource.toUtf8().constData());
		if (source) {
			obs_data_t *settings = obs_source_get_settings(source);
			obs_data_set_string(settings, "text", text.toUtf8().constData());
			obs_source_update(source, settings);
			obs_data_release(settings);
			obs_source_release(source);
		} else {
			missing = true;
		}
	}

	if (missing != m_sourceMissing) {
		m_sourceMissing = missing;
		UpdateMeta();
	}
}

void TimerWidget::ShowMenu(const QPoint &globalPos)
{
	const TimerState state = m_engine->state();
	QMenu menu(this);

	if (state == TimerState::Finished) {
		menu.addAction("Reset", this, &TimerWidget::RestartTimer);
	} else {
		menu.addAction(m_primaryBtn->text(), this, &TimerWidget::ToggleStartPause);
		menu.addAction("Reset", this, &TimerWidget::RestartTimer);

		QMenu *addTime = menu.addMenu("Add Time");
		for (const auto &entry : NudgeEntries()) {
			const int deltaMs = entry.deltaSeconds * 1000;
			addTime->addAction(QString("%1  (%2)").arg(FormatDelta(entry.deltaSeconds), entry.label), this,
					   [this, deltaMs]() { m_engine->addTime(deltaMs); });
		}
	}

	menu.addSeparator();
	menu.addAction("Edit Timer...", this, &TimerWidget::OpenSettings);
	menu.addAction("Duplicate", this, [this]() { emit duplicateRequested(m_data.timerId); });
	menu.addAction("Move Up", this, [this]() {
		    emit moveRequested(Direction::Up, m_data.timerId);
	    })->setEnabled(m_canMoveUp);
	menu.addAction("Move Down", this, [this]() {
		    emit moveRequested(Direction::Down, m_data.timerId);
	    })->setEnabled(m_canMoveDown);
	menu.addSeparator();
	menu.addAction("Delete...", this, &TimerWidget::ConfirmDelete);

	menu.exec(globalPos);
}

void TimerWidget::ConfirmDelete()
{
	const auto answer = QMessageBox::question(
		this, "Delete Timer",
		QString::fromUtf8("Delete “%1”? Its end actions and hotkey bindings are removed too.")
			.arg(DisplayLabel()),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (answer == QMessageBox::Yes)
		emit removeRequested(m_data.timerId);
}

void TimerWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
	QWidget::mouseDoubleClickEvent(event);
	OpenSettings();
}

void TimerWidget::OpenSettings()
{
	TimerSettingsDialog dialog(m_data, this);
	if (dialog.exec() == QDialog::Accepted) {
		// Re-register every hotkey around the change, carrying the key
		// bindings over: descriptions pick up a new name, edited add-time
		// amounts get new callbacks, and removed entries are dropped.
		obs_data_t *bindings = obs_data_create();
		SaveHotkeyBindings(bindings);
		UnregisterHotkeys();

		dialog.ApplyToData(m_data);

		RegisterHotkeys(bindings);
		obs_data_release(bindings);

		m_engine->configure(m_data.timerType, m_data.countdownDuration, m_data.targetDateTime);
		UpdateHeaderLabels();
		UpdateSubLabel();
		UpdateMeta();
		UpdateDisplay(m_engine->currentTimeMs());
		RebuildNudgeButtons();

		emit settingsChanged();
	}
}

void TimerWidget::SaveDuplicateData(obs_data_t *obj)
{
	SaveData(obj);

	obs_data_set_string(obj, "timerId", GenerateUniqueID().toUtf8().constData());
	if (!m_data.displayName.isEmpty())
		obs_data_set_string(obj, "displayName",
				    QString("%1 (copy)").arg(m_data.displayName).toUtf8().constData());

	// The copy starts without key bindings: start/pause and restart
	// hotkeys are keyed by timer ID, and clearing the registration names
	// gives its add-time hotkeys fresh ones.
	obs_data_array_t *addTimeArray = obs_data_get_array(obj, "addTimeHotkeys");
	if (addTimeArray) {
		for (size_t i = 0; i < obs_data_array_count(addTimeArray); i++) {
			obs_data_t *entryObj = obs_data_array_item(addTimeArray, i);
			obs_data_set_string(entryObj, "hotkeyRegName", "");
			obs_data_release(entryObj);
		}
		obs_data_array_release(addTimeArray);
	}
}

void TimerWidget::RegisterHotkeys(obs_data_t *savedData)
{
	std::string idStr = m_data.timerId.toStdString();
	// Use the display name in hotkey descriptions so entries in
	// OBS Settings > Hotkeys are recognizable.
	QString timerLabel = m_data.displayName.isEmpty() ? m_data.timerId : m_data.displayName;

	if (m_data.startPauseHotkeyId == -1) {
		std::string startPauseName = "TMinus_StartPause_" + idStr;
		std::string startPauseDesc = QString("T-Minus: Start/Pause Timer (%1)").arg(timerLabel).toStdString();
		LoadHotkey(
			m_data.startPauseHotkeyId, startPauseName.c_str(), startPauseDesc.c_str(), this,
			[this]() { ToggleStartPause(); }, "Start/Pause Timer " + idStr, savedData);
	}

	if (m_data.restartHotkeyId == -1) {
		std::string restartName = "TMinus_Restart_" + idStr;
		std::string restartDesc = QString("T-Minus: Restart Timer (%1)").arg(timerLabel).toStdString();
		LoadHotkey(
			m_data.restartHotkeyId, restartName.c_str(), restartDesc.c_str(), this,
			[this]() { RestartTimer(); }, "Restart Timer " + idStr, savedData);
	}

	// Register dynamic add-time hotkeys
	for (auto &entry : m_data.addTimeHotkeys) {
		if (entry.hotkeyId != -1)
			continue;

		if (entry.hotkeyRegistrationName.isEmpty()) {
			entry.hotkeyRegistrationName =
				QString("TMinus_AddTime_%1_%2").arg(m_data.timerId).arg(GenerateUniqueID());
		}

		std::string name = entry.hotkeyRegistrationName.toStdString();
		std::string desc = QString("T-Minus: %1 (%2)").arg(entry.label).arg(timerLabel).toStdString();
		int deltaMs = entry.deltaSeconds * 1000;

		LoadHotkey(
			entry.hotkeyId, name.c_str(), desc.c_str(), this,
			[this, deltaMs]() { m_engine->addTime(deltaMs); }, "Add Time " + entry.label.toStdString(),
			savedData);
	}
}

void TimerWidget::SaveHotkeyBindings(obs_data_t *bindings)
{
	SaveHotkey(bindings, m_data.startPauseHotkeyId, ("TMinus_StartPause_" + m_data.timerId.toStdString()).c_str());
	SaveHotkey(bindings, m_data.restartHotkeyId, ("TMinus_Restart_" + m_data.timerId.toStdString()).c_str());
	for (const auto &entry : m_data.addTimeHotkeys) {
		if (!entry.hotkeyRegistrationName.isEmpty())
			SaveHotkey(bindings, entry.hotkeyId, entry.hotkeyRegistrationName.toUtf8().constData());
	}
}

void TimerWidget::UnregisterHotkeys()
{
	CleanupHotkey(m_data.startPauseHotkeyId);
	m_data.startPauseHotkeyId = -1;
	CleanupHotkey(m_data.restartHotkeyId);
	m_data.restartHotkeyId = -1;

	for (auto &entry : m_data.addTimeHotkeys) {
		CleanupHotkey(entry.hotkeyId);
		entry.hotkeyId = -1;
	}
}

void TimerWidget::StopAll()
{
	m_engine->stop();
	m_actionExecutor->cancel();
}

void TimerWidget::SaveData(obs_data_t *obj)
{
	obs_data_set_string(obj, "timerId", m_data.timerId.toUtf8().constData());
	obs_data_set_string(obj, "displayName", m_data.displayName.toUtf8().constData());
	obs_data_set_int(obj, "timerType", static_cast<int>(m_data.timerType));

	// Countdown duration
	obs_data_set_int(obj, "countdownDays", m_data.countdownDuration.days);
	obs_data_set_int(obj, "countdownHours", m_data.countdownDuration.hours);
	obs_data_set_int(obj, "countdownMinutes", m_data.countdownDuration.minutes);
	obs_data_set_int(obj, "countdownSeconds", m_data.countdownDuration.seconds);

	// TimeUntil target
	obs_data_set_string(obj, "targetDateTime", m_data.targetDateTime.toString(Qt::ISODate).toUtf8().constData());

	// Text source
	obs_data_set_string(obj, "selectedTextSource", m_data.selectedTextSource.toUtf8().constData());

	obs_data_set_string(obj, "finishedText", m_data.finishedText.toUtf8().constData());

	// Stream behavior
	obs_data_set_bool(obj, "startOnStreamStart", m_data.startOnStreamStart);
	obs_data_set_bool(obj, "resetOnStreamStart", m_data.resetOnStreamStart);

	// Display format
	obs_data_set_bool(obj, "autoUnits", m_data.display.autoUnits);
	obs_data_set_bool(obj, "showDays", m_data.display.showDays);
	obs_data_set_bool(obj, "showHours", m_data.display.showHours);
	obs_data_set_bool(obj, "showMinutes", m_data.display.showMinutes);
	obs_data_set_bool(obj, "showSeconds", m_data.display.showSeconds);
	obs_data_set_bool(obj, "showLeadingZero", m_data.display.showLeadingZero);
	obs_data_set_bool(obj, "useFormatString", m_data.display.useFormatString);
	obs_data_set_string(obj, "formatString", m_data.display.formatString.toUtf8().constData());

	// Hotkeys
	SaveHotkey(obj, m_data.startPauseHotkeyId, ("TMinus_StartPause_" + m_data.timerId.toStdString()).c_str());
	SaveHotkey(obj, m_data.restartHotkeyId, ("TMinus_Restart_" + m_data.timerId.toStdString()).c_str());

	// Add-time hotkeys. Key bindings are stored on the timer object keyed
	// by registration name, which is where RegisterHotkeys looks them up.
	obs_data_array_t *addTimeArray = obs_data_array_create();
	for (const auto &entry : m_data.addTimeHotkeys) {
		obs_data_t *entryObj = obs_data_create();
		obs_data_set_string(entryObj, "label", entry.label.toUtf8().constData());
		obs_data_set_int(entryObj, "deltaSeconds", entry.deltaSeconds);
		obs_data_set_string(entryObj, "hotkeyRegName", entry.hotkeyRegistrationName.toUtf8().constData());
		obs_data_array_push_back(addTimeArray, entryObj);
		obs_data_release(entryObj);

		if (!entry.hotkeyRegistrationName.isEmpty())
			SaveHotkey(obj, entry.hotkeyId, entry.hotkeyRegistrationName.toUtf8().constData());
	}
	obs_data_set_array(obj, "addTimeHotkeys", addTimeArray);
	obs_data_array_release(addTimeArray);

	// End actions
	obs_data_array_t *actionsArray = obs_data_array_create();
	for (const auto &step : m_data.endActions) {
		obs_data_t *stepObj = obs_data_create();
		obs_data_set_int(stepObj, "type", static_cast<int>(step.type));
		obs_data_set_string(stepObj, "text", step.text.toUtf8().constData());
		obs_data_set_string(stepObj, "filterSourceName", step.filterSourceName.toUtf8().constData());
		obs_data_set_string(stepObj, "filterName", step.filterName.toUtf8().constData());
		obs_data_set_bool(stepObj, "filterEnabled", step.filterEnabled);
		obs_data_set_string(stepObj, "sceneName", step.sceneName.toUtf8().constData());
		obs_data_set_int(stepObj, "delayMs", step.delayMs);
		obs_data_set_string(stepObj, "globalHotkeyName", step.globalHotkeyName.toUtf8().constData());
		obs_data_set_string(stepObj, "sourceHotkeySourceName",
				    step.sourceHotkeySourceName.toUtf8().constData());
		obs_data_set_string(stepObj, "sourceHotkeyName", step.sourceHotkeyName.toUtf8().constData());
		obs_data_array_push_back(actionsArray, stepObj);
		obs_data_release(stepObj);
	}
	obs_data_set_array(obj, "endActions", actionsArray);
	obs_data_array_release(actionsArray);
}

void TimerWidget::LoadData(obs_data_t *obj)
{
	m_data.timerId = obs_data_get_string(obj, "timerId");
	if (m_data.timerId.isEmpty())
		m_data.timerId = GenerateUniqueID();
	m_data.displayName = obs_data_get_string(obj, "displayName");
	m_data.timerType = static_cast<TimerType>(obs_data_get_int(obj, "timerType"));

	m_data.countdownDuration.days = static_cast<int>(obs_data_get_int(obj, "countdownDays"));
	m_data.countdownDuration.hours = static_cast<int>(obs_data_get_int(obj, "countdownHours"));
	m_data.countdownDuration.minutes = static_cast<int>(obs_data_get_int(obj, "countdownMinutes"));
	m_data.countdownDuration.seconds = static_cast<int>(obs_data_get_int(obj, "countdownSeconds"));

	m_data.targetDateTime = QDateTime::fromString(obs_data_get_string(obj, "targetDateTime"), Qt::ISODate);

	m_data.selectedTextSource = obs_data_get_string(obj, "selectedTextSource");

	m_data.finishedText = obs_data_get_string(obj, "finishedText");

	m_data.startOnStreamStart = obs_data_get_bool(obj, "startOnStreamStart");
	m_data.resetOnStreamStart = obs_data_get_bool(obj, "resetOnStreamStart");

	m_data.display.autoUnits = obs_data_get_bool(obj, "autoUnits");
	m_data.display.showDays = obs_data_get_bool(obj, "showDays");
	m_data.display.showHours = obs_data_get_bool(obj, "showHours");
	m_data.display.showMinutes = obs_data_get_bool(obj, "showMinutes");
	m_data.display.showSeconds = obs_data_get_bool(obj, "showSeconds");
	m_data.display.showLeadingZero = obs_data_get_bool(obj, "showLeadingZero");
	m_data.display.useFormatString = obs_data_get_bool(obj, "useFormatString");
	m_data.display.formatString = obs_data_get_string(obj, "formatString");

	// Add-time hotkeys
	obs_data_array_t *addTimeArray = obs_data_get_array(obj, "addTimeHotkeys");
	if (addTimeArray) {
		size_t count = obs_data_array_count(addTimeArray);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *entryObj = obs_data_array_item(addTimeArray, i);
			AddTimeHotkeyEntry entry;
			entry.label = obs_data_get_string(entryObj, "label");
			entry.deltaSeconds = static_cast<int>(obs_data_get_int(entryObj, "deltaSeconds"));
			entry.hotkeyRegistrationName = obs_data_get_string(entryObj, "hotkeyRegName");
			m_data.addTimeHotkeys.append(entry);
			obs_data_release(entryObj);
		}
		obs_data_array_release(addTimeArray);
	}

	// End actions
	obs_data_array_t *actionsArray = obs_data_get_array(obj, "endActions");
	if (actionsArray) {
		size_t count = obs_data_array_count(actionsArray);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *stepObj = obs_data_array_item(actionsArray, i);
			ActionStep step;
			step.type = static_cast<ActionType>(obs_data_get_int(stepObj, "type"));
			step.text = obs_data_get_string(stepObj, "text");
			step.filterSourceName = obs_data_get_string(stepObj, "filterSourceName");
			step.filterName = obs_data_get_string(stepObj, "filterName");
			step.filterEnabled = obs_data_get_bool(stepObj, "filterEnabled");
			step.sceneName = obs_data_get_string(stepObj, "sceneName");
			step.delayMs = static_cast<int>(obs_data_get_int(stepObj, "delayMs"));
			step.globalHotkeyName = obs_data_get_string(stepObj, "globalHotkeyName");
			step.sourceHotkeySourceName = obs_data_get_string(stepObj, "sourceHotkeySourceName");
			step.sourceHotkeyName = obs_data_get_string(stepObj, "sourceHotkeyName");
			m_data.endActions.append(step);
			obs_data_release(stepObj);
		}
		obs_data_array_release(actionsArray);
	}
}
