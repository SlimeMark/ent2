#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include <M5CoreS3.h>
#include <esp_camera.h>

/**
 * @class CameraManager
 * @brief 摄像头管理类，负责摄像头的初始化和图像获取
 */
class CameraManager {
public:
    /**
     * @brief 构造函数
     * @param frameWidth 帧宽度
     * @param frameHeight 帧高度
     */
    CameraManager(int frameWidth = 320, int frameHeight = 240);

    /**
     * @brief 析构函数
     */
    ~CameraManager();

    /**
     * @brief 初始化摄像头
     * @return 初始化是否成功
     */
    bool init();

    /**
     * @brief 获取摄像头帧
     * @return 摄像头帧指针，使用完毕后需要调用 freeFrame() 释放
     */
    camera_fb_t* getFrame();

    /**
     * @brief 释放摄像头帧
     */
    void freeFrame();

    /**
     * @brief 获取帧宽度
     * @return 帧宽度
     */
    int getFrameWidth() const;

    /**
     * @brief 获取帧高度
     * @return 帧高度
     */
    int getFrameHeight() const;

private:
    int m_frameWidth;    ///< 帧宽度
    int m_frameHeight;   ///< 帧高度
    camera_fb_t* m_fb;   ///< 摄像头帧指针
};

#endif // CAMERA_MANAGER_H
