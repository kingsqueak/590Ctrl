#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace ts590 {

// Is there a newer 590Ctrl? Asks GitHub for the latest release and compares
// it with this version. Nothing is downloaded or installed, and nothing is
// sent but the request itself.
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    static constexpr const char *kReleasesPage = "https://github.com/kingsqueak/590Ctrl/releases";

    explicit UpdateChecker(QObject *parent = nullptr);

    // Starts one request; finished() follows. ok is false when GitHub
    // couldn't be reached or answered with something unexpected.
    void check();

    // "0.15.10" against "0.15.9", number by number; a leading v is ignored.
    static bool isNewer(const QString &candidate, const QString &current);

signals:
    void finished(bool ok, const QString &version, const QString &page);

private:
    QNetworkAccessManager *m_network;
};

} // namespace ts590
