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
    //copy model files
    if(!copyModelFilesIfNotExists()){
        return;
    }

    //load class names
    if(!loadClassNames()){
        return;
    }

    const String model = (QDir::toNativeSeparators(appDataPath) + QDir::separator() + modelName).toStdString();
    const String config = (QDir::toNativeSeparators(appDataPath) + QDir::separator() + configName).toStdString();

    tfNetwork = readNetFromTensorflow(model,config);
    tfNetwork.setPreferableBackend(DNN_BACKEND_OPENCV);
    tfNetwork.setPreferableTarget(DNN_TARGET_CPU);

}

bool CVFilter::copyModelFilesIfNotExists()
{
    //create directory if it does not exist
    QDir appDataDir(appDataPath);

    if(!appDataDir.exists()){

        if(appDataDir.mkpath(appDataPath)){
            qDebug() << "App data directory created successfully";
        }else {
            qDebug() << "Could not create app data directory";
            return false;
        }
    }

    //copy-paste model files if they don't exist
    QFile modelFile(modelFilename);

    if(!modelFile.exists()){

        if(modelFile.copy(qrcModelFilename,modelFilename)){
            qDebug() << "Model file copied successfully";
            //set permissions
            modelFile.setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser);
        }else {
            qDebug() << "Could not copy model file";
            return false;
        }

    }

    QFile configFile(configFilename);

    if(!configFile.exists()){

        if(configFile.copy(qrcConfigFilename,configFilename)){
            qDebug() << "Config file copied successfully";
            //set permissions
            configFile.setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser);
        }else {
            qDebug() << "Could not copy config file";
            return false;
        }

    }

    return true;
}

bool CVFilter::loadClassNames()
{
    QFile labelsFile(qrcClassesFilename);

    if(labelsFile.open(QFile::ReadOnly | QFile::Text))
    {
        while(!labelsFile.atEnd())
        {
            QString line = labelsFile.readLine();
            classNames[line.split(',')[0].trimmed().toInt()] = line.split(',')[1].trimmed();
        }

        labelsFile.close();

    }else {
        qDebug() << "-- Unable to open class names file....";
        return false;
    }

    return true;
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
    Mat detections(result.size[2], result.size[3], CV_32F, result.ptr<float>(0,0));

    QJsonArray rects;
    QJsonObject rect;

    for(int i = 0; i < detections.rows; i++)
    {
        float confidence = detections.at<float>(i, 2);

        if(confidence > filter->confidenceThreshold)
        {            
            int objectClass = (int)(detections.at<float>(i, 1));

            int left = static_cast<int>(detections.at<float>(i, 3) * frame.cols);
            int top = static_cast<int>(detections.at<float>(i, 4) * frame.rows);
            int right = static_cast<int>(detections.at<float>(i, 5) * frame.cols);
            int bottom = static_cast<int>(detections.at<float>(i, 6) * frame.rows);

            double rX = (double)left/(double)frame.cols;
            double rY = (double)top/(double)frame.rows;
            double rWidth = right - left;
            rWidth = rWidth/(double)frame.cols;
            double rHeight = bottom - top;
            rHeight = rHeight/(double)frame.rows;

            rect.insert("className",filter->classNames[objectClass]);
            rect.insert("rX",rX);
            rect.insert("rY",rY);
            rect.insert("rWidth",rWidth);
            rect.insert("rHeight",rHeight);

            rects.append(rect);

            //qDebug() << objectClass;
        }
    }

    if(rects.count() > 0){
        emit filter->objectsDetected(QString::fromStdString(QJsonDocument(rects).toJson().toStdString()));
    }

    filter->isProcessing = false;

}

