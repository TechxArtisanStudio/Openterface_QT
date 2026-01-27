#include "videohidchip.h"
#include "videohid.h"

QPair<QByteArray, bool> Ms2109Chip::read4Byte(quint16 address) {
    // Delegate to VideoHid's MS2109-specific method
    return m_owner ? m_owner->usbXdataRead4ByteMS2109(address) : qMakePair(QByteArray(4, 0), false);
}

bool Ms2109Chip::write4Byte(quint16 address, const QByteArray &data) {
    return m_owner ? m_owner->usbXdataWrite4Byte(address, data) : false;
}

QPair<QByteArray, bool> Ms2130sChip::read4Byte(quint16 address) {
    return m_owner ? m_owner->usbXdataRead4ByteMS2130S(address) : qMakePair(QByteArray(4, 0), false);
}

bool Ms2130sChip::write4Byte(quint16 address, const QByteArray &data) {
    return m_owner ? m_owner->usbXdataWrite4Byte(address, data) : false;
}

QPair<QByteArray, bool> Ms2109sChip::read4Byte(quint16 address) {
    // Use the MS2109S-specific read implementation
    return m_owner ? m_owner->usbXdataRead4ByteMS2109S(address) : qMakePair(QByteArray(4, 0), false);
}

bool Ms2109sChip::write4Byte(quint16 address, const QByteArray &data) {
    return m_owner ? m_owner->usbXdataWrite4Byte(address, data) : false;
}
