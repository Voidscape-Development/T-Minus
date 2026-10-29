#include "action-list-widget.hpp"
#include "drag-reorder.hpp"
#include "button-style.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QMenu>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>

namespace {

// The hotkey combos display the translated hotkey description
// (obs_hotkey_get_description) while carrying the registration name in the
// item data; the registration name is what gets saved and triggered.

QString HotkeyComboValue(const QComboBox *combo)
{
	int idx = combo->currentIndex();
	if (idx >= 0 && combo->currentText() == combo->itemText(idx))
		return combo->itemData(idx).toString();
	// Manually typed registration name
	return combo->currentText();
}

// Returns false when name isn't a registered hotkey right now; it is then
// kept as typed text so the step still saves it.
bool SetHotkeyComboValue(QComboBox *combo, const QString &name)
{
	int idx = combo->findData(name);
	if (idx >= 0) {
		combo->setCurrentIndex(idx);
		return true;
	}
	combo->setEditText(name);
	return false;
}

void AddHotkeyComboItem(QComboBox *combo, obs_hotkey_t *hotkey)
{
	const char *name = obs_hotkey_get_name(hotkey);
	if (!name || !name[0])
		return;
	const char *desc = obs_hotkey_get_description(hotkey);
	combo->addItem((desc && desc[0]) ? QString::fromUtf8(desc) : QString::fromUtf8(name), QString::fromUtf8(name));
}

void PopulateScenes(QComboBox *combo)
{
	combo->clear();
	struct obs_frontend_source_list sceneList = {};
	obs_frontend_get_scenes(&sceneList);
	for (size_t i = 0; i < sceneList.sources.num; i++) {
		const char *name = obs_source_get_name(sceneList.sources.array[i]);
		if (name)
			combo->addItem(QString::fromUtf8(name));
	}
	obs_frontend_source_list_free(&sceneList);
}

// Every source plus the scenes (which can carry filters and hotkeys too)
void PopulateSources(QComboBox *combo)
{
	combo->clear();
	obs_enum_sources(
		[](void *data, obs_source_t *source) -> bool {
			auto *combo = static_cast<QComboBox *>(data);
			const char *name = obs_source_get_name(source);
			if (name && name[0])
				combo->addItem(QString::fromUtf8(name));
			return true;
		},
		combo);

	struct obs_frontend_source_list sceneList = {};
	obs_frontend_get_scenes(&sceneList);
	for (size_t i = 0; i < sceneList.sources.num; i++) {
		const char *name = obs_source_get_name(sceneList.sources.array[i]);
		if (name && combo->findText(QString::fromUtf8(name)) < 0)
			combo->addItem(QString::fromUtf8(name));
	}
	obs_frontend_source_list_free(&sceneList);
}

void PopulateFilters(QComboBox *combo, const QString &sourceName)
{
	combo->clear();
	if (sourceName.isEmpty())
		return;

	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source)
		return;

	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *data) {
			static_cast<QComboBox *>(data)->addItem(QString::fromUtf8(obs_source_get_name(filter)));
		},
		combo);

	obs_source_release(source);
}

void PopulateGlobalHotkeys(QComboBox *combo)
{
	combo->clear();
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id, obs_hotkey_t *hotkey) -> bool {
			// Only frontend-registered hotkeys are global; source
			// hotkeys are covered by Press Source Hotkey.
			if (obs_hotkey_get_registerer_type(hotkey) == OBS_HOTKEY_REGISTERER_FRONTEND)
				AddHotkeyComboItem(static_cast<QComboBox *>(data), hotkey);
			return true;
		},
		combo);
}

void PopulateSourceHotkeys(QComboBox *combo, const QString &sourceName)
{
	combo->clear();
	if (sourceName.isEmpty())
		return;

	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source)
		return;

	// Source hotkeys are registered with the source's weak reference as
	// the registerer, so that is what the comparison has to use.
	struct EnumData {
		QComboBox *combo;
		obs_weak_source_t *weak;
	} enumData = {combo, obs_source_get_weak_source(source)};

	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id, obs_hotkey_t *hotkey) -> bool {
			auto *enumData = static_cast<EnumData *>(data);
			if (obs_hotkey_get_registerer_type(hotkey) == OBS_HOTKEY_REGISTERER_SOURCE &&
			    obs_hotkey_get_registerer(hotkey) == enumData->weak)
				AddHotkeyComboItem(enumData->combo, hotkey);
			return true;
		},
		&enumData);

	obs_weak_source_release(enumData.weak);
	obs_source_release(source);
}

QPushButton *IconButton(const char *themeID, const char *cls, const QString &tip, QWidget *parent)
{
	auto *btn = new QPushButton(parent);
	btn->setFlat(true);
	StyleIconButton(btn, ButtonSize::Inline, themeID, cls);
	btn->setToolTip(tip);
	btn->setAccessibleName(tip);
	return btn;
}

} // namespace

