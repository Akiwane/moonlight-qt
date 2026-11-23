// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtGlobal>

namespace JAHDeskControl {
// The parent creates a private, per-session directory and supplies only this file path.
inline bool enabled()
{
    return qEnvironmentVariableIsSet("JAHDESK_STREAM_CONTROL");
}

// An absolute mouse starts with its local cursor visible unless the user hid it explicitly.
inline bool showLocalCursor()
{
    return qgetenv("JAHDESK_SHOW_LOCAL_CURSOR") != "0";
}

// The parent reads the same file after exit and can resume the app at a new resolution.
inline bool save(bool embedded, bool absolute, bool localCursor, int width = 0, int height = 0)
{
    QSaveFile file(qEnvironmentVariable("JAHDESK_STREAM_CONTROL"));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QJsonObject state {
        { QStringLiteral("embedded"), embedded },
        { QStringLiteral("absolute"), absolute },
        { QStringLiteral("localCursor"), localCursor },
    };
    if (width && height) {
        state.insert(QStringLiteral("restart"), QJsonObject {
            { QStringLiteral("width"), width },
            { QStringLiteral("height"), height },
        });
    }
    const auto bytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}
}
