#pragma once

#include "CatCodec.h"

#include <QMap>
#include <QObject>
#include <QStringList>

#include <optional>

namespace ts590 {

// Named EQ curves kept on disk, one JSON file per preset:
//   <directory>/rx/<name>.json, <directory>/tx/<name>.json
// plus <directory>/bindings.json mapping a mode family to the preset that
// should be loaded into the radio's single User curve in that mode.
class EqPresetLibrary : public QObject
{
    Q_OBJECT

public:
    explicit EqPresetLibrary(const QString &directory, QObject *parent = nullptr);

    QString directory() const { return m_directory; }

    QStringList names(EqType type) const;   // sorted, case-insensitive
    std::optional<EqLevels> levels(EqType type, const QString &name) const;
    bool contains(EqType type, const QString &name) const;

    // Creates or overwrites.
    bool save(EqType type, const QString &name, const EqLevels &levels, QString *error = nullptr);
    // Also drops any bindings that pointed at the preset.
    bool remove(EqType type, const QString &name, QString *error = nullptr);

    // Imports a preset file into `type`. The name comes from the file's
    // "name" field, else its base name. Returns the stored name.
    std::optional<QString> importFile(EqType type, const QString &path, QString *error = nullptr);
    bool exportFile(EqType type, const QString &name, const QString &path, QString *error = nullptr) const;

    // Empty string means no binding.
    QString binding(EqType type, EqMode mode) const;
    void setBinding(EqType type, EqMode mode, const QString &name);

    // Preset file format, shared by the library and import/export.
    static QByteArray encode(EqType type, const QString &name, const EqLevels &levels);
    static std::optional<std::pair<QString, EqLevels>> decode(const QByteArray &json);

signals:
    void presetsChanged(ts590::EqType type);
    void bindingsChanged(ts590::EqType type);

private:
    QString typeDirectory(EqType type) const;
    QString presetPath(EqType type, const QString &name) const;
    void load();
    void saveBindings();

    QString m_directory;
    QMap<QString, EqLevels> m_presets[2];   // keyed by name
    QMap<int, QString> m_bindings[2];       // EqMode -> preset name
};

}   // namespace ts590