// --- ActionStepCard ---

ActionStepCard::ActionStepCard(const ActionStep &step, QWidget *parent) : QFrame(parent), m_step(step)
{
	setObjectName("actionCard");
	setStyleSheet("QFrame#actionCard { background-color: rgba(127, 127, 127, 12%); "
		      "border: 1px solid rgba(127, 127, 127, 25%); border-radius: 6px; }");

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 6, 6, 8);
	layout->setSpacing(6);

	auto *header = new QHBoxLayout();
	header->setSpacing(6);
	m_dragHandle = new DragHandle(this);
	header->addWidget(m_dragHandle);
	m_numberLabel = new QLabel(this);
	m_numberLabel->setFixedSize(20, 20);
	m_numberLabel->setAlignment(Qt::AlignCenter);
	m_numberLabel->setStyleSheet("background-color: rgba(127, 127, 127, 30%); border-radius: 10px; "
				     "font-size: 11px; font-weight: bold;");
	header->addWidget(m_numberLabel);

	auto *title = new QLabel(TypeName(m_step.type), this);
	title->setStyleSheet("font-weight: bold;");
	header->addWidget(title, 1);

	auto *removeBtn = IconButton("removeIconSmall", "icon-trash", "Remove Step", this);
	header->addWidget(removeBtn);
	layout->addLayout(header);

	m_fieldsRow = new QHBoxLayout();
	m_fieldsRow->setSpacing(6);
	layout->addLayout(m_fieldsRow);

	m_warningLabel = new QLabel(this);
	m_warningLabel->setWordWrap(true);
	m_warningLabel->setStyleSheet("color: #d9a441;");
	m_warningLabel->hide();
	layout->addWidget(m_warningLabel);

	// Dragging the grip is the main way to reorder; the context menu keeps
	// a keyboard-reachable alternative
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this,
		[this](const QPoint &pos) { ShowMenu(mapToGlobal(pos)); });
	connect(removeBtn, &QPushButton::clicked, this, [this]() { emit removeRequested(this); });

	BuildFields();
	UpdateWarning();
}

QString ActionStepCard::TypeName(ActionType type)
{
	switch (type) {
	case ActionType::SetText:
		return "Set Text";
	case ActionType::EnableFilter:
		return "Enable/Disable Filter";
	case ActionType::SwitchScene:
		return "Switch Scene";
	case ActionType::Delay:
		return "Wait";
	case ActionType::TriggerGlobalHotkey:
		return "Press OBS Hotkey";
	case ActionType::TriggerSourceHotkey:
		return "Press Source Hotkey";
	}
	return "Unknown";
}

void ActionStepCard::SetPosition(int index, int count)
{
	m_numberLabel->setText(QString::number(index + 1));
	m_index = index;
	m_count = count;
}

void ActionStepCard::ShowMenu(const QPoint &globalPos)
{
	QMenu menu(this);
	menu.addAction("Move Up", this, [this]() { emit moveUpRequested(this); })->setEnabled(m_index > 0);
	menu.addAction("Move Down", this, [this]() {
		    emit moveDownRequested(this);
	    })->setEnabled(m_index < m_count - 1);
	menu.addSeparator();
	menu.addAction("Remove Step", this, [this]() { emit removeRequested(this); });
	menu.exec(globalPos);
}

QComboBox *ActionStepCard::AddCombo(const QString &accessibleName, int stretch)
{
	auto *combo = new QComboBox(this);
	combo->setAccessibleName(accessibleName);
	combo->setToolTip(accessibleName);
	combo->setMinimumWidth(80);
	m_fieldsRow->addWidget(combo, stretch);
	return combo;
}

bool ActionStepCard::SelectOrKeep(QComboBox *combo, const QString &name)
{
	if (name.isEmpty())
		return true;
	int idx = combo->findText(name);
	if (idx >= 0) {
		combo->setCurrentIndex(idx);
		return true;
	}
	combo->addItem(name);
	combo->setCurrentIndex(combo->count() - 1);
	return false;
}

void ActionStepCard::UpdateWarning()
{
	if (m_missing.isEmpty()) {
		m_warningLabel->hide();
		return;
	}
	m_warningLabel->setText(
		QString::fromUtf8("⚠ Not found in this scene collection: %1").arg(m_missing.join(", ")));
	m_warningLabel->show();
}

void ActionStepCard::BuildFields()
{
	switch (m_step.type) {
	case ActionType::SetText:
		BuildSetText();
		break;
	case ActionType::EnableFilter:
		BuildEnableFilter();
		break;
	case ActionType::SwitchScene:
		BuildSwitchScene();
		break;
	case ActionType::Delay:
		BuildDelay();
		break;
	case ActionType::TriggerGlobalHotkey:
		BuildGlobalHotkey();
		break;
	case ActionType::TriggerSourceHotkey:
		BuildSourceHotkey();
		break;
	}
}

