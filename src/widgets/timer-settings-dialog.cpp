#include "timer-settings-dialog.hpp"
#include "../core/action-executor.hpp"
#include "../utils/format-utils.hpp"
#include "datetime-picker.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QMenu>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cstring>

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
	setMinimumSize(720, 600);

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
	m_nav->addItems({"Timer", "Output", "Actions", "Hotkeys"});
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
	typeRow->setSpacing(6);
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

	// Time Until: our own date and time picker (calendar, time steppers,
	// presets and a live "in 2h 15m" summary)
	m_timeUntilBox = new QWidget(page);
	auto *timeUntilLayout = new QVBoxLayout(m_timeUntilBox);
	timeUntilLayout->setContentsMargins(0, 0, 0, 0);
	timeUntilLayout->setSpacing(6);
	timeUntilLayout->addWidget(SectionLabel("Counts down to", m_timeUntilBox));
	m_targetPicker = new DateTimePicker(m_timeUntilBox);
	timeUntilLayout->addWidget(m_targetPicker);
	layout->addWidget(m_timeUntilBox);
	layout->addSpacing(10);

	// Overtime: countdown and time-until keep counting past zero
	m_overtimeBox = new QWidget(page);
	auto *overtimeRow = new QHBoxLayout(m_overtimeBox);
	overtimeRow->setContentsMargins(0, 0, 0, 0);
	m_overtimeCheck = new QCheckBox("Keep counting past zero (overtime), shown with", m_overtimeBox);
	overtimeRow->addWidget(m_overtimeCheck);
	m_overtimePrefixEdit = new QLineEdit(m_overtimeBox);
	m_overtimePrefixEdit->setMaxLength(16);
	m_overtimePrefixEdit->setFixedWidth(60);
	m_overtimePrefixEdit->setAccessibleName("Overtime prefix");
	overtimeRow->addWidget(m_overtimePrefixEdit);
	overtimeRow->addWidget(new QLabel("in front", m_overtimeBox));
	overtimeRow->addStretch();
	layout->addWidget(m_overtimeBox);
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
	connect(m_targetPicker, &DateTimePicker::dateTimeChanged, this, &TimerSettingsDialog::UpdatePreview);
	connect(m_overtimePrefixEdit, &QLineEdit::textChanged, this, &TimerSettingsDialog::UpdatePreview);

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
	auto *insertBtn = new QPushButton("Insert", page);
	insertBtn->setAutoDefault(false);
	auto *insertMenu = new QMenu(insertBtn);
	const QList<QPair<QString, QString>> placeholders = {{"%time%", "The time"},
							     {"%name%", "The timer's name"},
							     {"%target%", "The clock time it reaches zero"},
							     {"%percent%", "How far through the countdown (0-100)"}};
	for (const auto &ph : placeholders) {
		insertMenu->addAction(QString("%1  %2").arg(ph.first, ph.second), this, [this, ph]() {
			m_templateEdit->insert(ph.first);
			m_templateEdit->setFocus();
		});
	}
	insertBtn->setMenu(insertMenu);
	templateRow->addWidget(insertBtn);
	layout->addLayout(templateRow);
	layout->addWidget(HintLabel(QString::fromUtf8("For example “Starting in %time%” or “Back at %target%”. "
						      "Leave it as %time% to show just the time."),
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

	auto *fractionRow = new QHBoxLayout();
	fractionRow->addWidget(new QLabel("Fractions of a second", page));
	m_fractionCombo = new QComboBox(page);
	m_fractionCombo->addItems({"None", "Tenths (0:05.3)", "Hundredths (0:05.27)"});
	fractionRow->addWidget(m_fractionCombo);
	fractionRow->addStretch();
	layout->addLayout(fractionRow);
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
	connect(m_fractionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&TimerSettingsDialog::UpdatePreview);
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

	layout->addWidget(SectionLabel("Run steps", page));
	auto *triggerRow = new QHBoxLayout();
	m_triggerCombo = new QComboBox(page);
	m_triggerCombo->setAccessibleName("Trigger");
	triggerRow->addWidget(m_triggerCombo, 1);
	auto *addTriggerBtn = new QPushButton("Add Time Trigger", page);
	addTriggerBtn->setAutoDefault(false);
	triggerRow->addWidget(addTriggerBtn);
	m_removeTriggerBtn = new QPushButton("Remove", page);
	m_removeTriggerBtn->setAutoDefault(false);
	triggerRow->addWidget(m_removeTriggerBtn);
	auto *testBtn = new QPushButton("Test Run", page);
	testBtn->setAutoDefault(false);
	testBtn->setToolTip("Run these steps now, on your live scenes and sources");
	triggerRow->addWidget(testBtn);
	layout->addLayout(triggerRow);

	m_triggerTimeBox = new QWidget(page);
	auto *timeRow = new QHBoxLayout(m_triggerTimeBox);
	timeRow->setContentsMargins(0, 0, 0, 0);
	timeRow->addWidget(new QLabel("At", m_triggerTimeBox));
	m_triggerTime = new QTimeEdit(m_triggerTimeBox);
	m_triggerTime->setDisplayFormat("HH:mm:ss");
	m_triggerTime->setAccessibleName("Trigger time");
	timeRow->addWidget(m_triggerTime);
	timeRow->addStretch();
	layout->addWidget(m_triggerTimeBox);

	layout->addWidget(HintLabel("Steps run top to bottom. Time triggers fire while the timer runs, when the time "
				    "left (or, for stopwatches, the time elapsed) passes their time. Resetting the "
				    "timer cancels steps still in progress. Set Text steps write to the timer's text "
				    "source and support %time%, %name%, %target% and %percent%.",
				    page));
	layout->addSpacing(4);

	// Owned by the dialog, so closing it cancels a test that's mid-Wait
	m_testExecutor = new ActionExecutor(this);
	connect(testBtn, &QPushButton::clicked, this, [this]() {
		m_testExecutor->execute(m_actionList->actions(), m_textSourceCombo->currentData().toString(),
					SampleValues(0));
	});

	m_actionList = new ActionListWidget(page);
	layout->addWidget(m_actionList, 1);

	connect(m_actionList, &ActionListWidget::stepsChanged, this, &TimerSettingsDialog::UpdateNavCounts);
	connect(m_triggerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&TimerSettingsDialog::SelectTrigger);
	connect(addTriggerBtn, &QPushButton::clicked, this, [this]() {
		m_timedGroups.append(TimedActionGroup());
		m_triggerCombo->addItem(TriggerLabel(m_timedGroups.size()));
		m_triggerCombo->setCurrentIndex(m_timedGroups.size());
		UpdateNavCounts();
	});
	connect(m_removeTriggerBtn, &QPushButton::clicked, this, [this]() {
		const int trigger = m_currentTrigger;
		if (trigger == 0)
			return;
		// Switch to the end trigger without storing the removed
		// group's steps back
		m_currentTrigger = 0;
		m_timedGroups.removeAt(trigger - 1);
		m_actionList->setActions(m_endSteps);
		{
			QSignalBlocker blocker(m_triggerCombo);
			m_triggerCombo->removeItem(trigger);
			m_triggerCombo->setCurrentIndex(0);
		}
		SelectTrigger(0);
		UpdateNavCounts();
	});
	connect(m_triggerTime, &QTimeEdit::timeChanged, this, [this](const QTime &time) {
		if (m_currentTrigger == 0)
			return;
		m_timedGroups[m_currentTrigger - 1].atSeconds = QTime(0, 0).secsTo(time);
		m_triggerCombo->setItemText(m_currentTrigger, TriggerLabel(m_currentTrigger));
	});

	return page;
}

QList<ActionStep> TimerSettingsDialog::StepsFor(int trigger) const
{
	if (trigger == m_currentTrigger)
		return m_actionList->actions();
	return trigger == 0 ? m_endSteps : m_timedGroups[trigger - 1].steps;
}

QString TimerSettingsDialog::TriggerLabel(int trigger) const
{
	if (trigger == 0)
		return "When the timer ends";
	DisplayFormat fmt;
	fmt.autoUnits = true;
	return QString("At %1 %2")
		.arg(FormatTimerDisplay(fmt, m_timedGroups[trigger - 1].atSeconds * 1000LL),
		     SelectedType() == TimerType::Stopwatch ? "elapsed" : "left");
}

void TimerSettingsDialog::RefreshTriggerLabels()
{
	for (int i = 0; i < m_triggerCombo->count(); i++)
		m_triggerCombo->setItemText(i, TriggerLabel(i));
}

void TimerSettingsDialog::SelectTrigger(int trigger)
{
	if (trigger < 0)
		return;
	// Store the steps being edited, then show the selected trigger's
	if (m_currentTrigger == 0)
		m_endSteps = m_actionList->actions();
	else if (m_currentTrigger <= m_timedGroups.size())
		m_timedGroups[m_currentTrigger - 1].steps = m_actionList->actions();

	m_currentTrigger = trigger;
	m_actionList->setActions(trigger == 0 ? m_endSteps : m_timedGroups[trigger - 1].steps);
	m_triggerTimeBox->setVisible(trigger > 0);
	m_removeTriggerBtn->setEnabled(trigger > 0);
	if (trigger > 0) {
		QSignalBlocker blocker(m_triggerTime);
		m_triggerTime->setTime(QTime(0, 0).addSecs(m_timedGroups[trigger - 1].atSeconds));
	}
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

	m_overtimeCheck->setChecked(data.overtime);
	m_overtimePrefixEdit->setText(data.display.overtimePrefix);
	m_fractionCombo->setCurrentIndex(qBound(0, data.display.fractionDigits, 2));

	m_endSteps = data.endActions;
	m_timedGroups = data.timedActions;
	{
		QSignalBlocker blocker(m_triggerCombo);
		for (int i = 0; i <= m_timedGroups.size(); i++)
			m_triggerCombo->addItem(TriggerLabel(i));
	}
	m_actionList->setActions(m_endSteps);
	SelectTrigger(0);

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

	QStringList names;
	obs_enum_sources(
		[](void *data, obs_source_t *source) -> bool {
			if (IsTextSource(obs_source_get_unversioned_id(source))) {
				const char *name = obs_source_get_name(source);
				if (name && name[0])
					static_cast<QStringList *>(data)->append(QString::fromUtf8(name));
			}
			return true;
		},
		&names);

	// Natural order ("Text 2" before "Text 10") instead of OBS's internal
	// enumeration order
	NaturalSort(names);
	for (const QString &name : names)
		m_textSourceCombo->addItem(name, name);

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
	fmt.fractionDigits = m_fractionCombo->currentIndex();
	fmt.overtimePrefix = m_overtimePrefixEdit->text();

	const QString tpl = m_templateEdit->text();
	fmt.useFormatString = !tpl.trimmed().isEmpty() && tpl != QStringLiteral("%time%");
	fmt.formatString = fmt.useFormatString ? tpl : QStringLiteral("%time%");
	return fmt;
}

QDateTime TimerSettingsDialog::SelectedTargetDateTime() const
{
	return m_targetPicker->dateTime();
}

void TimerSettingsDialog::SetTargetDateTime(const QDateTime &dt)
{
	m_targetPicker->setDateTime(dt);
	UpdatePreview();
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
	m_overtimeBox->setVisible(type != TimerType::Stopwatch);
	RefreshTriggerLabels();
	UpdatePreview();
}

void TimerSettingsDialog::UpdateUnitsVisibility()
{
	m_customUnitsBox->setVisible(m_unitsCombo->currentData().toInt() == UnitsCustom);
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

	m_previewLabel->setText(ExpandPlaceholders(TemplateText(SelectedFormat()), SampleValues(ms)));

	const QString source = m_textSourceCombo->currentData().toString();
	QString where = source.isEmpty() ? QString("Not shown on stream") : QString::fromUtf8("→ %1").arg(source);
	const QString finished = m_finishedTextEdit->text();
	if (!source.isEmpty() && !finished.isEmpty())
		where += QString::fromUtf8(" · then “%1”").arg(finished);
	m_previewSourceLabel->setText(where);
}

QHash<QString, QString> TimerSettingsDialog::SampleValues(long long ms) const
{
	const TimerType type = SelectedType();
	const QString name = m_nameEdit->text().trimmed();
	return {{"time", FormatTimerDisplay(SelectedFormat(), ms)},
		{"name", name.isEmpty() ? m_timerLabel : name},
		{"target",
		 type == TimerType::Stopwatch
			 ? QString()
			 : QLocale().toString(QDateTime::currentDateTime().addMSecs(ms).time(), QLocale::ShortFormat)},
		{"percent", type == TimerType::Countdown ? QStringLiteral("0") : QString()}};
}

void TimerSettingsDialog::UpdateNavCounts()
{
	auto label = [](const QString &name, int count) {
		return count > 0 ? QString("%1 (%2)").arg(name).arg(count) : name;
	};
	int steps = 0;
	for (int i = 0; i < m_triggerCombo->count(); i++)
		steps += StepsFor(i).size();
	m_nav->item(FinishPage)->setText(label("Actions", steps));
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

	data.overtime = m_overtimeCheck->isChecked();
	data.endActions = StepsFor(0);
	data.timedActions = m_timedGroups;
	for (int i = 0; i < m_timedGroups.size(); i++)
		data.timedActions[i].steps = StepsFor(i + 1);

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
