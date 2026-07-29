// Unit tests for formatWordCardHtml (dictionary card rendering).

#include "../src/WordFormatter.h"

#include <QtTest>

class TestWordFormatter : public QObject {
    Q_OBJECT

private slots:
    void fullCard();
    void missingFields();
    void invalidJsonIsEscaped();
    void htmlInjectionIsEscaped();
};

void TestWordFormatter::fullCard() {
    const QString html = formatWordCardHtml(QStringLiteral(R"({
        "word": "bank", "phonetic": "/bæŋk/", "pos": "n.",
        "meaning": "the land next to a river",
        "explanation": "Used for river sides.",
        "example": "We sat on the bank."
    })"));

    QVERIFY(html.contains("bank"));
    QVERIFY(html.contains("/bæŋk/"));
    QVERIFY(html.contains("n."));
    QVERIFY(html.contains("the land next to a river"));
    QVERIFY(html.contains("Used for river sides."));
    QVERIFY(html.contains("We sat on the bank."));
    // Structure: word header is bold and sized.
    QVERIFY(html.contains("font-weight:bold"));
    // No raw JSON braces leak into the card.
    QVERIFY(!html.contains("\"word\""));
}

void TestWordFormatter::missingFields() {
    const QString html = formatWordCardHtml(QStringLiteral(R"({"word": "bank"})"));
    QVERIFY(html.contains("bank"));
    // Missing fields produce no empty-section artifacts.
    QVERIFY(!html.contains("undefined"));
    QVERIFY(!html.contains("null"));
}

void TestWordFormatter::invalidJsonIsEscaped() {
    const QString html = formatWordCardHtml(QStringLiteral("not <valid> json"));
    QVERIFY(!html.contains("<valid>"));
    QVERIFY(html.contains("&lt;valid&gt;"));
}

void TestWordFormatter::htmlInjectionIsEscaped() {
    const QString html = formatWordCardHtml(
        QStringLiteral(R"({"word": "<b>x</b>", "meaning": "<script>alert(1)</script>"})"));
    QVERIFY(!html.contains("<script>"));
    QVERIFY(html.contains("&lt;script&gt;"));
    QVERIFY(!html.contains("<b>x</b>"));
}

QTEST_MAIN(TestWordFormatter)
#include "tst_wordformatter.moc"
