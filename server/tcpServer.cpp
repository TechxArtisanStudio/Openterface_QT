#include "tcpServer.h"
#include "tcpResponse.h"
#include <QDebug>
#include <QLoggingCategory>
#include <QBuffer>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QFile>
#include "../host/cameramanager.h"

#ifndef Q_OS_WIN
#include "../host/backend/gstreamerbackendhandler.h"
#endif

Q_LOGGING_CATEGORY(log_server_tcp, "opf.server.tcp")

TcpServer::TcpServer(QObject *parent) : QTcpServer(parent), currentClient(nullptr), m_cameraManager(nullptr), actionStatus(Finish) {}

void TcpServer::startServer(quint16 port) {
    if (this->listen(QHostAddress::Any, port)) {
        qDebug() << "Server started on port:" << port;
        connect(this, &QTcpServer::newConnection, this, &TcpServer::onNewConnection);
    } else {
        qDebug() << "Server could not start!";
    }
}

void TcpServer::setCameraManager(CameraManager* cameraManager) {
    m_cameraManager = cameraManager;
    if (m_cameraManager) {
        // Connect to camera image capture signal to store the latest frame
        connect(m_cameraManager, QOverload<int, const QImage&>::of(&CameraManager::imageCaptured),
                this, &TcpServer::onImageCaptured, Qt::DirectConnection);
        qCDebug(log_server_tcp) << "CameraManager connected to TcpServer";
    }
}

void TcpServer::onNewConnection() {
    currentClient = this->nextPendingConnection();
    connect(currentClient, &QTcpSocket::readyRead, this, &TcpServer::onReadyRead);
    connect(currentClient, &QTcpSocket::disconnected, currentClient, &QTcpSocket::deleteLater);
    qCDebug(log_server_tcp) << "New client connected!";
}

void TcpServer::onReadyRead() {
    QByteArray data = currentClient->readAll();
    
    qCDebug(log_server_tcp) << "Received data:" << data;
    ActionCommand cmd = parseCommand(data);
    processCommand(cmd);
    // Process the data (this is where you handle the incoming data)
    
}

void TcpServer::handleImgPath(const QString& imagePath){
    lastImgPath = imagePath;
    qCDebug(log_server_tcp) << "img path updated: " << lastImgPath;
}

void TcpServer::onImageCaptured(int id, const QImage& img) {
    Q_UNUSED(id);
    // Store the latest captured frame in thread-safe manner
    QMutexLocker locker(&m_frameMutex);
    m_currentFrame = img;
    qCDebug(log_server_tcp) << "Frame captured and stored, size:" << img.size();
}

QImage TcpServer::getCurrentFrameFromCamera() {
    QMutexLocker locker(&m_frameMutex);
    return m_currentFrame;
}

#ifndef Q_OS_WIN
QImage TcpServer::captureFrameFromGStreamer() {
    if (!m_cameraManager || !m_cameraManager->isGStreamerBackend()) {
        return QImage();
    }
    
    // GStreamer is only available on non-Windows platforms
    GStreamerBackendHandler* gstBackend = m_cameraManager->getGStreamerBackend();
    if (!gstBackend) {
        qCDebug(log_server_tcp) << "Error: Could not get GStreamer backend";
        return QImage();
    }
    
    try {
        // Use the GStreamer backend's takeImage method
        // Save to a temporary file in the temp directory
        QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/openterface_gst_frame.jpg";
        gstBackend->takeImage(tempPath);
        
        // Load the image from the temp file
        QImage image(tempPath);
        
        // Delete the temp file
        QFile::remove(tempPath);
        
        if (!image.isNull()) {
            qCDebug(log_server_tcp) << "Successfully captured frame from GStreamer backend, size:" << image.size();
        } else {
            qCDebug(log_server_tcp) << "Failed to load image from GStreamer temp file";
        }
        
        return image;
    } catch (const std::exception &e) {
        qCDebug(log_server_tcp) << "Exception while capturing from GStreamer:" << e.what();
        return QImage();
    }
}
#endif

ActionCommand TcpServer::parseCommand(const QByteArray& data){
    QString command = QString(data).trimmed().toLower();

    if (command == "lastimage"){
        return CmdGetLastImage;
    }else if(command == "gettargetscreen") {
        return CmdGetTargetScreen;
    }else if(command == "checkstatus") {
        return CheckStatus;
    }else{
        scriptStatement = QString::fromUtf8(data);
        return ScriptCommand;
    }
}

void TcpServer::sendImageToClient(){
    try {
        if (lastImgPath.isEmpty()) {
            QByteArray responseData = TcpResponse::createErrorResponse("No image available. Please capture an image first.");
            if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
                currentClient->write(responseData);
                currentClient->flush();
            }
            return;
        }
        
        QFile imageFile(lastImgPath);
        if (!imageFile.open(QIODevice::ReadOnly)) {
            QByteArray responseData = TcpResponse::createErrorResponse("Could not open image file: " + lastImgPath);
            if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
                currentClient->write(responseData);
                currentClient->flush();
            }
            qCDebug(log_server_tcp) << "Error: Failed to open image file:" << lastImgPath;
            return;
        }
        
        QByteArray imageData = imageFile.readAll();
        imageFile.close();
        
        QByteArray responseData = TcpResponse::createImageResponse(imageData, "raw");
        if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
            currentClient->write(responseData);
            qCDebug(log_server_tcp) << "Sending image to client, size:" << imageData.size() << "bytes";
            currentClient->flush();
        }
    } catch (const std::exception &e) {
        QByteArray responseData = TcpResponse::createErrorResponse(QString("Exception occurred: %1").arg(e.what()));
        if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
            currentClient->write(responseData);
            currentClient->flush();
        }
        qCDebug(log_server_tcp) << "Exception in sendImageToClient:" << e.what();
    }
}

