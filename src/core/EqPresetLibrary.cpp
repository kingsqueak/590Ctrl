#include "EqPresetLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>

namespace ts590 {

namespace {

constexpr auto kFormat = "590ctrl-eq";
constexpr int kVersion = 1;
constexpr auto kBindingsFile = "bindings.json";

QString typeKey(EqType type)
{
    return type == EqType::Rx ? QStringLiteral("rx") : QStringLiteral("tx");
}

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
        return true;
    if (error)
        *error = QObject::tr("Could not write %1: %2").arg(path, file.errorString());
    return false;
}

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

}   // namespace

EqPresetLibrary::EqPresetLibrary(const QString &directory, QObject *parent)
    : QObject(parent)
    , m_directory(directory)
{
    load();
}

QString EqPresetLibrary::typeDirectory(EqType type) const
{
    return m_directory + QLatin1Char('/') + typeKey(type);
}

QString EqPresetLibrary::presetPath(EqType type, const QString &name) const
{
    // Percent-encoding keeps any name ("Rag/chew", "DX: punchy") a safe file name.
    return typeDirectory(type) + QLatin1Char('/') + QString::fromLatin1(QUrl::toPercentEncoding(name, " "))
           + QStringLiteral(".json");
}

void EqPresetLibrary::load()
{
    for (EqType type : {EqType::Rx, EqType::Tx}) {
        const QDir dir(typeDirectory(type));
        for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
            QFile file(info.filePath());
            if (!file.open(QIODevice::ReadOnly))
                continue;
            if (const auto preset = decode(file.readAll())) {
                const QString name = preset->first.isEmpty() ? info.completeBaseName() : preset->first;
                m_presets[int(type)].insert(name, preset->second);
            }
        }
    }

    QFile file(m_directory + QLatin1Char('/') + QLatin1String(kBindingsFile));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (EqType type : {EqType::Rx, EqType::Tx}) {
        const QJsonObject perMode = root.value(typeKey(type)).toObject();
        for (auto it = perMode.begin(); it != perMode.end(); ++it) {
            bool ok = false;
            const int mode = it.key().toInt(&ok);
            if (ok && mode >= int(EqMode::Ssb) && mode <= int(EqMode::Fsk) && !it.value().toString().isEmpty())
                m_bindings[int(type)].insert(mode, it.value().toString());
        }
    }
}

QStringList EqPresetLibrary::names(EqType type) const
{
    QStringList list = m_presets[int(type)].keys();
    std::sort(list.begin(), list.end(),
              [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    return list;
}

std::optional<EqLevels> EqPresetLibrary::levels(EqType type, const QString &name) const
{
    const auto &presets = m_presets[int(type)];
    const auto it = presets.find(name);
    return it == presets.end() ? std::nullopt : std::optional(*it);
}

bool EqPresetLibrary::contains(EqType type, const QString &name) const
{
    return m_presets[int(type)].contains(name);
}

bool EqPresetLibrary::save(EqType type, const QString &name, const EqLevels &levels, QString *error)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setError(error, tr("A preset needs a name."));
        return false;
    }
    if (!QDir().mkpath(typeDirectory(type))) {
        setError(error, tr("Could not create %1").arg(typeDirectory(type)));
        return false;
    }
    if (!writeFile(presetPath(type, trimmed), encode(type, trimmed, levels), error))
        return false;
    m_presets[int(type)].insert(trimmed, levels);
    emit presetsChanged(type);
    return true;
}

bool EqPresetLibrary::remove(EqType type, const QString &name, QString *error)
{
    if (!contains(type, name))
        return true;
    QFile file(presetPath(type, name));
    if (file.exists() && !file.remove()) {
        setError(error, tr("Could not delete %1: %2").arg(file.fileName(), file.errorString()));
        return false;
    }
    m_presets[int(type)].remove(name);
    emit presetsChanged(type);

    auto &bindings = m_bindings[int(type)];
    if (bindings.removeIf([&](const auto &it) { return it.value() == name; }) > 0) {
        saveBindings();
        emit bindingsChanged(type);
    }
    return true;
}

std::optional<QString> EqPresetLibrary::importFile(EqType type, const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, tr("Could not read %1: %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    const auto preset = decode(file.readAll());
    if (!preset) {
        setError(error, tr("%1 is not a 590Ctrl EQ preset.").arg(path));
        return std::nullopt;
    }
    const QString name = preset->first.trimmed().isEmpty() ? QFileInfo(path).completeBaseName() : preset->first;
    if (!save(type, name, preset->second, error))
        return std::nullopt;
    return name.trimmed();
}

bool EqPresetLibrary::exportFile(EqType type, const QString &name, const QString &path, QString *error) const
{
    const auto preset = levels(type, name);
    if (!preset) {
        setError(error, tr("No preset named %1.").arg(name));
        return false;
    }
    return writeFile(path, encode(type, name, *preset), error);
}

QString EqPresetLibrary::binding(EqType type, EqMode mode) const
{
    return m_bindings[int(type)].value(int(mode));
}

void EqPresetLibrary::setBinding(EqType type, EqMode mode, const QString &name)
{
    auto &bindings = m_bindings[int(type)];
    if (bindings.value(int(mode)) == name)
        return;
    if (name.isEmpty())
        bindings.remove(int(mode));
    else
        bindings.insert(int(mode), name);
    saveBindings();
    emit bindingsChanged(type);
}

void EqPresetLibrary::saveBindings()
{
    QJsonObject root;
    for (EqType type : {EqType::Rx, EqType::Tx}) {
        QJsonObject perMode;
        for (auto it = m_bindings[int(type)].begin(); it != m_bindings[int(type)].end(); ++it)
            perMode.insert(QString::number(it.key()), it.value());
        root.insert(typeKey(type), perMode);
    }
    QDir().mkpath(m_directory);
    writeFile(m_directory + QLatin1Char('/') + QLatin1String(kBindingsFile), QJsonDocument(root).toJson(), nullptr);
}

QByteArray EqPresetLibrary::encode(EqType type, const QString &name, const EqLevels &levels)
{
    QJsonArray bandsHz;
    QJsonArray levelsDb;
    for (int band = 0; band < kEqBandCount; ++band) {
        bandsHz.append(eqBandFrequencyHz(band));
        levelsDb.append(eqRawToDb(levels[band]));
    }
    const QJsonObject preset{
        {QStringLiteral("format"), QString::fromLatin1(kFormat)},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("name"), name},
        {QStringLiteral("type"), typeKey(type)},
        {QStringLiteral("bands_hz"), bandsHz},
        {QStringLiteral("levels_db"), levelsDb},
    };
    return QJsonDocument(preset).toJson();
}

std::optional<std::pair<QString, EqLevels>> EqPresetLibrary::decode(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonObject preset = QJsonDocument::fromJson(json, &error).object();
    const QJsonArray levelsDb = preset.value(QStringLiteral("levels_db")).toArray();
    if (error.error != QJsonParseError::NoError
        || preset.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)
        || levelsDb.size() != kEqBandCount)
        return std::nullopt;

    EqLevels levels;
    for (int band = 0; band < kEqBandCount; ++band)
        levels[band] = eqDbToRaw(levelsDb[band].toInt());
    return std::pair{preset.value(QStringLiteral("name")).toString(), levels};
}

}   // namespace ts590
