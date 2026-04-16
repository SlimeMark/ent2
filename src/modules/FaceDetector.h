#ifndef FACE_DETECTOR_H
#define FACE_DETECTOR_H

#include <memory>
#include <vector>
#include <cstdint>
#include <esp_camera.h>
#include "human_face_detect_mnp01.hpp"
#include "human_face_detect_msr01.hpp"

/**
 * @struct FaceInfo
 * @brief 人脸信息结构体
 */
struct FaceInfo {
    int centerX;      ///< 人脸中心 x 坐标
    int centerY;      ///< 人脸中心 y 坐标
    int width;        ///< 人脸宽度
    int height;       ///< 人脸高度
    float confidence; ///< 置信度
};

/**
 * @class FaceDetector
 * @brief 人脸检测器类，负责人脸检测相关操作
 */
class FaceDetector {
public:
    /**
     * @brief 构造函数
     * @param maxSkipFrames 最大跳过帧数
     */
    FaceDetector(int maxSkipFrames = 2);

    /**
     * @brief 析构函数
     */
    ~FaceDetector();

    /**
     * @brief 初始化人脸检测器
     * @return 初始化是否成功
     */
    bool init();

    /**
     * @brief 检测人脸
     * @param fb 摄像头帧
     * @return 人脸信息，centerX < 0 表示未检测到人脸
     */
    FaceInfo detectFace(camera_fb_t* fb);

    /**
     * @brief 获取帧计数器
     * @return 帧计数器
     */
    int getFrameCounter() const;

private:
    int m_maxSkipFrames;                          ///< 最大跳过帧数
    int m_frameCounter;                           ///< 帧计数器
    int m_lastFaceX;                              ///< 上一帧人脸 x 坐标
    FaceInfo m_lastFaceInfo;                      ///< 最近一次完整检测结果
    bool m_detectorReady;                         ///< 检测器是否准备就绪
    std::unique_ptr<HumanFaceDetectMSR01> m_stage1; ///< 第一级检测器
    std::unique_ptr<HumanFaceDetectMNP01> m_stage2; ///< 第二级检测器
};

#endif // FACE_DETECTOR_H
