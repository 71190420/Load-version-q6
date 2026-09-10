#include "audiorecorder.h"
#include <QDebug>
#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QByteArray>
#include <QStyle>
#include <QApplication>
#include <QProcess>
#include "newwindow.h"  // 包含新窗口类的头文件
#include <QtMath>  // 包含Qt数学函数库，提供qSqrt等函数
#include <QFile>

#include "gps.h"
AudioRecorder::AudioRecorder(QWidget *parent, QString firmware)
    : QMainWindow(parent),
      m_audioInput(nullptr),
      m_audioOutput(nullptr),
      m_inputDevice(nullptr),
      m_outputDevice(nullptr),
      isRecording(false),
      recordTimer(nullptr),
      a(0.5),
      m_maxLeftChannelLevel(0),
      firmware(firmware),
      serialPort(new QSerialPort(this)),
      // 初始化新增的计数器
      m_intensityUpdateCounter(0)
{
    /* 初始化布局 */
    layoutInit();

    /* 设置音频 */
    setupAudio();

    system("echo 255 > /sys/devices/platform/soc/40015000.i2c/i2c-2/2-002c/rdac1");  // 执行第一个命令
    system("echo 255 > /sys/devices/platform/soc/40015000.i2c/i2c-2/2-002c/rdac0");  // 执行第一个命令

    system("amixer -c 0 cset numid=20 0");  // 执行第一个命令
    system("amixer -c 0 cset numid=21 0");  // 执行第一个命令


    this->firmware = firmware;

    qDebug() << "固件名字" << firmware << endl;


    serialPort = new QSerialPort(this);


    connect(serialPort, SIGNAL(readyRead()),this, SLOT(serialPortReadyRead()));

    // 连接信号槽
    connect(serialPort, &QSerialPort::readyRead, this, &AudioRecorder::serialPortReadyRead);


    // 在AudioRecorder的构造函数中添加
    connect(this, &AudioRecorder::leftChannelLevelUpdated,
            this, &AudioRecorder::onLevelUpdated);  // 先连接到自身槽函数

}

// 新增槽函数，转发信号到已创建的窗口
void AudioRecorder::onLevelUpdated(qreal level) {
    if (m_newWindow) {  // 若窗口已创建，直接转发
        m_newWindow->setLeftChannelLevel(level);
    }
}




AudioRecorder::~AudioRecorder()
{
    if (isRecording) {
        m_audioInput->stop();
        m_audioOutput->stop();
    }
    delete m_audioInput;
    delete m_audioOutput;
    if (recordTimer) {
        recordTimer->stop();
        delete recordTimer;
    }

    delete m_decreaseLongPressTimer;    // 释放减号定时器
    delete m_increaseLongPressTimer;    // 释放加号定时器
}


#include <QRegExp> // 用于解析TXT文件格式

// 从PCM-TXT文件还原为可播放WAV
bool AudioRecorder::restorePcmFromTxtToWav(const QString& txtFilePath, const QString& outputWavPath)
{
    // ---------------------- 1. 验证文件与参数 ----------------------
    QFile txtFile(txtFilePath);
    if (!txtFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "PCM-TXT文件打开失败：" << txtFile.errorString();
        return false;
    }

    // 初始化WAV文件（复用你已有的WAV头逻辑）
    QFile wavFile(outputWavPath);
    QByteArray wavBuffer;
    qint64 wavDataSize = 0;

    // 关键：WAV头参数必须与你保存PCM时的格式一致（44100Hz、16位、双声道）
    WavHeader wavHeader;
    wavHeader.sampleRate = 44100;       // 与setupAudio中的采样率一致
    wavHeader.channelCount = 2;         // 双声道
    wavHeader.bitsPerSample = 16;       // 16位采样
    wavHeader.byteRate = wavHeader.sampleRate * wavHeader.channelCount * (wavHeader.bitsPerSample / 8); // 计算字节率
    wavHeader.blockAlign = wavHeader.channelCount * (wavHeader.bitsPerSample / 8); // 计算块对齐
    wavHeader.audioFormat = 1;          // PCM格式（1表示线性PCM）
    wavHeader.dataSize = 0;             // 初始数据大小为0，后续更新
    wavHeader.riffSize = 36 + wavHeader.dataSize; // WAV头固定计算方式

    // 打开WAV文件并写入初始头
    if (!wavFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "WAV输出文件创建失败：" << wavFile.errorString();
        txtFile.close();
        return false;
    }
    wavFile.write(reinterpret_cast<const char*>(&wavHeader), sizeof(WavHeader));
    qDebug() << "开始还原PCM→WAV：" << txtFilePath << " → " << outputWavPath;

    // ---------------------- 2. 解析TXT文件中的PCM数据 ----------------------
    QTextStream txtIn(&txtFile);
    QString line;
    QRegExp pcmRegExp("帧\\s+\\d+:\\s+声道0=(-?\\d+)\\s+声道1=(-?\\d+)"); // 匹配TXT行格式："帧 X: 声道0=值 声道1=值"
    pcmRegExp.setMinimal(true);

    while (!txtIn.atEnd()) {
        line = txtIn.readLine().trimmed();
        if (line.isEmpty() || !pcmRegExp.exactMatch(line)) {
            continue; // 跳过空行或格式错误的行
        }

        // 提取左右声道的PCM数值（16位有符号整数）
        qint16 leftSample = pcmRegExp.cap(1).toInt();  // 左声道值
        qint16 rightSample = pcmRegExp.cap(2).toInt(); // 右声道值

        // 边界检查：确保数值在16位有符号整数范围内（-32768 ~ 32767）
        leftSample = qBound(static_cast<qint16>(-32768), leftSample, static_cast<qint16>(32767));
        rightSample = qBound(static_cast<qint16>(-32768), rightSample, static_cast<qint16>(32767));

        // ---------------------- 3. 转换为二进制PCM并写入WAV ----------------------
        // 16位PCM为小端字节序（与你保存时的格式一致）
        char leftBytes[2], rightBytes[2];
        memcpy(leftBytes, &leftSample, 2);  // 左声道转换为2字节
        memcpy(rightBytes, &rightSample, 2); // 右声道转换为2字节

        // 写入WAV文件（左声道→右声道，符合双声道PCM顺序）
        wavFile.write(leftBytes, 2);
        wavFile.write(rightBytes, 2);
        wavDataSize += 4; // 每帧（左右声道）占4字节（16位×2）
    }

    // ---------------------- 4. 更新WAV头的实际数据大小 ----------------------
    wavHeader.dataSize = wavDataSize;
    wavHeader.riffSize = 36 + wavDataSize;
    wavFile.seek(4);  // 更新riffSize（WAV头第4字节开始）
    wavFile.write(reinterpret_cast<const char*>(&wavHeader.riffSize), 4);
    wavFile.seek(40); // 更新dataSize（WAV头第40字节开始）
    wavFile.write(reinterpret_cast<const char*>(&wavHeader.dataSize), 4);

    // ---------------------- 5. 清理资源 ----------------------
    txtFile.close();
    wavFile.close();
    qDebug() << "PCM还原完成！WAV文件：" << outputWavPath << "（大小：" << (wavDataSize + 44) << "字节）";
    return true;
}


void AudioRecorder::onRestorePcmBtnClicked()
{

}



void AudioRecorder::onFilteredIntensityReceived(double intensity) {
    if (m_barChartWindow) {
        m_barChartWindow->setGlobalMaxFilteredData(intensity);
    }
}


//void AudioRecorder::layoutInit()
//{
//    this->setGeometry(100, 100, 800, 480);
//    this->setWindowTitle("音频调节工具");

//    mainWidget = new QWidget(this);
//    setCentralWidget(mainWidget);

//    QVBoxLayout *vBoxLayout = new QVBoxLayout(mainWidget);
//    vBoxLayout->setContentsMargins(5, 5, 5, 5);
//    vBoxLayout->setSpacing(8);

//    QWidget *topControlWidget = new QWidget();
//    QHBoxLayout *topHBox = new QHBoxLayout(topControlWidget);
//    topHBox->setSpacing(5);

//    QWidget *leftControlWidget = new QWidget();
//    QHBoxLayout *leftHBox = new QHBoxLayout(leftControlWidget);
//    leftHBox->setSpacing(5);

//    recorderBt = new QPushButton("启动", this);
//    recorderBt->setFixedSize(90, 32);
//    leftHBox->addWidget(recorderBt);

//    executeBt = new QPushButton("返回", this);
//    executeBt->setFixedSize(90, 32);
//    leftHBox->addWidget(executeBt);

//    topHBox->addWidget(leftControlWidget);

