#include "mainwindow.h"

#include "editor.h"
#include "syntaxhighlighter.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSaveFile>
#include <QShortcut>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QIcon themedIcon(const QString &name, QStyle::StandardPixmap fallback, QWidget *widget)
{
    const QIcon icon = QIcon::fromTheme(name);
    return icon.isNull() ? widget->style()->standardIcon(fallback) : icon;
}

QIcon themedIcon(const QString &name)
{
    return QIcon::fromTheme(name);
}

QFont defaultEditorFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (font.pointSize() <= 0)
        font.setPointSize(11);
    return font;
}

QString openFileFilter()
{
    return QStringLiteral(
        "All Files (*);;Text Files (*.txt *.md);;C/C++ Files (*.c *.cc *.cpp *.cxx *.h *.hh *.hpp *.hxx);;"
        "Python Files (*.py *.pyw);;Shell Scripts (*.sh *.bash *.zsh);;JSON Files (*.json)");
}

QString saveFileFilter()
{
    return QStringLiteral(
        "Text Files (*.txt);;C/C++ Files (*.cpp *.h);;Python Files (*.py);;"
        "Shell Scripts (*.sh);;JSON Files (*.json);;All Files (*)");
}

QString startDirectory(const QString &filePath)
{
    if (filePath.isEmpty())
        return QDir::homePath();
    return QFileInfo(filePath).absolutePath();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setAcceptDrops(true);
    resize(1024, 720);

    m_lightPalette = qApp->palette();

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_editor = new Editor(central);
    m_findWidget = createFindBar(central);
    layout->addWidget(m_findWidget);
    layout->addWidget(m_editor, 1);
    setCentralWidget(central);

    m_highlighter = new SyntaxHighlighter(m_editor->document());

    createActions();
    createMenus();
    createToolBar();
    createStatusBar();

    m_wordCountTimer = new QTimer(this);
    m_wordCountTimer->setSingleShot(true);
    m_wordCountTimer->setInterval(200);
    connect(m_wordCountTimer, &QTimer::timeout, this, &MainWindow::updateWordCount);

    m_highlightTimer = new QTimer(this);
    m_highlightTimer->setSingleShot(true);
    m_highlightTimer->setInterval(250);
    connect(m_highlightTimer, &QTimer::timeout, this, &MainWindow::refreshFindHighlights);

    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        updateStatus();
        m_wordCountTimer->start();
        if (m_findWidget->isVisible() && !m_findEdit->text().isEmpty())
            m_highlightTimer->start();
    });
    connect(m_editor, &QPlainTextEdit::modificationChanged, this, [this](bool) {
        updateTitle();
        updateStatus();
    });
    connect(m_editor, &Editor::cursorMoved, this, [this](int line, int column) {
        m_positionLabel->setText(tr("Ln %1, Col %2").arg(line).arg(column));
    });
    connect(m_editor, &QPlainTextEdit::undoAvailable, m_undoAction, &QAction::setEnabled);
    connect(m_editor, &QPlainTextEdit::redoAvailable, m_redoAction, &QAction::setEnabled);
    connect(m_editor, &QPlainTextEdit::copyAvailable, this, [this](bool available) {
        m_cutAction->setEnabled(available);
        m_copyAction->setEnabled(available);
    });

    restoreSettings();
    updateTitle();
    updateLanguage();
    updateStatus();
    updateWordCount();

    m_editor->setFocus();
}

