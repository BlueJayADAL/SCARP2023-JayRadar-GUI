#include <iostream>

#include <QCloseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QPointer>

#include "MainWindow.h"
#include "TimerHandler.h"
#include "ui_MainWindow.h"
#include "../camera.h"
#include "../yolo.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
	
	classNames = nullptr;
	videoProcessingThread = nullptr;

    ui->setupUi(this);

    videoWidget = new VideoWidget(ui->videoWidgetContainer);
    videoWidget->setGeometry(ui->videoWidgetContainer->rect());
		
	QSlider *confSlider = ui->confThresholdSlider;
	QLabel *confValueLabel = ui->confValueLabel;
	confValueLabel->setText(QString("%1").arg(static_cast<float>(confSlider->value()) / 100, 0, 'f', 2));
	connect(confSlider, &QSlider::valueChanged, this, &MainWindow::onConfThresholdSliderValueChanged);
	
    QSlider *nmsSlider = ui->nmsThresholdSlider;	
	QLabel *nmsValueLabel = ui->nmsValueLabel;
	nmsValueLabel->setText(QString("%1").arg(static_cast<float>(nmsSlider->value()) / 100, 0, 'f', 2));
    connect(nmsSlider, &QSlider::valueChanged, this, &MainWindow::onNmsThresholdSliderValueChanged);

    startButton = ui->startButton;
    connect(startButton, &QPushButton::clicked, this, &MainWindow::onStartButtonClicked);
    
    stopButton = ui->stopButton;
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::onStopButtonClicked);
    
	yolo = new YOLO();
    yolo->confThreshold = static_cast<float>(confSlider->value()) / 100.0f;
	yolo->nmsThreshold = static_cast<float>(nmsSlider->value()) / 100.0f;
}

MainWindow::~MainWindow() {
    if (videoProcessingThread) {
        delete videoProcessingThread;
		std::cout << "VidProcessingThread has been deleted" << std::endl;
    }

    if (yolo) {
        delete yolo;
		std::cout << "yolo has been deleted" << std::endl;
        yolo = nullptr;
    }

    if (classNames) {
        delete classNames;
		std::cout << "classNames has been deleted" << std::endl;
        classNames = nullptr;
    }

    delete videoWidget;
	std::cout << "videoWidget has been deleted" << std::endl;

    delete ui;
	std::cout << "ui has been deleted" << std::endl;
}



void MainWindow::connectSignalsSlots() {
    QObject::connect(this, &MainWindow::confThresholdChanged, [&](float value) {
        yolo->confThreshold = value;
    });

    QObject::connect(this, &MainWindow::nmsThresholdChanged, [&](float value) {
        yolo->nmsThreshold = value;
    });

    QObject::connect(this, &MainWindow::startClicked, [this]() {
        // Init yolo and camera
        int framerate = 30;

        std::string model_path = "models/weights/yolov7-tiny.weights";
		std::string config_path = "models/cfg/yolov7-tiny.cfg";
		std::string classNames_path = "models/names/coco.names";
		
		yolo_init(yolo, model_path, config_path, yolo->confThreshold, yolo->nmsThreshold, 416, 416);
		classNames = new std::vector<std::string>();
		*classNames = readClassNames(classNames_path);

		setupCamera(*this, framerate);
		//

        videoProcessingThread = new QThread();
		TimerHandler *timerHandler = new TimerHandler();
		QPointer<TimerHandler> timerHandlerPtr(timerHandler);
		timerHandler->moveToThread(videoProcessingThread);
		videoProcessingThread->start();

        QObject::connect(videoProcessingThread, &QThread::finished, videoProcessingThread, &QThread::deleteLater);

        QObject::connect(videoProcessingThread, &QThread::started, timerHandler, [this, timerHandlerPtr, framerate]() {
			if (!timerHandlerPtr) return;
			QObject::connect(timerHandlerPtr->timer, &QTimer::timeout, [this]() {
				processFrame(*this, cap, *yolo, *classNames);
			});
			timerHandlerPtr->startTimer(1000 / framerate);
		});

        QObject::connect(videoProcessingThread, &QThread::finished, timerHandler, &TimerHandler::deleteLater);
        QObject::connect(this, &MainWindow::stopClicked, timerHandler, &TimerHandler::stopTimer);
        QObject::connect(this, &MainWindow::stopClicked, [this]() {
            stopVideoProcessing();
        });
    });
}

void MainWindow::showFrame(const QImage &frame) {
    videoWidget->showFrame(frame);
}

void MainWindow::onConfThresholdSliderValueChanged(int value) {
   float confThreshold = static_cast<float>(value) / 100;
   emit confThresholdChanged(confThreshold);
   ui->confValueLabel->setText(QString("%1").arg(confThreshold, 0, 'f', 2));
}

void MainWindow::onNmsThresholdSliderValueChanged(int value) {
   float nmsThreshold = static_cast<float>(value) / 100;
   emit nmsThresholdChanged(nmsThreshold);
   ui->nmsValueLabel->setText(QString("%1").arg(nmsThreshold, 0, 'f', 2));
}

void MainWindow::onStartButtonClicked() {
	ui->startButton->setEnabled(false);
    ui->stopButton->setEnabled(true);
    
    emit startClicked();
}

void MainWindow::onStopButtonClicked() {
    ui->stopButton->setEnabled(false);
    ui->startButton->setEnabled(true);

    emit stopClicked();

    if (!yolo) {
        yolo = new YOLO();
        yolo->confThreshold = static_cast<float>(ui->confThresholdSlider->value()) / 100.0f;
        yolo->nmsThreshold = static_cast<float>(ui->nmsThresholdSlider->value()) / 100.0f;
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    stopVideoProcessing();
    
    if (videoProcessingThread) {
        videoProcessingThread->quit();
        videoProcessingThread->wait(1000);
        delete videoProcessingThread;
        std::cout << "VidProcessingThread has been deleted" << std::endl;
        videoProcessingThread = nullptr;
    }
    
    QMainWindow::closeEvent(event);
}

void MainWindow::stopVideoProcessing() {
    if (videoProcessingThread && videoProcessingThread->isRunning()) {
        videoProcessingThread->requestInterruption();
        videoProcessingThread->quit();
        videoProcessingThread->wait(1000);
        delete videoProcessingThread;
		std::cout << "VidProcessingThread has been deleted" << std::endl;
        videoProcessingThread = nullptr;
    }
    
    if (cap.isOpened()) {
        cap.release();
    }
    
    if (yolo) {
        delete yolo;
		std::cout << "yolo has been deleted" << std::endl;
        yolo = nullptr;
    }
    
    if (classNames) {
        delete classNames;
		std::cout << "classNames has been deleted" << std::endl;
        classNames = nullptr;
    }
}


