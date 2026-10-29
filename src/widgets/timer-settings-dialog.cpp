#include "timer-settings-dialog.hpp"
#include "../core/action-executor.hpp"
#include "../utils/format-utils.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cstring>
#include <functional>

namespace {

enum Page { TimerPage = 0, OutputPage, FinishPage, HotkeysPage };

// "When streaming starts" choices, mapped onto the two stored flags
enum StreamStart { StreamNothing = 0, StreamStart, StreamRestart, StreamReset };

// Time format presets; Custom exposes the individual unit checkboxes
enum Units { UnitsAuto = 0, UnitsHMS, UnitsMS, UnitsDHMS, UnitsSeconds, UnitsCustom };

const char *kMutedStyle = "color: rgba(127, 127, 127, 100%);";

QLabel *SectionLabel(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setStyleSheet("font-weight: bold;");
	return label;
}

QLabel *HintLabel(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setWordWrap(true);
	label->setStyleSheet(kMutedStyle);
	return label;
}

// Wraps a page in a frameless scroll area so tall content (the calendar)
// never forces the dialog taller than the screen.
QWidget *Scrollable(QWidget *content)
{
	auto *scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->viewport()->setAutoFillBackground(false);
	content->setAutoFillBackground(false);
	scroll->setWidget(content);
	return scroll;
}

bool IsTextSource(const char *id)
{
	return id && (strcmp(id, "text_gdiplus") == 0 || strcmp(id, "text_gdiplus_v2") == 0 ||
		      strcmp(id, "text_gdiplus_v3") == 0 || strcmp(id, "text_ft2_source") == 0 ||
		      strcmp(id, "text_ft2_source_v2") == 0);
}

QString DefaultAddTimeLabel(int seconds)
{
	return QString("%1%2s").arg(seconds >= 0 ? "+" : "").arg(seconds);
}

} // namespace

TimerSettingsDialog::TimerSettingsDialog(const TimerData &data, QWidget *parent) : QDialog(parent)
{
	m_timerLabel = data.displayName.isEmpty() ? QString("Timer") : data.displayName;
	setWindowTitle(QString::fromUtf8("Edit Timer — %1").arg(m_timerLabel));
	setMinimumSize(720, 560);

	SetupUI();
	LoadFromData(data);

	// Editing a timer mid-run keeps its current run; say so up front
	m_inUseNote->setVisible(data.state == TimerState::Running || data.state == TimerState::Paused);

	UpdateTypeVisibility();
	UpdateUnitsVisibility();
	UpdateNavCounts();
	UpdatePreview();
}

void TimerSettingsDialog::SetupUI()
{
	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 8);
	mainLayout->setSpacing(0);

	auto *body = new QHBoxLayout();
	body->setContentsMargins(0, 0, 0, 0);
	body->setSpacing(0);

	m_nav = new QListWidget(this);
	m_nav->setFixedWidth(160);
	m_nav->setFrameShape(QFrame::NoFrame);
	m_nav->setSpacing(2);
	m_nav->addItems({"Timer", "Output", "On finish", "Hotkeys"});
	body->addWidget(m_nav);

	auto *divider = new QFrame(this);
	divider->setFrameShape(QFrame::VLine);
	divider->setFrameShadow(QFrame::Plain);
	divider->setStyleSheet("color: rgba(127, 127, 127, 25%);");
	body->addWidget(divider);

	auto *right = new QVBoxLayout();
	right->setContentsMargins(0, 0, 0, 0);
	right->setSpacing(0);
	right->addWidget(CreatePreview());

	m_pages = new QStackedWidget(this);
	m_pages->addWidget(CreateTimerPage());
	m_pages->addWidget(CreateOutputPage());
	m_pages->addWidget(CreateFinishPage());
	m_pages->addWidget(CreateHotkeysPage());
	right->addWidget(m_pages, 1);
	body->addLayout(right, 1);

	mainLayout->addLayout(body, 1);

	auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	buttonBox->setContentsMargins(12, 8, 12, 0);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	mainLayout->addWidget(buttonBox);

	connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
	m_nav->setCurrentRow(TimerPage);
}

