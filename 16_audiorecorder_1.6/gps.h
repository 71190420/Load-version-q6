#ifndef GPS_H
#define GPS_H
#include <QWidget>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QList> // 必须包含QList头文件

class NewWindow;  // 前向声明

namespace Ui {
class gps;
}

// 简化结构体：仅存储经纬度（无时间戳）
struct GpsData {
    double latitude;  // 纬度
    double longitude; // 经度
};


class gps : public QWidget
{
    Q_OBJECT

public:
    explicit gps(QWidget *parent = nullptr);
    ~gps();

    void setNewWindow(NewWindow* nw);  // 设置NewWindow指针

public slots:

    // 关键：声明必须是两个double参数，与cpp实现完全一致
     void onGpsDataReceived(double latitude, double longitude);

private slots:
    void on_pushButton_clicked();
    void onSaveChartClicked();   // 保存图表
    void onOpenChartClicked();   // 打开图表

private:
    Ui::gps *ui;
    QWidget *m_originalWindow;
    QList<GpsData> m_allGpsData;
    QPushButton *m_saveChartBtn;
    QPushButton *m_openChartBtn;
    NewWindow *m_newWindow = nullptr;


};

#endif // GPS_H
