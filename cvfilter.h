#ifndef MYFILTER_H
#define MYFILTER_H

#include <QVideoFilterRunnable>
#include <QDebug>
#include <QQmlEngine>
#include <QFuture>
#include <QtConcurrent/QtConcurrent>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOpenGLFunctions>
#include <QOpenGLContext>

#include <private/qvideoframe_p.h>

#include "opencv2/opencv.hpp"

using namespace cv;
using namespace dnn;
using namespace std;

class CVFilter : public QAbstractVideoFilter {
    Q_OBJECT
friend class CVFilterRunnable;

public:
    explicit CVFilter(QObject *parent = nullptr);
    virtual ~CVFilter();

    QVideoFilterRunnable *createFilterRunnable();

    void static registerQMLType();

signals:
    void objectDetected(QString rects);

private:
    QFuture<void> processThread;
    bool isProcessing = false;   

    // see the following for more on this parameters
    // https://www.tensorflow.org/tutorials/image_retraining
    const int inWidth = 300;
    const int inHeight = 300;
    const float meanVal = 127.5; // 255 divided by 2
    const float inScaleFactor = 1.0f / meanVal;
    const float confidenceThreshold = 0.5f;

    Net tfNetwork;

    QImage videoFrameToImage(QVideoFrame *frame);
};




class CVFilterRunnable : public QObject, public QVideoFilterRunnable {

public:
    explicit CVFilterRunnable(CVFilter *filter);
    virtual ~CVFilterRunnable();

    QVideoFrame run(QVideoFrame *input, const QVideoSurfaceFormat &surfaceFormat, RunFlags flags);

    void processImage(QImage &image);
    void detect(QImage image);

private:
    CVFilter *filter;
};


#endif // MYFILTER_H