void MainWindow::createActions()
{
    const auto makeAction = [this](const QString &text,
                                   const QKeySequence &shortcut = QKeySequence()) {
        auto *action = new QAction(text, this);
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        return action;
    };

    m_newAction = makeAction(tr("&New"), QKeySequence::New);
    m_newAction->setIcon(themedIcon(QStringLiteral("document-new"), QStyle::SP_FileIcon, this));
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newFile);

    m_openAction = makeAction(tr("&Open…"), QKeySequence::Open);
    m_openAction->setIcon(
        themedIcon(QStringLiteral("document-open"), QStyle::SP_DialogOpenButton, this));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openDialog);

    m_saveAction = makeAction(tr("&Save"), QKeySequence::Save);
    m_saveAction->setIcon(
        themedIcon(QStringLiteral("document-save"), QStyle::SP_DialogSaveButton, this));
    connect(m_saveAction, &QAction::triggered, this, [this] { save(); });

    m_saveAsAction = makeAction(tr("Save &As…"), QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, [this] { saveAs(); });

    m_closeAction = makeAction(tr("&Close Window"), QKeySequence::Close);
    connect(m_closeAction, &QAction::triggered, this, &QWidget::close);

    m_quitAction = makeAction(tr("&Quit"), QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = makeAction(tr("&Undo"), QKeySequence::Undo);
    m_undoAction->setIcon(
        themedIcon(QStringLiteral("edit-undo"), QStyle::SP_ArrowBack, this));
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, m_editor, &QPlainTextEdit::undo);

    m_redoAction = makeAction(tr("&Redo"));
    m_redoAction->setShortcuts(
        {QKeySequence(QKeySequence::Redo), QKeySequence(QStringLiteral("Ctrl+Y"))});
    m_redoAction->setIcon(
        themedIcon(QStringLiteral("edit-redo"), QStyle::SP_ArrowForward, this));
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, m_editor, &QPlainTextEdit::redo);

    m_cutAction = makeAction(tr("Cu&t"), QKeySequence::Cut);
    m_cutAction->setIcon(themedIcon(QStringLiteral("edit-cut")));
    m_cutAction->setEnabled(false);
    connect(m_cutAction, &QAction::triggered, m_editor, &QPlainTextEdit::cut);

    m_copyAction = makeAction(tr("&Copy"), QKeySequence::Copy);
    m_copyAction->setIcon(themedIcon(QStringLiteral("edit-copy")));
    m_copyAction->setEnabled(false);
    connect(m_copyAction, &QAction::triggered, m_editor, &QPlainTextEdit::copy);

    m_pasteAction = makeAction(tr("&Paste"), QKeySequence::Paste);
    m_pasteAction->setIcon(themedIcon(QStringLiteral("edit-paste")));
    connect(m_pasteAction, &QAction::triggered, m_editor, &QPlainTextEdit::paste);

    m_selectAllAction = makeAction(tr("Select &All"), QKeySequence::SelectAll);
    connect(m_selectAllAction, &QAction::triggered, m_editor, &QPlainTextEdit::selectAll);

    m_findAction = makeAction(tr("&Find…"), QKeySequence::Find);
    m_findAction->setIcon(
        themedIcon(QStringLiteral("edit-find"), QStyle::SP_FileDialogContentsView, this));
    connect(m_findAction, &QAction::triggered, this, &MainWindow::showFindBar);

    m_findNextAction = makeAction(tr("Find &Next"), QKeySequence(QStringLiteral("F3")));
    connect(m_findNextAction, &QAction::triggered, this, &MainWindow::findNext);

    m_findPreviousAction = makeAction(tr("Find &Previous"),
                                      QKeySequence(QStringLiteral("Shift+F3")));
    connect(m_findPreviousAction, &QAction::triggered, this, &MainWindow::findPrevious);

    m_goToLineAction = makeAction(tr("&Go to Line…"), QKeySequence(QStringLiteral("Ctrl+G")));
    connect(m_goToLineAction, &QAction::triggered, this, &MainWindow::goToLine);

    m_wordWrapAction = makeAction(tr("&Word Wrap"), QKeySequence(QStringLiteral("Ctrl+Alt+W")));
    m_wordWrapAction->setCheckable(true);
    m_wordWrapAction->setChecked(true);
    connect(m_wordWrapAction, &QAction::toggled, this, &MainWindow::setWordWrap);

    m_lineNumbersAction = makeAction(tr("Line &Numbers"));
    m_lineNumbersAction->setCheckable(true);
    m_lineNumbersAction->setChecked(true);
    connect(m_lineNumbersAction, &QAction::toggled, this, &MainWindow::setLineNumbers);

    m_darkModeAction = makeAction(tr("&Dark Mode"));
    m_darkModeAction->setCheckable(true);
    connect(m_darkModeAction, &QAction::toggled, this, &MainWindow::setDarkMode);

    m_fontAction = makeAction(tr("Choose &Font…"));
    connect(m_fontAction, &QAction::triggered, this, &MainWindow::chooseFont);

    m_zoomInAction = makeAction(tr("Zoom &In"));
    m_zoomInAction->setShortcuts(
        {QKeySequence(QKeySequence::ZoomIn), QKeySequence(QStringLiteral("Ctrl+="))});
    connect(m_zoomInAction, &QAction::triggered, this, [this] { zoomBy(1); });

    m_zoomOutAction = makeAction(tr("Zoom &Out"), QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, [this] { zoomBy(-1); });

    m_zoomResetAction = makeAction(tr("&Reset Zoom"), QKeySequence(QStringLiteral("Ctrl+0")));
    connect(m_zoomResetAction, &QAction::triggered, this, &MainWindow::resetZoom);

    m_fullScreenAction = makeAction(tr("&Full Screen"), QKeySequence(QStringLiteral("F11")));
    m_fullScreenAction->setCheckable(true);
    connect(m_fullScreenAction, &QAction::toggled, this, [this](bool enabled) {
        if (enabled)
            showFullScreen();
        else
            showNormal();
    });

    m_aboutAction = makeAction(tr("&About QtEdit"));
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    m_aboutQtAction = makeAction(tr("About &Qt"));
    connect(m_aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);

    // Editing shortcuts belong to the editor, so they do not steal key presses
    // (Ctrl+Z, Ctrl+A, ...) from the find field or other child widgets.
    const QList<QAction *> editorActions = {
        m_undoAction, m_redoAction, m_cutAction, m_copyAction,
        m_pasteAction, m_selectAllAction
    };
    for (QAction *action : editorActions) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_editor->addAction(action);
    }
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_closeAction);
    fileMenu->addAction(m_quitAction);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_cutAction);
    editMenu->addAction(m_copyAction);
    editMenu->addAction(m_pasteAction);
    editMenu->addSeparator();
    editMenu->addAction(m_selectAllAction);
    editMenu->addSeparator();
    editMenu->addAction(m_findAction);
    editMenu->addAction(m_findNextAction);
    editMenu->addAction(m_findPreviousAction);
    editMenu->addAction(m_goToLineAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_wordWrapAction);
    viewMenu->addAction(m_lineNumbersAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_darkModeAction);
    viewMenu->addAction(m_fontAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addAction(m_zoomResetAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fullScreenAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(m_aboutAction);
    helpMenu->addAction(m_aboutQtAction);
}

void MainWindow::createToolBar()
{
    auto *toolBar = addToolBar(tr("Main"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setIconSize(QSize(18, 18));
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    toolBar->addAction(m_newAction);
    toolBar->addAction(m_openAction);
    toolBar->addAction(m_saveAction);
    toolBar->addSeparator();
    toolBar->addAction(m_undoAction);
    toolBar->addAction(m_redoAction);
    toolBar->addSeparator();
    toolBar->addAction(m_cutAction);
    toolBar->addAction(m_copyAction);
    toolBar->addAction(m_pasteAction);
    toolBar->addSeparator();
    toolBar->addAction(m_findAction);
}

void MainWindow::createStatusBar()
{
    m_positionLabel = new QLabel(tr("Ln 1, Col 1"), this);
    m_wordsLabel = new QLabel(tr("0 words"), this);
    m_languageLabel = new QLabel(tr("Plain Text"), this);
    m_encodingLabel = new QLabel(m_encoding, this);
    m_modifiedLabel = new QLabel(this);

    m_positionLabel->setMinimumWidth(110);
    m_wordsLabel->setMinimumWidth(90);
    m_languageLabel->setMinimumWidth(100);

    statusBar()->addWidget(m_positionLabel);
    statusBar()->addWidget(m_wordsLabel);
    statusBar()->addWidget(m_languageLabel);
    statusBar()->addPermanentWidget(m_modifiedLabel);
    statusBar()->addPermanentWidget(m_encodingLabel);
}

QWidget *MainWindow::createFindBar(QWidget *parent)
{
    auto *bar = new QWidget(parent);
    bar->setObjectName(QStringLiteral("findBar"));
    bar->setAutoFillBackground(true);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(6);

    auto *label = new QLabel(tr("Find:"), bar);

    m_findEdit = new QLineEdit(bar);
    m_findEdit->setPlaceholderText(tr("Search in document…"));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setMinimumWidth(220);

    m_caseCheck = new QCheckBox(tr("Match case"), bar);

    auto *previousButton = new QToolButton(bar);
    previousButton->setIcon(themedIcon(QStringLiteral("go-up"), QStyle::SP_ArrowUp, this));
    previousButton->setToolTip(tr("Find previous (Shift+F3)"));
    previousButton->setAutoRaise(true);

    auto *nextButton = new QToolButton(bar);
    nextButton->setIcon(themedIcon(QStringLiteral("go-down"), QStyle::SP_ArrowDown, this));
    nextButton->setToolTip(tr("Find next (F3)"));
    nextButton->setAutoRaise(true);

    auto *closeButton = new QToolButton(bar);
    closeButton->setIcon(
        themedIcon(QStringLiteral("window-close"), QStyle::SP_DialogCloseButton, this));
    closeButton->setToolTip(tr("Close find bar (Esc)"));
    closeButton->setAutoRaise(true);

    m_findStatus = new QLabel(bar);
    m_findStatus->setMinimumWidth(80);

    layout->addWidget(label);
    layout->addWidget(m_findEdit, 1);
    layout->addWidget(m_caseCheck);
    layout->addWidget(previousButton);
    layout->addWidget(nextButton);
    layout->addWidget(m_findStatus);
    layout->addWidget(closeButton);

    connect(m_findEdit, &QLineEdit::textChanged, this, [this] {
        m_findStatus->clear();
        refreshFindHighlights();
    });
    connect(m_findEdit, &QLineEdit::returnPressed, this, &MainWindow::findNext);
    connect(m_caseCheck, &QCheckBox::toggled, this, [this] { refreshFindHighlights(); });
    connect(nextButton, &QToolButton::clicked, this, &MainWindow::findNext);
    connect(previousButton, &QToolButton::clicked, this, &MainWindow::findPrevious);
    connect(closeButton, &QToolButton::clicked, this, &MainWindow::closeFindBar);

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), bar);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &MainWindow::closeFindBar);

    auto *shiftReturn = new QShortcut(QKeySequence(QStringLiteral("Shift+Return")), bar);
    shiftReturn->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shiftReturn, &QShortcut::activated, this, &MainWindow::findPrevious);

    bar->hide();
    return bar;
}