void TcpServer::sendScreenToClient(){
    try {
        QImage frameToSend;
        
        if (!m_cameraManager) {
            QByteArray responseData = TcpResponse::createErrorResponse("CameraManager not initialized. Call setCameraManager() first.");
            qCDebug(log_server_tcp) << "Error: CameraManager not set";
            if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
                currentClient->write(responseData);
                currentClient->flush();
            }
            return;
        }
        
        if (m_cameraManager->isFFmpegBackend()) {
            // FFmpeg backend - get frame from stored image
            frameToSend = getCurrentFrameFromCamera();
            if (frameToSend.isNull()) {
                QByteArray responseData = TcpResponse::createErrorResponse("No frame available from FFmpeg backend. Camera may not be running or no frames captured yet.");
                qCDebug(log_server_tcp) << "Error: No frame captured yet from FFmpeg backend";
                if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
                    currentClient->write(responseData);
                    currentClient->flush();
                }
                return;
            }
        }
#ifndef Q_OS_WIN
        else if (m_cameraManager->isGStreamerBackend()) {
            // GStreamer backend - capture frame directly
            qCDebug(log_server_tcp) << "Capturing frame from GStreamer backend";
            frameToSend = captureFrameFromGStreamer();
            if (frameToSend.isNull()) {
                QByteArray responseData = TcpResponse::createErrorResponse("Failed to capture frame from GStreamer backend. Check if camera is running.");
                qCDebug(log_server_tcp) << "Error: GStreamer frame capture returned null";
                if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
                    currentClient->write(responseData);
                    currentClient->flush();
                }
                return;
            }
        }
#endif
        else {
            QByteArray responseData = TcpResponse::createErrorResponse("Unknown or unsupported backend. Please check your multimedia context setup.");
            qCDebug(log_server_tcp) << "Error: Unable to determine active backend";
            if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
                currentClient->write(responseData);
                currentClient->flush();
            }
            return;
        }
        
        // Encode frame as JPEG to memory
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        
        if (!frameToSend.save(&buffer, "JPEG", 90)) {
            QByteArray responseData = TcpResponse::createErrorResponse("Failed to encode frame as JPEG. Image may be corrupted.");
            qCDebug(log_server_tcp) << "Error: Failed to encode frame as JPEG";
            if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
                currentClient->write(responseData);
                currentClient->flush();
            }
            buffer.close();
            return;
        }
        
        QByteArray jpegData = buffer.data();
        buffer.close();
        
        // Create base64 encoded response
        QByteArray base64Data = jpegData.toBase64();
        QByteArray responseData = TcpResponse::createScreenResponse(base64Data, frameToSend.width(), frameToSend.height());
        
        if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
            currentClient->write(responseData);
            qCDebug(log_server_tcp) << "Screen data captured - JPEG size:" << jpegData.size() 
                                   << "bytes, Base64 size:" << base64Data.size() 
                                   << "bytes, Resolution:" << frameToSend.width() << "x" << frameToSend.height();
            currentClient->flush();
        }
    } catch (const std::exception &e) {
        QByteArray responseData = TcpResponse::createErrorResponse(QString("Exception during screen capture: %1").arg(e.what()));
        qCDebug(log_server_tcp) << "Exception in sendScreenToClient:" << e.what();
        if (currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
            currentClient->write(responseData);
            currentClient->flush();
        }
    }
}

void TcpServer::processCommand(ActionCommand cmd){
    QByteArray responseData;
    switch (cmd)
    {
    case CmdGetLastImage:
        sendImageToClient();
        break;
    case CmdGetTargetScreen:
        sendScreenToClient();
        break;
    case CheckStatus:
        correponseClientStauts();
        break;
    default:
        compileScript();
        break;
    }
}

void TcpServer::compileScript(){
    if (scriptStatement.isEmpty()){
        qCDebug(log_server_tcp) << "The statement is empty";
        return;
    }
    if(actionStatus == Running){
        qCDebug(log_server_tcp) << "The statement is empty";
        return;
    }
    
    // Mark command as running before compilation/execution
    actionStatus = Running;
    
    lexer.setSource(scriptStatement.toStdString());
    tokens = lexer.tokenize();

    Parser parser(tokens);
    std::shared_ptr<ASTNode> syntaxTree = parser.parse();
    emit syntaxTreeReady(syntaxTree);
}

void TcpServer::recvTCPCommandStatus(bool status){
    qCDebug(log_server_tcp) << "The command status: " << status;
    if (status) {
        actionStatus = Finish;
    }else{
        actionStatus = Fail;
    }
    correponseClientStauts();
}

void TcpServer::correponseClientStauts(){
    try {
        QString status;
        QString message;
        
        switch(actionStatus) {
            case Finish:
                status = "finish";
                message = "Command execution completed successfully";
                break;
            case Running:
                status = "running";
                message = "Command is currently executing";
                break;
            case Fail:
                status = "fail";
                message = "Command execution failed";
                break;
            default:
                status = "unknown";
                message = "Unknown execution state";
                break;
        }
        
        QByteArray responseData = TcpResponse::createStatusResponse(status, message);
        
        if(currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
            currentClient->write(responseData);
            qCDebug(log_server_tcp) << "Sending status response - Status:" << status;
            currentClient->flush();
        }
    } catch (const std::exception &e) {
        QByteArray responseData = TcpResponse::createErrorResponse(QString("Failed to send status: %1").arg(e.what()));
        if(currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
            currentClient->write(responseData);
            currentClient->flush();
        }
        qCDebug(log_server_tcp) << "Exception in correponseClientStauts:" << e.what();
    }
}
