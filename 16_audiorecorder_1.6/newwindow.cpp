#include "newwindow.h"
#include "ui_newwindow.h"
#include <cmath>
#include <vector>
#include <complex>  // 添加复数头文件
#include <algorithm> // 用于std::max_element
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>

NewWindow::NewWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::NewWindow)
{
    ui->setupUi(this); // 先初始化UI
       // 获取控件指针
       radioButton = ui->radioButton;
       radioButton_2 = ui->radioButton_2;
       label_5 = ui->label_5;
       label_7 = ui->label_7;
       pushButton_2 = ui->pushButton_2;
       pushButton_3 = ui->pushButton_3;

       // 连接按钮与功能函数
       connect(pushButton_2, &QPushButton::clicked, this, &NewWindow::onAddClicked);
       connect(pushButton_3, &QPushButton::clicked, this, &NewWindow::onSubtractClicked);

       // 初始化显示（默认选中label_5，显示初始数值）
       radioButton->setChecked(true);
       updateLabelSize();

    // ========== 底部区域重新规划布局 ==========
    // 第一排 y=355：操作按钮（左：图表操作 | 右：频率调整）
    ui->pushButton_4->setGeometry(50, 355, 55, 31);     // 记录
    ui->pushButton_5->setGeometry(115, 355, 55, 31);    // 清空
    ui->pushButton_2->setGeometry(520, 355, 41, 31);    // 频率 +
    ui->pushButton_3->setGeometry(630, 355, 41, 31);    // 频率 -

    // 第二排 y=393：信息显示（左：a值 | 右：频率参数）
    ui->label_5->setGeometry(240, 393, 31, 20);          // 100
    ui->label_6->setGeometry(280, 393, 31, 20);          // ~~~
    ui->label_7->setGeometry(320, 393, 41, 20);          // 1500
    ui->radioButton->setGeometry(370, 393, 51, 20);      // s
    ui->radioButton_2->setGeometry(440, 393, 60, 20);    // x
    ui->label->setGeometry(570, 393, 51, 20);            // 中:500

    // 初始化坐标轴，固定纵轴范围
    ui->myCustomPlot->xAxis->setLabel("频率 (Hz)");
    ui->myCustomPlot->yAxis->setLabel("幅值");
    ui->myCustomPlot->xAxis->setRange(100, 2000); // 横轴范围不变
    // 纵轴固定范围：根据实际信号幅值调整（例如0~50000，确保最大信号不超出）
    ui->myCustomPlot->yAxis->setRange(0, 100);

    // 在NewWindow构造函数中修改m_spectrumBars的样式
    m_spectrumBars = new QCPBars(ui->myCustomPlot->xAxis, ui->myCustomPlot->yAxis);
    m_spectrumBars->setWidth(3.0); // 适当加宽柱子
    m_spectrumBars->setPen(QPen(Qt::darkBlue, 1)); // 深色边框，突出轮廓
    m_spectrumBars->setBrush(QColor(50, 150, 255, 220)); // 提高不透明度（220/255），蓝色更鲜艳
    // 可选：添加网格线辅助观察
    ui->myCustomPlot->xAxis->grid()->setVisible(true);
    ui->myCustomPlot->yAxis->grid()->setVisible(true);




    m_avgBars = new QCPBars(ui->widget->xAxis, ui->widget->yAxis);
    m_avgBars->setWidth(0.2);

    ui->widget->yAxis->setRange(0, 100);

    ui->widget->xAxis->setLabel("记录次数");
    ui->widget->xAxis->setRange(0, m_maxBars + 1);

    // 深度显示标签（第一排中间）
    m_depthLabel = new QLabel("深度: --", this);
    m_depthLabel->setGeometry(220, 350, 300, 40);
    m_depthLabel->setAlignment(Qt::AlignCenter);
    m_depthLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #000000; background: #ffffff; border: 2px solid #555; border-radius: 4px;");

    // a值调整控件（第二排左侧）
    m_aMinusBtn = new QPushButton("-", this);
    m_aMinusBtn->setGeometry(50, 393, 30, 31);
    m_aMinusBtn->setStyleSheet("QPushButton { color: black; background: #e0e0e0; border: 1px solid #888; border-radius: 4px; font-size: 16px; font-weight: bold; } QPushButton:hover { background: #ccc; }");
    connect(m_aMinusBtn, &QPushButton::clicked, this, &NewWindow::onADecrease);

    m_aValueLabel = new QLabel("a:0.4", this);
    m_aValueLabel->setGeometry(85, 393, 50, 31);
    m_aValueLabel->setAlignment(Qt::AlignCenter);
    m_aValueLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #2c3e50; background: #ffffff; border: 1px solid #888; border-radius: 3px;");

    m_aPlusBtn = new QPushButton("+", this);
    m_aPlusBtn->setGeometry(140, 393, 30, 31);
    m_aPlusBtn->setStyleSheet("QPushButton { color: black; background: #e0e0e0; border: 1px solid #888; border-radius: 4px; font-size: 16px; font-weight: bold; } QPushButton:hover { background: #ccc; }");
    connect(m_aPlusBtn, &QPushButton::clicked, this, &NewWindow::onAIncrease);


    ui->widget->yAxis->setTicks(true);
    ui->widget->yAxis->setTickLabels(true);
    ui->widget->yAxis->setNumberFormat("f");
    ui->widget->yAxis->setNumberPrecision(0);   // 0位小数，想要1位就改成1


    ui->widget->replot();
}