void MainWindow::restoreSettings()
{
    QFont font = m_settings.value(QStringLiteral("editor/font"), defaultEditorFont()).value<QFont>();
    if (font.pointSize() <= 0)
        font.setPointSize(11);
    m_editor->setEditorFont(font);

    const bool wrap = m_settings.value(QStringLiteral("view/wordWrap"), true).toBool();
    m_wordWrapAction->setChecked(wrap);
    setWordWrap(wrap);

    const bool lineNumbers = m_settings.value(QStringLiteral("view/lineNumbers"), true).toBool();
    m_lineNumbersAction->setChecked(lineNumbers);
    setLineNumbers(lineNumbers);

    m_darkMode = m_settings.value(QStringLiteral("view/darkMode"), false).toBool();
    m_darkModeAction->setChecked(m_darkMode);
    setDarkMode(m_darkMode);

    const QByteArray geometry = m_settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    updateRecentMenu();
}

void MainWindow::saveSettings()
{
    m_settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    m_settings.setValue(QStringLiteral("editor/font"), m_editor->font());
    m_settings.setValue(QStringLiteral("view/wordWrap"), m_wordWrapAction->isChecked());
    m_settings.setValue(QStringLiteral("view/lineNumbers"), m_lineNumbersAction->isChecked());
    m_settings.setValue(QStringLiteral("view/darkMode"), m_darkMode);
    m_settings.sync();
}

