#include "WordFormatter.h"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>

using namespace Qt::StringLiterals;

QString extractJsonPayload(const QString &raw) {
    const int first = raw.indexOf(u'{');
    const int last = raw.lastIndexOf(u'}');
    if (first < 0 || last <= first)
        return raw.trimmed();
    return raw.mid(first, last - first + 1);
}

QString formatWordCardHtml(const QString &jsonPayload) {
    const QJsonDocument doc = QJsonDocument::fromJson(jsonPayload.toUtf8());
    if (!doc.isObject())
        return jsonPayload.toHtmlEscaped(); // model did not produce valid JSON

    // Palette-driven, so the card follows the system theme (Adwaita
    // light/dark) instead of hardcoded colors.
    const QPalette palette = qApp->palette();
    const QString accent = palette.color(QPalette::Link).name();
    const QString muted = palette.color(QPalette::PlaceholderText).name();

    const QJsonObject obj = doc.object();
    const auto field = [&obj](const char *key) {
        return obj[QLatin1StringView(key)].toString().toHtmlEscaped();
    };

    QString html = "<div style='margin-bottom:6px'>"
                   "<span style='font-size:18px; font-weight:bold; color:"
        + accent + "'>" + field("word") + "</span>";
    const QString phonetic = field("phonetic");
    if (!phonetic.isEmpty())
        html += "  <span style='color:" + muted + "'>" + phonetic + "</span>";
    const QString pos = field("pos");
    if (!pos.isEmpty())
        html += "  <span style='color:" + muted + "'>" + pos + "</span>";
    html += "</div>";

    const QString meaning = field("meaning");
    if (!meaning.isEmpty())
        html += "<div style='font-weight:600; margin-top:8px; margin-bottom:8px'>" + meaning
            + "</div>";
    const QString explanation = field("explanation");
    if (!explanation.isEmpty())
        html += "<div style='margin-bottom:8px'>" + explanation + "</div>";
    const QString example = field("example");
    if (!example.isEmpty())
        html += "<div style='color:" + muted + "'>" + example + "</div>";
    return html;
}
