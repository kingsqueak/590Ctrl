#include "MenuPanel.h"

#include "Radio.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace ts590;

namespace {

enum Column { NumberColumn, NameColumn, BankAColumn, BankBColumn, ColumnCount };

constexpr MenuBank kBanks[] = {MenuBank::A, MenuBank::B};

QString bankName(MenuBank bank) { return bank == MenuBank::A ? QStringLiteral("A") : QStringLiteral("B"); }

bool editable(const MenuItem &item)
{
    return !item.locked && item.kind != MenuItem::Kind::ReadOnly;
}

}   // namespace

MenuPanel::MenuPanel(Radio *radio, QWidget *parent)
    : QWidget(parent)
    , m_radio(radio)
{
    m_bankGroup = new QButtonGroup(this);
    auto *bankRow = new QHBoxLayout;
    bankRow->addWidget(new QLabel(tr("Menu in use:")));
    for (MenuBank bank : kBanks) {
        auto *button = new QPushButton(bankName(bank));
        button->setCheckable(true);
        button->setToolTip(tr("Switch the radio to menu %1. Every menu setting changes with it.").arg(bankName(bank)));
        m_bankGroup->addButton(button, int(bank));
        m_bankButtons[int(bank)] = button;
        bankRow->addWidget(button);
    }
    connect(m_bankGroup, &QButtonGroup::idClicked, this, [this](int id) { m_radio->selectMenuBank(MenuBank(id)); });

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Filter by number or name"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &MenuPanel::applyFilter);
    bankRow->addSpacing(12);
    bankRow->addWidget(m_search, 1);

    m_reloadButton = new QPushButton(tr("Reload"));
    m_reloadButton->setToolTip(tr("Read both menu sets from the radio again"));
    connect(m_reloadButton, &QPushButton::clicked, this, &MenuPanel::reload);
    bankRow->addWidget(m_reloadButton);
    for (MenuBank from : kBanks) {
        const MenuBank to = from == MenuBank::A ? MenuBank::B : MenuBank::A;
        auto *button = new QPushButton(tr("Copy %1 → %2…").arg(bankName(from), bankName(to)));
        button->setToolTip(tr("Write every menu %1 setting into menu %2").arg(bankName(from), bankName(to)));
        connect(button, &QPushButton::clicked, this, [this, from, to] { copyBank(from, to); });
        m_copyButtons[int(from)] = button;
        bankRow->addWidget(button);
    }

    m_table = new QTableWidget(0, ColumnCount);
    m_table->setHorizontalHeaderLabels({tr("No."), tr("Function"), tr("Menu A"), tr("Menu B")});
    m_table->verticalHeader()->hide();
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    QHeaderView *header = m_table->horizontalHeader();
    header->setSectionResizeMode(NumberColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    for (int column : {BankAColumn, BankBColumn}) {
        header->setSectionResizeMode(column, QHeaderView::Fixed);
        m_table->setColumnWidth(column, 170);
    }

    m_status = new QLabel;
    m_status->setForegroundRole(QPalette::PlaceholderText);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(bankRow);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_status);

    connect(m_radio, &Radio::identified, this, [this](int modelId) {
        if (modelId != m_modelId)
            rebuild();
    });
    connect(m_radio, &Radio::stateChanged, this, [this](Radio::State state) {
        if (state != Radio::State::Connected) {
            m_readSinceConnect = false;
            clearValues();
        }
        updateEnabledState();
    });
    connect(m_radio, &Radio::menuBankChanged, this, [this] {
        if (isVisible() && !m_readSinceConnect)
            reload();
        updateEnabledState();
    });
    connect(m_radio, &Radio::menuItemRead, this, &MenuPanel::showValue);

    rebuild();
}

void MenuPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_readSinceConnect && m_radio->state() == Radio::State::Connected && m_radio->menuBank())
        reload();
}

void MenuPanel::rebuild()
{
    m_modelId = m_radio->modelId();
    m_rows.clear();
    m_rowForNumber.clear();
    m_table->setRowCount(0);
    if (m_modelId == 0)
        return;   // nothing to show until the radio has identified itself

    const QList<MenuItem> &items = menuItemsFor(m_modelId);
    m_table->setRowCount(int(items.size()));
    for (const MenuItem &item : items) {
        const int row = int(m_rows.size());
        m_rows.append(Row{item, {}});
        m_rowForNumber.insert(item.number, row);

        auto *number = new QTableWidgetItem(QStringLiteral("%1").arg(item.number, 3, 10, QLatin1Char('0')));
        number->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, NumberColumn, number);
        m_table->setItem(row, NameColumn, new QTableWidgetItem(item.name));
        for (MenuBank bank : kBanks) {
            QWidget *editor = createEditor(row, bank);
            m_rows[row].cells[int(bank)].editor = editor;
            m_table->setCellWidget(row, bank == MenuBank::A ? BankAColumn : BankBColumn, editor);
        }
    }
    applyFilter();
    updateEnabledState();
}