QWidget *TimerSettingsDialog::CreatePreview()
{
	auto *box = new QFrame(this);
	box->setObjectName("previewBox");
	box->setStyleSheet("QFrame#previewBox { background-color: rgba(127, 127, 127, 10%); "
			   "border-bottom: 1px solid rgba(127, 127, 127, 25%); }");

	auto *layout = new QHBoxLayout(box);
	layout->setContentsMargins(16, 10, 16, 10);
	layout->setSpacing(12);

	auto *caption = new QLabel("PREVIEW", box);
	caption->setStyleSheet(QString("font-size: 10px; font-weight: bold; %1").arg(kMutedStyle));
	layout->addWidget(caption);

	m_previewLabel = new QLabel(box);
	m_previewLabel->setStyleSheet("font-size: 20px; font-weight: bold;");
	m_previewLabel->setTextInteractionFlags(Qt::NoTextInteraction);
	layout->addWidget(m_previewLabel, 1);

	m_previewSourceLabel = new QLabel(box);
	m_previewSourceLabel->setStyleSheet(kMutedStyle);
	layout->addWidget(m_previewSourceLabel);

	return box;
}

QWidget *TimerSettingsDialog::CreateTimerPage()
{
	auto *page = new QWidget();
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(16, 14, 16, 14);
	layout->setSpacing(6);

	m_inUseNote = HintLabel("This timer is in use. Changes to its type, length or target date apply when you "
				"reset it.",
				page);
	m_inUseNote->setStyleSheet("color: #d9a441;");
	m_inUseNote->hide();
	layout->addWidget(m_inUseNote);

	// Name
	layout->addWidget(SectionLabel("Name", page));
	m_nameEdit = new QLineEdit(page);
	m_nameEdit->setPlaceholderText("Leave blank to show the timer type");
	m_nameEdit->setMaxLength(64);
	layout->addWidget(m_nameEdit);
	layout->addSpacing(10);

	// Type, as a segmented control
	layout->addWidget(SectionLabel("Type", page));
	auto *typeRow = new QHBoxLayout();
	typeRow->setSpacing(0);
	m_typeGroup = new QButtonGroup(this);
	m_typeGroup->setExclusive(true);
	const QList<QPair<TimerType, QString>> types = {{TimerType::Countdown, "Countdown"},
							{TimerType::Stopwatch, "Stopwatch"},
							{TimerType::TimeUntil, "Time Until"}};
	for (const auto &type : types) {
		auto *btn = new QPushButton(type.second, page);
		btn->setCheckable(true);
		btn->setAutoDefault(false);
		btn->setMinimumHeight(28);
		m_typeGroup->addButton(btn, static_cast<int>(type.first));
		typeRow->addWidget(btn, 1);
	}
	layout->addLayout(typeRow);
	layout->addSpacing(10);

	// Countdown duration with presets
	m_countdownBox = new QWidget(page);
	auto *countdownLayout = new QVBoxLayout(m_countdownBox);
	countdownLayout->setContentsMargins(0, 0, 0, 0);
	countdownLayout->setSpacing(6);
	countdownLayout->addWidget(SectionLabel("Duration", m_countdownBox));

	auto *durationRow = new QHBoxLayout();
	auto addUnit = [this, durationRow](QSpinBox *&spin, int max, const QString &suffix) {
		spin = new QSpinBox(m_countdownBox);
		spin->setRange(0, max);
		spin->setSuffix(suffix);
		spin->setMinimumWidth(70);
		durationRow->addWidget(spin);
	};
	addUnit(m_daysSpin, 999, " d");
	addUnit(m_hoursSpin, 23, " h");
	addUnit(m_minutesSpin, 59, " m");
	addUnit(m_secondsSpin, 59, " s");
	durationRow->addStretch();
	countdownLayout->addLayout(durationRow);

	auto *presetRow = new QHBoxLayout();
	presetRow->setSpacing(4);
	for (int minutes : {1, 5, 10, 15, 30, 60}) {
		auto *chip = new QPushButton(minutes == 60 ? QString("1 hour") : QString("%1 min").arg(minutes),
					     m_countdownBox);
		chip->setAutoDefault(false);
		connect(chip, &QPushButton::clicked, this,
			[this, minutes]() { SetDuration(minutes / 60, minutes % 60, 0); });
		presetRow->addWidget(chip);
	}
	presetRow->addStretch();
	countdownLayout->addLayout(presetRow);
	layout->addWidget(m_countdownBox);

	// Time Until: calendar, time of day, quick presets and a summary of
	// how long the countdown will run
	m_timeUntilBox = new QWidget(page);
	auto *timeUntilLayout = new QVBoxLayout(m_timeUntilBox);
	timeUntilLayout->setContentsMargins(0, 0, 0, 0);
	timeUntilLayout->setSpacing(6);
	timeUntilLayout->addWidget(SectionLabel("Target date and time", m_timeUntilBox));

	m_calendar = new QCalendarWidget(m_timeUntilBox);
	m_calendar->setGridVisible(false);
	m_calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
	m_calendar->setMinimumDate(QDate::currentDate());
	m_calendar->setMaximumHeight(220);
	timeUntilLayout->addWidget(m_calendar);

	auto *timeRow = new QHBoxLayout();
	timeRow->addWidget(new QLabel("Time of day", m_timeUntilBox));
	m_targetTimeEdit = new QTimeEdit(m_timeUntilBox);
	const bool ampm = QLocale().timeFormat(QLocale::ShortFormat).contains(QLatin1Char('A'), Qt::CaseInsensitive);
	m_targetTimeEdit->setDisplayFormat(ampm ? "h:mm:ss AP" : "HH:mm:ss");
	timeRow->addWidget(m_targetTimeEdit, 1);
	timeUntilLayout->addLayout(timeRow);

	auto *targetPresetRow = new QHBoxLayout();
	targetPresetRow->setSpacing(4);
	auto addTargetPreset = [this, targetPresetRow](const QString &text, std::function<QDateTime()> calc) {
		auto *btn = new QPushButton(text, m_timeUntilBox);
		btn->setAutoDefault(false);
		connect(btn, &QPushButton::clicked, this, [this, calc]() { SetTargetDateTime(calc()); });
		targetPresetRow->addWidget(btn);
	};
	addTargetPreset("+15 min", []() { return QDateTime::currentDateTime().addSecs(15 * 60); });
	addTargetPreset("+1 hour", []() { return QDateTime::currentDateTime().addSecs(3600); });
	addTargetPreset("Next hour", []() {
		QDateTime now = QDateTime::currentDateTime();
		return QDateTime(now.date(), QTime(now.time().hour(), 0)).addSecs(3600);
	});
	addTargetPreset("Midnight", []() { return QDateTime(QDate::currentDate().addDays(1), QTime(0, 0)); });
	targetPresetRow->addStretch();
	timeUntilLayout->addLayout(targetPresetRow);

	m_targetSummaryLabel = new QLabel(m_timeUntilBox);
	m_targetSummaryLabel->setWordWrap(true);
	timeUntilLayout->addWidget(m_targetSummaryLabel);
	layout->addWidget(m_timeUntilBox);
	layout->addSpacing(10);

	// Stream start behaviour: one choice instead of two checkboxes
	layout->addWidget(SectionLabel("When streaming starts", page));
	m_streamStartCombo = new QComboBox(page);
	m_streamStartCombo->addItem("Do nothing", StreamNothing);
	m_streamStartCombo->addItem("Start (or resume) the timer", StreamStart);
	m_streamStartCombo->addItem("Restart the timer from the beginning", StreamRestart);
	m_streamStartCombo->addItem("Reset the timer and wait", StreamReset);
	layout->addWidget(m_streamStartCombo);

	layout->addStretch();

	connect(m_typeGroup, &QButtonGroup::idClicked, this, &TimerSettingsDialog::UpdateTypeVisibility);
	for (QSpinBox *spin : {m_daysSpin, m_hoursSpin, m_minutesSpin, m_secondsSpin})
		connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &TimerSettingsDialog::UpdatePreview);
	connect(m_calendar, &QCalendarWidget::selectionChanged, this, &TimerSettingsDialog::UpdateTargetSummary);
	connect(m_targetTimeEdit, &QTimeEdit::timeChanged, this, &TimerSettingsDialog::UpdateTargetSummary);

	return Scrollable(page);
}