void MainWindow::newFile()
{
    if (!maybeSave())
        return;

    m_filePath.clear();
    m_encoding = QStringLiteral("UTF-8");
    m_editor->clear();
    m_editor->document()->setModified(false);
    m_editor->clearSearch();
    m_editor->setFocus();

    if (m_findWidget->isVisible()) {
        m_findEdit->clear();
        m_findStatus->clear();
    }

    updateTitle();
    updateLanguage();
    updateStatus();
    updateWordCount();
}

void MainWindow::openDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open File"), startDirectory(m_filePath), openFileFilter());
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::openFile(const QString &path)
{
    if (!maybeSave())
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Open Failed"),
                              tr("Could not open the file:\n%1\n\n%2")
                                  .arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    m_encoding = QStringLiteral("UTF-8");
    if (data.startsWith(QByteArray::fromHex("EFBBBF"))) {
        data.remove(0, 3);
        m_encoding = QStringLiteral("UTF-8 BOM");
    }

    m_filePath = QFileInfo(path).absoluteFilePath();
    m_editor->setPlainText(QString::fromUtf8(data));
    m_editor->document()->setModified(false);
    m_editor->moveCursor(QTextCursor::Start);
    m_editor->clearSearch();

    addRecentFile(m_filePath);
    updateTitle();
    updateLanguage();
    updateStatus();
    updateWordCount();
    m_encodingLabel->setText(m_encoding);

    statusBar()->showMessage(tr("Opened %1").arg(QFileInfo(m_filePath).fileName()), 2000);
    return true;
}