//    QWidget *aControlWidget = new QWidget();
//    QHBoxLayout *aControlLayout = new QHBoxLayout(aControlWidget);
//    aControlLayout->setSpacing(2);

//    // ========== 重点优化：valueControlWidget 布局 ==========
//    QWidget *valueControlWidget = new QWidget();
//    QHBoxLayout *valueHBox = new QHBoxLayout(valueControlWidget);
//    valueHBox->setContentsMargins(10, 0, 10, 0); // 增加左右边距，避免贴边
//    valueHBox->setSpacing(8); // 增大控件间距，更透气
//    valueHBox->setAlignment(Qt::AlignCenter); // 整体居中对齐

//    // 数值显示标签优化
//    valueDisplayLabel = new QLabel("255", this);
//    valueDisplayLabel->setFixedSize(50, 35); // 调整尺寸，匹配输入框高度
//    valueDisplayLabel->setStyleSheet(R"(
//        QLabel {
//            background: white;
//            border: 1px solid #ccc;
//            border-radius: 4px; /* 圆角更美观 */
//            text-align: center;
//            font-size: 14px;
//            font-weight: 500;
//        }
//    )");
//    valueDisplayLabel->setAlignment(Qt::AlignCenter); // 文字居中
//    valueHBox->addWidget(valueDisplayLabel);

//    // 优化输入框样式和布局
//    m_valueLineEdit = new QLineEdit(this);
//    m_valueLineEdit->setFixedSize(120, 35); // 调整宽度，更协调
//    m_valueLineEdit->setStyleSheet(R"(
//        QLineEdit {
//            background: white;
//            border: 1px solid #ccc;
//            border-radius: 4px; /* 圆角 */
//            padding: 0 8px; /* 内边距，避免文字贴边 */
//            text-align: center;
//            font-size: 14px;
//            font-weight: 500;
//            color: #333;
//        }
//        QLineEdit:hover {
//            border-color: #66afe9; /* hover 高亮边框 */
//        }
//        QLineEdit:focus {
//            border-color: #4ecdc4; /* 聚焦高亮 */
//            outline: none;
//        }
//    )");
//    m_valueLineEdit->setPlaceholderText("输入调节值 (0-255)"); // 更明确的提示
//    m_valueLineEdit->setAlignment(Qt::AlignCenter); // 文字居中
//    valueHBox->addWidget(m_valueLineEdit);

//    // 优化增减按钮样式和尺寸
//    decreaseValueBtn = new QPushButton("-", this);
//    decreaseValueBtn->setFixedSize(40, 35); // 调整尺寸，匹配输入框高度
//    decreaseValueBtn->setStyleSheet(R"(
//        QPushButton {
//            background-color: #ff6b6b;
//            color: white;
//            border: none;
//            border-radius: 4px; /* 圆角 */
//            font-size: 16px;
//            font-weight: bold;
//        }
//        QPushButton:hover {
//            background-color: #ff5252; /* hover 加深颜色 */
//        }
//        QPushButton:pressed {
//            background-color: #d32f2f; /* 按下加深 */
//        }
//    )");
//    valueHBox->addWidget(decreaseValueBtn);

//    increaseValueBtn = new QPushButton("+", this);
//    increaseValueBtn->setFixedSize(40, 35); // 调整尺寸，匹配输入框高度
//    increaseValueBtn->setStyleSheet(R"(
//        QPushButton {
//            background-color: #4ecdc4;
//            color: white;
//            border: none;
//            border-radius: 4px; /* 圆角 */
//            font-size: 16px;
//            font-weight: bold;
//        }
//        QPushButton:hover {
//            background-color: #26a69a; /* hover 加深颜色 */
//        }
//        QPushButton:pressed {
//            background-color: #00897b; /* 按下加深 */
//        }
//    )");
//    valueHBox->addWidget(increaseValueBtn);

//    // 给valueControlWidget添加轻微边框和背景，增强视觉区分
//    valueControlWidget->setStyleSheet(R"(
//        QWidget {
//            background-color: #f9f9f9;
//            border: 1px solid #eee;
//            border-radius: 6px;
//            padding: 5px;
//        }
//    )");
//    topHBox->addWidget(valueControlWidget);
//    // ========== valueControlWidget 优化结束 ==========

//    vBoxLayout->addWidget(topControlWidget);

//    QWidget *functionWidget = new QWidget();
//    QGridLayout *functionGrid = new QGridLayout(functionWidget);
//    functionGrid->setHorizontalSpacing(5);
//    functionGrid->setVerticalSpacing(8);

//    QString textStyle = "QLabel { font-size: 20px; background: white; border: 1px solid #999; border-radius: 2px; text-align: center; }";

//    newBtn1 = new QPushButton("远程", this);
//    newBtn1->setFixedSize(80, 22);
//    functionGrid->addWidget(newBtn1, 2, 0);

//    newBtn2 = new QPushButton("获取", this);
//    newBtn2->setFixedSize(80, 22);
//    functionGrid->addWidget(newBtn2, 2, 1);

//    newBtn3 = new QPushButton("频率", this);
//    newBtn3->setFixedSize(80, 22);
//    functionGrid->addWidget(newBtn3, 2, 2);

//    newBtn4 = new QPushButton("delete", this);
//    newBtn4->setFixedSize(80, 22);
//    functionGrid->addWidget(newBtn4, 2, 3);

//    QWidget *volumeControlWidget = new QWidget();
//    QHBoxLayout *volumeHBox = new QHBoxLayout(volumeControlWidget);
//    volumeHBox->setContentsMargins(0, 0, 0, 0);
//    volumeHBox->setSpacing(5);

//    volumeDownBtn = new QPushButton("-", this);
//    volumeDownBtn->setFixedSize(30, 25);
//    volumeHBox->addWidget(volumeDownBtn);

//    hSlider = new QSlider(Qt::Horizontal, this);
//    hSlider->setRange(0, 127);
//    hSlider->setValue(64);
//    hSlider->setTickPosition(QSlider::NoTicks);
//    hSlider->setStyleSheet(R"(
//                           QSlider::groove:horizontal { height: 12px; background: #f0f0f0; border-radius: 6px; }
//                           QSlider::handle:horizontal { width: 20px; background: #ff5555; margin: -4px 0; border-radius: 10px; }
//                           )");
//    volumeHBox->addWidget(hSlider);

//    volumeUpBtn = new QPushButton("+", this);
//    volumeUpBtn->setFixedSize(30, 25);
//    volumeHBox->addWidget(volumeUpBtn);

//    functionGrid->addWidget(volumeControlWidget, 3, 0, 1, 4);

//    testBrowser = new QTextBrowser(this);
//    testBrowser->setStyleSheet(R"(
//                               border: 1px solid #ccc;
//                               border-radius: 4px;
//                               background-color: #f8f8f8;
//                               font-size: 20px;
//                               padding: 8px;
//                               )");
//    testBrowser->setMinimumHeight(80);
//    testBrowser->setMaximumHeight(100);
//    testBrowser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
//    testBrowser->setReadOnly(true);

//    QString defaultText = "当前音量: 64/127\n";
//    testBrowser->setPlainText(defaultText);

//    functionGrid->setRowStretch(4, 1);
//    functionGrid->addWidget(testBrowser, 4, 0, 1, 4);

//    vBoxLayout->addWidget(functionWidget);

//    QWidget *bottomWidget = new QWidget();
//    QHBoxLayout *bottomHBox = new QHBoxLayout(bottomWidget);
//    bottomHBox->setSpacing(8);
//    bottomHBox->setContentsMargins(0, 0, 0, 0);

//    QWidget *infoWidget = new QWidget();
//    QVBoxLayout *infoVBox = new QVBoxLayout(infoWidget);
//    infoVBox->setSpacing(5);

//    leftChannelLevelLabel = new QLabel("L: 0%", this);
//    leftChannelLevelLabel->setStyleSheet("QLabel { font-size: 20px; }");
//    infoVBox->addWidget(leftChannelLevelLabel, 0, Qt::AlignLeft);

//    maxLeftChannelLevelLabel = new QLabel("Max: 0%", this);
//    maxLeftChannelLevelLabel->setStyleSheet("QLabel { font-size: 20px; color: #666; }");
//    infoVBox->addWidget(maxLeftChannelLevelLabel, 0, Qt::AlignLeft);

//    modeLabel = new QLabel("滤Max:", this);
//    modeLabel->setStyleSheet("QLabel { font-size: 20px; color: #2c3e50; }");
//    infoVBox->addWidget(modeLabel, 0, Qt::AlignLeft);