QWidget *MenuPanel::createEditor(int row, MenuBank bank)
{
    const MenuItem &item = m_rows[row].item;
    if (!editable(item)) {
        auto *label = new QLabel;
        label->setContentsMargins(6, 0, 6, 0);
        if (item.locked)
            label->setToolTip(tr("Change this on the radio. 590Ctrl may be connected through this port, so "
                                 "changing it here could cut the connection."));
        return label;
    }

    switch (item.kind) {
    case MenuItem::Kind::Choice: {
        auto *combo = new QComboBox;
        combo->addItems(item.choices);
        connect(combo, &QComboBox::activated, this, [this, row, bank](int index) {
            write(row, bank, encodeIndex(m_rows[row].cells[int(bank)], m_rows[row].item, index));
        });
        return combo;
    }
    case MenuItem::Kind::Function: {
        auto *spin = new QSpinBox;
        spin->setRange(0, kMenuFunctionOff);
        spin->setToolTip(tr("Function number from the TS-590 instruction manual. %1 = OFF.").arg(kMenuFunctionOff));
        connect(spin, &QSpinBox::editingFinished, this, [this, row, bank, spin] {
            const Cell &cell = m_rows[row].cells[int(bank)];
            if (cell.known && spin->value() != menuValueIndex(m_rows[row].item, cell.raw))
                write(row, bank, encodeIndex(cell, m_rows[row].item, spin->value()));
        });
        return spin;
    }
    case MenuItem::Kind::Text: {
        auto *edit = new QLineEdit;
        edit->setMaxLength(item.maxLength);
        edit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[ -~]*")), edit));
        connect(edit, &QLineEdit::editingFinished, this, [this, row, bank, edit] {
            const Cell &cell = m_rows[row].cells[int(bank)];
            const QByteArray text = edit->text().toLatin1();
            if (cell.known && text != cell.raw.trimmed())
                write(row, bank, text);
        });
        return edit;
    }
    case MenuItem::Kind::ReadOnly:
        break;
    }
    return new QLabel;
}

void MenuPanel::reload()
{
    if (m_rows.isEmpty() || !m_radio->menuBank())
        return;
    clearValues();
    m_readSinceConnect = true;
    QList<int> numbers;
    for (const Row &row : std::as_const(m_rows))
        numbers << row.item.number;
    for (MenuBank bank : kBanks)
        m_radio->readMenuItems(bank, numbers);
    m_status->setText(tr("Reading menus A and B…"));
}

void MenuPanel::clearValues()
{
    for (int row = 0; row < m_rows.size(); ++row) {
        for (Cell &cell : m_rows[row].cells) {
            cell.known = false;
            cell.raw.clear();
            const QSignalBlocker blocker(cell.editor);
            if (auto *combo = qobject_cast<QComboBox *>(cell.editor))
                combo->setCurrentIndex(-1);
            else if (auto *spin = qobject_cast<QSpinBox *>(cell.editor))
                spin->clear();
            else if (auto *edit = qobject_cast<QLineEdit *>(cell.editor))
                edit->clear();
            else if (auto *label = qobject_cast<QLabel *>(cell.editor))
                label->clear();
        }
        refreshRowHighlight(row);
    }
    updateEnabledState();
}

void MenuPanel::showValue(MenuBank bank, int number, const QByteArray &raw)
{
    const int row = m_rowForNumber.value(number, -1);
    if (row < 0)
        return;
    const MenuItem &item = m_rows[row].item;
    Cell &cell = m_rows[row].cells[int(bank)];
    cell.raw = raw;
    cell.known = true;

    const QSignalBlocker blocker(cell.editor);
    const int index = menuValueIndex(item, raw);
    if (auto *combo = qobject_cast<QComboBox *>(cell.editor)) {
        combo->setCurrentIndex(index);
        combo->setToolTip(index < 0 ? tr("The radio answered \"%1\", which isn't in the list.")
                                          .arg(QString::fromLatin1(raw))
                                    : QString());
    } else if (auto *spin = qobject_cast<QSpinBox *>(cell.editor)) {
        spin->setValue(std::max(index, 0));
    } else if (auto *edit = qobject_cast<QLineEdit *>(cell.editor)) {
        edit->setText(QString::fromLatin1(raw.trimmed()));
    } else if (auto *label = qobject_cast<QLabel *>(cell.editor)) {
        label->setText(menuValueText(item, raw));
    }
    refreshRowHighlight(row);
    updateEnabledState();
}

