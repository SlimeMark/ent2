/**
 * @file main.cpp
 * @brief M5CoreS3 人脸追踪系统主文件
 * 
 * 该文件实现了一个基于 M5CoreS3 的人脸追踪系统，使用摄像头获取图像，
 * 通过人脸检测算法检测人脸位置，并使用伺服电机控制云台跟随人脸移动。
 */

#include <Arduino.h>
#include <M5CoreS3.h>

// 包含模块头文件
#include "modules/CameraManager.h"
#include "modules/FaceDetector.h"
#include "modules/ServoController.h"
#include "modules/DisplayManager.h"

/**
 * @namespace Config
 * @brief 系统配置参数
 */
namespace Config {
    // 摄像头配置
    static constexpr int FRAME_WIDTH = 320;    ///< 帧宽度
    static constexpr int FRAME_HEIGHT = 240;   ///< 帧高度
    
    // 人脸检测配置
    static constexpr int FACE_DEADZONE_PX = 28;        ///< 人脸跟踪死区范围
    static constexpr uint32_t LOOP_INTERVAL_MS = 20;    ///< 视频刷新间隔时间
    static constexpr uint32_t DETECT_INTERVAL_MS = 120; ///< 人脸检测间隔时间
    static constexpr int MAX_SKIP_FRAMES = 2;          ///< 最大跳过帧数
}

// 全局变量
uint32_t g_lastLoopMs = 0;  ///< 上一次循环的时间戳
uint32_t g_lastDetectMs = 0; ///< 上一次检测的时间戳
FaceInfo g_lastFaceInfo = { -1, -1, 0, 0, 0.0f }; ///< 最近一次检测结果

// 模块实例
CameraManager g_camera(Config::FRAME_WIDTH, Config::FRAME_HEIGHT);      ///< 摄像头管理器
FaceDetector g_faceDetector(Config::MAX_SKIP_FRAMES);                  ///< 人脸检测器
ServoController g_servoController;                                    ///< 伺服电机控制器
DisplayManager g_display;                                              ///< 显示管理器

/**
 * @brief 系统初始化函数
 * 
 * 初始化 M5CoreS3、串口、摄像头、人脸检测器和伺服电机控制器。
 */
void setup() {
  auto cfg = M5.config();
  CoreS3.begin(cfg);

  // 初始化串口
  Serial.begin(115200);
  delay(300);
  Serial.println("CoreS3 camera face-centering scaffold start");

  // 初始化显示
  g_display.init();
  g_display.drawStatus("Init", -1, 0, 0, 0);

  // 初始化摄像头
  Serial.println("Init camera...");
  if (!g_camera.init()) {
    g_display.drawStatus("Camera init failed", -1, 0, 0, 0);
    while (true) {
      delay(1000);
    }
  }

  // 初始化人脸检测器
  Serial.println("Init face detector...");
  if (!g_faceDetector.init()) {
    g_display.drawStatus("Face detector init failed", -1, 0, 0, 0);
    while (true) {
      delay(1000);
    }
  }

  // 初始化伺服电机
  Serial.println("Init servo controller...");
  if (!g_servoController.init()) {
    g_display.drawStatus("Servo controller init failed", -1, 0, 0, 0);
    while (true) {
      delay(1000);
    }
  }

  // 初始化完成
  g_display.drawStatus("Waiting", -1, 0, 0, 0);
  Serial.println("Init completed");
}

/**
 * @brief 系统主循环函数
 * 
 * 循环执行以下操作：
 * 1. 检查循环间隔时间
 * 2. 获取摄像头帧
 * 3. 检测人脸
 * 4. 显示摄像头图像和人脸检测框
 * 5. 释放摄像头帧
 * 6. 根据人脸位置调整伺服电机
 */
void loop() {
  M5.update();
  g_servoController.update();

  // 检查循环间隔时间
  const uint32_t now = millis();
  if (now - g_lastLoopMs < Config::LOOP_INTERVAL_MS) {
    delay(5);
    return;
  }
  g_lastLoopMs = now;

  // 获取摄像头帧
  camera_fb_t* fb = g_camera.getFrame();
  if (!fb) {
    g_display.drawStatus("No frame", -1, 0, 0, g_faceDetector.getFrameCounter());
    delay(10);
    return;
  }

  FaceInfo faceInfo = g_lastFaceInfo;
  if (now - g_lastDetectMs >= Config::DETECT_INTERVAL_MS) {
    g_lastDetectMs = now;
    faceInfo = g_faceDetector.detectFace(fb);
    g_lastFaceInfo = faceInfo;
  }

  // 显示摄像头图像和人脸检测框
  g_display.drawCameraAndFaces(fb, faceInfo);

  // 释放摄像头帧
  g_camera.freeFrame();

  // 处理人脸检测结果
  if (faceInfo.centerX < 0) {
    g_servoController.stopBoth();
    g_display.drawOverlay("No face");
    return;
  }

  // 计算误差
  const int frameCenterX = Config::FRAME_WIDTH / 2;
  const int errorPx = faceInfo.centerX - frameCenterX;

  // 调整伺服电机
  if (abs(errorPx) < Config::FACE_DEADZONE_PX) {
    g_servoController.stopBoth();
    g_display.drawOverlay("Centered", errorPx);
  } else {
    g_servoController.adjust(errorPx, Config::FACE_DEADZONE_PX);
    g_display.drawOverlay("Adjusting", errorPx);
  }
}
