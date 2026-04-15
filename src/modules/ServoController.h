#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <Arduino.h>
#include <ESP32Servo.h>

/**
 * @class ServoController
 * @brief 伺服电机控制器类，负责伺服电机的控制
 */
class ServoController {
public:
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
     */
    ServoController(
        int leftPin = 8,
        int rightPin = 9,
        int freq = 50,
        int minUs = 500,
        int maxUs = 2500,
        int stopUs = 1500,
        int nudgeUs = 70,
        int nudgeMs = 80,
        int settleMs = 120,
        float kp = 0.5f,
        float ki = 0.1f,
        float kd = 0.2f
    );

    /**
     * @brief 析构函数
     */
    ~ServoController();

    /**
     * @brief 初始化伺服电机
     * @return 初始化是否成功
     */
    bool init();

    /**
     * @brief 停止伺服电机
     */
    void stopBoth();

    /**
     * @brief 轻拨左侧伺服电机
     */
    void nudgeLeft();

    /**
     * @brief 轻拨右侧伺服电机
     */
    void nudgeRight();

    /**
     * @brief 根据误差调整伺服电机
     * @param error 误差值
     * @param deadzone 死区范围
     */
    void adjust(int error, int deadzone);

    /**
     * @brief 更新舵机脉冲状态，避免使用阻塞式 delay
     */
    void update();

private:
    int m_leftPin;     ///< 左侧伺服电机引脚
    int m_rightPin;    ///< 右侧伺服电机引脚
    int m_freq;        ///< 伺服电机频率
    int m_minUs;       ///< 最小脉冲宽度
    int m_maxUs;       ///< 最大脉冲宽度
    int m_stopUs;      ///< 停止脉冲宽度
    int m_nudgeUs;     ///< 轻拨脉冲宽度
    int m_nudgeMs;     ///< 轻拨持续时间
    int m_settleMs;    ///< 稳定时间
    float m_kp;        ///< PID 比例系数
    float m_ki;        ///< PID 积分系数
    float m_kd;        ///< PID 微分系数
    float m_pidError;      ///< PID 误差
    float m_pidIntegral;    ///< PID 积分
    float m_pidLastError;   ///< 上一次 PID 误差
    uint32_t m_pulseEndMs;  ///< 当前脉冲结束时间
    uint32_t m_nextMoveMs;  ///< 下一次允许调整的时间
    bool m_motionActive;    ///< 当前是否处于调整脉冲中
    Servo m_servoLeft;      ///< 左侧伺服电机
    Servo m_servoRight;     ///< 右侧伺服电机

    /**
     * @brief 向伺服电机写入脉冲宽度
     * @param leftUs 左侧伺服电机脉冲宽度
     * @param rightUs 右侧伺服电机脉冲宽度
     */
    void writeBothMicroseconds(int leftUs, int rightUs);

    /**
     * @brief 计算 PID 控制输出
     * @param error 误差值
     * @return PID 控制输出
     */
    int calculatePID(int error);
};

#endif // SERVO_CONTROLLER_H