QWidget *TimerSettingsDialog::CreateOutputPage()
{
	auto *page = new QWidget();
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(16, 14, 16, 14);
	layout->setSpacing(6);

	layout->addWidget(SectionLabel("Text source", page));
	m_textSourceCombo = new QComboBox(page);
	layout->addWidget(m_textSourceCombo);
	layout->addWidget(HintLabel("The timer writes its text into this source every second.", page));
	layout->addSpacing(10);

	layout->addWidget(SectionLabel("Text", page));
	auto *templateRow = new QHBoxLayout();
	m_templateEdit = new QLineEdit(page);
	m_templateEdit->setPlaceholderText("%time%");
	templateRow->addWidget(m_templateEdit, 1);
	auto *insertTime = new QPushButton("Insert %time%", page);
	insertTime->setAutoDefault(false);
	connect(insertTime, &QPushButton::clicked, this, [this]() {
		m_templateEdit->insert("%time%");
		m_templateEdit->setFocus();
	});
	templateRow->addWidget(insertTime);
	layout->addLayout(templateRow);
	layout->addWidget(HintLabel("For example “Starting in %time%”. Leave it as %time% to show just the "
				    "time.",
				    page));
	layout->addSpacing(10);

	layout->addWidget(SectionLabel("Time format", page));
	m_unitsCombo = new QComboBox(page);
	m_unitsCombo->addItem("Auto (hide hours and days while they're zero)", UnitsAuto);
	m_unitsCombo->addItem("H:MM:SS", UnitsHMS);
	m_unitsCombo->addItem("MM:SS", UnitsMS);
	m_unitsCombo->addItem("D:HH:MM:SS", UnitsDHMS);
	m_unitsCombo->addItem("Seconds only", UnitsSeconds);
	m_unitsCombo->addItem("Custom", UnitsCustom);
	layout->addWidget(m_unitsCombo);

	m_customUnitsBox = new QWidget(page);
	auto *unitsRow = new QHBoxLayout(m_customUnitsBox);
	unitsRow->setContentsMargins(0, 0, 0, 0);
	m_showDaysCheck = new QCheckBox("Days", m_customUnitsBox);
	m_showHoursCheck = new QCheckBox("Hours", m_customUnitsBox);
	m_showMinutesCheck = new QCheckBox("Minutes", m_customUnitsBox);
	m_showSecondsCheck = new QCheckBox("Seconds", m_customUnitsBox);
	for (QCheckBox *check : {m_showDaysCheck, m_showHoursCheck, m_showMinutesCheck, m_showSecondsCheck})
		unitsRow->addWidget(check);
	unitsRow->addStretch();
	layout->addWidget(m_customUnitsBox);
	layout->addWidget(HintLabel("Larger units that are hidden carry into the first one shown, so an hour and "
				    "five minutes in MM:SS reads 65:00.",
				    page));

	m_leadingZeroCheck = new QCheckBox("Pad the first unit (05:00 instead of 5:00)", page);
	layout->addWidget(m_leadingZeroCheck);
	layout->addSpacing(10);

	layout->addWidget(SectionLabel("When finished, show", page));
	m_finishedTextEdit = new QLineEdit(page);
	m_finishedTextEdit->setPlaceholderText("e.g. We're live!");
	m_finishedTextEdit->setMaxLength(256);
	layout->addWidget(m_finishedTextEdit);
	layout->addWidget(HintLabel("Replaces the time in the text source once the timer ends, until it's reset. "
				    "Leave empty to keep showing the final time.",
				    page));

	layout->addStretch();

	connect(m_textSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&TimerSettingsDialog::UpdatePreview);
	connect(m_templateEdit, &QLineEdit::textChanged, this, &TimerSettingsDialog::UpdatePreview);
	connect(m_finishedTextEdit, &QLineEdit::textChanged, this, &TimerSettingsDialog::UpdatePreview);
	connect(m_unitsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&TimerSettingsDialog::UpdateUnitsVisibility);
	for (QCheckBox *check :
	     {m_showDaysCheck, m_showHoursCheck, m_showMinutesCheck, m_showSecondsCheck, m_leadingZeroCheck})
		connect(check, &QCheckBox::toggled, this, &TimerSettingsDialog::UpdatePreview);

	return Scrollable(page);
}