NewWindow::~NewWindow()
{
    // m_spectrumBars 和 m_avgBars 由 QCustomPlot 自动管理，无需手动删除
    delete ui;
}

void NewWindow::prepareForEmbedding()
{
    ui->pushButton->hide();   // 隐藏back按钮
    ui->menubar->hide();      // 隐藏菜单栏
    ui->statusbar->hide();    // 隐藏状态栏
}

// 在newwindow.cpp中替换原fft函数为迭代版本
QVector<std::complex<double>> NewWindow::fft(const QVector<std::complex<double>>& x)
{
    int N = x.size();
    QVector<std::complex<double>> result = x;

    // 位反转
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(result[i], result[j]);
    }

    // 迭代FFT
    for (int len = 2; len <= N; len <<= 1) {
        double ang = 2 * M_PI / len;
        std::complex<double> wlen(cos(ang), -sin(ang)); // 旋转因子
        for (int i = 0; i < N; i += len) {
            std::complex<double> w(1);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<double> u = result[i + j];
                std::complex<double> v = result[i + j + len/2] * w;
                result[i + j] = u + v;
                result[i + j + len/2] = u - v;
                w *= wlen;
            }
        }
    }
    return result;
}
//接收原始pcm数据
void NewWindow::updateSpectrumBars(const QVector<double>& filteredAudioData)
{
    // 直接使用原始数据，不做滤波
    const QVector<double>& rawData = filteredAudioData;

    // 计算强度并显示在 label_3
    if (!rawData.isEmpty()) {
        // 1. 计数器递增，达到阈值才更新显示
        m_labelUpdateCounter++;
        if (m_labelUpdateCounter >= m_labelUpdateThreshold) {
            m_labelUpdateCounter = 0;  // 重置计数器

            // 2. 找到原始数据的峰值（最大绝对值）
            double maxVal = *std::max_element(rawData.begin(), rawData.end());
            double minVal = *std::min_element(rawData.begin(), rawData.end());
            double peakValue = qMax(qAbs(maxVal), qAbs(minVal));

            // 3. 归一化到 0-100（16位PCM最大幅值32767）
            double refMax = 32767.0;
            double filteredIntensity = (peakValue / refMax) * 100.0;

            // 4. 边界约束（0-100范围）
            filteredIntensity = qBound(0.0, filteredIntensity, 100.0);

            // 你原本就有的显示
            ui->label_3->setText(QString(" %1%").arg(filteredIntensity, 0, 'f', 1));

            // ===== 可选：继续维护最近10次缓存（你已有就保留）=====
            m_intensityBuffer.append(filteredIntensity);
            if (m_intensityBuffer.size() > 10)
                m_intensityBuffer.removeFirst();

            // ===== 关键：如果正在录制，就累计6次 =====
            if (m_recording) {
                m_recordBuffer.append(filteredIntensity);

                if (m_recordBuffer.size() >= 6) {
                    // 6个值排序 → 去头去尾 → 中间4个取大的2个求平均
                    QVector<double> sorted = m_recordBuffer;
                    std::sort(sorted.begin(), sorted.end());
                    double result = (sorted[3] + sorted[4]) / 2.0;

                    // 结束本次录制
                    m_recording = false;
                    m_recordBuffer.clear();

                    // 把结果追加为一根柱子
                    m_avgHistory.append(result);
                    if (m_avgHistory.size() > m_maxBars)
                        m_avgHistory.removeFirst();

                    // 更新柱状图数据
                    QVector<double> keys, vals;
                    int n = m_avgHistory.size();
                    keys.reserve(n);
                    vals.reserve(n);
                    for (int i = 0; i < n; ++i) {
                        keys << (i + 1);
                        vals << m_avgHistory[i];
                    }

                    m_avgBars->setData(keys, vals);

                    // X轴固定显示1~10
                    ui->widget->xAxis->setRange(0, 11);

                    // 打开Y轴刻度数字
                    ui->widget->yAxis->setTicks(true);
                    ui->widget->yAxis->setTickLabels(true);
                    ui->widget->yAxis->setNumberFormat("f");
                    ui->widget->yAxis->setNumberPrecision(0);

                    // 计算ymax并留足顶部空间，防止文字被裁
                    double ymax = 0.0;
                    for (double v : m_avgHistory) ymax = qMax(ymax, v);
                    ui->widget->yAxis->setRange(0, ymax * 1.15 + 8.0);

                    // 先清掉旧的文字标签
                    ui->widget->clearItems();

                    // 给每根柱子加一个数值显示
                    for (int i = 0; i < n; ++i) {
                        QCPItemText *text = new QCPItemText(ui->widget);
                        text->setLayer("overlay");
                        text->position->setType(QCPItemPosition::ptPlotCoords);

                        // 往上抬一点，避免贴边
                        text->position->setCoords(i + 1, m_avgHistory[i] + 1.0);

                        text->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
                        text->setText(QString::number(m_avgHistory[i], 'f', 1));
                    }

                    ui->widget->replot();

                    // 当记录数≥7时，自动计算深度
                    if (m_avgHistory.size() >= 7) {
                        calculateDepthWhenReady();
                    }
                }
            }



            // 计算出filteredIntensity后直接打印
           // qDebug() << "NewWindow中计算的滤波强度：" << filteredIntensity;

            // 发送信号
            emit filteredIntensityUpdated(filteredIntensity);


        }
    }

    // 后续FFT计算和频谱显示逻辑（使用原始数据）
    // 1. 准备FFT输入（基于原始数据）
    QVector<std::complex<double>> fftInput;
    for (int i = 0; i < FFT_SIZE; ++i) {
        double sample = (i < rawData.size()) ? rawData[i] / 32768.0 : 0.0;
        fftInput.append(std::complex<double>(sample, 0.0));
    }

    // 2. 执行FFT变换
    QVector<std::complex<double>> fftResult = fft(fftInput);

    // 3. 计算频率轴和幅值（显示全部频率，不受下方范围控制）
    QVector<double> frequencies;
    QVector<double> magnitudes;
    double freqStep = SAMPLE_RATE / FFT_SIZE;

    for (int i = 0; i < FFT_SIZE/2; ++i) {
        double freq = i * freqStep;
        frequencies.append(freq);
        magnitudes.append(std::abs(fftResult[i]));
    }

    // 5. 设置柱状图数据并刷新
    m_spectrumBars->setData(frequencies, magnitudes);
    ui->myCustomPlot->replot();
}