bool MainWindow::save()
{
    if (m_filePath.isEmpty())
        return saveAs();
    return writeFile(m_filePath);
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save As"), startDirectory(m_filePath), saveFileFilter());
    if (path.isEmpty())
        return false;
    return writeFile(path);
}

bool MainWindow::writeFile(const QString &path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Save Failed"),
                              tr("Could not save the file:\n%1\n\n%2")
                                  .arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }

    QByteArray data = m_editor->toPlainText().toUtf8();
    if (m_encoding == QLatin1String("UTF-8 BOM"))
        data.prepend(QByteArray::fromHex("EFBBBF"));

    if (file.write(data) != data.size() || !file.commit()) {
        QMessageBox::critical(this, tr("Save Failed"),
                              tr("Could not save the file:\n%1\n\n%2")
                                  .arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }

    m_filePath = QFileInfo(path).absoluteFilePath();
    m_editor->document()->setModified(false);
    addRecentFile(m_filePath);
    updateTitle();
    updateLanguage();
    updateStatus();

    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(m_filePath).fileName()), 2000);
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_editor->document()->isModified())
        return true;

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, tr("Unsaved Changes"),
        tr("The document has been modified.\nDo you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::showFindBar()
{
    if (!m_findWidget->isVisible())
        m_findWidget->show();

    m_findEdit->setFocus(Qt::ShortcutFocusReason);
    m_findEdit->selectAll();
    refreshFindHighlights();
}

void MainWindow::closeFindBar()
{
    m_findWidget->hide();
    m_findStatus->clear();
    m_editor->clearSearch();
    m_editor->setFocus();
}

void MainWindow::refreshFindHighlights()
{
    if (!m_findWidget->isVisible()) {
        m_editor->clearSearch();
        return;
    }
    m_editor->setSearchTerm(m_findEdit->text(), m_caseCheck->isChecked());
}

void MainWindow::findNext()
{
    if (!m_findWidget->isVisible())
        showFindBar();
    doFind(true);
}

void MainWindow::findPrevious()
{
    if (!m_findWidget->isVisible())
        showFindBar();
    doFind(false);
}

void MainWindow::doFind(bool forward)
{
    const QString term = m_findEdit->text();
    if (term.isEmpty()) {
        m_findEdit->setFocus();
        return;
    }

    QTextDocument::FindFlags flags;
    if (!forward)
        flags |= QTextDocument::FindBackward;
    if (m_caseCheck->isChecked())
        flags |= QTextDocument::FindCaseSensitively;

    QTextDocument *document = m_editor->document();
    QTextCursor found = document->find(term, m_editor->textCursor(), flags);

    if (found.isNull()) {
        QTextCursor start(document);
        if (!forward)
            start.movePosition(QTextCursor::End);
        found = document->find(term, start, flags);
    }

    if (found.isNull()) {
        m_findStatus->setText(tr("Not found"));
        return;
    }

    m_findStatus->clear();
    m_editor->setTextCursor(found);
    m_editor->centerCursor();
}

void MainWindow::goToLine()
{
    bool accepted = false;
    const int current = m_editor->textCursor().blockNumber() + 1;
    const int line = QInputDialog::getInt(
        this, tr("Go to Line"), tr("Line number:"), current, 1, m_editor->blockCount(), 1,
        &accepted);

    if (!accepted)
        return;

    const QTextBlock block = m_editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;

    m_editor->setTextCursor(QTextCursor(block));
    m_editor->centerCursor();
    m_editor->setFocus();
}

void MainWindow::updateTitle()
{
    const QString name = m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).fileName();
    const QString modified = m_editor->document()->isModified() ? QStringLiteral("*") : QString();
    setWindowTitle(QStringLiteral("%1%2 — QtEdit").arg(modified, name));
}

