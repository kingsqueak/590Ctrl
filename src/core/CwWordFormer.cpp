#include "CwWordFormer.h"

#include <QFile>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <limits>
#include <optional>

// The word list is a resource of this static library.
static void initWordResource()
{
    Q_INIT_RESOURCE(cwwords);
}

namespace ts590 {

namespace {

constexpr int kMinLength = 4;        // shorter runs are left as they are
constexpr int kMaxPieceLength = 12;

QSet<QString> loadWords()
{
    initWordResource();
    QSet<QString> words;
    QFile file(QStringLiteral(":/cw-words.txt"));
    if (!file.open(QIODevice::ReadOnly))
        return words;
    while (!file.atEnd()) {
        const QString line = QString::fromLatin1(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('#')))
            continue;
        const auto parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &word : parts)
            words.insert(word.toUpper());
    }
    return words;
}

const QSet<QString> &words()
{
    static const QSet<QString> set = loadWords();
    return set;
}

// Sent so often that a split producing them is very likely right.
const QSet<QString> &frequent()
{
    static const QSet<QString> set = {
        QStringLiteral("CQ"),  QStringLiteral("DE"),   QStringLiteral("K"),   QStringLiteral("KN"),
        QStringLiteral("BK"),  QStringLiteral("TU"),   QStringLiteral("TNX"), QStringLiteral("UR"),
        QStringLiteral("RST"), QStringLiteral("ES"),   QStringLiteral("R"),   QStringLiteral("73"),
        QStringLiteral("5NN"), QStringLiteral("599"),  QStringLiteral("NR"),  QStringLiteral("QTH"),
        QStringLiteral("NAME"), QStringLiteral("OP"),  QStringLiteral("HR"),  QStringLiteral("FB"),
        QStringLiteral("GM"),  QStringLiteral("GA"),   QStringLiteral("GE"),  QStringLiteral("SS"),
        QStringLiteral("TEST"), QStringLiteral("PSE"), QStringLiteral("AGN"), QStringLiteral("EE"),
    };
    return set;
}

bool matches(const QRegularExpression &expression, const QString &text)
{
    return expression.match(text).hasMatch();
}

bool isCallsign(const QString &text)
{
    static const QRegularExpression expression(QRegularExpression::anchoredPattern(QStringLiteral(
        "(?:[A-Z0-9]{1,4}/)?(?:[A-Z]{1,2}|[A-Z][0-9]|[0-9][A-Z])[0-9][A-Z]{1,4}(?:/[A-Z0-9]{1,4})?")));
    return matches(expression, text);
}

bool isNumber(const QString &text)
{
    static const QRegularExpression expression(QRegularExpression::anchoredPattern(QStringLiteral(
        "(?:[0-9]{1,5}|[0-9]+\\.[0-9]+|[0-9]{4}Z|[1-5][1-9][1-9N]|[1-5]NN|[0-9]{1,4}[ABQMSU])")));
    return matches(expression, text);
}

bool isGrid(const QString &text)
{
    static const QRegularExpression expression(
        QRegularExpression::anchoredPattern(QStringLiteral("[A-R]{2}[0-9]{2}(?:[A-X]{2})?")));
    return matches(expression, text);
}

bool isPunctuation(QChar c)
{
    return c == QLatin1Char('?') || c == QLatin1Char('.') || c == QLatin1Char(',');
}

// Without the punctuation that may close a piece.
QString trimmed(const QString &piece)
{
    qsizetype n = piece.size();
    while (n > 0 && isPunctuation(piece[n - 1]))
        --n;
    return piece.left(n);
}

bool allDigits(const QString &text)
{
    return std::all_of(text.cbegin(), text.cend(),
                       [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); });
}

double callsignCost(const QString &piece, const QSet<QString> &seen)
{
    // A known call with a word stuck on ("AE0QDE", "KC2RGWK") is the call and the word.
    for (qsizetype cut = piece.size() - 1; cut >= 3; --cut) {
        const QString head = piece.left(cut);
        if (frequent().contains(piece.mid(cut)) && (seen.contains(head) || isCallsign(head)))
            return 2.6;
    }
    qsizetype lastDigit = -1;
    for (qsizetype i = piece.size() - 1; i >= 0 && lastDigit < 0; --i) {
        if (piece[i].isDigit())
            lastDigit = i;
    }
    const qsizetype suffix = piece.size() - 1 - lastDigit;
    return suffix >= 4 ? 2.6 : 1.6;   // four-letter suffixes are rare
}