void NewWindow::on_pushButton_clicked()
{
    // 嵌入模式：隐藏自身，不做窗口切换
    this->hide();
}



void NewWindow::setBandpassParams(double sampleRate, double centerFreq, double bandwidth)
{
    m_sampleRate = sampleRate;       // 使用传入的采样率
    m_centerFreq = centerFreq;       // 使用传入的中心频率
    m_bandwidth = bandwidth;         // 使用传入的带宽
}

QVector<double> NewWindow::butterworthBandpassFilter(const QVector<double> &data)
{
    if (data.isEmpty() || m_sampleRate <= 0)
        return data;

    // 1. 获取并校验滤波上下限
    double lowCut = label5Size;
    double highCut = label7Size;
    const double nyquist = m_sampleRate / 2.0;
    lowCut = qMax(1.0, lowCut);
    highCut = qMin(nyquist - 1.0, highCut);
    if (lowCut > highCut)
        qSwap(lowCut, highCut);

    // 2. 计算核心参数
    m_centerFreq = (lowCut + highCut) / 2.0;
    m_bandwidth = highCut - lowCut;
    double w0 = 2 * M_PI * m_centerFreq / m_sampleRate;  // 归一化中心角频率
    double bw = 2 * M_PI * m_bandwidth / m_sampleRate;   // 归一化带宽
    double Q = w0 / bw;                                  // 品质因数

    // 3. 8阶滤波器需要4个二阶节（基于巴特沃斯8阶极点分布特性）
    // 二阶节1（第一对极点）
    double alpha1 = sin(w0) / (2 * Q * 0.3827);  // 8阶第一级系数（基于√2/2≈0.707的衍生调整）
    double b0_1 = alpha1;
    double b1_1 = 0;
    double b2_1 = -alpha1;
    double a0_1 = 1 + alpha1;
    double a1_1 = -2 * cos(w0);
    double a2_1 = 1 - alpha1;
    // 归一化
    b0_1 /= a0_1; b1_1 /= a0_1; b2_1 /= a0_1;
    a1_1 /= a0_1; a2_1 /= a0_1;

    // 二阶节2（第二对极点）
    double alpha2 = sin(w0) / (2 * Q * 0.9239);  // 8阶第二级系数
    double b0_2 = alpha2;
    double b1_2 = 0;
    double b2_2 = -alpha2;
    double a0_2 = 1 + alpha2;
    double a1_2 = -2 * cos(w0);
    double a2_2 = 1 - alpha2;
    // 归一化
    b0_2 /= a0_2; b1_2 /= a0_2; b2_2 /= a0_2;
    a1_2 /= a0_2; a2_2 /= a0_2;

    // 二阶节3（第三对极点，与第二对对称）
    double alpha3 = sin(w0) / (2 * Q * 0.9239);
    double b0_3 = alpha3;
    double b1_3 = 0;
    double b2_3 = -alpha3;
    double a0_3 = 1 + alpha3;
    double a1_3 = -2 * cos(w0);
    double a2_3 = 1 - alpha3;
    // 归一化
    b0_3 /= a0_3; b1_3 /= a0_3; b2_3 /= a0_3;
    a1_3 /= a0_3; a2_3 /= a0_3;

    // 二阶节4（第四对极点，与第一对对称）
    double alpha4 = sin(w0) / (2 * Q * 0.3827);
    double b0_4 = alpha4;
    double b1_4 = 0;
    double b2_4 = -alpha4;
    double a0_4 = 1 + alpha4;
    double a1_4 = -2 * cos(w0);
    double a2_4 = 1 - alpha4;
    // 归一化
    b0_4 /= a0_4; b1_4 /= a0_4; b2_4 /= a0_4;
    a1_4 /= a0_4; a2_4 /= a0_4;

    // 4. 级联滤波（4个二阶节依次处理）
    QVector<double> temp1 = applySecondOrderSection(data, b0_1, b1_1, b2_1, a1_1, a2_1);
    QVector<double> temp2 = applySecondOrderSection(temp1, b0_2, b1_2, b2_2, a1_2, a2_2);
    QVector<double> temp3 = applySecondOrderSection(temp2, b0_3, b1_3, b2_3, a1_3, a2_3);
    QVector<double> filteredData = applySecondOrderSection(temp3, b0_4, b1_4, b2_4, a1_4, a2_4);

    return filteredData;
}

