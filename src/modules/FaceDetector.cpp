#include "FaceDetector.h"
#include <algorithm>

/**
 * @brief 构造函数
 * @param maxSkipFrames 最大跳过帧数
 */
FaceDetector::FaceDetector(int maxSkipFrames)
    : m_maxSkipFrames(maxSkipFrames),
      m_frameCounter(0),
      m_lastFaceX(-1),
      m_lastFaceInfo({-1, -1, 0, 0, 0.0f}),
      m_detectorReady(false),
      m_stage1(nullptr),
      m_stage2(nullptr) {
}

/**
 * @brief 析构函数
 */
FaceDetector::~FaceDetector() {
}

/**
 * @brief 初始化人脸检测器
 * @return 初始化是否成功
 */
bool FaceDetector::init() {
    m_stage1.reset(new HumanFaceDetectMSR01(0.1F, 0.5F, 10, 0.2F));
    m_stage2.reset(new HumanFaceDetectMNP01(0.5F, 0.3F, 5));
    m_detectorReady = (m_stage1 != nullptr && m_stage2 != nullptr);
    return m_detectorReady;
}

/**
 * @brief 检测人脸
 * @param fb 摄像头帧
 * @return 人脸信息，centerX < 0 表示未检测到人脸
 */
FaceInfo FaceDetector::detectFace(camera_fb_t* fb) {
    FaceInfo faceInfo = { -1, -1, 0, 0, 0.0f };
    
    if (!m_detectorReady || !m_stage1 || !m_stage2 || !fb) {
        return faceInfo;
    }

    if (fb->format != PIXFORMAT_RGB565) {
        return faceInfo;
    }

    // 动态调整帧跳过策略
    // 当检测到人脸时，减少帧跳过的数量，提高跟踪精度
    // 当未检测到人脸时，增加帧跳过的数量，提高系统性能
    int skipFrames = m_maxSkipFrames;
    if (m_lastFaceX >= 0) {
        // 检测到人脸，减少帧跳过
        skipFrames = std::max(0, skipFrames - 1);
    } else {
        // 未检测到人脸，增加帧跳过
        skipFrames = std::min(m_maxSkipFrames * 2, skipFrames + 1);
    }

    // 只在需要检测的帧进行处理
    if (m_frameCounter % (skipFrames + 1) != 0) {
        m_frameCounter++;
        // 如果上一帧检测到人脸，返回上一帧的位置
        if (m_lastFaceX >= 0) {
            faceInfo = m_lastFaceInfo;
        }
        return faceInfo;
    }
    m_frameCounter++;

    auto &candidates = m_stage1->infer(
        reinterpret_cast<uint16_t *>(fb->buf),
        {static_cast<int>(fb->height), static_cast<int>(fb->width), 3});
    auto &results = m_stage2->infer(
        reinterpret_cast<uint16_t *>(fb->buf),
        {static_cast<int>(fb->height), static_cast<int>(fb->width), 3},
        candidates);

    if (results.empty()) {
        m_lastFaceX = -1;
        m_lastFaceInfo = faceInfo;
        return faceInfo;
    }

    // 直接使用迭代器遍历，避免不必要的 vector 转换
    size_t bestFaceIndex = 0;
    int maxArea = 0;
    size_t currentIndex = 0;
    
    for (const auto &face : results) {
        if (face.box.size() >= 4) {
            int width = face.box[2] - face.box[0];
            int height = face.box[3] - face.box[1];
            int area = width * height;
            if (area > maxArea) {
                maxArea = area;
                bestFaceIndex = currentIndex;
            }
        }
        currentIndex++;
    }

    // 返回最大人脸的信息
    currentIndex = 0;
    for (const auto &face : results) {
        if (currentIndex == bestFaceIndex && face.box.size() >= 4) {
            faceInfo.centerX = (face.box[0] + face.box[2]) / 2;
            faceInfo.centerY = (face.box[1] + face.box[3]) / 2;
            faceInfo.width = face.box[2] - face.box[0];
            faceInfo.height = face.box[3] - face.box[1];
            faceInfo.confidence = face.score;
            m_lastFaceX = faceInfo.centerX;
            m_lastFaceInfo = faceInfo;
            break;
        }
        currentIndex++;
    }

    return faceInfo;
}

/**
 * @brief 获取帧计数器
 * @return 帧计数器
 */
int FaceDetector::getFrameCounter() const {
    return m_frameCounter;
}