QWidget *TimerSettingsDialog::CreateFinishPage()
{
	auto *page = new QWidget();
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(16, 14, 16, 14);
	layout->setSpacing(6);

	auto *header = new QHBoxLayout();
	auto *intro = new QVBoxLayout();
	intro->setSpacing(4);
	intro->addWidget(SectionLabel("When the timer reaches zero", page));
	intro->addWidget(HintLabel("Steps run top to bottom. Resetting or restarting the timer cancels a run that "
				   "is still in progress. Set Text steps write to the timer's text source and "
				   "support %time%.",
				   page));
	header->addLayout(intro, 1);

	auto *testBtn = new QPushButton("Test Run", page);
	testBtn->setAutoDefault(false);
	testBtn->setToolTip("Run these steps now, on your live scenes and sources");
	header->addWidget(testBtn, 0, Qt::AlignTop);
	layout->addLayout(header);
	layout->addSpacing(4);

	// Owned by the dialog, so closing it cancels a test that's mid-Wait
	m_testExecutor = new ActionExecutor(this);
	connect(testBtn, &QPushButton::clicked, this, [this]() {
		DisplayFormat timeOnly = SelectedFormat();
		timeOnly.useFormatString = false;
		m_testExecutor->execute(m_actionList->actions(), m_textSourceCombo->currentData().toString(),
					FormatTimerDisplay(timeOnly, 0));
	});

	m_actionList = new ActionListWidget(page);
	layout->addWidget(m_actionList, 1);

	connect(m_actionList, &ActionListWidget::stepsChanged, this, &TimerSettingsDialog::UpdateNavCounts);

	return page;
}

