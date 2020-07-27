#include <QDebug>

#include "cvfilter.h"

CVFilter::CVFilter(QObject *parent) : QAbstractVideoFilter(parent)
{
    //initialize network
    initNetwork();
}

CVFilter::~CVFilter()
{
    if(!processThread.isFinished()) {
        processThread.cancel();
        processThread.waitForFinished();
    }
}

QVideoFilterRunnable *CVFilter::createFilterRunnable()
{    
    return new CVFilterRunnable(this);
}

void CVFilter::registerQMLType()
{    
    qmlRegisterType<CVFilter>("CVFilter", 1, 0, "CVFilter");
}

QImage CVFilter::videoFrameToImage(QVideoFrame *frame)
{
    if(frame->handleType() == QAbstractVideoBuffer::NoHandle){

        QImage image = qt_imageFromVideoFrame(*frame);

        if(image.isNull()){
            qDebug() << "-- null image from qt_imageFromVideoFrame";
            return QImage();
        }

        if(image.format() != QImage::Format_RGB32){
            image = image.convertToFormat(QImage::Format_RGB32);
        }

        return image;
    }

    if(frame->handleType() == QAbstractVideoBuffer::GLTextureHandle){
        QImage image(frame->width(), frame->height(), QImage::Format_RGB32);
        GLuint textureId = frame->handle().toUInt();//static_cast<GLuint>(frame.handle().toInt());
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        QOpenGLFunctions *f = ctx->functions();
        GLuint fbo;
        f->glGenFramebuffers(1,&fbo);
        GLint prevFbo;
        f->glGetIntegerv(GL_FRAMEBUFFER_BINDING,&prevFbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);
        f->glReadPixels(0, 0, frame->width(), frame->height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        f->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        return image.rgbSwapped();
    }

    qDebug() << "-- Invalid image format...";
    return QImage();
}

void CVFilter::initNetwork()
{
    QFile modelFile(":/assets/classifiers/haarcascade_frontalface_default.xml");
    QFile configFile(":/assets/classifiers/haarcascade_frontalface_default.xml");

    QTemporaryFile modelTempFile;
    QTemporaryFile configTempFile;

    if(modelFile.open(QFile::ReadOnly | QFile::Text)){

        if(modelTempFile.open())
        {
            modelTempFile.write(modelFile.readAll());
            modelFile.close();
        }
        else
        {
            qDebug() << "Can't open model temp file.";
            return;
        }
    }
    else
    {
        qDebug() << "Can't open model file.";
        return;
    }

    if(configFile.open(QFile::ReadOnly | QFile::Text)){

        if(configTempFile.open())
        {
            configTempFile.write(modelFile.readAll());
            configFile.close();
        }
        else
        {
            qDebug() << "Can't open model temp file.";
            return;
        }
    }
    else
    {
        qDebug() << "Can't open model file.";
        return;
    }

    tfNetwork = readNetFromTensorflow(modelTempFile.fileName().toStdString(),configFile.fileName().toStdString());

}

CVFilterRunnable::CVFilterRunnable(CVFilter *filter) : QObject(nullptr), filter(filter)
{

}

CVFilterRunnable::~CVFilterRunnable()
{
    filter = nullptr;
}

QVideoFrame CVFilterRunnable::run(QVideoFrame *input, const QVideoSurfaceFormat &surfaceFormat, QVideoFilterRunnable::RunFlags flags)
{
    Q_UNUSED(surfaceFormat);
    Q_UNUSED(flags);

    if(!input || !input->isValid()){
        return QVideoFrame();
    }

    if(filter->isProcessing){
        return * input;
    }

    if(!filter->processThread.isFinished()){
        return * input;
    }

    filter->isProcessing = true;   

    QImage image = filter->videoFrameToImage(input);    

    // All processing has to happen in another thread, as we are now in the UI thread.
    filter->processThread = QtConcurrent::run(this, &CVFilterRunnable::processImage, image);

    return * input;
}

void CVFilterRunnable::processImage(QImage &image)
{    

    //if android, make image upright
#ifdef Q_OS_ANDROID
    QPoint center = image.rect().center();
    QMatrix matrix;
    matrix.translate(center.x(), center.y());
    matrix.rotate(90);
    image = image.transformed(matrix);
#endif

    if(!image.isNull()){        
        detect(image);
    }

//    QString filename = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/" + "my_image.png";

//    if(!QFile::exists(filename)){
//        image.save(filename);
//    }

}

void CVFilterRunnable::detect(QImage image)
{

    image = image.convertToFormat(QImage::Format_RGB888);
    cv::Mat frame(image.height(),
                image.width(),
                CV_8UC3,
                image.bits(),
                image.bytesPerLine());


    Mat inputBlob = blobFromImage(frame,
                                  filter->inScaleFactor,
                                  Size(filter->inWidth, filter->inHeight),
                                  Scalar(filter->meanVal, filter->meanVal, filter->meanVal),
                                  true,
                                  false);
    filter->tfNetwork.setInput(inputBlob);
    Mat result = filter->tfNetwork.forward();
    Mat detections(result.size[2], result.size[3], CV_32F, result.ptr<float>());

    for(int i = 0; i < detections.rows; i++)
    {
        float confidence = detections.at<float>(i, 2);

        if(confidence > filter->confidenceThreshold)
        {
            using namespace cv;

            int objectClass = (int)(detections.at<float>(i, 1));

            int left = static_cast<int>(detections.at<float>(i, 3) * frame.cols);
            int top = static_cast<int>(detections.at<float>(i, 4) * frame.rows);
            int right = static_cast<int>(detections.at<float>(i, 5) * frame.cols);
            int bottom = static_cast<int>(detections.at<float>(i, 6) * frame.rows);

            rectangle(frame, Point(left, top), Point(right, bottom), Scalar(0, 255, 0));
            String label = filter->classNames[objectClass].toStdString();
        }
    }

    filter->isProcessing = false;

}
