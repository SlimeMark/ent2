#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <M5CoreS3.h>
#include <esp_camera.h>
#include "FaceDetector.h"

/**
 * @class DisplayManager
 * @brief 显示管理器类，负责显示相关操作
 */
class DisplayManager {
public:
    /**
     * @brief 构造函数
     */
    DisplayManager();

    /**
     * @brief 析构函数
     */
    ~DisplayManager();

    /**
     * @brief 初始化显示
     * @return 初始化是否成功
     */
    bool init();

    /**
     * @brief 显示状态信息
     * @param state 状态字符串
     * @param faceX 人脸 x 坐标
     * @param errorPx 误差值
     * @param faceWidth 人脸宽度
     * @param frameCounter 帧计数器
     */
    void drawStatus(const char* state, int faceX, int errorPx, int faceWidth, int frameCounter);

    /**
     * @brief 显示摄像头图像和人脸检测框
     * @param fb 摄像头帧
     * @param faceInfo 人脸信息
     */
    void drawCameraAndFaces(camera_fb_t* fb, const FaceInfo& faceInfo);

    /**
     * @brief 显示覆盖信息
     * @param state 状态字符串
     * @param errorPx 误差值
     */
    void drawOverlay(const char* state, int errorPx = 0);

private:
    static constexpr uint32_t OVERLAY_HEIGHT = 60; ///< 覆盖层高度
    static constexpr uint16_t TEXT_COLOR = m5gfx::ili9341_colors::WHITE;
    static constexpr uint16_t BG_COLOR = m5gfx::ili9341_colors::BLACK;
    static constexpr uint16_t FACE_BOX_COLOR = m5gfx::ili9341_colors::GREEN;

    String m_lastOverlayText;                      ///< 上一次显示的顶部文本
    uint32_t m_lastFrameMs;                        ///< 上一次帧绘制时间
    bool m_headerDrawn;                            ///< 顶部区域是否已初始化

    void ensureHeader();
};

#endif // DISPLAY_MANAGER_H