// 辅助函数保持不变（单个二阶节处理）
QVector<double> NewWindow::applySecondOrderSection(const QVector<double>& input,
                                                  double b0, double b1, double b2,
                                                  double a1, double a2)
{
    QVector<double> output(input.size());
    if (input.isEmpty()) return output;

    // 边界处理
    if (input.size() >= 1) output[0] = b0 * input[0];
    if (input.size() >= 2) output[1] = b0 * input[1] + b1 * input[0] - a1 * output[0];

    // 递归滤波
    for (int i = 2; i < input.size(); ++i) {
        output[i] = b0 * input[i] + b1 * input[i-1] + b2 * input[i-2]
                  - a1 * output[i-1] - a2 * output[i-2];
    }

    return output;
}


void NewWindow::updateWaveform(const QVector<double> &waveData)
{


}



void NewWindow::onAddClicked()
{
    if (radioButton->isChecked()) {
        label5Size += 100;
        label5Size = qMin(label5Size, label7Size);  // 下限不能超过上限
    } else if (radioButton_2->isChecked()) {
        label7Size += 100;
        label7Size = qMin(label7Size, (int)(m_sampleRate / 2 - 1));  // 不超过奈奎斯特频率
    }
    updateLabelSize();
}

void NewWindow::onSubtractClicked()
{
    if (radioButton->isChecked()) {
        label5Size = qMax(1, label5Size - 100);  // 下限不低于1
    } else if (radioButton_2->isChecked()) {
        label7Size -= 100;
        label7Size = qMax(label7Size, label5Size);  // 上限不能低于下限
    }
    updateLabelSize();
}
void NewWindow::updateLabelSize()
{
    if (radioButton->isChecked()) {
        // 只显示label_5的当前数值，不修改字体大小
        label_5->setText(QString("%1").arg(label5Size));
    } else if (radioButton_2->isChecked()) {
        // 只显示label_7的当前数值，不修改字体大小
        label_7->setText(QString("%1").arg(label7Size));
    }
    // 自动计算并更新中心频率显示
    int centerFreq = (label5Size + label7Size) / 2;
    ui->label->setText(QString("中:%1").arg(centerFreq));

    // 中心频率为整百数且变化时，发送串口指令
    if (centerFreq % 100 == 0 && centerFreq != m_lastSentCenterFreq) {
        m_lastSentCenterFreq = centerFreq;
        emit centerFrequencyChanged(centerFreq);
    }
}

