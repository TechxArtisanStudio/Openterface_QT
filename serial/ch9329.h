#ifndef CH9329_H
#define CH9329_H

#include <cstdint>
#include <QDebug>

const QByteArray MOUSE_ABS_ACTION_PREFIX = QByteArray::fromHex("57 AB 00 04 07 02");
const QByteArray MOUSE_REL_ACTION_PREFIX = QByteArray::fromHex("57 AB 00 05 05 01");
const QByteArray CMD_GET_PARA_CFG = QByteArray::fromHex("57 AB 00 08 00");
const QByteArray CMD_RESET = QByteArray::fromHex("57 AB 00 0F 00");
const QByteArray CMD_SET_PARA_CFG_PREFIX = QByteArray::fromHex("57 AB 00 09 32 82 80 00 00 01 C2 00");
const QByteArray CMD_SET_PARA_CFG_MID = QByteArray::fromHex("08 00 00 03 86 1a 29 e1 00 00 00 01 00 0d 00 00 00 00 00 00") + QByteArray(23, 0x00) ;

/* Command success */
const uint8_t DEF_CMD_SUCCESS = 0x00;
/* Command error receive 1 byte timeout */
const uint8_t DEF_CMD_ERR_TIMEOUT = 0xE1;
/* Command error in header bytes */
const uint8_t DEF_CMD_ERR_HEAD = 0xE2;
/* Command error in command bytes */
const uint8_t DEF_CMD_ERR_CMD = 0xE3;
/* Command error in checksum */
const uint8_t DEF_CMD_ERR_SUM = 0xE4;
/* Command error in parameter */
const uint8_t DEF_CMD_ERR_PARA = 0xE5;
/* Command error when operate */
const uint8_t DEF_CMD_ERR_OPERATE = 0xE6;

static uint16_t toLittleEndian(uint16_t value) {
    return (value >> 8) | (value << 8);
}

static uint32_t toLittleEndian(uint32_t value) {
    return ((value >> 24) & 0xff) | // Move byte 3 to byte 0
           ((value << 8) & 0xff0000) | // Move byte 1 to byte 2
           ((value >> 8) & 0xff00) | // Move byte 2 to byte 1
           ((value << 24) & 0xff000000); // Move byte 0 to byte 3
}

template <typename T>
T fromByteArray(const QByteArray &data) {
    T result;
    if (data.size() > 0) {
        std::memcpy(&result, data.constData(), sizeof(T));
        // Debugging: Print the raw data
        qDebug() << "Raw data:" << data.toHex(' ');

        // Debugging: Print the parsed fields
        result.dump();
    } else {
        qWarning() << "Data size is too small to parse" << typeid(T).name();
        qDebug() << "Data content:" << data.toHex(' ');
    }
    return result;
}

struct CmdDataParamConfig
{
    uint16_t prefix;    //0x57AB
    uint8_t addr1;      //0x00
    uint8_t cmd;        //0x08
    uint8_t len;        //0x32
    uint8_t mode;       //0x82
    uint8_t cfg;
    uint8_t addr2;
    uint32_t baudrate;
    uint16_t reserved1;
    uint16_t serial_interval;
    uint16_t vid;
    uint16_t pid;
    uint16_t keyboard_upload_interval;
    uint16_t keyboard_release_timeout;
    uint8_t keyboard_auto_enter;
    uint32_t enterkey1;
    uint32_t enterkey2;
    uint32_t filter_start;
    uint32_t filter_end;
    uint8_t custom_usb_desc;
    uint8_t speed_mode;
    uint16_t reserved2;
    uint16_t reserved3;
    uint16_t reserved4;
    uint8_t sum;

