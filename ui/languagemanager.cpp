#include "languagemanager.h"
#include "globalsetting.h"

#include <QDir>
#include <QDebug>

LanguageManager::LanguageManager(QApplication *app, QObject *parent)
    : QObject(parent),
      m_app(app),
      m_translator(new QTranslator(this)),
      m_translationPath(QCoreApplication::applicationDirPath() + "/config/languages/") {

    deployTranslationFiles();
    qDebug() << "Initial translation path:" << m_translationPath;
}

LanguageManager::~LanguageManager() {
    delete m_translator;
}

void LanguageManager::deployTranslationFiles() {
    QDir dir;
    if (!dir.exists(m_translationPath)) {
        dir.mkpath(m_translationPath);
    }

    QStringList qmFiles = {
        ":/config/languages/openterface_en.qm",
        ":/config/languages/openterface_fr.qm",
        ":/config/languages/openterface_da.qm",
        ":/config/languages/openterface_ja.qm",
        ":/config/languages/openterface_se.qm",
        ":/config/languages/openterface_de.qm"
    };

    for (const QString &resourcePath : qmFiles) {
        QString fileName = resourcePath.split('/').last();
        QString targetPath = m_translationPath + fileName;

        if (!QFile::exists(targetPath)) {
            QFile resourceFile(resourcePath);
            if (resourceFile.open(QIODevice::ReadOnly)) {
                QFile targetFile(targetPath);
                if (targetFile.open(QIODevice::WriteOnly)) {
                    targetFile.write(resourceFile.readAll());
                    targetFile.close();
                    qDebug() << "Deployed translation file:" << targetPath;
                } else {
                    qWarning() << "Failed to write file:" << targetPath;
                }
                resourceFile.close();
            } else {
                qWarning() << "Failed to open resource:" << resourcePath;
            }
        } else {
            qDebug() << "Translation file already exists:" << targetPath;
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