void NewWindow::setLeftChannelLevel(qreal level)
{


    // 将强度归一化到0-100范围（与之前逻辑一致）
    int displayLevel = qBound(0, static_cast<int>(qRound(level)), 100);


}


void NewWindow::on_pushButton_4_clicked()
{
      m_recording = true;
      m_recordBuffer.clear();
}

void NewWindow::on_pushButton_5_clicked()
{
    // 1️⃣ 清空柱子数据
      m_avgHistory.clear();
      m_avgBars->setData(QVector<double>(), QVector<double>());

      // 2️⃣ 清空柱子顶部的文字（QCPItemText）
      ui->widget->clearItems();

      // 3️⃣ 重置坐标轴显示范围
      ui->widget->xAxis->setRange(0, 11);   // 仍然保持 10 根柱子的范围
      ui->widget->yAxis->setRange(0, 100);  // 强度范围 0~100

      // 4️⃣ 刷新显示
      ui->widget->replot();
}

// a值调整（范围0.2~0.5，步长0.1）
void NewWindow::onADecrease()
{
    m_currentA -= 0.1;
    if (m_currentA < 0.2) m_currentA = 0.2;
    m_aValueLabel->setText(QString("a:%1").arg(m_currentA, 0, 'f', 1));
    // 如果有足够数据，重新计算深度
    if (m_avgHistory.size() >= 7) calculateDepthWhenReady();
}

