#ifndef UI_LANGUAGE_MANAGER_H
#define UI_LANGUAGE_MANAGER_H

#include <QObject>
#include <QTranslator>
#include <QApplication>
#include <QSettings>

class LanguageManager : public QObject
{
    Q_OBJECT
public:
    explicit LanguageManager(QApplication *app,QObject *parent = nullptr);
    ~LanguageManager();

    void initialize(const QString &defaultLanguage);
    void switchLanguage(const QString &language);

    inline QString currentLanguage() const { return m_currentLanguage; };

    QStringList availableLanguages() const;

signals:
    void languageChanged();


private:
    void deployTranslationFiles();
    
    QApplication *m_app;
    QTranslator *m_translator;
    QString m_currentLanguage;
    QString m_translationPath;
};

#endif // UI_LANGUAGE_MANAGER_H