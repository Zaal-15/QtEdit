#include "syntaxhighlighter.h"

#include <QFileInfo>

namespace {

QTextCharFormat makeFormat(const QColor &color, bool bold = false, bool italic = false)
{
    QTextCharFormat format;
    format.setForeground(color);
    if (bold)
        format.setFontWeight(QFont::Bold);
    if (italic)
        format.setFontItalic(true);
    return format;
}

} // namespace

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    rebuildRules();
}

void SyntaxHighlighter::setLanguage(Language language)
{
    if (m_language == language)
        return;

    m_language = language;
    rebuildRules();
    rehighlight();
}

void SyntaxHighlighter::setDark(bool dark)
{
    if (m_dark == dark)
        return;

    m_dark = dark;
    rebuildRules();
    rehighlight();
}

SyntaxHighlighter::Language SyntaxHighlighter::languageForFile(const QString &fileName)
{
    if (fileName.isEmpty())
        return Language::Plain;

    const QFileInfo info(fileName);
    if (info.fileName().compare(QLatin1String("CMakeLists.txt"), Qt::CaseInsensitive) == 0)
        return Language::Plain;

    const QString suffix = info.suffix().toLower();

    if (suffix == QLatin1String("c") || suffix == QLatin1String("cc")
        || suffix == QLatin1String("cpp") || suffix == QLatin1String("cxx")
        || suffix == QLatin1String("h") || suffix == QLatin1String("hh")
        || suffix == QLatin1String("hpp") || suffix == QLatin1String("hxx")
        || suffix == QLatin1String("m") || suffix == QLatin1String("mm")
        || suffix == QLatin1String("ino")) {
        return Language::Cpp;
    }

    if (suffix == QLatin1String("py") || suffix == QLatin1String("pyw"))
        return Language::Python;

    if (suffix == QLatin1String("json") || suffix == QLatin1String("jsonc"))
        return Language::Json;

    if (suffix == QLatin1String("sh") || suffix == QLatin1String("bash")
        || suffix == QLatin1String("zsh") || suffix == QLatin1String("ksh")) {
        return Language::Shell;
    }

    return Language::Plain;
}

QString SyntaxHighlighter::languageName(Language language)
{
    switch (language) {
    case Language::Cpp:
        return QStringLiteral("C/C++");
    case Language::Python:
        return QStringLiteral("Python");
    case Language::Json:
        return QStringLiteral("JSON");
    case Language::Shell:
        return QStringLiteral("Shell Script");
    case Language::Plain:
    default:
        return QStringLiteral("Plain Text");
    }
}

