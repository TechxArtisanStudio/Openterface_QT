#include "languagemanager.h"
#include "globalsetting.h"

#include <QDir>
#include <QDebug>

LanguageManager::LanguageManager(QApplication *app, QObject *parent)
    : QObject(parent),
      m_app(app),
      m_translator(new QTranslator(this))
     {

    deployTranslationFiles();
}

LanguageManager::~LanguageManager() {
    delete m_translator;
}

void LanguageManager::deployTranslationFiles() {
    QDir resourceDir(":/config/languages");
    if (resourceDir.exists()) {
        QStringList filters;
        filters << "openterface_*.qm";
        QFileInfoList files = resourceDir.entryInfoList(filters, QDir::Files);
        
        for (const QFileInfo& file : files) {
            QString targetPath = m_translationPath + file.fileName();
            if (!QFile::exists(targetPath)) {
                QFile resourceFile(file.absoluteFilePath());
                if (resourceFile.open(QIODevice::ReadOnly)) {
                    QFile targetFile(targetPath);
                    if (targetFile.open(QIODevice::WriteOnly)) {
                        targetFile.write(resourceFile.readAll());
                        targetFile.close();
                        qDebug() << "Deployed translation file from resources:" << targetPath;
                    }
                    resourceFile.close();
                }
            }
        }
    }
}

void LanguageManager::initialize(const QString &defaultLanguage) {
    Q_UNUSED(defaultLanguage);
    GlobalSetting::instance().getLanguage(m_currentLanguage); 
    switchLanguage(m_currentLanguage);
}

void LanguageManager::switchLanguage(const QString &language) {
    if (!m_currentLanguage.isEmpty()) {
        m_app->removeTranslator(m_translator);
    }

    QString filePath = m_translationPath + "openterface_" + language + ".qm";
    if (m_translator->load(filePath)) {
        m_app->installTranslator(m_translator);
        m_currentLanguage = language;
        GlobalSetting::instance().setLangeuage(m_currentLanguage);
        emit languageChanged();
    } else {
        qWarning() << "Failed to load translation file:" << filePath;
    }
}

QStringList LanguageManager::availableLanguages() const {
    QDir dir(m_translationPath);
    QStringList filters;
    filters << "openterface_*.qm";
    QStringList files = dir.entryList(filters, QDir::Files);
    
    QStringList languages;
    for (const QString &file : files) {
        QString lang = file.mid(strlen("openterface_"), file.indexOf('.') - strlen("openterface_"));
        languages << lang;
    }
    return languages;
}
