#ifndef RENDERMANAGER_H
#define RENDERMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtGui/QPixmap>
#include <QtGui/QTransform>
#include <QtWidgets/QGraphicsView>

// 前置声明
class QGraphicsScene;
class QGraphicsPixmapItem;
class QGraphicsView;
class QPainter;
class QTimer;

/**
 * @brief RenderManager类负责管理远程桌面的渲染和视图相关功能
 * 
 * 该类封装了以下功能：
 * - 远程屏幕内容的显示和更新
 * - 视图模式管理（适应窗口、实际大小、自定义缩放等）
 * - 缩放因子计算和应用
 * - 坐标映射（本地坐标与远程坐标之间的转换）
 * - 场景和视图的设置与管理
 * - 渲染性能优化
 */
class RenderManager : public QObject
{
    Q_OBJECT
    
public:
    /**
     * @brief 视图模式枚举
     */
    enum ViewMode {
        FitToWindow,    ///< 适应窗口大小
        ActualSize,     ///< 实际大小
        CustomScale,    ///< 自定义缩放
        FillWindow      ///< 填充窗口
    };
    Q_ENUM(ViewMode)
    
    /**
     * @brief 图片质量枚举
     */
    enum ImageQuality {
        FastRendering,      ///< 快速渲染（最近邻插值）
        SmoothRendering,    ///< 平滑渲染（双线性插值）
        HighQualityRendering ///< 高质量渲染（双三次插值）
    };
    Q_ENUM(ImageQuality)
    
    /**
     * @brief 缩放动画模式枚举
     */
    enum AnimationMode {
        NoAnimation,        ///< 无动画
        SmoothAnimation,    ///< 平滑动画
        FastAnimation       ///< 快速动画
    };
    Q_ENUM(AnimationMode)
    