// Each builder fills its fields from the step, then stores the fields back
// (so a new step saves the values its fields show) and on every change.

void ActionStepCard::BuildSetText()
{
	auto *edit = new QLineEdit(m_step.text, this);
	edit->setPlaceholderText("Text for the timer's source (supports %time%)");
	edit->setAccessibleName("Text");
	m_fieldsRow->addWidget(edit, 1);
	connect(edit, &QLineEdit::textChanged, this, [this](const QString &text) { m_step.text = text; });
}

void ActionStepCard::BuildEnableFilter()
{
	QComboBox *source = AddCombo("Source");
	QComboBox *filter = AddCombo("Filter");
	QComboBox *state = AddCombo("Enable or disable", 0);
	state->addItem("Enable", true);
	state->addItem("Disable", false);

	PopulateSources(source);
	if (!SelectOrKeep(source, m_step.filterSourceName))
		m_missing << QString::fromUtf8("source “%1”").arg(m_step.filterSourceName);
	PopulateFilters(filter, source->currentText());
	if (!SelectOrKeep(filter, m_step.filterName) && m_missing.isEmpty())
		m_missing << QString::fromUtf8("filter “%1”").arg(m_step.filterName);
	state->setCurrentIndex(m_step.filterEnabled ? 0 : 1);

	auto store = [this, source, filter, state]() {
		m_step.filterSourceName = source->currentText();
		m_step.filterName = filter->currentText();
		m_step.filterEnabled = state->currentData().toBool();
	};
	store();

	connect(source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [filter, source, store](int) {
		PopulateFilters(filter, source->currentText());
		store();
	});
	connect(filter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, store);
	connect(state, QOverload<int>::of(&QComboBox::currentIndexChanged), this, store);
}

void ActionStepCard::BuildSwitchScene()
{
	QComboBox *scene = AddCombo("Scene");
	PopulateScenes(scene);
	if (!SelectOrKeep(scene, m_step.sceneName))
		m_missing << QString::fromUtf8("scene “%1”").arg(m_step.sceneName);

	auto store = [this, scene]() {
		m_step.sceneName = scene->currentText();
	};
	store();
	connect(scene, QOverload<int>::of(&QComboBox::currentIndexChanged), this, store);
}

void ActionStepCard::BuildDelay()
{
	auto *spin = new QDoubleSpinBox(this);
	spin->setRange(0.1, 300.0);
	spin->setDecimals(1);
	spin->setSingleStep(0.5);
	spin->setSuffix(" seconds");
	spin->setAccessibleName("Wait time");
	spin->setValue(m_step.delayMs / 1000.0);
	m_fieldsRow->addWidget(spin);
	m_fieldsRow->addStretch();

	auto store = [this, spin]() {
		m_step.delayMs = static_cast<int>(spin->value() * 1000.0 + 0.5);
	};
	store();
	connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, store);
}

void ActionStepCard::BuildGlobalHotkey()
{
	QComboBox *hotkey = AddCombo("Hotkey");
	hotkey->setEditable(true);
	PopulateGlobalHotkeys(hotkey);
	if (!m_step.globalHotkeyName.isEmpty() && !SetHotkeyComboValue(hotkey, m_step.globalHotkeyName))
		m_missing << QString::fromUtf8("hotkey “%1”").arg(m_step.globalHotkeyName);

	auto store = [this, hotkey]() {
		m_step.globalHotkeyName = HotkeyComboValue(hotkey);
	};
	store();
	connect(hotkey, &QComboBox::currentTextChanged, this, store);
}

void ActionStepCard::BuildSourceHotkey()
{
	QComboBox *source = AddCombo("Source");
	QComboBox *hotkey = AddCombo("Hotkey");
	hotkey->setEditable(true);

	PopulateSources(source);
	if (!SelectOrKeep(source, m_step.sourceHotkeySourceName))
		m_missing << QString::fromUtf8("source “%1”").arg(m_step.sourceHotkeySourceName);
	PopulateSourceHotkeys(hotkey, source->currentText());
	if (!m_step.sourceHotkeyName.isEmpty() && !SetHotkeyComboValue(hotkey, m_step.sourceHotkeyName) &&
	    m_missing.isEmpty())
		m_missing << QString::fromUtf8("hotkey “%1”").arg(m_step.sourceHotkeyName);

	auto store = [this, source, hotkey]() {
		m_step.sourceHotkeySourceName = source->currentText();
		m_step.sourceHotkeyName = HotkeyComboValue(hotkey);
	};
	store();

	connect(source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [hotkey, source, store](int) {
		PopulateSourceHotkeys(hotkey, source->currentText());
		store();
	});
	connect(hotkey, &QComboBox::currentTextChanged, this, store);
}