//    globalMaxLabel = new QLabel("全局最大滤强: 0%", this);
//    globalMaxLabel->setStyleSheet("QLabel { font-size: 20px; color: #e74c3c; }");
//    infoVBox->addWidget(globalMaxLabel);

//    countLabel = new QLabel("0s", this);
//    countLabel->setStyleSheet("QLabel { font-size: 20px; font-weight: bold; }");
//    infoVBox->addWidget(countLabel);

//    bottomHBox->addWidget(infoWidget);

//    QWidget *commandWidget = new QWidget();
//    QVBoxLayout *commandVBox = new QVBoxLayout(commandWidget);
//    commandVBox->setSpacing(5);
//    commandVBox->setContentsMargins(0, 0, 0, 0);

//    QWidget *topCommandWidget = new QWidget();
//    QHBoxLayout *topCommandHBox = new QHBoxLayout(topCommandWidget);
//    topCommandHBox->setSpacing(5);

//    newLeftBtn = new QPushButton("保存", this);
//    newLeftBtn->setFixedSize(75, 32);
//    topCommandHBox->addWidget(newLeftBtn);
//    connect(newLeftBtn, &QPushButton::clicked, this, [=]() {
//        m_saveBothChannels = !m_saveBothChannels;
//        if (m_saveBothChannels) {
//            newLeftBtn->setText("保存");
//            qDebug() << "开始同时保存左右声道滤波数据（目标5万帧）";
//            m_leftDataBuffer.clear();
//            m_rightDataBuffer.clear();
//            m_bothDataCount = 0;
//        } else {
//            newLeftBtn->setText("保存");
//            qDebug() << "手动停止双声道数据保存";
//        }
//    });

//    m_saveRawBtn = new QPushButton("保原", this);
//    m_saveRawBtn->setFixedSize(50, 32);
//    m_saveRawBtn->setStyleSheet(R"(
//        QPushButton {
//            background-color: #3498db;
//            color: white;
//            border: none;
//            border-radius: 4px;
//        }
//        QPushButton:hover {
//            background-color: #2980b9;
//        }
//    )");
//    topCommandHBox->addWidget(m_saveRawBtn);

//    QPushButton *m_playRawBtn = new QPushButton("播原", this);
//    m_playRawBtn->setFixedSize(50, 32);
//    m_playRawBtn->setStyleSheet(R"(
//        QPushButton {
//            background-color: #2980b9;
//            color: white;
//            border: none;
//            border-radius: 4px;
//        }
//        QPushButton:hover {
//            background-color: #1f618d;
//        }
//    )");
//    topCommandHBox->addWidget(m_playRawBtn);

//    m_saveFilteredBtn = new QPushButton("保滤", this);
//    m_saveFilteredBtn->setFixedSize(50, 32);
//    m_saveFilteredBtn->setStyleSheet(R"(
//        QPushButton {
//            background-color: #2ecc71;
//            color: white;
//            border: none;
//            border-radius: 4px;
//        }
//        QPushButton:hover {
//            background-color: #27ae60;
//        }
//    )");
//    topCommandHBox->addWidget(m_saveFilteredBtn);

//    QPushButton *m_playFilteredBtn = new QPushButton("播滤", this);
//    m_playFilteredBtn->setFixedSize(50, 32);
//    m_playFilteredBtn->setStyleSheet(R"(
//        QPushButton {
//            background-color: #27ae60;
//            color: white;
//            border: none;
//            border-radius: 4px;
//        }
//        QPushButton:hover {
//            background-color: #219653;
//        }
//    )");
//    topCommandHBox->addWidget(m_playFilteredBtn);

//    commandVBox->addWidget(topCommandWidget);

//    QWidget *bottomCommandWidget = new QWidget();
//    QHBoxLayout *bottomCommandHBox = new QHBoxLayout(bottomCommandWidget);
//    bottomCommandHBox->setSpacing(5);

//    m_restorePcmBtn = new QPushButton("GPS", this);
//    m_restorePcmBtn->setFixedSize(75, 32);
//    m_restorePcmBtn->setStyleSheet(R"(
//        QPushButton {
//            background-color: #f39c12;
//            color: white;
//            border: none;
//            border-radius: 4px;
//        }
//        QPushButton:hover {
//            background-color: #e67e22;
//        }
//    )");
//    bottomCommandHBox->addWidget(m_restorePcmBtn);

//    // 复用GPS按钮，改为「保存日志」功能（layoutInit()函数内）
//    connect(m_restorePcmBtn, &QPushButton::clicked, this, [=]() {
//        // 1. 获取textBrowser中的所有日志内容（包括历史记录）
//        QString allLogText = testBrowser->toPlainText();

//        // 2. 处理空日志情况
//        if (allLogText.isEmpty()) {
//            testBrowser->append("[日志保存]：日志为空，无需保存！");
//            testBrowser->moveCursor(QTextCursor::End);
//            return;
//        }

//        // 3. 创建日志保存目录（LogFiles），不存在则自动创建
//        QString logDirPath = QCoreApplication::applicationDirPath() + "/LogFiles";
//        QDir logDir(logDirPath);
//        if (!logDir.exists()) {
//            if (logDir.mkpath(logDirPath)) {
//                testBrowser->append("[日志保存]：创建日志目录成功：" + logDirPath);
//            } else {
//                testBrowser->append("[日志保存失败]：创建日志目录失败！");
//                testBrowser->moveCursor(QTextCursor::End);
//                return;
//            }
//        }

//        // 4. 生成带时间戳的文件名（避免覆盖旧日志）
//        QString timeStamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
//        QString logFileName = "AudioLog_" + timeStamp + ".txt";
//        QString logFilePath = logDirPath + "/" + logFileName;

//        // 5. 写入日志文件
//        QFile logFile(logFilePath);
//        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
//            QTextStream out(&logFile);
//            // 写入文件头（增强可读性）
//            out << "===== 音频工具日志文件 =====\n";
//            out << "生成时间：" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
//            out << "日志总行数：" << allLogText.count("\n") + 1 << "\n";
//            out << "===========================\n\n";
//            // 写入所有日志内容
//            out << allLogText;
//            // 关闭文件
//            logFile.close();

//            // 6. 保存成功反馈
//            testBrowser->append("\n[日志保存成功]：" + logFilePath);
//            testBrowser->append("----------------------------------");
//            testBrowser->moveCursor(QTextCursor::End);
//        } else {
//            // 保存失败反馈
//            testBrowser->append("[日志保存失败]：无法打开文件：" + logFile.errorString());
//            testBrowser->append("----------------------------------");
//            testBrowser->moveCursor(QTextCursor::End);
//        }
//    });

//    QPushButton *barChartBtn = new QPushButton("柱状图", this);
//    barChartBtn->setFixedSize(75, 32);
//    barChartBtn->setStyleSheet(R"(
//        QPushButton {
//            background-color: #9b59b6;
//            color: white;
//            border: none;
//            border-radius: 4px;
//        }
//        QPushButton:hover {
//            background-color: #8e44ad;
//        }
//    )");
//    bottomCommandHBox->addWidget(barChartBtn);

//    // 在layoutInit()函数中，找到barChartBtn的点击事件连接
//    connect(barChartBtn, &QPushButton::clicked, this, [=]() {
//        if (!m_barChartWindow) {
//            m_barChartWindow = new BarChartMainWindow(this);
//            connect(m_barChartWindow, &BarChartMainWindow::destroyed, this, [=]() {
//                this->show();
//                m_barChartWindow = nullptr;
//            });

//            // 新增：连接NewWindow的滤波强度信号到BarChartMainWindow
//            if (m_newWindow) {  // 若NewWindow已创建
//                connect(m_newWindow, &NewWindow::filteredIntensityUpdated,
//                        m_barChartWindow, &BarChartMainWindow::setGlobalMaxFilteredData);
//            }
//        }

//        // 原有的清除全局最大值连接（保留）
//        connect(m_barChartWindow, &BarChartMainWindow::clearGlobalMaxFilteredIntensity,
//                this, [=]() {
//            m_globalMaxFilteredIntensity = 0.0;
//            globalMaxLabel->setText("全局最大滤强: 0%");
//        });

//        m_barChartWindow->show();
//        this->hide();
//    });

//    commandBt1 = new QPushButton("泄漏", this);
//    commandBt1->setFixedSize(75, 32);
//    commandBt1->setStyleSheet(R"(
//        QPushButton {
//            background-color: #e74c3c;
//            color: white;
//            border: none;
//            border-radius: 4px;
//        }
//        QPushButton:hover {
//            background-color: #c0392b;
//        }
//    )");
//    bottomCommandHBox->addWidget(commandBt1);