void MainWindow::updateStatus()
{
    const QTextCursor cursor = m_editor->textCursor();
    m_positionLabel->setText(
        tr("Ln %1, Col %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
    m_modifiedLabel->setText(m_editor->document()->isModified() ? tr("Modified") : QString());
    m_encodingLabel->setText(m_encoding);
}

void MainWindow::updateWordCount()
{
    const QString text = m_editor->toPlainText();
    int words = 0;
    bool inWord = false;
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            ++words;
        }
    }

    m_wordsLabel->setText(words == 1 ? tr("1 word") : tr("%1 words").arg(words));
}

void MainWindow::updateLanguage()
{
    const SyntaxHighlighter::Language language =
        SyntaxHighlighter::languageForFile(m_filePath);
    m_highlighter->setLanguage(language);
    m_languageLabel->setText(SyntaxHighlighter::languageName(language));
}

void MainWindow::updateRecentMenu()
{
    if (!m_recentMenu)
        return;

    m_recentMenu->clear();
    const QStringList recent = m_settings.value(QStringLiteral("recentFiles")).toStringList();

    if (recent.isEmpty()) {
        QAction *empty = m_recentMenu->addAction(tr("No Recent Files"));
        empty->setEnabled(false);
        return;
    }

    int index = 1;
    for (const QString &path : recent) {
        QString label = QDir::toNativeSeparators(path);
        label.replace(QLatin1Char('&'), QLatin1String("&&"));
        QAction *action = m_recentMenu->addAction(QStringLiteral("%1  %2").arg(index).arg(label));
        action->setToolTip(label);
        connect(action, &QAction::triggered, this, [this, path] { openFile(path); });
        ++index;
    }

    m_recentMenu->addSeparator();
    QAction *clear = m_recentMenu->addAction(tr("Clear Recent Files"));
    connect(clear, &QAction::triggered, this, &MainWindow::clearRecentFiles);
}

void MainWindow::addRecentFile(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QStringList recent = m_settings.value(QStringLiteral("recentFiles")).toStringList();
    recent.removeAll(absolute);
    recent.prepend(absolute);
    while (recent.size() > 10)
        recent.removeLast();

    m_settings.setValue(QStringLiteral("recentFiles"), recent);
    updateRecentMenu();
}