QWidget *TimerSettingsDialog::CreateHotkeysPage()
{
	auto *page = new QWidget();
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(16, 14, 16, 14);
	layout->setSpacing(6);

	layout->addWidget(SectionLabel("Add-time amounts", page));
	layout->addWidget(HintLabel("Each amount gets a hotkey that adds (or, if negative, subtracts) time. The first "
				    "three also appear as buttons on the timer in the dock.",
				    page));

	m_addTimeTable = new QTableWidget(0, 2, page);
	m_addTimeTable->setHorizontalHeaderLabels({"Label", "Seconds"});
	m_addTimeTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_addTimeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
	m_addTimeTable->setColumnWidth(1, 110);
	m_addTimeTable->verticalHeader()->setVisible(false);
	m_addTimeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_addTimeTable->setSelectionMode(QAbstractItemView::SingleSelection);
	layout->addWidget(m_addTimeTable, 1);

	auto *buttons = new QHBoxLayout();
	auto *addBtn = new QPushButton("Add Amount", page);
	addBtn->setAutoDefault(false);
	auto *removeBtn = new QPushButton("Remove", page);
	removeBtn->setAutoDefault(false);
	buttons->addWidget(addBtn);
	buttons->addWidget(removeBtn);
	buttons->addStretch();
	layout->addLayout(buttons);

	layout->addSpacing(10);
	layout->addWidget(SectionLabel("Key bindings", page));
	layout->addWidget(HintLabel(QString("Bind keys in OBS Settings › Hotkeys. This timer's entries are "
					    "listed as “T-Minus: … (%1)”, including Start/Pause and "
					    "Restart.")
					    .arg(m_timerLabel),
				    page));

	connect(addBtn, &QPushButton::clicked, this, [this]() {
		AddTimeHotkeyEntry entry;
		entry.deltaSeconds = 30;
		m_addTimeEntries.append(entry);
		AddAddTimeRow(entry);
		m_addTimeTable->setCurrentCell(m_addTimeTable->rowCount() - 1, 0);
		m_addTimeTable->editItem(m_addTimeTable->item(m_addTimeTable->rowCount() - 1, 0));
		UpdateNavCounts();
	});
	connect(removeBtn, &QPushButton::clicked, this, [this]() {
		const int row = m_addTimeTable->currentRow();
		if (row < 0 || row >= m_addTimeEntries.size())
			return;
		m_addTimeEntries.removeAt(row);
		m_addTimeTable->removeRow(row);
		UpdateNavCounts();
	});

	return page;
}

