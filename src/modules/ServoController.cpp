#include "ServoController.h"

/**
 * @brief 构造函数
 * @param leftPin 左侧伺服电机引脚
 * @param rightPin 右侧伺服电机引脚
 * @param freq 伺服电机频率
 * @param minUs 最小脉冲宽度
 * @param maxUs 最大脉冲宽度
 * @param stopUs 停止脉冲宽度
 * @param nudgeUs 轻拨脉冲宽度
 * @param nudgeMs 轻拨持续时间
 * @param settleMs 稳定时间
 * @param kp PID 比例系数
 * @param ki PID 积分系数
 * @param kd PID 微分系数
 */
ServoController::ServoController(
    int leftPin,
    int rightPin,
    int freq,
    int minUs,
    int maxUs,
    int stopUs,
    int nudgeUs,
    int nudgeMs,
    int settleMs,
    float kp,
    float ki,
    float kd
) : m_leftPin(leftPin),
    m_rightPin(rightPin),
    m_freq(freq),
    m_minUs(minUs),
    m_maxUs(maxUs),
    m_stopUs(stopUs),
    m_nudgeUs(nudgeUs),
    m_nudgeMs(nudgeMs),
    m_settleMs(settleMs),
    m_kp(kp),
    m_ki(ki),
    m_kd(kd),
    m_pidError(0),
    m_pidIntegral(0),
    m_pidLastError(0),
    m_pulseEndMs(0),
    m_nextMoveMs(0),
    m_motionActive(false) {
}

/**
 * @brief 析构函数
 */
ServoController::~ServoController() {
    stopBoth();
    m_servoLeft.detach();
    m_servoRight.detach();
}

/**
 * @brief 初始化伺服电机
 * @return 初始化是否成功
 */
bool ServoController::init() {
    // 分配定时器
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    // 设置伺服电机参数
    m_servoLeft.setPeriodHertz(m_freq);
    m_servoRight.setPeriodHertz(m_freq);
    m_servoLeft.attach(m_leftPin, m_minUs, m_maxUs);
    m_servoRight.attach(m_rightPin, m_minUs, m_maxUs);

    // 停止伺服电机
    stopBoth();
    return true;
}

/**
 * @brief 向伺服电机写入脉冲宽度
 * @param leftUs 左侧伺服电机脉冲宽度
 * @param rightUs 右侧伺服电机脉冲宽度
 */
void ServoController::writeBothMicroseconds(int leftUs, int rightUs) {
    m_servoLeft.writeMicroseconds(leftUs);
    m_servoRight.writeMicroseconds(rightUs);
}

/**
 * @brief 停止伺服电机
 */
void ServoController::stopBoth() {
    writeBothMicroseconds(m_stopUs, m_stopUs);
    m_motionActive = false;
}

/**
 * @brief 轻拨左侧伺服电机
 */
void ServoController::nudgeLeft() {
    const uint32_t now = millis();
    if (m_motionActive || now < m_nextMoveMs) {
        return;
    }

    // 两个舵机同向轻拨，模拟云台向左转一点
    writeBothMicroseconds(m_stopUs - m_nudgeUs, m_stopUs - m_nudgeUs);
    m_motionActive = true;
    m_pulseEndMs = now + static_cast<uint32_t>(m_nudgeMs);
}

/**
 * @brief 轻拨右侧伺服电机
 */
void ServoController::nudgeRight() {
    const uint32_t now = millis();
    if (m_motionActive || now < m_nextMoveMs) {
        return;
    }

    writeBothMicroseconds(m_stopUs + m_nudgeUs, m_stopUs + m_nudgeUs);
    m_motionActive = true;
    m_pulseEndMs = now + static_cast<uint32_t>(m_nudgeMs);
}

/**
 * @brief 计算 PID 控制输出
 * @param error 误差值
 * @return PID 控制输出
 */
int ServoController::calculatePID(int error) {
    m_pidError = error;
    
    // 积分饱和防止
    // 当输出已经达到极限时，停止积分，防止积分饱和
    const float maxOutput = m_nudgeUs * 2;
    const float minOutput = -m_nudgeUs * 2;
    float proportional = m_kp * m_pidError;
    
    // 只有当输出在合理范围内时才进行积分
    float integralTerm = 0;
    if (proportional < maxOutput && proportional > minOutput) {
        m_pidIntegral += m_pidError;
        // 对积分项进行限制，防止积分饱和
        const float maxIntegral = maxOutput / m_ki;
        if (m_pidIntegral > maxIntegral) {
            m_pidIntegral = maxIntegral;
        } else if (m_pidIntegral < -maxIntegral) {
            m_pidIntegral = -maxIntegral;
        }
        integralTerm = m_ki * m_pidIntegral;
    }
    
    // 微分噪声过滤
    // 使用低通滤波器过滤微分项，减少噪声影响
    const float alpha = 0.8f; // 低通滤波器系数
    float derivative = m_pidError - m_pidLastError;
    static float filteredDerivative = 0;
    filteredDerivative = alpha * filteredDerivative + (1 - alpha) * derivative;
    float derivativeTerm = m_kd * filteredDerivative;
    
    // 计算 PID 输出
    float output = proportional + integralTerm + derivativeTerm;
    
    // 更新上一次误差
    m_pidLastError = m_pidError;
    
    // 限制输出范围
    if (output > maxOutput) {
        output = maxOutput;
    } else if (output < minOutput) {
        output = minOutput;
    }
    
    return static_cast<int>(output);
}

/**
 * @brief 根据误差调整伺服电机
 * @param error 误差值
 * @param deadzone 死区范围
 */
void ServoController::adjust(int error, int deadzone) {
    if (abs(error) < deadzone) {
        stopBoth();
        return;
    }

    const uint32_t now = millis();
    if (m_motionActive || now < m_nextMoveMs) {
        return;
    }

    // 计算 PID 输出
    int pidOutput = calculatePID(error);
    
    // 根据 PID 输出调整伺服电机
    int leftUs = m_stopUs - pidOutput;
    int rightUs = m_stopUs - pidOutput;
    
    // 限制伺服电机信号范围
    if (leftUs < m_minUs) leftUs = m_minUs;
    if (leftUs > m_maxUs) leftUs = m_maxUs;
    if (rightUs < m_minUs) rightUs = m_minUs;
    if (rightUs > m_maxUs) rightUs = m_maxUs;
    
    // 应用伺服电机信号
    writeBothMicroseconds(leftUs, rightUs);
    m_motionActive = true;
    m_pulseEndMs = now + static_cast<uint32_t>(m_nudgeMs);
}

void ServoController::update() {
    if (!m_motionActive) {
        return;
    }

    const uint32_t now = millis();
    if (now < m_pulseEndMs) {
        return;
    }

    stopBoth();
    m_nextMoveMs = now + static_cast<uint32_t>(m_settleMs);
}