void SyntaxHighlighter::rebuildRules()
{
    m_rules.clear();

    const QColor keywordColor = m_dark ? QColor(QStringLiteral("#569cd6"))
                                       : QColor(QStringLiteral("#0033b3"));
    const QColor typeColor = m_dark ? QColor(QStringLiteral("#4ec9b0"))
                                    : QColor(QStringLiteral("#267f99"));
    const QColor stringColor = m_dark ? QColor(QStringLiteral("#ce9178"))
                                      : QColor(QStringLiteral("#a31515"));
    const QColor numberColor = m_dark ? QColor(QStringLiteral("#b5cea8"))
                                      : QColor(QStringLiteral("#098658"));
    const QColor functionColor = m_dark ? QColor(QStringLiteral("#dcdcaa"))
                                        : QColor(QStringLiteral("#795e26"));
    const QColor variableColor = m_dark ? QColor(QStringLiteral("#9cdcfe"))
                                        : QColor(QStringLiteral("#001080"));
    const QColor preprocessorColor = m_dark ? QColor(QStringLiteral("#c586c0"))
                                            : QColor(QStringLiteral("#9e880d"));
    m_commentFormat = makeFormat(
        m_dark ? QColor(QStringLiteral("#6a9955")) : QColor(QStringLiteral("#008000")),
        false, true);

    if (m_language == Language::Plain)
        return;

    const QTextCharFormat keywordFormat = makeFormat(keywordColor, true);
    const QTextCharFormat typeFormat = makeFormat(typeColor);
    const QTextCharFormat stringFormat = makeFormat(stringColor);
    const QTextCharFormat numberFormat = makeFormat(numberColor);
    const QTextCharFormat functionFormat = makeFormat(functionColor);
    const QTextCharFormat variableFormat = makeFormat(variableColor);
    const QTextCharFormat preprocessorFormat = makeFormat(preprocessorColor);

    const auto add = [this](const QString &pattern, const QTextCharFormat &format) {
        m_rules.append({QRegularExpression(pattern), format});
    };

    switch (m_language) {
    case Language::Cpp:
        add(QStringLiteral(R"(\b[A-Za-z_]\w*\s*(?=\())"), functionFormat);
        add(QStringLiteral(R"(\b(?:Q[A-Z]\w*|std|size_t|int\d*_t|uint\d*_t|string|vector|map|unique_ptr|shared_ptr|optional|variant)\b)"),
            typeFormat);
        add(QStringLiteral(R"(\b(?:alignas|alignof|and|and_eq|asm|auto|bitand|bitor|bool|break|case|catch|char|char8_t|char16_t|char32_t|class|compl|concept|const|consteval|constexpr|constinit|const_cast|continue|co_await|co_return|co_yield|decltype|default|delete|do|double|dynamic_cast|else|enum|explicit|export|extern|false|float|for|friend|goto|if|inline|int|long|mutable|namespace|new|noexcept|not|not_eq|nullptr|operator|or|or_eq|private|protected|public|register|reinterpret_cast|requires|return|short|signed|sizeof|static|static_assert|static_cast|struct|switch|template|this|thread_local|throw|true|try|typedef|typeid|typename|union|unsigned|using|virtual|void|volatile|wchar_t|while|xor|xor_eq)\b)"),
            keywordFormat);
        add(QStringLiteral(R"(^\s*#\s*\w+)"), preprocessorFormat);
        add(QStringLiteral(R"(\b(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)[uUlLfF]*\b)"),
            numberFormat);
        add(QStringLiteral(R"("(?:\\.|[^"\\])*")"), stringFormat);
        add(QStringLiteral(R"('(?:\\.|[^'\\])*')"), stringFormat);
        add(QStringLiteral(R"(//.*)"), m_commentFormat);
        break;

    case Language::Python:
        add(QStringLiteral(R"(\b[A-Za-z_]\w*(?=\s*\())"), functionFormat);
        add(QStringLiteral(R"(\b(?:and|as|assert|async|await|break|class|continue|def|del|elif|else|except|False|finally|for|from|global|if|import|in|is|lambda|None|nonlocal|not|or|pass|raise|return|True|try|while|with|yield|match|case)\b)"),
            keywordFormat);
        add(QStringLiteral(R"(\b(?:print|len|range|str|int|float|list|dict|set|tuple|open|super|self|cls|enumerate|zip|map|filter|sum|min|max|abs|any|all|type|isinstance|getattr|setattr|hasattr|Exception|ValueError|TypeError|KeyError|IndexError|RuntimeError)\b)"),
            typeFormat);
        add(QStringLiteral(R"(@[\w.]+)"), preprocessorFormat);
        add(QStringLiteral(R"(\b(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\b)"),
            numberFormat);
        add(QStringLiteral(R"("""[\s\S]*?"""|'''[\s\S]*?''')"), stringFormat);
        add(QStringLiteral(R"("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')"), stringFormat);
        add(QStringLiteral(R"(#.*)"), m_commentFormat);
        break;

    case Language::Json:
        add(QStringLiteral(R"("(?:\\.|[^"\\])*")"), stringFormat);
        add(QStringLiteral(R"(\b-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?\b)"),
            numberFormat);
        add(QStringLiteral(R"(\b(?:true|false|null)\b)"), keywordFormat);
        add(QStringLiteral(R"("(?:\\.|[^"\\])*"(?=\s*:))"), variableFormat);
        break;

    case Language::Shell:
        add(QStringLiteral(R"(\b(?:if|then|else|elif|fi|for|while|do|done|case|esac|function|in|select|until|time|coproc)\b)"),
            keywordFormat);
        add(QStringLiteral(R"(\b(?:echo|printf|read|cd|export|local|return|exit|source|set|unset|shift|test)\b)"),
            typeFormat);
        add(QStringLiteral(R"("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')"), stringFormat);
        add(QStringLiteral(R"(\$\{?[A-Za-z_]\w*\}?)"), variableFormat);
        add(QStringLiteral(R"(\b\d+\b)"), numberFormat);
        add(QStringLiteral(R"(#.*)"), m_commentFormat);
        break;

    case Language::Plain:
        break;
    }
}

void SyntaxHighlighter::applyRules(const QString &text, const QVector<Rule> &rules)
{
    for (const Rule &rule : rules) {
        QRegularExpressionMatchIterator iterator = rule.pattern.globalMatch(text);
        while (iterator.hasNext()) {
            const QRegularExpressionMatch match = iterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}

void SyntaxHighlighter::highlightCppComments(const QString &text)
{
    int index = 0;

    if (previousBlockState() == 1) {
        const int end = text.indexOf(QLatin1String("*/"));
        if (end < 0) {
            setFormat(0, text.length(), m_commentFormat);
            setCurrentBlockState(1);
            return;
        }
        setFormat(0, end + 2, m_commentFormat);
        index = end + 2;
    }

    while (true) {
        const int start = text.indexOf(QLatin1String("/*"), index);
        if (start < 0)
            break;

        const int end = text.indexOf(QLatin1String("*/"), start + 2);
        if (end < 0) {
            setFormat(start, text.length() - start, m_commentFormat);
            setCurrentBlockState(1);
            return;
        }

        setFormat(start, end + 2 - start, m_commentFormat);
        index = end + 2;
    }

    setCurrentBlockState(0);
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
    setCurrentBlockState(0);

    if (m_language == Language::Plain)
        return;

    applyRules(text, m_rules);

    if (m_language == Language::Cpp)
        highlightCppComments(text);
}
