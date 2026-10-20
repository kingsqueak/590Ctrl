#pragma once

#include "CatCodec.h"
#include "MenuTable.h"

#include <QHash>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace ts590 {
class Radio;
}

// Every EX menu item side by side for menu A and menu B, editable in place,
// plus the switch between the two sets (MF) and copying one set onto the
// other. Both sets are read the first time the panel is shown after
// connecting.
class MenuPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MenuPanel(ts590::Radio *radio, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    struct Cell {
        QWidget *editor = nullptr;
        QByteArray raw;     // P5 as the radio last answered it
        bool known = false;
    };
    struct Row {
        ts590::MenuItem item;
        Cell cells[2];      // indexed by MenuBank
    };

    void rebuild();
    QWidget *createEditor(int row, ts590::MenuBank bank);
    void reload();
    void clearValues();
    void showValue(ts590::MenuBank bank, int number, const QByteArray &raw);
    void write(int row, ts590::MenuBank bank, const QByteArray &value);
    QByteArray encodeIndex(const Cell &cell, const ts590::MenuItem &item, int index) const;
    void copyBank(ts590::MenuBank from, ts590::MenuBank to);
    void applyFilter();
    void refreshRowHighlight(int row);
    void updateEnabledState();

    ts590::Radio *m_radio;
    int m_modelId = 0;
    QList<Row> m_rows;
    QHash<int, int> m_rowForNumber;
    bool m_readSinceConnect = false;

    QTableWidget *m_table = nullptr;
    QButtonGroup *m_bankGroup = nullptr;
    QPushButton *m_bankButtons[2] = {};
    QLineEdit *m_search = nullptr;
    QPushButton *m_reloadButton = nullptr;
    QPushButton *m_copyButtons[2] = {};   // [0]: A -> B, [1]: B -> A
    QLabel *m_status = nullptr;
};
