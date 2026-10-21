#pragma once

#include <QFont>
#include <QMainWindow>
#include <QPalette>
#include <QSettings>
#include <QString>

class QAction;
class QCheckBox;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QTimer;
class QWidget;

class Editor;
class SyntaxHighlighter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

    bool openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createStatusBar();
    QWidget *createFindBar(QWidget *parent);
    void restoreSettings();
    void saveSettings();

    void newFile();
    void openDialog();
    bool save();
    bool saveAs();
    bool writeFile(const QString &path);
    bool maybeSave();

    void updateTitle();
    void updateStatus();
    void updateWordCount();
    void updateLanguage();
    void updateRecentMenu();
    void addRecentFile(const QString &path);
    void clearRecentFiles();

    void showFindBar();
    void closeFindBar();
    void refreshFindHighlights();
    void findNext();
    void findPrevious();
    void doFind(bool forward);
    void goToLine();

    void setWordWrap(bool enabled);
    void setLineNumbers(bool enabled);
    void setDarkMode(bool dark);
    void applyTheme(bool dark);
    void chooseFont();
    void zoomBy(int delta);
    void resetZoom();
    void showAbout();

    Editor *m_editor = nullptr;
    SyntaxHighlighter *m_highlighter = nullptr;

    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_closeAction = nullptr;
    QAction *m_quitAction = nullptr;

    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_cutAction = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_selectAllAction = nullptr;

    QAction *m_findAction = nullptr;
    QAction *m_findNextAction = nullptr;
    QAction *m_findPreviousAction = nullptr;
    QAction *m_goToLineAction = nullptr;

    QAction *m_wordWrapAction = nullptr;
    QAction *m_lineNumbersAction = nullptr;
    QAction *m_darkModeAction = nullptr;
    QAction *m_fontAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_zoomResetAction = nullptr;
    QAction *m_fullScreenAction = nullptr;

    QAction *m_aboutAction = nullptr;
    QAction *m_aboutQtAction = nullptr;

    QMenu *m_recentMenu = nullptr;

    QWidget *m_findWidget = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QCheckBox *m_caseCheck = nullptr;
    QLabel *m_findStatus = nullptr;

    QLabel *m_positionLabel = nullptr;
    QLabel *m_wordsLabel = nullptr;
    QLabel *m_languageLabel = nullptr;
    QLabel *m_encodingLabel = nullptr;
    QLabel *m_modifiedLabel = nullptr;

    QTimer *m_wordCountTimer = nullptr;
    QTimer *m_highlightTimer = nullptr;

    QSettings m_settings;
    QPalette m_lightPalette;
    QString m_filePath;
    QString m_encoding = QStringLiteral("UTF-8");
    bool m_darkMode = false;
};
