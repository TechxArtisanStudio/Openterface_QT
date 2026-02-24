#ifndef TCP_RESPONSE_H
#define TCP_RESPONSE_H

#include <QByteArray>
#include <QString>
#include <QImage>
#include <QJsonObject>
#include <QJsonDocument>

class TcpResponse {
public:
    enum ResponseStatus {
        Success,
        Error,
        Warning,
        Pending
    };

    enum ResponseType {
        TypeImage,
        TypeScreen,
        TypeStatus,
        TypeError,
        TypeUnknown
    };

    // Factory methods for creating responses
    static QByteArray createSuccessResponse(ResponseType type, const QString& message = "");
    static QByteArray createErrorResponse(const QString& errorMessage);
    static QByteArray createImageResponse(const QByteArray& imageData, const QString& format = "raw");
    static QByteArray createScreenResponse(const QByteArray& base64Data, int width, int height);
    static QByteArray createStatusResponse(const QString& status, const QString& message = "");
    
private:
    // Helper methods
    static QJsonObject buildBaseResponse(ResponseType type, ResponseStatus status);
    static QString responseTypeToString(ResponseType type);
    static QString responseStatusToString(ResponseStatus status);
};

#endif // TCP_RESPONSE_H
