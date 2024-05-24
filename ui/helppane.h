#include <QWidget>

class HelpPane : public QWidget
{
    Q_OBJECT

public:
    explicit HelpPane(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};