//    commandBt2 = new QPushButton("清Max", this);
//    commandBt2->setFixedSize(75, 32);
//    commandBt2->setStyleSheet(R"(
//        QPushButton {
//            background-color: #95a5a6;
//            color: white;
//            border: none;
//            border-radius: 4px;
//        }
//        QPushButton:hover {
//            background-color: #7f8c8d;
//        }
//    )");
//    bottomCommandHBox->addWidget(commandBt2);

//    commandBt3 = new QPushButton("频+", this);
//    commandBt3->setFixedSize(55, 32);
//    commandBt3->setStyleSheet(R"(
//        QPushButton {
//            background-color: #1abc9c;
//            color: white;
//            border: none;
//            border-radius: 4px;
//        }
//        QPushButton:hover {
//            background-color: #16a085;
//        }
//    )");
//    bottomCommandHBox->addWidget(commandBt3);

//    commandBt6 = new QPushButton("频-", this);
//    commandBt6->setFixedSize(55, 32);
//    commandBt6->setStyleSheet(R"(
//        QPushButton {
//            background-color: #16a085;
//            color: white;
//            border: none;
//            border-radius: 4px;
//        }
//        QPushButton:hover {
//            background-color: #138d75;
//        }
//    )");
//    bottomCommandHBox->addWidget(commandBt6);

//    commandVBox->addWidget(bottomCommandWidget);

//    bottomHBox->addWidget(commandWidget);

//    connect(m_saveRawBtn, &QPushButton::clicked, this, &AudioRecorder::onSaveRawBtnClicked);
//    connect(m_playRawBtn, &QPushButton::clicked, this, &AudioRecorder::onPlayRawBtnClicked);
//    connect(m_saveFilteredBtn, &QPushButton::clicked, this, &AudioRecorder::onSaveFilteredBtnClicked);
//    connect(m_playFilteredBtn, &QPushButton::clicked, this, &AudioRecorder::onPlayFilteredBtnClicked);
//    connect(recorderBt, &QPushButton::clicked, this, &AudioRecorder::recorderBtClicked);
//    connect(commandBt1, &QPushButton::clicked, this, &AudioRecorder::commandBt1Clicked);
//    connect(commandBt2, &QPushButton::clicked, this, &AudioRecorder::commandBt2Clicked);
//    connect(commandBt3, &QPushButton::clicked, this, &AudioRecorder::commandBt3Clicked);
//    connect(commandBt6, &QPushButton::clicked, this, &AudioRecorder::commandBt6Clicked);

//    connect(executeBt, &QPushButton::clicked, this, &AudioRecorder::executeBtClicked);
//    connect(newBtn1, &QPushButton::clicked, this, &AudioRecorder::newBtn1Clicked);
//    connect(newBtn2, &QPushButton::clicked, this, &AudioRecorder::newBtn2Clicked);
//    connect(newBtn3, &QPushButton::clicked, this, &AudioRecorder::newBtn3Clicked);
//    connect(newBtn4, &QPushButton::clicked, this, &AudioRecorder::newBtn4Clicked);
//    connect(hSlider, &QSlider::valueChanged, this, &AudioRecorder::updateHValueFromSlider);

//    vBoxLayout->addWidget(bottomWidget);
//    vBoxLayout->addStretch(0);

//    m_decreaseLongPressTimer = new QTimer(this);
//    m_decreaseLongPressTimer->setInterval(50);
//    m_decreaseLongPressTimer->setSingleShot(false);

//    m_increaseLongPressTimer = new QTimer(this);
//    m_increaseLongPressTimer->setInterval(50);
//    m_increaseLongPressTimer->setSingleShot(false);

//    m_volumeDownLongPressTimer = new QTimer(this);
//    m_volumeDownLongPressTimer->setInterval(100);
//    m_volumeDownLongPressTimer->setSingleShot(false);

//    m_volumeUpLongPressTimer = new QTimer(this);
//    m_volumeUpLongPressTimer->setInterval(100);
//    m_volumeUpLongPressTimer->setSingleShot(false);

//    currentValue = 255;

//    connect(decreaseValueBtn, &QPushButton::pressed, this, [=]() {
//        if (currentValue > 0) {
//            currentValue--;
//            valueDisplayLabel->setText(QString::number(currentValue));
//            updateVerticalSliderValue(currentValue);
//        }
//        m_decreaseLongPressTimer->start();
//    });
//    connect(decreaseValueBtn, &QPushButton::released, m_decreaseLongPressTimer, &QTimer::stop);
//    connect(m_decreaseLongPressTimer, &QTimer::timeout, this, [=]() {
//        if (currentValue > 0) {
//            currentValue--;
//            valueDisplayLabel->setText(QString::number(currentValue));
//            updateVerticalSliderValue(currentValue);
//        } else {
//            m_decreaseLongPressTimer->stop();
//        }
//    });

//    connect(increaseValueBtn, &QPushButton::pressed, this, [=]() {
//        if (currentValue < 255) {
//            currentValue++;
//            valueDisplayLabel->setText(QString::number(currentValue));
//            updateVerticalSliderValue(currentValue);
//        }
//        m_increaseLongPressTimer->start();
//    });
//    connect(increaseValueBtn, &QPushButton::released, m_increaseLongPressTimer, &QTimer::stop);
//    connect(m_increaseLongPressTimer, &QTimer::timeout, this, [=]() {
//        if (currentValue < 255) {
//            currentValue++;
//            valueDisplayLabel->setText(QString::number(currentValue));
//            updateVerticalSliderValue(currentValue);
//        } else {
//            m_increaseLongPressTimer->stop();
//        }
//    });

//    connect(volumeDownBtn, &QPushButton::pressed, this, [=]() {
//        int currentVolume = hSlider->value();
//        if (currentVolume > 0) {
//            hSlider->setValue(currentVolume - 1);
//        }
//        m_volumeDownLongPressTimer->start();
//    });
//    connect(volumeDownBtn, &QPushButton::released, m_volumeDownLongPressTimer, &QTimer::stop);
//    connect(m_volumeDownLongPressTimer, &QTimer::timeout, this, [=]() {
//        int currentVolume = hSlider->value();
//        if (currentVolume > 0) {
//            hSlider->setValue(currentVolume - 1);
//        } else {
//            m_volumeDownLongPressTimer->stop();
//        }
//    });

//    connect(volumeUpBtn, &QPushButton::pressed, this, [=]() {
//        int currentVolume = hSlider->value();
//        if (currentVolume < 127) {
//            hSlider->setValue(currentVolume + 1);
//        }
//        m_volumeUpLongPressTimer->start();
//    });
//    connect(volumeUpBtn, &QPushButton::released, m_volumeUpLongPressTimer, &QTimer::stop);
//    connect(m_volumeUpLongPressTimer, &QTimer::timeout, this, [=]() {
//        int currentVolume = hSlider->value();
//        if (currentVolume < 127) {
//            hSlider->setValue(currentVolume + 1);
//        } else {
//            m_volumeUpLongPressTimer->stop();
//        }
//    });

//    connect(hSlider, &QSlider::valueChanged, this, &AudioRecorder::updateHValueFromSlider);
//}