    static CmdDataParamConfig fromByteArray(const QByteArray &data) {
        CmdDataParamConfig config;
        // change to 3th byte value to 1
        if (data.size() >= sizeof(CmdDataParamConfig)) {
            std::memcpy(&config, data.constData(), sizeof(CmdDataParamConfig));

            config.baudrate = toLittleEndian(config.baudrate);
            config.reserved1 = toLittleEndian(config.reserved1);
            config.serial_interval = toLittleEndian(config.serial_interval);
            // config.vid = toLittleEndian(config.vid);
            // config.pid = toLittleEndian(config.pid);
            config.keyboard_upload_interval = toLittleEndian(config.keyboard_upload_interval);
            config.keyboard_release_timeout = toLittleEndian(config.keyboard_release_timeout);
            config.enterkey1 = toLittleEndian(config.enterkey1);
            config.enterkey2 = toLittleEndian(config.enterkey2);
            config.filter_start = toLittleEndian(config.filter_start);
            config.filter_end = toLittleEndian(config.filter_end);

            // Debugging: Print the raw data
            qDebug() << "Raw data:" << data.toHex(' ');

            // Debugging: Print the parsed fields
            config.dump();
        } else {
            qWarning() << "Data size is too small to parse CmdDataParamConfig";
            qWarning() << data.size() <<  sizeof(CmdDataParamConfig);
        }
        return config;
    }

    void dump() {
        qDebug() << "prefix:" << QString::number(prefix, 16)
        << "| addr1:" << addr1
        << "| cmd:" << QString::number(cmd, 16)
        << "| len:" << len
        << "| mode:" << QString::number(mode, 16)
        << "| cfg:" << QString::number(cfg, 16)
        << "| addr2:" << QString::number(addr2, 16)
        << "| baudrate:" << baudrate
        << "| reserved1:" << QString::number(reserved1, 16)
        << "| serial_interval:" << serial_interval
        << "| vid:" << QString::number(vid, 16)
        << "| pid:" << QString::number(pid, 16)
        << "| keyboard_upload_interval:" << keyboard_upload_interval
        << "| keyboard_release_timeout:" << keyboard_release_timeout
        << "| keyboard_auto_enter:" << keyboard_auto_enter
        << "| enterkey1:" << QString::number(enterkey1, 16)
        << "| enterkey2:" << QString::number(enterkey2, 16)
        << "| filter_start:" << QString::number(filter_start, 16)
        << "| filter_end:" << QString::number(filter_end, 16)
        << "| custom_usb_desc:" << custom_usb_desc
        << "| speed_mode:" << speed_mode
        << "| reserved2:" << QString::number(reserved2, 16)
        << "| reserved3:" << QString::number(reserved3, 16)
        << "| reserved4:" << QString::number(reserved4, 16)
        << "| sum:" << QString::number(sum, 16);
    }
};

struct CmdDataResult {
    uint16_t prefix;    //0x57AB
    uint8_t addr1;      //0x00
    uint8_t cmd;        
    uint8_t len;
    uint8_t data;
    uint8_t sum;

    static CmdDataResult fromByteArray(const QByteArray &data) {
        CmdDataResult result;
        if (data.size() >= sizeof(CmdDataResult)) {
            std::memcpy(&result, data.constData(), sizeof(CmdDataResult));
            // Debugging: Print the raw data
            qDebug() << "Raw data:" << data.toHex(' ');

            // Debugging: Print the parsed fields
            result.dump();
        } else {
            qWarning() << "Data size is too small to parse CmdDataResult";
        }
        return result;
    }

    void dump() {
        qDebug() << "prefix:" << QString::number(prefix, 16)
        << "| addr1:" << addr1
        << "| cmd:" << QString::number(cmd, 16)
        << "| len:" << len
        << "| data:" << QString::number(data, 16)
        << "| sum:" << QString::number(sum, 16);
    }
};

struct CmdResetResult {
    uint16_t prefix;
    uint8_t addr1;
    uint8_t cmd;
    uint8_t len;
    uint8_t sum;

    static CmdDataResult fromByteArray(const QByteArray &data) {
        CmdDataResult result;
        if (data.size() >= sizeof(CmdDataResult)) {
            std::memcpy(&result, data.constData(), sizeof(CmdDataResult));
            // Debugging: Print the raw data
            qDebug() << "Raw data:" << data.toHex(' ');

            // Debugging: Print the parsed fields
            result.dump();
        } else {
            qWarning() << "Data size is too small to parse CmdDataResult";
        }
        return result;
    }

    void dump() {
        qDebug() << "prefix:" << QString::number(prefix, 16)
        << "| addr1:" << addr1
        << "| cmd:" << QString::number(cmd, 16)
        << "| len:" << len
        << "| sum:" << QString::number(sum, 16);
    }
};

#endif // CH9329_H