void NewWindow::onAIncrease()
{
    m_currentA += 0.1;
    if (m_currentA > 0.5) m_currentA = 0.5;
    m_aValueLabel->setText(QString("a:%1").arg(m_currentA, 0, 'f', 1));
    if (m_avgHistory.size() >= 7) calculateDepthWhenReady();
}

// ========== 深度计算算法（与BarChartMainWindow一致） ==========

std::vector<double> NewWindow::computeResiduals(const double* params, const std::vector<double>& V, double a_val)
{
    double k0 = params[0];
    double k2 = params[1];
    double h = params[2];
    double x0 = params[3];
    std::vector<double> residuals;

    for (int i = 0; i < 7; ++i) {
        double x = a_val * i;
        double distance = sqrt(h * h + (x + x0) * (x + x0));
        double theory = (k0 * k2 * exp(-distance)) / distance;
        residuals.push_back(V[i] - theory);
    }

    double dataVariance = 0.0, meanV = 0.0;
    for (double v : V) meanV += v;
    meanV /= V.size();
    for (double v : V) dataVariance += (v - meanV) * (v - meanV);
    dataVariance /= V.size();
    double peakWeight = std::max(1e-2, dataVariance * 1e3);

    std::vector<double> theory_values, x_offsets;
    for (int i = 0; i < 7; ++i) {
        double x = a_val * i;
        x_offsets.push_back(x);
        double distance = sqrt(h * h + (x + x0) * (x + x0));
        theory_values.push_back((k0 * k2 * exp(-distance)) / distance);
    }

    int peak_idx = 0;
    double max_val = theory_values[0];
    for (int i = 1; i < 7; ++i) {
        if (theory_values[i] > max_val) { max_val = theory_values[i]; peak_idx = i; }
    }
    double peak_offset = x_offsets[peak_idx];
    double target_offset = 3 * a_val;
    residuals.push_back(peakWeight * (peak_offset - target_offset));

    return residuals;
}

double NewWindow::computeObjective(const double* params, const std::vector<double>& V, double a_val)
{
    std::vector<double> residuals = computeResiduals(params, V, a_val);
    double sum = 0.0;
    for (double r : residuals) sum += r * r;
    return 0.5 * sum;
}

std::vector<std::vector<double>> NewWindow::computeJacobian(const double* params, const std::vector<double>& V, double a_val)
{
    const double epsilon = 1e-8;
    int num_params = 4;
    std::vector<double> r0 = computeResiduals(params, V, a_val);
    int num_residuals = r0.size();

    std::vector<std::vector<double>> J(num_residuals, std::vector<double>(num_params, 0.0));
    double temp_params[4];

    for (int j = 0; j < num_params; ++j) {
        for (int i = 0; i < num_params; ++i) temp_params[i] = params[i];
        temp_params[j] += epsilon;
        std::vector<double> r = computeResiduals(temp_params, V, a_val);
        for (int i = 0; i < num_residuals; ++i) {
            J[i][j] = (r[i] - r0[i]) / epsilon;
        }
    }

    return J;
}

bool NewWindow::gaussianElimination(std::vector<std::vector<double>> A, std::vector<double> b, std::vector<double>& x)
{
    int n = A.size();
    if (n != (int)b.size()) return false;

    for (int i = 0; i < n; ++i) A[i].push_back(b[i]);

    for (int i = 0; i < n; ++i) {
        int max_row = i;
        for (int j = i; j < n; ++j) {
            if (fabs(A[j][i]) > fabs(A[max_row][i])) max_row = j;
        }
        if (fabs(A[max_row][i]) < 1e-12) return false;
        std::swap(A[i], A[max_row]);

        double div = A[i][i];
        for (int j = i; j <= n; ++j) A[i][j] /= div;
        for (int j = 0; j < n; ++j) {
            if (j != i) {
                double factor = A[j][i];
                for (int k = i; k <= n; ++k) A[j][k] -= factor * A[i][k];
            }
        }
    }

    x.resize(n);
    for (int i = 0; i < n; ++i) x[i] = A[i][n];
    return true;
}