// --- ActionListWidget ---

ActionListWidget::ActionListWidget(QWidget *parent) : QWidget(parent)
{
	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(8);

	m_scroll = new QScrollArea(this);
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scroll->viewport()->setAutoFillBackground(false);

	auto *content = new QWidget(m_scroll);
	content->setAutoFillBackground(false);
	m_cardsLayout = new QVBoxLayout(content);
	m_cardsLayout->setContentsMargins(0, 0, 4, 0);
	m_cardsLayout->setSpacing(6);

	m_emptyLabel = new QLabel("No steps yet. Add one to switch scenes, toggle filters, set text or press "
				  "hotkeys when the timer ends.",
				  content);
	m_emptyLabel->setWordWrap(true);
	m_emptyLabel->setStyleSheet("color: rgba(127, 127, 127, 100%);");
	m_cardsLayout->addWidget(m_emptyLabel);
	m_cardsLayout->addStretch();

	m_scroll->setWidget(content);

	m_reorder = new DragReorderController(
		m_scroll,
		[this]() {
			QList<QWidget *> items;
			for (auto *card : m_cards)
				items.append(card);
			return items;
		},
		[this](int from, int to) {
			m_cards.move(from, to);
			Relayout();
		},
		[this]() { emit stepsChanged(); }, this);
	mainLayout->addWidget(m_scroll, 1);

	m_addBtn = new QPushButton("Add Step", this);
	m_addBtn->setAutoDefault(false);
	auto *addMenu = new QMenu(m_addBtn);
	auto addSection = [this, addMenu](const QString &title, std::initializer_list<ActionType> types) {
		addMenu->addSection(title);
		for (ActionType type : types) {
			addMenu->addAction(ActionStepCard::TypeName(type), this, [this, type]() {
				ActionStep step;
				step.type = type;
				AddCard(step);
				emit stepsChanged();

				// Bring the new card into view once the layout has
				// placed it
				ActionStepCard *card = m_cards.last();
				QTimer::singleShot(0, this, [this, card]() { m_scroll->ensureWidgetVisible(card); });
			});
		}
	};
	addSection("Scenes and sources", {ActionType::SwitchScene, ActionType::EnableFilter, ActionType::SetText});
	addSection("Hotkeys", {ActionType::TriggerGlobalHotkey, ActionType::TriggerSourceHotkey});
	addSection("Timing", {ActionType::Delay});
	m_addBtn->setMenu(addMenu);

	auto *buttonRow = new QHBoxLayout();
	buttonRow->addWidget(m_addBtn);
	buttonRow->addStretch();
	mainLayout->addLayout(buttonRow);
}

void ActionListWidget::setActions(const QList<ActionStep> &actions)
{
	qDeleteAll(m_cards);
	m_cards.clear();
	for (const auto &step : actions)
		AddCard(step);
	Relayout();
}

QList<ActionStep> ActionListWidget::actions() const
{
	QList<ActionStep> steps;
	for (const auto *card : m_cards)
		steps.append(card->step());
	return steps;
}

void ActionListWidget::AddCard(const ActionStep &step)
{
	auto *card = new ActionStepCard(step, this);
	connect(card, &ActionStepCard::moveUpRequested, this, [this](ActionStepCard *c) { MoveCard(c, -1); });
	connect(card, &ActionStepCard::moveDownRequested, this, [this](ActionStepCard *c) { MoveCard(c, 1); });
	connect(card, &ActionStepCard::removeRequested, this, &ActionListWidget::RemoveCard);
	m_reorder->Attach(card->dragHandle(), card);
	m_cards.append(card);
	Relayout();
}

void ActionListWidget::MoveCard(ActionStepCard *card, int delta)
{
	const int from = m_cards.indexOf(card);
	const int to = from + delta;
	if (from < 0 || to < 0 || to >= m_cards.size())
		return;
	m_cards.move(from, to);
	Relayout();
	emit stepsChanged();
}

void ActionListWidget::RemoveCard(ActionStepCard *card)
{
	if (!m_cards.removeOne(card))
		return;
	card->deleteLater();
	Relayout();
	emit stepsChanged();
}

void ActionListWidget::Relayout()
{
	for (auto *card : m_cards)
		m_cardsLayout->removeWidget(card);

	// Cards go after the empty-state label and before the stretch
	for (int i = 0; i < m_cards.size(); i++) {
		m_cardsLayout->insertWidget(i + 1, m_cards[i]);
		m_cards[i]->SetPosition(i, m_cards.size());
	}
	m_emptyLabel->setVisible(m_cards.isEmpty());
}
