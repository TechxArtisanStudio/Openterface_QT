#include "tcpResponse.h"
#include <QLoggingCategory>
#include <QDateTime>

Q_LOGGING_CATEGORY(log_tcp_response, "opf.server.tcp.response")

QByteArray TcpResponse::createSuccessResponse(ResponseType type, const QString& message) {
    QJsonObject response = buildBaseResponse(type, Success);
    if (!message.isEmpty()) {
        response["message"] = message;
    }
    QJsonDocument doc(response);
    return doc.toJson(QJsonDocument::Compact);
}

QByteArray TcpResponse::createErrorResponse(const QString& errorMessage) {
    QJsonObject response = buildBaseResponse(TypeError, Error);
    response["message"] = errorMessage;
    
    qCDebug(log_tcp_response) << "Error response:" << errorMessage;
    
    QJsonDocument doc(response);
    return doc.toJson(QJsonDocument::Compact);
}

QByteArray TcpResponse::createImageResponse(const QByteArray& imageData, const QString& format) {
    QJsonObject response = buildBaseResponse(TypeImage, Success);
    
    QByteArray base64Data = imageData.toBase64();
    QJsonObject data;
    data["size"] = static_cast<int>(base64Data.size());
    data["format"] = format;
    data["content"] = QString::fromLatin1(base64Data);
    
    response["data"] = data;
    
    qCDebug(log_tcp_response) << "Image response created, size:" << base64Data.size() << "bytes";
    
    QJsonDocument doc(response);
    return doc.toJson(QJsonDocument::Compact);
}

QByteArray TcpResponse::createScreenResponse(const QByteArray& base64Data, int width, int height) {
    QJsonObject response = buildBaseResponse(TypeScreen, Success);
    
    QJsonObject data;
    data["size"] = static_cast<int>(base64Data.size());
    data["width"] = width;
    data["height"] = height;
    data["format"] = "jpeg";
    data["encoding"] = "base64";
    data["content"] = QString::fromLatin1(base64Data);
    
    response["data"] = data;
    
    qCDebug(log_tcp_response) << "Screen response created, size:" << base64Data.size() 
                             << "bytes, resolution:" << width << "x" << height;
    
    QJsonDocument doc(response);
    return doc.toJson(QJsonDocument::Compact);
}

QByteArray TcpResponse::createStatusResponse(const QString& status, const QString& message) {
    QJsonObject response = buildBaseResponse(TypeStatus, Success);
    
    QJsonObject data;
    data["state"] = status;
    if (!message.isEmpty()) {
        data["message"] = message;
    }
    
    response["data"] = data;
    
    qCDebug(log_tcp_response) << "Status response:" << status;
    
    QJsonDocument doc(response);
    return doc.toJson(QJsonDocument::Compact);
}

QJsonObject TcpResponse::buildBaseResponse(ResponseType type, ResponseStatus status) {
    QJsonObject response;
    response["type"] = responseTypeToString(type);
    response["status"] = responseStatusToString(status);
    response["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    
    return response;
}

QString TcpResponse::responseTypeToString(ResponseType type) {
    switch (type) {
        case TypeImage: return "image";
        case TypeScreen: return "screen";
        case TypeStatus: return "status";
        case TypeError: return "error";
        case TypeUnknown: return "unknown";
        default: return "unknown";
    }
}

QString TcpResponse::responseStatusToString(ResponseStatus status) {
    switch (status) {
        case Success: return "success";
        case Error: return "error";
        case Warning: return "warning";
        case Pending: return "pending";
        default: return "unknown";
    }
}
