#include "editor.h"

#include <QEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>

namespace {
constexpr int kTabWidth = 4;
constexpr int kMaxSearchHighlights = 5000;
} // namespace

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(Editor *editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }

    QSize sizeHint() const override
    {
        return QSize(m_editor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        m_editor->lineNumberAreaPaintEvent(event);
    }

private:
    Editor *m_editor;
};

Editor::Editor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    m_lineNumberArea = new LineNumberArea(this);

    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setTabChangesFocus(false);
    setUndoRedoEnabled(true);
    setCenterOnScroll(true);
    setFrameShape(QFrame::NoFrame);

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (font.pointSize() <= 0)
        font.setPointSize(11);
    setEditorFont(font);

    connect(this, &QPlainTextEdit::blockCountChanged,
            this, &Editor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest,
            this, &Editor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        refreshExtraSelections();
        const QTextCursor cursor = textCursor();
        emit cursorMoved(cursor.blockNumber() + 1, cursor.positionInBlock() + 1);
    });
    connect(this, &QPlainTextEdit::modificationChanged,
            this, &Editor::modificationStateChanged);

    updateLineNumberAreaWidth(0);
    refreshExtraSelections();
}

void Editor::setEditorFont(const QFont &font)
{
    m_editorFont = font;
    setFont(font);
    document()->setDefaultFont(font);
    setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * kTabWidth);
    updateLineNumberAreaWidth(0);
}

void Editor::setLineNumbersVisible(bool visible)
{
    if (m_lineNumbersVisible == visible)
        return;

    m_lineNumbersVisible = visible;
    m_lineNumberArea->setVisible(visible);
    updateLineNumberAreaWidth(0);
    m_lineNumberArea->update();
}

int Editor::lineNumberAreaWidth() const
{
    if (!m_lineNumbersVisible)
        return 0;

    int digits = 1;
    int largest = qMax(1, blockCount());
    while (largest >= 10) {
        largest /= 10;
        ++digits;
    }

    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void Editor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);

    const QRect area = contentsRect();
    m_lineNumberArea->setGeometry(
        QRect(area.left(), area.top(), lineNumberAreaWidth(), area.height()));
}

void Editor::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);

    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange) {
        refreshExtraSelections();
        m_lineNumberArea->update();
    }
}

void Editor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void Editor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void Editor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);

    const QColor base = palette().color(QPalette::Base);
    const bool dark = base.lightness() < 128;
    const QColor background = dark ? base.lighter(125) : base.darker(104);
    const QColor numberColor = palette().color(QPalette::PlaceholderText);
    const QColor currentColor = palette().color(QPalette::Text);

    painter.fillRect(event->rect(), background);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int currentBlock = textCursor().blockNumber();
    const int lineHeight = fontMetrics().height();

    QFont numberFont = font();
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const bool current = blockNumber == currentBlock;
            numberFont.setBold(current);
            painter.setFont(numberFont);
            painter.setPen(current ? currentColor : numberColor);
            painter.drawText(0, top, m_lineNumberArea->width() - 6, lineHeight,
                             Qt::AlignRight, QString::number(blockNumber + 1));
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void Editor::refreshExtraSelections()
{
    QList<QTextEdit::ExtraSelection> selections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection currentLine;
        currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
        QColor color = palette().color(QPalette::Base);
        color = color.lightness() < 128 ? color.lighter(118) : color.darker(105);
        currentLine.format.setBackground(color);
        currentLine.cursor = textCursor();
        currentLine.cursor.clearSelection();
        selections.append(currentLine);
    }

    selections.append(m_searchSelections);
    setExtraSelections(selections);
}

void Editor::rebuildSearchSelections()
{
    m_searchSelections.clear();

    if (m_searchTerm.isEmpty())
        return;

    QTextDocument::FindFlags flags;
    if (m_caseSensitive)
        flags |= QTextDocument::FindCaseSensitively;

    QTextCursor cursor(document());
    int count = 0;
    while (!cursor.isNull() && count < kMaxSearchHighlights) {
        cursor = document()->find(m_searchTerm, cursor, flags);
        if (cursor.isNull())
            break;

        QTextEdit::ExtraSelection match;
        match.cursor = cursor;
        match.format.setBackground(QColor(255, 213, 79));
        match.format.setForeground(QColor(32, 32, 32));
        m_searchSelections.append(match);
        ++count;
    }
}