// How unlikely a piece is (lower is likelier), or nothing if it isn't known.
std::optional<double> cost(const QString &piece, const QSet<QString> &seen)
{
    if (piece.isEmpty())
        return std::nullopt;
    if (seen.contains(piece))
        return 0.3;
    if (frequent().contains(piece))
        return piece == QLatin1String("K") ? 0.8 : piece == QLatin1String("R") ? 1.0 : 0.6;
    if (words().contains(piece)) {
        // A lone E or T is often noise.
        return piece.size() > 1 ? 1.0 : piece == QLatin1String("E") || piece == QLatin1String("T") ? 2.0 : 1.4;
    }
    if (isNumber(piece))
        return 1.2;
    if (isGrid(piece))
        return 2.6;   // grid shapes turn up by chance ("NR17", "EE44EE")
    if (piece.size() >= 3 && isCallsign(piece))
        return callsignCost(piece, seen);
    return std::nullopt;
}

// A whole run that's surely right as it is: a seen callsign, a word or a
// number. A callsign or grid shape alone isn't enough ("TU5NNNY" fits a callsign).
bool certain(const QString &piece, const QSet<QString> &seen)
{
    return seen.contains(piece) || frequent().contains(piece) || words().contains(piece) || isNumber(piece);
}

// One run of characters, split if every piece is known.
QString split(const QString &token, const QSet<QString> &seen)
{
    if (token.size() < kMinLength || certain(trimmed(token), seen))
        return token;
    const qsizetype n = token.size();
    constexpr double unreachable = std::numeric_limits<double>::infinity();
    QList<double> best(n + 1, unreachable);
    QList<qsizetype> from(n + 1, 0);
    best[0] = 0;
    for (qsizetype end = 1; end <= n; ++end) {
        for (qsizetype start = std::max<qsizetype>(0, end - kMaxPieceLength); start < end; ++start) {
            if (best[start] == unreachable)
                continue;
            const QString piece = token.mid(start, end - start);
            // Punctuation may only close a piece ("QRZ?" or "FB.").
            if (std::any_of(piece.cbegin(), piece.cend() - 1, isPunctuation))
                continue;
            const auto pieceCost = cost(trimmed(piece), seen);
            if (!pieceCost)
                continue;
            if (best[start] + *pieceCost < best[end]) {
                best[end] = best[start] + *pieceCost;
                from[end] = start;
            }
        }
    }
    if (best[n] == unreachable)
        return token;
    QStringList pieces;
    for (qsizetype end = n; end > 0; end = from[end])
        pieces.prepend(token.mid(from[end], end - from[end]));
    // Mostly little two-letter scraps ("LONG IS LA ND") means the run wasn't
    // really made of words: leave it.
    const auto scraps = std::count_if(pieces.cbegin(), pieces.cend(), [](const QString &piece) {
        const QString t = trimmed(piece);
        return t.size() <= 2 && !frequent().contains(t) && !allDigits(t);
    });
    if (pieces.size() > 1 && scraps * 2 > pieces.size())
        return token;
    return pieces.join(QLatin1Char(' '));
}

}   // namespace

QString formCwWords(const QString &text)
{
    if (text.isEmpty())
        return text;
    const QStringList tokens = text.split(QLatin1Char(' '));
    const bool lastComplete = text.endsWith(QLatin1Char(' '));
    // Callsigns the decoder already printed on their own, last word aside. A
    // run doesn't count as seen just because of itself.
    QHash<QString, int> counts;
    for (qsizetype i = 0; i < tokens.size() - (lastComplete ? 0 : 1); ++i) {
        const QString t = trimmed(tokens[i]);
        if (t.size() >= 3 && isCallsign(t))
            ++counts[t];
    }
    QSet<QString> all;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        all.insert(it.key());

    QStringList out;
    out.reserve(tokens.size());
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const bool open = i == tokens.size() - 1 && !lastComplete;
        if (open) {
            out.append(tokens[i]);
            continue;
        }
        const QString self = trimmed(tokens[i]);
        if (counts.value(self) == 1) {
            // Seen only as this very run: it mustn't vouch for itself.
            QSet<QString> others = all;
            others.remove(self);
            out.append(split(tokens[i], others));
        } else {
            out.append(split(tokens[i], all));
        }
    }
    return out.join(QLatin1Char(' '));
}

}   // namespace ts590
