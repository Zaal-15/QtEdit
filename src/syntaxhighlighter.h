#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class SyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    enum class Language {
        Plain,
        Cpp,
        Python,
        Json,
        Shell
    };

    explicit SyntaxHighlighter(QTextDocument *document = nullptr);

    void setLanguage(Language language);
    Language language() const { return m_language; }

    void setDark(bool dark);
    bool dark() const { return m_dark; }

    static Language languageForFile(const QString &fileName);
    static QString languageName(Language language);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void rebuildRules();
    void applyRules(const QString &text, const QVector<Rule> &rules);
    void highlightCppComments(const QString &text);

    Language m_language = Language::Plain;
    bool m_dark = false;
    QVector<Rule> m_rules;
    QTextCharFormat m_commentFormat;
};