void TimerSettingsDialog::AddAddTimeRow(const AddTimeHotkeyEntry &entry)
{
	const int row = m_addTimeTable->rowCount();
	m_addTimeTable->insertRow(row);

	auto *labelItem = new QTableWidgetItem(entry.label);
	labelItem->setToolTip("Leave blank to name it after the amount");
	m_addTimeTable->setItem(row, 0, labelItem);

	auto *spin = new QSpinBox(m_addTimeTable);
	spin->setRange(-86400, 86400);
	spin->setValue(entry.deltaSeconds);
	spin->setFrame(false);
	m_addTimeTable->setCellWidget(row, 1, spin);
}

void TimerSettingsDialog::LoadFromData(const TimerData &data)
{
	m_nameEdit->setText(data.displayName);

	if (QAbstractButton *btn = m_typeGroup->button(static_cast<int>(data.timerType)))
		btn->setChecked(true);

	m_daysSpin->setValue(data.countdownDuration.days);
	m_hoursSpin->setValue(data.countdownDuration.hours);
	m_minutesSpin->setValue(data.countdownDuration.minutes);
	m_secondsSpin->setValue(data.countdownDuration.seconds);

	SetTargetDateTime(data.targetDateTime.isValid() ? data.targetDateTime
							: QDateTime::currentDateTime().addSecs(3600));

	int streamStart = StreamNothing;
	if (data.startOnStreamStart && data.resetOnStreamStart)
		streamStart = StreamRestart;
	else if (data.startOnStreamStart)
		streamStart = StreamStart;
	else if (data.resetOnStreamStart)
		streamStart = StreamReset;
	m_streamStartCombo->setCurrentIndex(m_streamStartCombo->findData(streamStart));

	PopulateTextSourceDropdown(data.selectedTextSource);

	m_templateEdit->setText(data.display.useFormatString && !data.display.formatString.isEmpty()
					? data.display.formatString
					: QStringLiteral("%time%"));

	m_showDaysCheck->setChecked(data.display.showDays);
	m_showHoursCheck->setChecked(data.display.showHours);
	m_showMinutesCheck->setChecked(data.display.showMinutes);
	m_showSecondsCheck->setChecked(data.display.showSeconds);
	m_leadingZeroCheck->setChecked(data.display.showLeadingZero);

	m_finishedTextEdit->setText(data.finishedText);

	const DisplayFormat &d = data.display;
	int units = UnitsCustom;
	if (d.autoUnits)
		units = UnitsAuto;
	else if (!d.showDays && d.showHours && d.showMinutes && d.showSeconds)
		units = UnitsHMS;
	else if (!d.showDays && !d.showHours && d.showMinutes && d.showSeconds)
		units = UnitsMS;
	else if (d.showDays && d.showHours && d.showMinutes && d.showSeconds)
		units = UnitsDHMS;
	else if (!d.showDays && !d.showHours && !d.showMinutes && d.showSeconds)
		units = UnitsSeconds;
	m_unitsCombo->setCurrentIndex(m_unitsCombo->findData(units));

	m_actionList->setActions(data.endActions);

	m_addTimeEntries = data.addTimeHotkeys;
	for (const auto &entry : m_addTimeEntries)
		AddAddTimeRow(entry);
}