    /**
     * @brief 构造函数
     * @param graphicsView 关联的QGraphicsView对象
     * @param parent 父对象
     */
    explicit RenderManager(QGraphicsView *graphicsView, QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~RenderManager();
    
    // 场景和视图管理
    /**
     * @brief 初始化图形场景
     */
    void initializeScene();
    
    /**
     * @brief 设置视图属性
     */
    void setupView();
    
    // 远程屏幕管理
    /**
     * @brief 设置远程屏幕内容
     * @param pixmap 远程屏幕的像素图
     */
    void setRemoteScreen(const QPixmap &pixmap);
    
    /**
     * @brief 更新远程屏幕内容
     * @param screen 新的屏幕内容
     */
    void updateRemoteScreen(const QPixmap &screen);
    
    /**
     * @brief 更新远程屏幕的指定区域
     * @param region 区域内容
     * @param rect 更新区域
     */
    void updateRemoteRegion(const QPixmap &region, const QRect &rect);
    
    // 视图模式和缩放
    /**
     * @brief 设置视图模式
     * @param mode 视图模式
     */
    void setViewMode(ViewMode mode);
    
    /**
     * @brief 获取当前视图模式
     * @return 当前视图模式
     */
    ViewMode viewMode() const { return m_viewMode; }
    
    /**
     * @brief 设置图片质量
     * @param quality 图片质量级别
     */
    void setImageQuality(ImageQuality quality);
    
    /**
     * @brief 获取当前图片质量
     * @return 当前图片质量级别
     */
    ImageQuality imageQuality() const { return m_imageQuality; }
    
    /**
     * @brief 设置缩放动画模式
     * @param mode 动画模式
     */
    void setAnimationMode(AnimationMode mode);
    
    /**
     * @brief 获取当前动画模式
     * @return 当前动画模式
     */
    AnimationMode animationMode() const { return m_animationMode; }
    
    /**
     * @brief 启用或禁用图片缓存
     * @param enable 是否启用缓存
     */
    void enableImageCache(bool enable);
    
    /**
     * @brief 清除图片缓存
     */
    void clearImageCache();
    
    /**
     * @brief 设置缓存大小限制（MB）
     * @param sizeMB 缓存大小限制
     */
    void setCacheSizeLimit(int sizeMB);
    
    /**
     * @brief 应用当前视图模式
     */
    void applyViewMode();
    
    /**
     * @brief 设置缩放因子
     * @param factor 缩放因子
     */
    void setScaleFactor(double factor);
    
    /**
     * @brief 获取当前缩放因子
     * @return 当前缩放因子
     */
    double scaleFactor() const { return m_scaleFactor; }
    
    /**
     * @brief 设置自定义缩放因子
     * @param factor 自定义缩放因子
     */
    void setCustomScaleFactor(double factor);
    
    /**
     * @brief 获取自定义缩放因子
     * @return 自定义缩放因子
     */
    double customScaleFactor() const { return m_customScaleFactor; }
    
    // 尺寸和坐标管理
    /**
     * @brief 获取远程屏幕尺寸
     * @return 远程屏幕尺寸
     */
    QSize remoteSize() const { return m_remoteSize; }
    
    /**
     * @brief 获取缩放后的尺寸
     * @return 缩放后的尺寸
     */
    QSize scaledSize() const { return m_scaledSize; }
    
    /**
     * @brief 将本地坐标映射到远程坐标
     * @param localPoint 本地坐标点
     * @return 远程坐标点
     */
    QPoint mapToRemote(const QPoint &localPoint) const;
    
    /**
     * @brief 将远程坐标映射到本地坐标
     * @param remotePoint 远程坐标点
     * @return 本地坐标点
     */
    QPoint mapFromRemote(const QPoint &remotePoint) const;
    
    /**
     * @brief 将本地矩形映射到远程矩形
     * @param localRect 本地矩形
     * @return 远程矩形
     */
    QRect mapToRemote(const QRect &localRect) const;
    
    /**
     * @brief 将远程矩形映射到本地矩形
     * @param remoteRect 远程矩形
     * @return 本地矩形
     */
    QRect mapFromRemote(const QRect &remoteRect) const;
    
    // Screen access
    /**
     * @brief 获取远程屏幕内容
     * @return 远程屏幕的像素图
     */
    QPixmap getRemoteScreen() const;
    
    // 渲染控制
    /**
     * @brief 更新显示
     */
    void updateDisplay();
    
    /**
     * @brief 强制刷新视图
     */
    void forceUpdate();
    
    /**
     * @brief 启用或禁用OpenGL渲染
     * @param enable 是否启用OpenGL
     */
    void enableOpenGL(bool enable = true);
    
    /**
     * @brief 设置视图更新模式
     * @param mode 更新模式
     */
    void setUpdateMode(QGraphicsView::ViewportUpdateMode mode);
    
    // 便捷方法
    /**
     * @brief 适应窗口大小
     */
    void fitToWindow();
    
    /**
     * @brief 显示实际大小
     */
    void actualSize();
    
    /**
     * @brief 放大
     */
    void zoomIn();
    
    /**
     * @brief 缩小
     */
    void zoomOut();
    
    /**
     * @brief 重置缩放
     */
    void resetZoom();
    
    /**
     * @brief 处理窗口大小改变
     * @param newSize 新的窗口大小
     */
    void handleResize(const QSize &newSize);
    
    /**
     * @brief 获取图形场景
     * @return 图形场景指针
     */
    QGraphicsScene* scene() const { return m_scene; }
    
    /**
     * @brief 获取像素图项
     * @return 像素图项指针
     */
    QGraphicsPixmapItem* pixmapItem() const { return m_pixmapItem; }
    
signals:
    /**
     * @brief 视图模式改变信号
     * @param mode 新的视图模式
     */
    void viewModeChanged(ViewMode mode);
    
    /**
     * @brief 缩放因子改变信号
     * @param factor 新的缩放因子
     */
    void scaleFactorChanged(double factor);
    
public slots:
    /**
     * @brief 处理视图大小改变
     */
    void onViewResized();
    
    /**
     * @brief 处理场景改变
     */
    void onSceneChanged();
    
private:
    /**
     * @brief 计算缩放后的尺寸
     */
    void calculateScaledSize();
    
    /**
     * @brief 更新场景矩形
     */
    void updateSceneRect();
    
    /**
     * @brief 更新视图变换
     */
    void updateViewTransform();
    
    /**
     * @brief 确保像素图项存在
     */
    void ensurePixmapItem();
    
    /**
     * @brief 应用图片质量设置
     */
    void applyImageQualitySettings();
    
    /**
     * @brief 延迟更新调度
     */
    void scheduleUpdate();
    
    // 成员变量
    QGraphicsView *m_graphicsView;          ///< 关联的图形视图
    QGraphicsScene *m_scene;                ///< 图形场景
    QGraphicsPixmapItem *m_pixmapItem;      ///< 像素图项

    QPixmap m_remoteScreen;                 ///< 远程屏幕内容
    QSize m_remoteSize;                     ///< 远程屏幕尺寸
    QSize m_scaledSize;                     ///< 缩放后的尺寸

    ViewMode m_viewMode;                    ///< 当前视图模式
    double m_scaleFactor;                   ///< 当前缩放因子
    double m_customScaleFactor;             ///< 自定义缩放因子

    QTimer *m_updateTimer;                  ///< 更新定时器
    bool m_pendingUpdate;                   ///< 是否有待处理的更新
    
    // 新增的成员变量
    ImageQuality m_imageQuality;            ///< 图片质量设置
    AnimationMode m_animationMode;          ///< 动画模式
    bool m_cacheEnabled;                    ///< 是否启用缓存
    int m_cacheSizeLimit;                   ///< 缓存大小限制（MB）
    QHash<QString, QPixmap> m_pixmapCache;  ///< 图片缓存
    int m_currentCacheSize;                 ///< 当前缓存大小（字节）
};

#endif // RENDERMANAGER_H