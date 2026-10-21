#pragma once

#include <QFont>
#include <QList>
#include <QPlainTextEdit>
#include <QString>
#include <QTextEdit>

class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QWidget;

class Editor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit Editor(QWidget *parent = nullptr);

    void setEditorFont(const QFont &font);

    void setLineNumbersVisible(bool visible);
    bool lineNumbersVisible() const { return m_lineNumbersVisible; }

    void setSearchTerm(const QString &term, bool caseSensitive);
    void clearSearch();

    void refreshTheme();

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

signals:
    void cursorMoved(int line, int column);
    void modificationStateChanged(bool modified);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);
    void refreshExtraSelections();

private:
    void indentSelection(bool indent);
    void rebuildSearchSelections();

    QWidget *m_lineNumberArea = nullptr;
    bool m_lineNumbersVisible = true;

    QString m_searchTerm;
    bool m_caseSensitive = false;
    QList<QTextEdit::ExtraSelection> m_searchSelections;

    QFont m_editorFont;
};