bool NewWindow::solveNonLinearLeastSquares(const std::vector<double>& V, double a_val,
                                            double& k0, double& k2, double& h, double& x0)
{
    double params[4] = {k0, k2, h, x0};

    const double k0_low = 1e-3, k0_high = 1e5;
    const double k2_low = 1.0001, k2_high = 1e3;
    const double h_low = 1e-2, h_high = 1e2;
    const double x0_low = -1e2, x0_high = 1e2;

    const int max_iter = 2000;
    const double tol = 1e-12;
    double lambda = 1e-4;
    const double lambda_factor = 15.0;

    double f = computeObjective(params, V, a_val);

    for (int iter = 0; iter < max_iter; ++iter) {
        std::vector<double> residuals = computeResiduals(params, V, a_val);
        std::vector<std::vector<double>> J = computeJacobian(params, V, a_val);
        int num_residuals = residuals.size();

        std::vector<std::vector<double>> H(4, std::vector<double>(4, 0.0));
        std::vector<double> g(4, 0.0);

        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                for (int k = 0; k < num_residuals; ++k) {
                    H[i][j] += J[k][i] * J[k][j];
                }
            }
            for (int k = 0; k < num_residuals; ++k) {
                g[i] += J[k][i] * residuals[k];
            }
        }

        for (int i = 0; i < 4; ++i) H[i][i] += lambda;

        std::vector<double> delta(4, 0.0);
        bool solve_success = gaussianElimination(H, g, delta);
        if (!solve_success) { lambda *= lambda_factor; continue; }

        double new_params[4];
        for (int i = 0; i < 4; ++i) new_params[i] = params[i] - delta[i];

        new_params[0] = std::max(k0_low, std::min(k0_high, new_params[0]));
        new_params[1] = std::max(k2_low, std::min(k2_high, new_params[1]));
        new_params[2] = std::max(h_low, std::min(h_high, new_params[2]));
        new_params[3] = std::max(x0_low, std::min(x0_high, new_params[3]));

        double f_new = computeObjective(new_params, V, a_val);
        if (f_new < f) {
            for (int i = 0; i < 4; ++i) params[i] = new_params[i];
            f = f_new;
            lambda /= lambda_factor;
            double delta_norm = 0.0;
            for (double d : delta) delta_norm += d * d;
            if (sqrt(delta_norm) < tol || f < tol) break;
        } else {
            lambda *= lambda_factor;
        }
    }

    k0 = params[0]; k2 = params[1]; h = params[2]; x0 = params[3];
    return true;
}

void NewWindow::calculateDepthWhenReady()
{
    if (m_avgHistory.size() < 7) return;

    std::vector<double> V(m_avgHistory.begin(), m_avgHistory.end());
    V.resize(7); // 取前7个

    double k0 = 1000.0, k2 = 50.0, h = 1.5, x0 = -1.0;

    bool isAllZero = true;
    for (double v : V) { if (v != 0) { isAllZero = false; break; } }
    if (!isAllZero) {
        double v_max = *std::max_element(V.begin(), V.end());
        k0 = v_max * 10;
    }

    solveNonLinearLeastSquares(V, m_currentA, k0, k2, h, x0);

    m_lastDepth = h;  // 存储深度值供保存使用

    if (m_depthLabel) {
        m_depthLabel->setText(QString("深度: %1 米").arg(h, 0, 'f', 3));
        m_depthLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #000000; background: transparent; border: none;");
    }
}

