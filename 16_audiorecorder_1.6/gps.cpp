#include "gps.h"
#include "ui_gps.h"
#include "newwindow.h"
#include <QCoreApplication>
#include <QFileDialog>
#include <QDebug>
gps::gps(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::gps),
    m_originalWindow(parent)  // 保存原窗口指针（从构造函数传入）
{
    ui->setupUi(this);

    // 设置textBrowser样式（优化显示效果）
    ui->textBrowser->setStyleSheet(R"(
        font-size: 18px;
        padding: 10px;
        background-color: #fafafa;
        border: 1px solid #eee;
    )");
    ui->textBrowser->setReadOnly(true);
    // 初始显示标题（明确格式）
    ui->textBrowser->append("📡 GPS数据接收日志");
    ui->textBrowser->append("==================================");
    ui->textBrowser->append("格式：序号（No.） | 纬度（8位小数） | 经度（8位小数）");
    ui->textBrowser->append("==================================");

    // 替换原保存按钮为保存图表/打开文件
    ui->pushButton_2->setText("保存图表");
    ui->pushButton_2->setGeometry(170, 390, 89, 25);
    disconnect(ui->pushButton_2, nullptr, nullptr, nullptr);
    connect(ui->pushButton_2, &QPushButton::clicked, this, &gps::onSaveChartClicked);

    m_openChartBtn = new QPushButton("打开文件", this);
    m_openChartBtn->setGeometry(270, 390, 89, 25);
    m_openChartBtn->setStyleSheet("QPushButton { color: black; background: #e0e0e0; border: 1px solid #888; border-radius: 4px; font-size: 13px; } QPushButton:hover { background: #ccc; }");
    connect(m_openChartBtn, &QPushButton::clicked, this, &gps::onOpenChartClicked);
}

gps::~gps()
{
    delete ui;
}

void gps::on_pushButton_clicked()
{
    this->close();  // 关闭当前GPS窗口
    if (m_originalWindow) {
        m_originalWindow->show();  // 显示原窗口
    }
}

void gps::onGpsDataReceived(double latitude, double longitude)
{
    // 1. 先打印日志（调试用，确认信号触发）
    qDebug() << "[GPS槽函数] 收到新数据：纬度=" << latitude << " 经度=" << longitude;

    // 2. 过滤无效数据（避免收到0.0等无效值时显示空数据）
    if (qFuzzyCompare(latitude, 0.0) && qFuzzyCompare(longitude, 0.0)) {
        qDebug() << "[GPS槽函数] 无效数据（经纬度均为0），跳过显示";
        return;
    }

    // 3. 构造当前组数据（仅经纬度，无时间）
    GpsData currentData;
    currentData.latitude = latitude;
    currentData.longitude = longitude;

    // 4. 缓存数据（追加不覆盖）
    m_allGpsData.append(currentData);

    // 5. 显示到界面（序号区分，优化格式）
    int dataIndex = m_allGpsData.size();
    // 序号补0+加粗，无时间戳
    QString indexStr = QString("<span style='font-weight: bold; color: #2c3e50;'>No.%1</span>")
                      .arg(dataIndex, 2, 10, QChar('0'));
    // 8位小数显示（确保精度）
    QString latStr = QString::number(latitude, 'f', 8);
    QString lonStr = QString::number(longitude, 'f', 8);
    // 组合显示文本（优化分隔符，更清晰）
    QString displayText = QString("%1 | 纬度：%2 | 经度：%3")
                          .arg(indexStr)
                          .arg(latStr)
                          .arg(lonStr);

    // 6. 追加到textBrowser（确保显示且滚动到底部）
    ui->textBrowser->append(displayText);
    ui->textBrowser->append("----------------------------------");
    ui->textBrowser->moveCursor(QTextCursor::End); // 强制滚动到最新数据
}

void gps::setNewWindow(NewWindow* nw) {
    m_newWindow = nw;
}

void gps::onSaveChartClicked()
{
    if (m_newWindow) m_newWindow->onSaveChartClicked();
}

void gps::onOpenChartClicked()
{
    if (m_newWindow) m_newWindow->onOpenChartClicked();
}
