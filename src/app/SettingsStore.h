#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

namespace llocr {

class SettingsStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(int connectionTimeoutMs READ connectionTimeoutMs WRITE setConnectionTimeoutMs NOTIFY connectionTimeoutMsChanged)
    Q_PROPERTY(QString modelName READ modelName WRITE setModelName NOTIFY modelNameChanged)
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(int maxTokens READ maxTokens WRITE setMaxTokens NOTIFY maxTokensChanged)
    Q_PROPERTY(QString parserId READ parserId WRITE setParserId NOTIFY parserIdChanged)
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(int windowX READ windowX WRITE setWindowX NOTIFY windowXChanged)
    Q_PROPERTY(int windowY READ windowY WRITE setWindowY NOTIFY windowYChanged)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY windowWidthChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY windowHeightChanged)
    Q_PROPERTY(int windowState READ windowState WRITE setWindowState NOTIFY windowStateChanged)

public:
    explicit SettingsStore(QObject *parent = nullptr);
    Q_INVOKABLE void forceSave();

    // Connection
    QString baseUrl() const;
    void setBaseUrl(const QString &url);
    QString apiKey() const;
    void setApiKey(const QString &key);
    int connectionTimeoutMs() const;
    void setConnectionTimeoutMs(int timeOut);

    // Model
    QString modelName() const;
    void setModelName(const QString &modelName);
    double temperature() const;
    void setTemperature(double temp);
    int maxTokens() const;
    void setMaxTokens(int maxTkns);

    // Parser
    QString parserId() const;
    void setParserId(const QString &parserName);

    // UI
    int themeMode() const;
    void setThemeMode(int mode);
    QString language() const;
    void setLanguage(const QString &language);
    int windowX() const;
    void setWindowX(int winX);
    int windowY() const;
    void setWindowY(int winY);
    int windowWidth() const;
    void setWindowWidth(int winWidth);
    int windowHeight() const;
    void setWindowHeight(int winHeight);
    int windowState() const;
    void setWindowState(int winState);

signals:
    void baseUrlChanged();
    void apiKeyChanged();
    void connectionTimeoutMsChanged();
    void modelNameChanged();
    void temperatureChanged();
    void maxTokensChanged();
    void parserIdChanged();
    void themeModeChanged();
    void languageChanged();
    void windowXChanged();
    void windowYChanged();
    void windowWidthChanged();
    void windowHeightChanged();
    void windowStateChanged();

private:
    QSettings m_settings;

    // Connection
    static constexpr const char *kBaseUrl = "provider/baseUrl";
    static constexpr const char *kApiKey = "provider/apiKey";
    static constexpr const char *kTimeoutMs = "provider/timeoutMs";

    // Model
    static constexpr const char *kModelName = "model/name";
    static constexpr const char *kTemperature = "model/temperature";
    static constexpr const char *kMaxTokens = "model/maxTokens";

    // Output / parser
    static constexpr const char *kParserId = "output/parser";

    // UI
    static constexpr const char *kThemeMode = "ui/theme";
    static constexpr const char *kLanguage = "ui/language";
    static constexpr const char *kWindowX = "ui/windowX";
    static constexpr const char *kWindowY = "ui/windowY";
    static constexpr const char *kWindowWidth = "ui/windowWidth";
    static constexpr const char *kWindowHeight = "ui/windowHeight";
    static constexpr const char *kWindowState = "ui/windowState";
};

}  // namespace llocr