void Editor::setSearchTerm(const QString &term, bool caseSensitive)
{
    m_searchTerm = term;
    m_caseSensitive = caseSensitive;
    rebuildSearchSelections();
    refreshExtraSelections();
}

void Editor::clearSearch()
{
    m_searchTerm.clear();
    m_searchSelections.clear();
    refreshExtraSelections();
}

void Editor::refreshTheme()
{
    updateLineNumberAreaWidth(0);
    m_lineNumberArea->update();
    refreshExtraSelections();
}

void Editor::indentSelection(bool indent)
{
    QTextCursor cursor = textCursor();
    int firstBlock = document()->findBlock(cursor.selectionStart()).blockNumber();
    int lastBlock = document()->findBlock(cursor.selectionEnd()).blockNumber();

    if (!cursor.hasSelection()) {
        firstBlock = cursor.blockNumber();
        lastBlock = firstBlock;
    }

    cursor.beginEditBlock();
    for (int number = firstBlock; number <= lastBlock; ++number) {
        const QTextBlock block = document()->findBlockByNumber(number);
        if (!block.isValid())
            continue;

        QTextCursor edit(block);
        edit.movePosition(QTextCursor::StartOfBlock);

        if (indent) {
            edit.insertText(QString(kTabWidth, QLatin1Char(' ')));
        } else {
            for (int i = 0; i < kTabWidth; ++i) {
                edit.movePosition(QTextCursor::StartOfBlock);
                const QString text = edit.block().text();
                if (text.startsWith(QLatin1Char(' ')) || text.startsWith(QLatin1Char('\t')))
                    edit.deleteChar();
                else
                    break;
            }
        }
    }
    cursor.endEditBlock();
}

void Editor::keyPressEvent(QKeyEvent *event)
{
    const bool hasSelection = textCursor().hasSelection();
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);

    if (event->key() == Qt::Key_Backtab) {
        indentSelection(false);
        return;
    }

    if (event->key() == Qt::Key_Tab && !control) {
        if (hasSelection || event->modifiers().testFlag(Qt::ShiftModifier)) {
            indentSelection(!event->modifiers().testFlag(Qt::ShiftModifier));
        } else {
            const int column = textCursor().positionInBlock();
            const int spaces = kTabWidth - (column % kTabWidth);
            insertPlainText(QString(spaces, QLatin1Char(' ')));
        }
        return;
    }

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !hasSelection
        && !event->modifiers().testFlag(Qt::ControlModifier)
        && !event->modifiers().testFlag(Qt::AltModifier)) {
        const QTextCursor cursor = textCursor();
        const QString before = cursor.block().text().left(cursor.positionInBlock());

        QString indent;
        for (const QChar ch : before) {
            if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t'))
                indent += ch;
            else
                break;
        }

        QString extra;
        const QString trimmed = before.trimmed();
        if (trimmed.endsWith(QLatin1Char('{')) || trimmed.endsWith(QLatin1Char(':')))
            extra = QString(kTabWidth, QLatin1Char(' '));

        QPlainTextEdit::keyPressEvent(event);
        if (!indent.isEmpty() || !extra.isEmpty())
            insertPlainText(indent + extra);
        return;
    }

    if (event->key() == Qt::Key_BraceRight && !hasSelection) {
        QTextCursor cursor = textCursor();
        const QString before = cursor.block().text().left(cursor.positionInBlock());
        if (!before.isEmpty() && before.trimmed().isEmpty()) {
            int remove = 0;
            while (remove < kTabWidth && remove < before.size()
                   && before.at(before.size() - 1 - remove) == QLatin1Char(' ')) {
                ++remove;
            }
            if (remove > 0) {
                cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, remove);
                cursor.removeSelectedText();
            }
        }
    }

    QPlainTextEdit::keyPressEvent(event);
}