// 保存记录柱状图数据到文件
void NewWindow::onSaveChartClicked()
{
    if (m_avgHistory.isEmpty()) {
        QMessageBox::information(this, "提示", "暂无记录数据可保存！");
        return;
    }

    // 生成时间戳文件名
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");

    // 确保 ChartFiles 目录存在
    QString saveDir = QCoreApplication::applicationDirPath() + "/ChartFiles";
    QDir dir(saveDir);
    if (!dir.exists()) {
        dir.mkpath(saveDir);
    }

    // 按已有文件数量编号
    QStringList filters;
    filters << "*.txt";
    int existingCount = dir.entryList(filters, QDir::Files).size();
    int fileIndex = existingCount + 1;

    // 文件名包含深度信息（如有）
    QString depthPart = (m_lastDepth > 0.0) ? QString("_h%1m").arg(m_lastDepth, 0, 'f', 3) : "";
    QString defaultFileName = QString("%1-RecordData%2_%3.txt").arg(fileIndex).arg(depthPart).arg(timestamp);
    QString defaultPath = saveDir + "/" + defaultFileName;

    QString fileName = QFileDialog::getSaveFileName(this, "保存记录柱状图",
        defaultPath,
        "文本文件 (*.txt)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法创建文件！");
        return;
    }

    QTextStream out(&file);
    if (m_lastDepth > 0.0) out << "depth=" << QString::number(m_lastDepth, 'f', 3) << "\n";
    out << "count=" << m_avgHistory.size() << "\n";
    for (int i = 0; i < m_avgHistory.size(); ++i) {
        out << m_avgHistory[i] << "\n";
    }

    file.close();
    QMessageBox::information(this, "完成",
        QString("已保存 %1 根柱子到：\n%2").arg(m_avgHistory.size()).arg(fileName));
}

// 从文件打开并显示记录柱状图
void NewWindow::onOpenChartClicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "打开记录柱状图文件",
        QCoreApplication::applicationDirPath(),
        "文本文件 (*.txt);;所有文件 (*)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法打开文件！");
        return;
    }

    // 清空当前图表
    m_avgHistory.clear();
    m_avgBars->setData(QVector<double>(), QVector<double>());
    ui->widget->clearItems();

    // 读取文件
    QTextStream in(&file);
    QVector<double> loadedValues;
    double loadedDepth = 0.0;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith("depth=")) {
            loadedDepth = line.mid(6).toDouble();
        } else if (line.startsWith("count=")) {
            continue; // count由数据行数决定
        } else if (!line.isEmpty()) {
            bool ok;
            double val = line.toDouble(&ok);
            if (ok) loadedValues.append(val);
        }
    }
    file.close();

    if (loadedValues.isEmpty()) {
        QMessageBox::warning(this, "错误", "文件中没有有效数据！");
        return;
    }

    // 恢复深度信息
    m_lastDepth = loadedDepth;
    if (m_depthLabel) {
        if (loadedDepth > 0.0) {
            m_depthLabel->setText(QString("深度: %1 米").arg(loadedDepth, 0, 'f', 3));
        } else {
            m_depthLabel->setText("深度: --");
        }
        m_depthLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #000000; background: transparent; border: none;");
    }

    // 重建柱状图
    QVector<double> keys, vals;
    int n = loadedValues.size();
    keys.reserve(n);
    vals.reserve(n);
    for (int i = 0; i < n; ++i) {
        keys << (i + 1);
        vals << loadedValues[i];
        m_avgHistory.append(loadedValues[i]);
    }

    m_avgBars->setData(keys, vals);
    ui->widget->xAxis->setRange(0, qMax(m_maxBars + 1, n + 1));
    double ymax = 0.0;
    for (double v : loadedValues) ymax = qMax(ymax, v);
    ui->widget->yAxis->setRange(0, ymax * 1.15 + 8.0);

    // 数值标签
    for (int i = 0; i < n; ++i) {
        QCPItemText *text = new QCPItemText(ui->widget);
        text->setLayer("overlay");
        text->position->setType(QCPItemPosition::ptPlotCoords);
        text->position->setCoords(i + 1, loadedValues[i] + 1.0);
        text->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        text->setText(QString::number(loadedValues[i], 'f', 1));
    }

    ui->widget->replot();
    QMessageBox::information(this, "完成",
        QString("已从文件加载 %1 根柱子").arg(n));
}