void MainWindow::clearRecentFiles()
{
    m_settings.remove(QStringLiteral("recentFiles"));
    updateRecentMenu();
    statusBar()->showMessage(tr("Recent files cleared"), 2000);
}

void MainWindow::setWordWrap(bool enabled)
{
    m_editor->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth
                                      : QPlainTextEdit::NoWrap);
    if (m_wordWrapAction->isChecked() != enabled)
        m_wordWrapAction->setChecked(enabled);
    m_settings.setValue(QStringLiteral("view/wordWrap"), enabled);
}

void MainWindow::setLineNumbers(bool enabled)
{
    m_editor->setLineNumbersVisible(enabled);
    if (m_lineNumbersAction->isChecked() != enabled)
        m_lineNumbersAction->setChecked(enabled);
    m_settings.setValue(QStringLiteral("view/lineNumbers"), enabled);
}

void MainWindow::setDarkMode(bool dark)
{
    m_darkMode = dark;
    applyTheme(dark);
    m_highlighter->setDark(dark);
    m_editor->refreshTheme();

    if (m_darkModeAction->isChecked() != dark)
        m_darkModeAction->setChecked(dark);
    m_settings.setValue(QStringLiteral("view/darkMode"), dark);
}

void MainWindow::applyTheme(bool dark)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QGuiApplication::styleHints()->setColorScheme(
        dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
#endif

    if (!dark) {
        qApp->setPalette(m_lightPalette);
        return;
    }

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(45, 45, 45));
    palette.setColor(QPalette::WindowText, QColor(230, 230, 230));
    palette.setColor(QPalette::Base, QColor(30, 30, 30));
    palette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    palette.setColor(QPalette::ToolTipBase, QColor(45, 45, 45));
    palette.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
    palette.setColor(QPalette::Text, QColor(230, 230, 230));
    palette.setColor(QPalette::PlaceholderText, QColor(145, 145, 145));
    palette.setColor(QPalette::Button, QColor(45, 45, 45));
    palette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor(80, 160, 255));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));

    qApp->setPalette(palette);
}

void MainWindow::chooseFont()
{
    bool accepted = false;
    QFont font = QFontDialog::getFont(&accepted, m_editor->font(), this,
                                      tr("Choose Editor Font"));
    if (!accepted)
        return;

    if (font.pointSize() <= 0)
        font.setPointSize(11);

    m_editor->setEditorFont(font);
    m_settings.setValue(QStringLiteral("editor/font"), font);
}

void MainWindow::zoomBy(int delta)
{
    QFont font = m_editor->font();
    int size = font.pointSize();
    if (size <= 0)
        size = 11;
    size = qBound(6, size + delta, 72);
    font.setPointSize(size);

    m_editor->setEditorFont(font);
    m_settings.setValue(QStringLiteral("editor/font"), font);
    statusBar()->showMessage(tr("Font size: %1 pt").arg(size), 1500);
}

void MainWindow::resetZoom()
{
    const QFont font = defaultEditorFont();
    m_editor->setEditorFont(font);
    m_settings.setValue(QStringLiteral("editor/font"), font);
    statusBar()->showMessage(tr("Font size reset to %1 pt").arg(font.pointSize()), 1500);
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About QtEdit"),
        tr("<h3>QtEdit 1.0</h3>"
           "<p>A small, fast text editor built with Qt 6 and C++.</p>"
           "<p>Features include syntax highlighting for C/C++, Python, JSON and shell "
           "scripts, line numbers, incremental find, configurable fonts, dark mode, "
           "recent files, and drag-and-drop file opening.</p>"
           "<p>Built with Qt %1.</p>")
            .arg(QLatin1String(qVersion())));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }

    saveSettings();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;

    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            openFile(url.toLocalFile());
            event->acceptProposedAction();
            return;
        }
    }
}
