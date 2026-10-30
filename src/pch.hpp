#pragma once
// pch.hpp -- the one precompiled header, for compile TIME only.
//
// Measured problem: this tree is ~350 translation units on a 2-physical-core
// box, and nearly every one of them re-parses the same Qt headers (QString,
// QJsonObject, QNetworkReply, QTimer ...) plus <string>/<vector>/<optional>.
// That parse is the bulk of a file's compile and it is identical every time.
//
// Rules for editing this file:
//   * Headers only. No macros, no `using`, nothing that changes a translation
//     unit's meaning -- a PCH is shared by every TU in the target, so anything
//     with a side effect here leaks into files that never asked for it.
//   * Prefer the headers that showed up in `ninja -t deps` most often. Adding a
//     header that only one file uses makes every other file pay for it.
//   * Anything project-specific belongs in the file that needs it. This header
//     is a build accelerator, not an include convention.
//
// Not included on purpose: QtQuick/QtQml/QtWidgets. They are large, they are
// used by a minority of TUs, and pulling them in here would slow the majority
// to speed up the rest.

#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