void AudioRecorder::layoutInit()
{
    this->setGeometry(100, 100, 800, 480);
    this->setWindowTitle("音频调节工具");

    mainWidget = new QWidget(this);
    setCentralWidget(mainWidget);

    // ========== 主布局：水平三栏（左音量 | 中功能区 | 右数值） ==========
    QHBoxLayout *mainHLayout = new QHBoxLayout(mainWidget);
    mainHLayout->setContentsMargins(2, 4, 2, 4);
    mainHLayout->setSpacing(4);

    // 统一按钮基础样式：黑白风格
    QString panelBtnBase = R"(
        QPushButton {
            color: black;
            background: #e0e0e0;
            border: 1px solid #888;
            border-radius: 6px;
            font-size: 22px;
            font-weight: bold;
        }
        QPushButton:hover { background: #cccccc; }
        QPushButton:pressed { background: #aaaaaa; }
    )";

    QString sliderStyle = R"(
        QSlider::groove:vertical { width: 10px; background: #e0e0e0; border-radius: 5px; }
        QSlider::handle:vertical { height: 22px; width: 22px; background: #ff5555; margin: 0 -6px; border-radius: 11px; }
    )";

    // ========== 左侧面板：启动 + 音量控制 ==========
    QWidget *leftPanel = new QWidget();
    leftPanel->setFixedWidth(50);
    QVBoxLayout *leftVBox = new QVBoxLayout(leftPanel);
    leftVBox->setContentsMargins(0, 0, 0, 0);
    leftVBox->setSpacing(8);
    leftVBox->addStretch(1);

    recorderBt = new QPushButton("启动", this);
    recorderBt->setFixedSize(45, 32);
    recorderBt->setStyleSheet(panelBtnBase + "QPushButton { font-size: 12px; }");
    leftVBox->addWidget(recorderBt, 0, Qt::AlignCenter);

    leftVBox->addSpacing(10);

    volumeUpBtn = new QPushButton("+", this);
    volumeUpBtn->setFixedSize(45, 28);
    volumeUpBtn->setStyleSheet(panelBtnBase + "QPushButton { font-size: 14px; }");
    leftVBox->addWidget(volumeUpBtn, 0, Qt::AlignCenter);

    hSlider = new QSlider(Qt::Vertical, this);
    hSlider->setRange(0, 127);
    hSlider->setValue(64);
    hSlider->setTickPosition(QSlider::NoTicks);
    hSlider->setStyleSheet(sliderStyle);
    hSlider->setEnabled(false);
    leftVBox->addWidget(hSlider, 2, Qt::AlignCenter);

    volumeDownBtn = new QPushButton("-", this);
    volumeDownBtn->setFixedSize(45, 28);
    volumeDownBtn->setStyleSheet(panelBtnBase + "QPushButton { font-size: 14px; }");
    leftVBox->addWidget(volumeDownBtn, 0, Qt::AlignCenter);

    leftVBox->addStretch(1);
    mainHLayout->addWidget(leftPanel);

    // ========== 中间主内容区域 ==========
    QWidget *centerWidget = new QWidget();
    QVBoxLayout *vBoxLayout = new QVBoxLayout(centerWidget);
    vBoxLayout->setContentsMargins(0, 0, 0, 0);
    vBoxLayout->setSpacing(8);

    // ========== 右侧面板：数值调节 ==========
    QWidget *rightPanel = new QWidget();
    rightPanel->setFixedWidth(50);
    QVBoxLayout *rightVBox = new QVBoxLayout(rightPanel);
    rightVBox->setContentsMargins(0, 0, 0, 0);
    rightVBox->setSpacing(8);
    rightVBox->addStretch(1);

    executeBt = new QPushButton("返回", this);
    executeBt->setFixedSize(45, 32);
    executeBt->setStyleSheet(panelBtnBase + "QPushButton { font-size: 12px; }");
    rightVBox->addWidget(executeBt, 0, Qt::AlignCenter);

    rightVBox->addSpacing(10);

    valueDisplayLabel = new QLabel("255", this);
    valueDisplayLabel->setFixedSize(45, 30);
    valueDisplayLabel->setStyleSheet(R"(
        QLabel {
            background: #2c3e50;
            color: #ecf0f1;
            border: 2px solid #34495e;
            border-radius: 4px;
            font-size: 14px;
            font-weight: bold;
        }
    )");
    valueDisplayLabel->setAlignment(Qt::AlignCenter);
    rightVBox->addWidget(valueDisplayLabel, 0, Qt::AlignCenter);

    m_valueLineEdit = new QLineEdit(this);
    m_valueLineEdit->setFixedSize(45, 25);
    m_valueLineEdit->setStyleSheet(R"(
        QLineEdit {
            background: white;
            border: 2px solid #bdc3c7;
            border-radius: 6px;
            padding: 0 4px;
            text-align: center;
            font-size: 14px;
            font-weight: 500;
            color: #333;
        }
        QLineEdit:hover { border-color: #66afe9; }
        QLineEdit:focus { border-color: #4ecdc4; outline: none; }
    )");
    m_valueLineEdit->setPlaceholderText("0-255");
    m_valueLineEdit->setAlignment(Qt::AlignCenter);
    rightVBox->addWidget(m_valueLineEdit, 0, Qt::AlignCenter);

    decreaseValueBtn = new QPushButton("-", this);
    decreaseValueBtn->setFixedSize(45, 28);
    decreaseValueBtn->setStyleSheet(panelBtnBase + "QPushButton { font-size: 14px; }");
    rightVBox->addWidget(decreaseValueBtn, 0, Qt::AlignCenter);

    increaseValueBtn = new QPushButton("+", this);
    increaseValueBtn->setFixedSize(45, 28);
    increaseValueBtn->setStyleSheet(panelBtnBase + "QPushButton { font-size: 14px; }");
    rightVBox->addWidget(increaseValueBtn, 0, Qt::AlignCenter);

    rightVBox->addStretch(1);

    // ========== 嵌入 NewWindow 频谱/柱状图界面到主窗口中间 ==========
    m_newWindow = new NewWindow(this);
    m_newWindow->setWindowFlags(Qt::Widget); // 从独立窗口转为可嵌入控件
    m_newWindow->prepareForEmbedding();      // 隐藏back/menubar/statusbar
    m_newWindow->setMinimumHeight(420);      // 确保底部控件(记录/清空/滤波)可见
    vBoxLayout->addWidget(m_newWindow, 1);   // stretch=1 让图表区域占满空间

    // 连接NewWindow信号
    connect(m_newWindow, &NewWindow::filteredIntensityUpdated,
            this, &AudioRecorder::onFilteredIntensityReceived);
    connect(m_newWindow, &NewWindow::destroyed, this, [=]() {
        m_newWindow = nullptr;
    });
    // 中心频率变化为整百数时，发送led4_具体频率值
    connect(m_newWindow, &NewWindow::centerFrequencyChanged, this, [=](double freq) {
        QByteArray data = QString("led4_%1").arg((int)freq).toUtf8();
        serialPort->write(data);
    });

    // ========== 底部按钮栏：8个按钮均匀布局（黑白风格） ==========
    QWidget *bottomWidget = new QWidget();
    QHBoxLayout *bottomHBox = new QHBoxLayout(bottomWidget);
    bottomHBox->setSpacing(6);
    bottomHBox->setContentsMargins(2, 4, 2, 4);

    QString bottomBtnStyle = R"(
        QPushButton {
            color: black;
            background: #e0e0e0;
            border: 1px solid #888;
            border-radius: 5px;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover { background: #cccccc; }
        QPushButton:pressed { background: #aaaaaa; }
    )";

    QSize btnSize(72, 36);

    newBtn1 = new QPushButton("远程", this);
    newBtn1->setFixedSize(btnSize);
    newBtn1->setStyleSheet(bottomBtnStyle);
    bottomHBox->addWidget(newBtn1);

    newBtn2 = new QPushButton("获取", this);
    newBtn2->setFixedSize(btnSize);
    newBtn2->setStyleSheet(bottomBtnStyle);
    bottomHBox->addWidget(newBtn2);

    newBtn3 = new QPushButton("频率", this);
    newBtn3->setFixedSize(btnSize);
    newBtn3->setStyleSheet(bottomBtnStyle);
    bottomHBox->addWidget(newBtn3);

    newBtn4 = new QPushButton("delete", this);
    newBtn4->setFixedSize(btnSize);
    newBtn4->setStyleSheet(bottomBtnStyle);
    bottomHBox->addWidget(newBtn4);

    newLeftBtn = new QPushButton("保存", this);
    newLeftBtn->setFixedSize(btnSize);
    newLeftBtn->setStyleSheet(bottomBtnStyle);
    bottomHBox->addWidget(newLeftBtn);
    connect(newLeftBtn, &QPushButton::clicked, this, [=]() {
        m_saveBothChannels = !m_saveBothChannels;
        if (m_saveBothChannels) {
            newLeftBtn->setText("保存中");
            qDebug() << "开始同时保存左右声道滤波数据（目标5万帧）";
            m_leftDataBuffer.clear();
            m_rightDataBuffer.clear();
            m_bothDataCount = 0;
        } else {
            newLeftBtn->setText("保存");
            qDebug() << "手动停止双声道数据保存";
        }
    });

    m_restorePcmBtn = new QPushButton("保存图表", this);
    m_restorePcmBtn->setFixedSize(btnSize);
    m_restorePcmBtn->setStyleSheet(bottomBtnStyle);
    bottomHBox->addWidget(m_restorePcmBtn);
    connect(m_restorePcmBtn, &QPushButton::clicked, this, [=]() {
        if (m_newWindow) m_newWindow->onSaveChartClicked();
    });

    QPushButton *barChartBtn = new QPushButton("柱状图", this);
    barChartBtn->setFixedSize(btnSize);
    barChartBtn->setStyleSheet(bottomBtnStyle);
    bottomHBox->addWidget(barChartBtn);
    connect(barChartBtn, &QPushButton::clicked, this, [=]() {
        if (!m_barChartWindow) {
            m_barChartWindow = new BarChartMainWindow(this);
            connect(m_barChartWindow, &BarChartMainWindow::destroyed, this, [=]() {
                this->show();
                m_barChartWindow = nullptr;
            });
            if (m_newWindow) {
                connect(m_newWindow, &NewWindow::filteredIntensityUpdated,
                        m_barChartWindow, &BarChartMainWindow::setGlobalMaxFilteredData);
            }
        }
        connect(m_barChartWindow, &BarChartMainWindow::clearGlobalMaxFilteredIntensity,
                this, [=]() {
            m_globalMaxFilteredIntensity = 0.0;
        });
        m_barChartWindow->show();
        this->hide();
    });

    commandBt1 = new QPushButton("打开文件", this);
    commandBt1->setFixedSize(btnSize);
    commandBt1->setStyleSheet(bottomBtnStyle);
    bottomHBox->addWidget(commandBt1);

    vBoxLayout->addWidget(bottomWidget);

    connect(recorderBt, &QPushButton::clicked, this, &AudioRecorder::recorderBtClicked);
    connect(commandBt1, &QPushButton::clicked, this, [=]() {
        if (m_newWindow) m_newWindow->onOpenChartClicked();
    });

    connect(executeBt, &QPushButton::clicked, this, &AudioRecorder::executeBtClicked);
    connect(newBtn1, &QPushButton::clicked, this, &AudioRecorder::newBtn1Clicked);
    connect(newBtn2, &QPushButton::clicked, this, &AudioRecorder::newBtn2Clicked);
    connect(newBtn3, &QPushButton::clicked, this, &AudioRecorder::newBtn3Clicked);
    connect(newBtn4, &QPushButton::clicked, this, &AudioRecorder::newBtn4Clicked);
    connect(hSlider, &QSlider::valueChanged, this, &AudioRecorder::updateHValueFromSlider);

    // 将三栏添加到主水平布局（左 → 中 → 右）
    mainHLayout->addWidget(centerWidget, 1); // stretch=1 让中间占满剩余空间
    mainHLayout->addWidget(rightPanel);

    m_decreaseLongPressTimer = new QTimer(this);
    m_decreaseLongPressTimer->setInterval(50);
    m_decreaseLongPressTimer->setSingleShot(false);

    m_increaseLongPressTimer = new QTimer(this);
    m_increaseLongPressTimer->setInterval(50);
    m_increaseLongPressTimer->setSingleShot(false);

    m_volumeDownLongPressTimer = new QTimer(this);
    m_volumeDownLongPressTimer->setInterval(50);
    m_volumeDownLongPressTimer->setSingleShot(false);

    m_volumeUpLongPressTimer = new QTimer(this);
    m_volumeUpLongPressTimer->setInterval(50);
    m_volumeUpLongPressTimer->setSingleShot(false);

    currentValue = 255;

    connect(decreaseValueBtn, &QPushButton::pressed, this, [=]() {
        if (currentValue > 0) {
            currentValue--;
            valueDisplayLabel->setText(QString::number(currentValue));
            updateVerticalSliderValue(currentValue);
        }
        m_decreaseLongPressTimer->start();
    });
    connect(decreaseValueBtn, &QPushButton::released, m_decreaseLongPressTimer, &QTimer::stop);
    connect(m_decreaseLongPressTimer, &QTimer::timeout, this, [=]() {
        if (currentValue > 0) {
            currentValue--;
            valueDisplayLabel->setText(QString::number(currentValue));
            updateVerticalSliderValue(currentValue);
        } else {
            m_decreaseLongPressTimer->stop();
        }
    });

    connect(increaseValueBtn, &QPushButton::pressed, this, [=]() {
        if (currentValue < 255) {
            currentValue++;
            valueDisplayLabel->setText(QString::number(currentValue));
            updateVerticalSliderValue(currentValue);
        }
        m_increaseLongPressTimer->start();
    });
    connect(increaseValueBtn, &QPushButton::released, m_increaseLongPressTimer, &QTimer::stop);
    connect(m_increaseLongPressTimer, &QTimer::timeout, this, [=]() {
        if (currentValue < 255) {
            currentValue++;
            valueDisplayLabel->setText(QString::number(currentValue));
            updateVerticalSliderValue(currentValue);
        } else {
            m_increaseLongPressTimer->stop();
        }
    });

    connect(volumeDownBtn, &QPushButton::pressed, this, [=]() {
        int currentVolume = hSlider->value();
        if (currentVolume > 0) {
            hSlider->setValue(currentVolume - 5);
        }
        m_volumeDownLongPressTimer->start();
    });
    connect(volumeDownBtn, &QPushButton::released, m_volumeDownLongPressTimer, &QTimer::stop);
    connect(m_volumeDownLongPressTimer, &QTimer::timeout, this, [=]() {
        int currentVolume = hSlider->value();
        if (currentVolume > 0) {
            hSlider->setValue(currentVolume - 5);
        } else {
            m_volumeDownLongPressTimer->stop();
        }
    });

    connect(volumeUpBtn, &QPushButton::pressed, this, [=]() {
        int currentVolume = hSlider->value();
        if (currentVolume < 127) {
            hSlider->setValue(currentVolume + 5);
        }
        m_volumeUpLongPressTimer->start();
    });
    connect(volumeUpBtn, &QPushButton::released, m_volumeUpLongPressTimer, &QTimer::stop);
    connect(m_volumeUpLongPressTimer, &QTimer::timeout, this, [=]() {
        int currentVolume = hSlider->value();
        if (currentVolume < 127) {
            hSlider->setValue(currentVolume + 5);
        } else {
            m_volumeUpLongPressTimer->stop();
        }
    });

    connect(hSlider, &QSlider::valueChanged, this, &AudioRecorder::updateHValueFromSlider);
}



void AudioRecorder::setupAudio()
{
    // 定义音频格式
    QAudioFormat format;
    format.setSampleRate(44100);                  // 采样率
    format.setChannelCount(2);                    // 声道数
    format.setSampleSize(16);                     // 每个样本的位数
    format.setCodec("audio/pcm");                 // 编码格式
    format.setByteOrder(QAudioFormat::LittleEndian);// 字节序
    format.setSampleType(QAudioFormat::SignedInt); // 样本类型

    // 检查系统是否支持该格式
    QAudioDeviceInfo inputInfo = QAudioDeviceInfo::defaultInputDevice();
    if (!inputInfo.isFormatSupported(format)) {
        qWarning() << "默认音频输入格式不受支持，尝试使用最接近的格式。";
        format = inputInfo.nearestFormat(format);
    }

    QAudioDeviceInfo outputInfo = QAudioDeviceInfo::defaultOutputDevice();
    if (!outputInfo.isFormatSupported(format)) {
        qWarning() << "默认音频输出格式不受支持，尝试使用最接近的格式。";
        format = outputInfo.nearestFormat(format);
    }

    // 创建音频输入和输出对象
    m_audioInput = new QAudioInput(inputInfo, format, this);
    m_audioOutput = new QAudioOutput(outputInfo, format, this);

    // 设置缓冲大小和通知间隔
    m_audioInput->setBufferSize(4096);
    m_audioInput->setNotifyInterval(5000); // 毫秒

    // 创建定时器用于更新时间
    recordTimer = new QTimer(this);
    connect(recordTimer, &QTimer::timeout, this, &AudioRecorder::updateProgress);
}

void AudioRecorder::recorderBtClicked()
{
    if (!isRecording) {
        // 开始录音和播放
        m_inputDevice = m_audioInput->start();
        if (!m_inputDevice) {
            qWarning() << "无法启动音频输入设备。";
            return;
        }

        m_outputDevice = m_audioOutput->start();
        if (!m_outputDevice) {
            qWarning() << "无法启动音频输出设备。";
            m_audioInput->stop();
            return;
        }

        // 连接 readyRead 信号从 m_inputDevice 而不是 m_audioInput
        connect(m_inputDevice, &QIODevice::readyRead, this, &AudioRecorder::handleAudioInput);

        recordTimer->start(1000); // 每秒更新时间

        recorderBt->setText("停止");
        isRecording = true;
    }

}

void AudioRecorder::executeBtClicked()
{
    // 创建 QProcess 对象
    QString program = "./jqv1.2 RPMsg_UART_CM4.elf";

    // 启动外部程序，使用 startDetached 保证外部程序独立运行
    QProcess::startDetached(program);

    // 退出当前程序
    QCoreApplication::quit();

}


// 新增：保存原始PCM数据到TXT文件（限制30万个样本点）
void AudioRecorder::saveRawDataToTxt(const QByteArray &buffer)
{
    // 静态计数器，记录已保存的总样本数（跨函数调用保持值）
    static int totalSavedSamples = 0;
    // 最大保存样本数（30万）
    const int MAX_SAMPLES = 150000;

    // 如果已达到最大样本数，直接返回
    if (totalSavedSamples >= MAX_SAMPLES) {
        return;
    }

    QAudioFormat format = m_audioInput->format();
    if (!format.isValid())
        return;

    // 打开文件（追加模式），如果文件不存在则创建
    QFile file("raw_audio_data.txt");
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        qWarning() << "无法打开文件用于写入:" << file.errorString();
        return;
    }

    QTextStream out(&file);

    int bytesPerSample = format.sampleSize() / 8;
    int channelCount = format.channelCount();
    int frameSize = bytesPerSample * channelCount;
    int frameCount = buffer.size() / frameSize;

    const char *data = buffer.constData();

    // 计算本次可写入的最大帧数（每个帧包含所有声道的一个样本）
    int remainingSamples = MAX_SAMPLES - totalSavedSamples;
    int writeFrames = qMin(frameCount, remainingSamples);

    // 写入每个采样点的原始值（限制在剩余可写入数量内）
    for (int i = 0; i < writeFrames; ++i) {
        out << "帧 " << (totalSavedSamples + i) << ": ";
        for (int ch = 0; ch < channelCount; ++ch) {
            if (format.sampleType() == QAudioFormat::SignedInt) {
                if (bytesPerSample == 2) { // 16-bit 有符号整数
                    qint16 value;
                    memcpy(&value, data + i * frameSize + ch * bytesPerSample, bytesPerSample);
                    out << "声道" << ch << "=" << value << " ";
                }
                // 可以添加对8-bit、32-bit等其他格式的支持
            }
            // 可以添加对无符号整数、浮点数等样本类型的支持
        }
        out << endl;
    }

    // 更新已保存样本数
    totalSavedSamples += writeFrames;

    // 关闭文件
    file.close();

    // 当达到最大样本数时输出提示
    if (totalSavedSamples >= MAX_SAMPLES) {
        qDebug() << "已达到最大保存样本数（" << MAX_SAMPLES << "），停止写入数据";
    }
}




//void AudioRecorder::commandBt1Clicked()
//{
//    if (!m_newWindow) {
//        m_newWindow = new NewWindow(this);
//        // 新增：连接左声道强度信号与槽
//          connect(this, &AudioRecorder::leftChannelLevelUpdated,
//                  m_newWindow, &NewWindow::setLeftChannelLevel);
//    }
//    m_newWindow->show();
//    m_newWindow->activateWindow();
//}



void AudioRecorder::commandBt1Clicked() {
    // NewWindow 已嵌入主界面，点击"泄漏"仅确保可见
    if (m_newWindow && m_newWindow->isHidden()) {
        m_newWindow->show();
    }
}


// 实现槽函数
void AudioRecorder::onCenterFrequencyChanged(double newFreq)
{
    // 更新本地中心频率参数
    m_centerFreq = newFreq;

    // 可以在这里添加需要的后续处理，例如：
    // 1. 更新UI显示
    qDebug() << "中心频率已更新为：" << newFreq;
}





// 修改后的函数：仅自动排序的文件名
//void AudioRecorder::saveBothFilteredData(const QVector<double>& /*filteredLeftData*/, const QVector<double>& /*filteredRightData*/,
//                                         const QVector<double>& originalLeftData, const QVector<double>& originalRightData) {
//    if (!m_saveBothChannels) return;  // 未开启保存时直接返回

//    // 仅校验原始数据的帧对齐（忽略滤波数据）
//    int validFrames = qMin(originalLeftData.size(), originalRightData.size());
//    if (validFrames == 0) return;

//    // 仅累加原始数据到缓冲区（移除滤波数据处理）
//    for (int i = 0; i < validFrames; ++i) {
//        // 只处理原始数据缓冲区
//        m_originalLeftDataBuffer.append(originalLeftData[i]);
//        m_originalRightDataBuffer.append(originalRightData[i]);

//        m_bothDataCount++;

//        // 达到5万帧时保存并重置
//        if (m_bothDataCount >= MAX_BOTH_DATA) {
//            // 只保存原始数据（移除滤波数据相关代码）
//            QString originalFileName = QString("both_channels_original_data_%1.txt").arg(m_bothFileIndex);
//            saveDataToFile(originalFileName, m_originalLeftDataBuffer, m_originalRightDataBuffer);

//            // 仅重置原始数据缓冲区
//            m_originalLeftDataBuffer.clear();
//            m_originalRightDataBuffer.clear();
//            m_bothDataCount = 0;
//            m_bothFileIndex++;

//            // 自动停止保存
//            m_saveBothChannels = false;
//            newLeftBtn->setText("保存");
//            return;
//        }
//    }
//}


void AudioRecorder::saveBothFilteredData(const QVector<double>& /*filteredLeftData*/, const QVector<double>& /*filteredRightData*/,
                                         const QVector<double>& originalLeftData, const QVector<double>& originalRightData) {
    if (!m_saveBothChannels) return;  // 未开启保存时直接返回

    // 仅校验原始数据的帧对齐（忽略滤波数据）
    int validFrames = qMin(originalLeftData.size(), originalRightData.size());
    if (validFrames == 0) return;

    // 仅累加原始数据到缓冲区（移除滤波数据处理）
    for (int i = 0; i < validFrames; ++i) {
        // 只处理原始数据缓冲区
        m_originalLeftDataBuffer.append(originalLeftData[i]);
        m_originalRightDataBuffer.append(originalRightData[i]);

        m_bothDataCount++;

        // 达到5万帧时保存并重置
        if (m_bothDataCount >= MAX_BOTH_DATA) {
            // 获取m_valueLineEdit中的文本作为基础文件名
            QString baseFileName = m_valueLineEdit->text().trimmed();

            // 处理空文件名的情况（提供默认值）
            if (baseFileName.isEmpty()) {
                baseFileName = "both_channels_original_data";
            }

            // 拼接完整的文件名（包含索引）
            QString originalFileName = QString("%1_%2.txt").arg(baseFileName).arg(m_bothFileIndex);

            // 保存数据到文件
            saveDataToFile(originalFileName, m_originalLeftDataBuffer, m_originalRightDataBuffer);

            // 仅重置原始数据缓冲区
            m_originalLeftDataBuffer.clear();
            m_originalRightDataBuffer.clear();
            m_bothDataCount = 0;
            m_bothFileIndex++;

            // 自动停止保存
            m_saveBothChannels = false;
            if (newLeftBtn) { // 空指针保护
                newLeftBtn->setText("保存");
            }
            return;
        }
    }
}






// 保持辅助函数不变，但实际只会处理原始数据
void AudioRecorder::saveDataToFile(const QString& fileName, const QVector<double>& leftData, const QVector<double>& rightData) {
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        // 按"左声道原始值,右声道原始值"格式写入，每行一帧
        for (int j = 0; j < leftData.size() && j < rightData.size(); ++j) {
            out << leftData[j] << "," << rightData[j] << "\n";
        }
        file.close();
        qDebug() << "已保存原始数据至" << fileName << "（" << leftData.size() << "帧）";
    } else {
        qDebug() << "无法写入文件" << fileName << "：" << file.errorString();
    }
}





void AudioRecorder::handleAudioInput()
{
    if (!m_inputDevice || !m_outputDevice)
        return;

    QByteArray buffer = m_inputDevice->readAll();
    if (buffer.isEmpty())
        return;

    // 实时播放音频
    m_outputDevice->write(buffer);

    // NewWindow 已嵌入主界面，无需再创建/弹出

    // ================= 提取左右声道原始波形数据 =================
    QVector<double> leftChannelData;
    QVector<double> rightChannelData;




    QAudioFormat format = m_audioInput->format(); // 获取音频格式
    if (format.isValid()) {
        int bytesPerSample = format.sampleSize() / 8; // 每个样本的字节数
        int channelCount = format.channelCount();     // 声道数（立体声为2）
        int frameSize = bytesPerSample * channelCount; // 每帧字节数
        int frameCount = buffer.size() / frameSize;    // 总帧数

        const char *data = buffer.constData();

        for (int i = 0; i < frameCount; ++i) {
            // 提取左声道数据（索引0）
            int leftChannelPos = i * frameSize + 0 * bytesPerSample;
            // 提取右声道数据（索引1）
            int rightChannelPos = i * frameSize + 1 * bytesPerSample;

            if (format.sampleType() == QAudioFormat::SignedInt && bytesPerSample == 2) {
                qint16 leftSample, rightSample;
                // 左声道
                memcpy(&leftSample, data + leftChannelPos, bytesPerSample);
                leftChannelData.append(static_cast<double>(leftSample));

                // 右声道
                memcpy(&rightSample, data + rightChannelPos, bytesPerSample);
                rightChannelData.append(static_cast<double>(rightSample));
            }
        }
    }


    if (m_newWindow) {
        m_newWindow->updateSpectrumBars(leftChannelData);  // 传入左声道数据
    }



    // 以下是原有的音量强度显示逻辑（保持不变）
    QVector<qreal> levels = getBufferLevels(buffer);


    // 初始化左右声道强度值
    qreal leftChannelLevel = 0.0;
    qreal rightChannelLevel = 0.0;

    // 获取左声道强度（索引0）
    if (levels.count() >= 1) {
        leftChannelLevel = levels.at(0);
        // 更新左声道最大值
        if (leftChannelLevel > m_maxLeftChannelLevel) {
            m_maxLeftChannelLevel = leftChannelLevel;
        }



    }

    // 获取右声道强度（索引1）
    if (levels.count() >= 2) {
        rightChannelLevel = levels.at(1);
        // 更新右声道最大值（需在类中添加m_maxRightChannelLevel成员变量）
        if (rightChannelLevel > m_maxRightChannelLevel) {
            m_maxRightChannelLevel = rightChannelLevel;
        }
    }






      saveBothFilteredData(QVector<double>(), QVector<double>(), leftChannelData, rightChannelData);

}



QVector<qreal> AudioRecorder::getBufferLevels(const QByteArray &buffer)
{
    QVector<qreal> values;

    // 获取音频格式
    QAudioFormat format = m_audioInput->format();
    if (!format.isValid())
        return values;

    int bytesPerSample = format.sampleSize() / 8;
    int channelCount = format.channelCount();
    int frameSize = bytesPerSample * channelCount;
    int frameCount = buffer.size() / frameSize;

    values.fill(0, channelCount);

    const char *data = buffer.constData();

    for (int i = 0; i < frameCount; ++i) {
        for (int ch = 0; ch < channelCount; ++ch) {
            qreal sample = 0.0;

            // 处理不同的样本类型
            if (format.sampleType() == QAudioFormat::SignedInt) {
                if (bytesPerSample == 2) { // 16-bit
                    qint16 value;
                    memcpy(&value, data + i * frameSize + ch * bytesPerSample, bytesPerSample);
                    sample = qAbs(static_cast<qreal>(value)) / static_cast<qreal>(SHRT_MAX);
                }
                // 可以添加更多样本大小的处理
            }
            // 可以添加更多样本类型的处理

            if (sample > values[ch])
                values[ch] = sample;
        }
    }

    return values;
}





void AudioRecorder::updateProgress()
{
    static qint64 recordedSeconds = 0;
    recordedSeconds += 1; // 增加1秒
}


void AudioRecorder::updateHValueFromSlider(int value)
{


    // 使用QProcess异步执行命令
    QProcess *process = new QProcess(this);
    process->start("amixer", QStringList() << "-c" << "0" << "cset" << "numid=1" << QString::number(value));

}

// 槽函数修改（滑动条变量名同步修改）
void AudioRecorder::updateVerticalSliderValue(int value)
{
    // 移除数值反转逻辑，直接使用滑动条的值
    int setValue = value;

    // 执行系统命令
    QString command1 = QString("echo %1 > /sys/devices/platform/soc/40015000.i2c/i2c-2/2-002c/rdac1").arg(setValue);
    QProcess *process1 = new QProcess(this);
    process1->start("/bin/sh", QStringList() << "-c" << command1);

    QString command2 = QString("echo %1 > /sys/devices/platform/soc/40015000.i2c/i2c-2/2-002c/rdac0").arg(setValue);
    QProcess *process2 = new QProcess(this);
    process2->start("/bin/sh", QStringList() << "-c" << command2);

    // 资源清理
    connect(process1, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            process1, &QProcess::deleteLater);
    connect(process2, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            process2, &QProcess::deleteLater);
}


void AudioRecorder::scanSerialPort()
{
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    qDebug() << "可用的串口列表:";
    for (const QSerialPortInfo &info : ports) {
        qDebug() << "Port:" << info.portName() << "Description:" << info.description();
    }

    if (!ports.isEmpty()) {
        // 如果有可用串口，设置默认串口
        serialPort->setPortName(ports.first().portName());
        qDebug() << "已选择串口:" << ports.first().portName();
    }
}



// 实现新按钮的槽函数
void AudioRecorder::newBtn1Clicked() {
    qDebug() << "新按钮1被点击";
    // 添加按钮1的功能代码

    // 禁用按钮，防止重复点击
    newBtn1->setEnabled(false);
    // 可选：改变按钮样式以直观显示已禁用
    newBtn1->setStyleSheet("background-color: black; color: lightgray;");

    QFile file("/dev/ttyRPMSG0");
    if (!file.exists()) {
        system("cd /lib/firmware");
        QString fw = tr("echo %1 > /sys/class/remoteproc/remoteproc0/firmware").arg(firmware);
        system(fw.toLatin1().data());
        system("echo start > /sys/class/remoteproc/remoteproc0/state");
        system("echo start > /dev/ttyRPMSG0");
        system("sleep 1");
        scanSerialPort();

    }



    // 固定参数配置
    serialPort->setPortName("/dev/ttyRPMSG0");  // 固定设备名
    serialPort->setBaudRate(115200);           // 固定波特率
    serialPort->setDataBits(QSerialPort::Data8); // 8位数据位
    serialPort->setParity(QSerialPort::NoParity); // 无校验
    serialPort->setStopBits(QSerialPort::OneStop); // 1位停止位
    serialPort->setFlowControl(QSerialPort::NoFlowControl); // 无流控



    // 尝试打开串口
    if (!serialPort->open(QIODevice::ReadWrite)) {
        QString errorMsg = QString("串口打开失败!\n"
                                   "设备: %1\n"
                                   "错误: %2\n"
                                   "可能原因:\n"
                                   "1. 设备不存在\n"
                                   "2. 权限不足(尝试: sudo chmod 666 /dev/ttyRPMSG0)\n"
                                   "3. 串口已被占用")
                .arg(serialPort->portName())
                .arg(serialPort->errorString());

    } else {


        qDebug() << "串口已打开 - 配置: 115200 8N1";
    }

}


void AudioRecorder::newBtn2Clicked() {
    qDebug() << "新按钮2被点击";
    // 添加按钮2的功能代码

    QByteArray data = "led1_on";  // 假设我们要发送的测试数据
    serialPort->write(data);

//    switch(ycyl) {
//    case 0: ycyl = 30; break;
//    case 30: ycyl = 50; break;
//    case 50: ycyl = 80; break;
//    case 80: ycyl = 100; break;
//    default: ycyl = 0; break;
//    }

    // 更新按钮文本显示当前音量
    //newBtn2->setText(QString("音量: %1%").arg(ycyl));


}

void AudioRecorder::newBtn3Clicked() {
    qDebug() << "新按钮3被点击";
    // 添加按钮3的功能代码
}

void AudioRecorder::newBtn4Clicked() {
    qDebug() << "新按钮4被点击";
    // 添加按钮4的功能代码
//扫频
//    QByteArray data = "led1_off";  // 假设我们要发送的测试数据
//    serialPort->write(data);

    qDebug() << "delete按钮被点击";

}






//void AudioRecorder::serialPortReadyRead()
//{

//    QByteArray buf = serialPort->readAll();

//    testBrowser->append("接收: " + QString(buf));




//}



void AudioRecorder::serialPortReadyRead()
{
    QByteArray buf = serialPort->readAll();
    QString data = QString(buf);
    qDebug() << "接收: " + data;

    // 解析纬度（兼容"维度"和"纬度"字段）
    QRegExp latReg("(维度|纬度)[:：]\\s*([-+]?\\d+\\.?\\d*)");
    if (latReg.indexIn(data) != -1) {
        m_latitude = latReg.cap(2).toDouble();
        // 关键：如果GPS窗口已打开，发送更新信号
        if (m_gpsWindow != nullptr && m_gpsWindow->isVisible()) {
            emit gpsDataSent(m_latitude, m_longitude);
        }
    }

    // 解析经度
    QRegExp lonReg("经度[:：]\\s*([-+]?\\d+\\.?\\d*)");
    if (lonReg.indexIn(data) != -1) {
        m_longitude = lonReg.cap(1).toDouble();
        // 关键：如果GPS窗口已打开，发送更新信号
        if (m_gpsWindow != nullptr && m_gpsWindow->isVisible()) {
            emit gpsDataSent(m_latitude, m_longitude);
        }
    }
}