void TimerSettingsDialog::PopulateTextSourceDropdown(const QString &selected)
{
	m_textSourceCombo->clear();
	// The empty item data means "no source", so a source that happens to
	// be named "(None)" still works
	m_textSourceCombo->addItem("(None)", QString());

	obs_enum_sources(
		[](void *data, obs_source_t *source) -> bool {
			auto *combo = static_cast<QComboBox *>(data);
			if (IsTextSource(obs_source_get_unversioned_id(source))) {
				const char *name = obs_source_get_name(source);
				if (name && name[0])
					combo->addItem(QString::fromUtf8(name), QString::fromUtf8(name));
			}
			return true;
		},
		m_textSourceCombo);

	if (selected.isEmpty())
		return;

	int idx = m_textSourceCombo->findData(selected);
	if (idx < 0) {
		// Keep a link to a source that's missing right now rather than
		// silently dropping it on save
		m_textSourceCombo->addItem(QString::fromUtf8("%1 (not found)").arg(selected), selected);
		idx = m_textSourceCombo->count() - 1;
	}
	m_textSourceCombo->setCurrentIndex(idx);
}

TimerType TimerSettingsDialog::SelectedType() const
{
	const int id = m_typeGroup->checkedId();
	return id < 0 ? TimerType::Countdown : static_cast<TimerType>(id);
}

DurationValue TimerSettingsDialog::SelectedDuration() const
{
	return {m_daysSpin->value(), m_hoursSpin->value(), m_minutesSpin->value(), m_secondsSpin->value()};
}

DisplayFormat TimerSettingsDialog::SelectedFormat() const
{
	DisplayFormat fmt;

	switch (m_unitsCombo->currentData().toInt()) {
	case UnitsAuto:
		fmt.autoUnits = true;
		break;
	case UnitsHMS:
		fmt.showDays = false;
		fmt.showHours = fmt.showMinutes = fmt.showSeconds = true;
		break;
	case UnitsMS:
		fmt.showDays = fmt.showHours = false;
		fmt.showMinutes = fmt.showSeconds = true;
		break;
	case UnitsDHMS:
		fmt.showDays = fmt.showHours = fmt.showMinutes = fmt.showSeconds = true;
		break;
	case UnitsSeconds:
		fmt.showDays = fmt.showHours = fmt.showMinutes = false;
		fmt.showSeconds = true;
		break;
	default:
		fmt.showDays = m_showDaysCheck->isChecked();
		fmt.showHours = m_showHoursCheck->isChecked();
		fmt.showMinutes = m_showMinutesCheck->isChecked();
		fmt.showSeconds = m_showSecondsCheck->isChecked();
		break;
	}

	fmt.showLeadingZero = m_leadingZeroCheck->isChecked();

	const QString tpl = m_templateEdit->text();
	fmt.useFormatString = !tpl.trimmed().isEmpty() && tpl != QStringLiteral("%time%");
	fmt.formatString = fmt.useFormatString ? tpl : QStringLiteral("%time%");
	return fmt;
}

QDateTime TimerSettingsDialog::SelectedTargetDateTime() const
{
	return QDateTime(m_calendar->selectedDate(), m_targetTimeEdit->time());
}

void TimerSettingsDialog::SetTargetDateTime(const QDateTime &dt)
{
	m_calendar->setSelectedDate(dt.date());
	m_targetTimeEdit->setTime(dt.time());
	UpdateTargetSummary();
}

void TimerSettingsDialog::SetDuration(int hours, int minutes, int seconds)
{
	m_daysSpin->setValue(0);
	m_hoursSpin->setValue(hours);
	m_minutesSpin->setValue(minutes);
	m_secondsSpin->setValue(seconds);
}

void TimerSettingsDialog::UpdateTypeVisibility()
{
	const TimerType type = SelectedType();
	m_countdownBox->setVisible(type == TimerType::Countdown);
	m_timeUntilBox->setVisible(type == TimerType::TimeUntil);
	UpdatePreview();
}

void TimerSettingsDialog::UpdateUnitsVisibility()
{
	m_customUnitsBox->setVisible(m_unitsCombo->currentData().toInt() == UnitsCustom);
	UpdatePreview();
}

