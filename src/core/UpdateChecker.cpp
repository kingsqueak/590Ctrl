#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>

#include <algorithm>

namespace ts590 {

namespace {

constexpr const char *kLatestApi = "https://api.github.com/repos/kingsqueak/590Ctrl/releases/latest";
constexpr int kTimeoutMs = 10000;

QString stripV(QString v)
{
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        v.remove(0, 1);
    return v;
}

QList<int> parts(const QString &v)
{
    QList<int> out;
    for (const QString &part : stripV(v).split(QLatin1Char('.'))) {
        int digits = 0;
        while (digits < part.size() && part.at(digits).isDigit())
            ++digits;
        out.append(part.left(digits).toInt());
    }
    return out;
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void UpdateChecker::check()
{
    QNetworkRequest request{QUrl(QString::fromLatin1(kLatestApi))};
    const QString version = QCoreApplication::applicationVersion();
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("590Ctrl/%1").arg(version.isEmpty() ? QStringLiteral("0") : version));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(false, {}, QString::fromLatin1(kReleasesPage));
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = stripV(root.value(QStringLiteral("tag_name")).toString());
        QString page = root.value(QStringLiteral("html_url")).toString();
        if (page.isEmpty())
            page = QString::fromLatin1(kReleasesPage);
        emit finished(!tag.isEmpty(), tag, page);
    });
}

bool UpdateChecker::isNewer(const QString &candidate, const QString &current)
{
    const QList<int> a = parts(candidate), b = parts(current);
    for (qsizetype i = 0; i < std::max(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a.at(i) : 0;
        const int y = i < b.size() ? b.at(i) : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

} // namespace ts590
