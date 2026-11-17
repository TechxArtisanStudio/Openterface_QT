#ifndef SHIFTEDKEYBUTTON_H
#define SHIFTEDKEYBUTTON_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QMouseEvent>
#include <QResizeEvent>

class ShiftedKeyButton : public QWidget
{
    Q_OBJECT

public:
    explicit ShiftedKeyButton(const QString& baseChar, const QString& shiftedChar, int keyCode, QWidget* parent = nullptr);
    int keyCode() const { return m_keyCode; }
    void setCharacters(const QString& baseChar, const QString& shiftedChar);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QPushButton* m_button;
    QLabel* m_baseLabel;
    QLabel* m_shiftedLabel;
    int m_keyCode;
};

#endif // SHIFTEDKEYBUTTON_H