void TimerSettingsDialog::UpdateTargetSummary()
{
	const QDateTime target = SelectedTargetDateTime();
	const qint64 secs = QDateTime::currentDateTime().secsTo(target);

	if (secs <= 0) {
		m_targetSummaryLabel->setStyleSheet("color: #d9534f;");
		m_targetSummaryLabel->setText("The selected time is in the past — the timer will read zero.");
	} else {
		const qint64 days = secs / 86400;
		const qint64 hours = (secs % 86400) / 3600;
		const qint64 minutes = (secs % 3600) / 60;

		QStringList parts;
		if (days > 0)
			parts << QString("%1d").arg(days);
		if (hours > 0 || days > 0)
			parts << QString("%1h").arg(hours);
		parts << QString("%1m").arg(minutes);

		m_targetSummaryLabel->setStyleSheet(kMutedStyle);
		m_targetSummaryLabel->setText(QString("Counts down for %1 (until %2)")
						      .arg(parts.join(" "))
						      .arg(QLocale().toString(target, QLocale::ShortFormat)));
	}

	UpdatePreview();
}

void TimerSettingsDialog::UpdatePreview()
{
	long long ms = 0;
	switch (SelectedType()) {
	case TimerType::Countdown:
		ms = DurationToMs(SelectedDuration());
		break;
	case TimerType::Stopwatch:
		ms = 0;
		break;
	case TimerType::TimeUntil:
		ms = qMax(0LL, static_cast<long long>(QDateTime::currentDateTime().msecsTo(SelectedTargetDateTime())));
		break;
	}

	m_previewLabel->setText(FormatTimerDisplay(SelectedFormat(), ms));

	const QString source = m_textSourceCombo->currentData().toString();
	QString where = source.isEmpty() ? QString("Not shown on stream") : QString::fromUtf8("→ %1").arg(source);
	const QString finished = m_finishedTextEdit->text();
	if (!source.isEmpty() && !finished.isEmpty())
		where += QString::fromUtf8(" · then “%1”").arg(finished);
	m_previewSourceLabel->setText(where);
}

void TimerSettingsDialog::UpdateNavCounts()
{
	auto label = [](const QString &name, int count) {
		return count > 0 ? QString("%1 (%2)").arg(name).arg(count) : name;
	};
	m_nav->item(FinishPage)->setText(label("On finish", m_actionList->actions().size()));
	m_nav->item(HotkeysPage)->setText(label("Hotkeys", m_addTimeEntries.size()));
}

void TimerSettingsDialog::ApplyToData(TimerData &data) const
{
	data.displayName = m_nameEdit->text().trimmed();
	data.timerType = SelectedType();
	data.countdownDuration = SelectedDuration();
	data.targetDateTime = SelectedTargetDateTime();

	const int streamStart = m_streamStartCombo->currentData().toInt();
	data.startOnStreamStart = streamStart == StreamStart || streamStart == StreamRestart;
	data.resetOnStreamStart = streamStart == StreamRestart || streamStart == StreamReset;

	data.selectedTextSource = m_textSourceCombo->currentData().toString();
	data.display = SelectedFormat();
	data.finishedText = m_finishedTextEdit->text();

	data.endActions = m_actionList->actions();

	// Entries carry their registration names over so key bindings are
	// kept; the caller re-registers every add-time hotkey afterwards.
	QList<AddTimeHotkeyEntry> entries = m_addTimeEntries;
	for (int row = 0; row < entries.size(); row++) {
		auto &entry = entries[row];
		auto *spin = qobject_cast<QSpinBox *>(m_addTimeTable->cellWidget(row, 1));
		const QTableWidgetItem *labelItem = m_addTimeTable->item(row, 0);

		entry.deltaSeconds = spin ? spin->value() : entry.deltaSeconds;
		entry.label = labelItem ? labelItem->text().trimmed() : QString();
		if (entry.label.isEmpty())
			entry.label = DefaultAddTimeLabel(entry.deltaSeconds);
		entry.hotkeyId = -1;
	}
	data.addTimeHotkeys = entries;
}
