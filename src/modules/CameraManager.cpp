#include "CameraManager.h"

/**
 * @brief 构造函数
 * @param frameWidth 帧宽度
 * @param frameHeight 帧高度
 */
CameraManager::CameraManager(int frameWidth, int frameHeight)
    : m_frameWidth(frameWidth),
      m_frameHeight(frameHeight),
      m_fb(nullptr) {
}

/**
 * @brief 析构函数
 */
CameraManager::~CameraManager() {
    freeFrame();
}

/**
 * @brief 初始化摄像头
 * @return 初始化是否成功
 */
bool CameraManager::init() {
    if (!CoreS3.Camera.begin()) {
        return false;
    }
    
    CoreS3.Camera.sensor->set_framesize(CoreS3.Camera.sensor, FRAMESIZE_QVGA);
    return true;
}

/**
 * @brief 获取摄像头帧
 * @return 摄像头帧指针，使用完毕后需要调用 freeFrame() 释放
 */
camera_fb_t* CameraManager::getFrame() {
    if (!CoreS3.Camera.get()) {
        return nullptr;
    }
    
    m_fb = CoreS3.Camera.fb;
    return m_fb;
}

/**
 * @brief 释放摄像头帧
 */
void CameraManager::freeFrame() {
    if (m_fb) {
        CoreS3.Camera.free();
        m_fb = nullptr;
    }
}

/**
 * @brief 获取帧宽度
 * @return 帧宽度
 */
int CameraManager::getFrameWidth() const {
    return m_frameWidth;
}

/**
 * @brief 获取帧高度
 * @return 帧高度
 */
int CameraManager::getFrameHeight() const {
    return m_frameHeight;
}
