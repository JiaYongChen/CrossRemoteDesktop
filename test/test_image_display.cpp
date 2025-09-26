/**
 * @file test_image_display.cpp
 * @brief 测试RenderManager图片显示功能的改进
 */

#include <QtTest/QtTest>
#include <QApplication>
#include <QPixmap>
#include <QPainter>
#include <QTimer>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include "../src/client/managers/RenderManager.h"

class TestImageDisplay : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testImageQualitySettings();
    void testAnimationModeSettings();
    void testImageCacheSettings();
    void testRemoteScreenUpdate();
    void testRegionUpdate();
    void testViewModeSettings();

private:
    QApplication *app;
    RenderManager *renderManager;
    QGraphicsView *view;
    
    /**
     * @brief 创建测试用的像素图
     */
    QPixmap createTestPixmap(int width = 800, int height = 600);
};

void TestImageDisplay::initTestCase()
{
    // 创建应用程序实例（如果不存在）
    if (!QApplication::instance()) {
        int argc = 1;
        char *argv[] = {(char*)"test"};
        app = new QApplication(argc, argv);
    } else {
        app = nullptr;
    }
    
    // 创建QGraphicsView和RenderManager实例
    view = new QGraphicsView();
    renderManager = new RenderManager(view);
    view->show();
    
    // 等待窗口初始化完成
    QTest::qWait(100);
}

void TestImageDisplay::cleanupTestCase()
{
    if (renderManager) {
        delete renderManager;
        renderManager = nullptr;
    }
    
    if (view) {
        delete view;
        view = nullptr;
    }
    
    if (app) {
        delete app;
        app = nullptr;
    }
}

QPixmap TestImageDisplay::createTestPixmap(int width, int height)
{
    QPixmap pixmap(width, height);
    pixmap.fill(Qt::white);
    
    QPainter painter(&pixmap);
    painter.setPen(Qt::black);
    painter.setBrush(Qt::blue);
    
    // 绘制一些测试图形
    painter.drawRect(50, 50, 200, 150);
    painter.setBrush(Qt::red);
    painter.drawEllipse(300, 100, 100, 100);
    
    painter.setPen(QPen(Qt::green, 3));
    painter.drawLine(0, 0, width, height);
    painter.drawLine(width, 0, 0, height);
    
    painter.end();
    return pixmap;
}

void TestImageDisplay::testImageQualitySettings()
{
    // 测试图片质量设置
    renderManager->setImageQuality(RenderManager::FastRendering);
    QCOMPARE(renderManager->imageQuality(), RenderManager::FastRendering);
    
    renderManager->setImageQuality(RenderManager::SmoothRendering);
    QCOMPARE(renderManager->imageQuality(), RenderManager::SmoothRendering);
    
    renderManager->setImageQuality(RenderManager::HighQualityRendering);
    QCOMPARE(renderManager->imageQuality(), RenderManager::HighQualityRendering);
    
    qDebug() << "图片质量设置测试通过";
}

void TestImageDisplay::testAnimationModeSettings()
{
    // 测试动画模式设置
    renderManager->setAnimationMode(RenderManager::NoAnimation);
    QCOMPARE(renderManager->animationMode(), RenderManager::NoAnimation);
    
    renderManager->setAnimationMode(RenderManager::SmoothAnimation);
    QCOMPARE(renderManager->animationMode(), RenderManager::SmoothAnimation);
    
    renderManager->setAnimationMode(RenderManager::FastAnimation);
    QCOMPARE(renderManager->animationMode(), RenderManager::FastAnimation);
    
    qDebug() << "动画模式设置测试通过";
}

void TestImageDisplay::testImageCacheSettings()
{
    // 测试图片缓存设置
    renderManager->enableImageCache(true);
    renderManager->setCacheSizeLimit(50); // 50MB
    
    // 清除缓存
    renderManager->clearImageCache();
    
    // 禁用缓存
    renderManager->enableImageCache(false);
    
    qDebug() << "图片缓存设置测试通过";
}

void TestImageDisplay::testRemoteScreenUpdate()
{
    // 创建测试图片
    QPixmap testPixmap = createTestPixmap(1024, 768);
    
    // 设置远程屏幕
    renderManager->setRemoteScreen(testPixmap);
    
    // 等待更新完成
    QTest::qWait(50);
    
    qDebug() << "远程屏幕更新测试通过";
}

void TestImageDisplay::testRegionUpdate()
{
    // 创建初始图片
    QPixmap initialPixmap = createTestPixmap(800, 600);
    renderManager->setRemoteScreen(initialPixmap);
    QTest::qWait(50);
    
    // 创建区域更新图片
    QPixmap regionPixmap(200, 200);
    regionPixmap.fill(Qt::yellow);
    QPainter painter(&regionPixmap);
    painter.setPen(Qt::black);
    painter.drawText(50, 100, "Region Update");
    painter.end();
    
    // 更新指定区域
    QRect updateRegion(100, 100, 200, 200);
    renderManager->updateRemoteRegion(regionPixmap, updateRegion);
    
    // 等待更新完成
    QTest::qWait(50);
    
    qDebug() << "区域更新测试通过";
}

void TestImageDisplay::testViewModeSettings()
{
    // 测试视图模式设置
    renderManager->setViewMode(RenderManager::FitToWindow);
    QCOMPARE(renderManager->viewMode(), RenderManager::FitToWindow);
    
    renderManager->setViewMode(RenderManager::ActualSize);
    QCOMPARE(renderManager->viewMode(), RenderManager::ActualSize);
    
    renderManager->setViewMode(RenderManager::CustomScale);
    QCOMPARE(renderManager->viewMode(), RenderManager::CustomScale);
    
    renderManager->setViewMode(RenderManager::FillWindow);
    QCOMPARE(renderManager->viewMode(), RenderManager::FillWindow);
    
    qDebug() << "视图模式设置测试通过";
}

QTEST_MAIN(TestImageDisplay)
#include "TestImageDisplay.moc"