QByteArray MenuPanel::encodeIndex(const Cell &cell, const MenuItem &item, int index) const
{
    // Match the width the radio used for this menu, e.g. "06" for CW pitch.
    const bool digits = !cell.raw.isEmpty() && parseNumber(cell.raw).has_value();
    return menuIndexValue(index, digits ? int(cell.raw.size()) : defaultMenuValueWidth(item));
}

void MenuPanel::write(int row, MenuBank bank, const QByteArray &value)
{
    m_radio->writeMenuItem(bank, m_rows[row].item.number, value);
}

void MenuPanel::copyBank(MenuBank from, MenuBank to)
{
    QList<int> rows;
    int unread = 0;
    for (int row = 0; row < m_rows.size(); ++row) {
        const Row &r = m_rows[row];
        if (!editable(r.item))
            continue;
        if (!r.cells[int(from)].known || !r.cells[int(to)].known)
            ++unread;
        else if (r.cells[int(from)].raw.trimmed() != r.cells[int(to)].raw.trimmed())
            rows << row;
    }
    const QString title = tr("Copy Menu %1 to %2").arg(bankName(from), bankName(to));
    if (unread > 0) {
        QMessageBox::information(this, title, tr("Some settings haven't been read yet. Try again when reading has finished."));
        return;
    }
    if (rows.isEmpty()) {
        m_status->setText(tr("Menu %1 already matches menu %2.").arg(bankName(to), bankName(from)));
        return;
    }
    if (QMessageBox::question(this, title,
                              tr("Write %n setting(s) from menu %1 into menu %2?", nullptr, int(rows.size()))
                                  .arg(bankName(from), bankName(to)))
        != QMessageBox::Yes)
        return;
    for (int row : std::as_const(rows))
        write(row, to, m_rows[row].cells[int(from)].raw);
}

void MenuPanel::applyFilter()
{
    const QString text = m_search->text().trimmed();
    for (int row = 0; row < m_rows.size(); ++row) {
        const MenuItem &item = m_rows[row].item;
        const bool match = text.isEmpty() || item.name.contains(text, Qt::CaseInsensitive)
                           || QStringLiteral("%1").arg(item.number, 3, 10, QLatin1Char('0')).startsWith(text);
        m_table->setRowHidden(row, !match);
    }
}

void MenuPanel::refreshRowHighlight(int row)
{
    const Row &r = m_rows[row];
    const bool differs = r.cells[0].known && r.cells[1].known && r.cells[0].raw.trimmed() != r.cells[1].raw.trimmed();
    QTableWidgetItem *name = m_table->item(row, NameColumn);
    QFont font = name->font();
    font.setBold(differs);
    name->setFont(font);
    name->setToolTip(differs ? tr("Menu A and B differ") : QString());
}

void MenuPanel::updateEnabledState()
{
    const bool connected = m_radio->state() == Radio::State::Connected;
    const auto active = m_radio->menuBank();
    const bool ready = connected && active.has_value();

    for (MenuBank bank : kBanks) {
        QPushButton *button = m_bankButtons[int(bank)];
        button->setEnabled(ready);
        button->setChecked(active == bank);
        m_copyButtons[int(bank)]->setEnabled(ready && !m_rows.isEmpty());
        m_table->horizontalHeaderItem(bank == MenuBank::A ? BankAColumn : BankBColumn)
            ->setText(active == bank ? tr("Menu %1 (in use)").arg(bankName(bank)) : tr("Menu %1").arg(bankName(bank)));
    }
    m_reloadButton->setEnabled(ready && !m_rows.isEmpty());

    int known = 0;
    int differing = 0;
    for (const Row &row : std::as_const(m_rows)) {
        for (const Cell &cell : row.cells) {
            cell.editor->setEnabled(ready && cell.known);
            known += cell.known ? 1 : 0;
        }
        if (row.cells[0].known && row.cells[1].known && row.cells[0].raw.trimmed() != row.cells[1].raw.trimmed())
            ++differing;
    }

    const int total = int(m_rows.size()) * 2;
    if (!connected)
        m_status->setText(tr("Not connected"));
    else if (m_rows.isEmpty() || !active)
        m_status->setText(tr("Waiting for the radio…"));
    else if (known < total)
        m_status->setText(m_readSinceConnect ? tr("Reading menus: %1 of %2 values").arg(known).arg(total)
                                             : QString());
    else
        m_status->setText(differing == 0 ? tr("Menu A and B are the same.")
                                         : tr("%n setting(s) differ between menu A and B (shown in bold).", nullptr,
                                              differing));
}
