#include "widget.h"
#include "ui_widget.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTime>
#include <QDebug>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);

    // Load placeholder images into labels
    QPixmap leftPlaceholderImage("://images/left.png");
    QPixmap rightPlaceholderImage("://images/right.png");

    ui->leftImage->setPixmap(leftPlaceholderImage);
    ui->leftImage->setScaledContents(true);
    ui->rightImage->setPixmap(rightPlaceholderImage);
    ui->rightImage->setScaledContents(true);
}

Widget::~Widget()
{
    delete ui;
}


void Widget::on_selectFileButton_clicked()
{
    // Get the JSON document filename
    QString filename = QFileDialog::getOpenFileName(
                this,
                "Select file for reading duplicates",
                QDir::homePath()
    );
    if (filename.isEmpty()) return;

    ui->statusLabel->setText(QString("Loading input from %1").arg(filename));
    ui->selectFileButton->setEnabled(false);

    // Open and read the JSON document
    parseDuplicateFile(filename);
    totalComparisons = jsonDuplicateArray.size();
    ui->statusLabel->setText(QString("JSON input data loaded - %1 potential duplicates to inspect").arg(totalComparisons));
    ui->progressBar->setRange(0,totalComparisons);

    // Start the comparison process and enable the buttons
    loadNextPair();
    ui->deleteLeftButton->setEnabled(true);
    ui->deleteRightButton->setEnabled(true);
    ui->skipButton->setEnabled(true);
}

void Widget::parseDuplicateFile(QString filename)
{
    QFile jsonFile(filename);
    bool success = jsonFile.open(QIODevice::ReadOnly | QIODevice::Text);
    if (!success) {
        QMessageBox::critical(this, "Error", "Unable to read JSON file");
        throw "unable to read " + filename;
    }

    QByteArray data = jsonFile.readAll();
    QJsonDocument document = QJsonDocument::fromJson(data);
    jsonDuplicateArray = document.array();
}

void Widget::loadNextPair()
{
    QTime t;
    t.start();
    ui->statusLabel->setText("Loading images...");

    auto imagePair = jsonDuplicateArray.at(completedComparisons).toArray();
    QString leftPath = imagePair.at(0).toString();
    QString rightPath = imagePair.at(1).toString();

    // Try to load the images and hope they are understandable by Qt
    QPixmap leftImage(leftPath);
    QPixmap rightImage(rightPath);

    // Display some metadata about the files to help in delete selection
    displayPhotoMetadata(leftPath, leftImage, rightPath, rightImage);

    qDebug() << "Images: " << leftPath << " <=> " << rightPath;

    ui->leftImage->setPixmap(leftImage);
    ui->leftImage->setScaledContents(true);
    ui->rightImage->setPixmap(rightImage);
    ui->rightImage->setScaledContents(true);

    // Calculate loading duration.
    QString loadDuration = QString("Loaded images in %1 ms").arg(t.elapsed());
    qDebug() << loadDuration;

    // Increment the completed count and progress bar
    completedComparisons++;
    ui->progressBar->setValue(completedComparisons);
    ui->statusLabel->setText(QString("%1. Comparison %2/%3.").arg(loadDuration).arg(completedComparisons).arg(totalComparisons));
}

void Widget::displayPhotoMetadata(QString leftFilename, QPixmap left, QString rightFilename, QPixmap right)
{
    // filenames
    ui->leftFilenameLineEdit->setText(leftFilename);
    ui->rightFilenameLineEdit->setText(rightFilename);

    // file sizes
    ui->leftSizeLineEdit->setText(QString("%1 bytes").arg(QFileInfo(leftFilename).size()));
    ui->rightSizeLineEdit->setText(QString("%1 bytes").arg(QFileInfo(rightFilename).size()));

    // file resolutions
    ui->leftResolutionLineEdit->setText(QString("%1 x %2").arg(left.size().width()).arg(left.size().height()));
    ui->rightResolutionLineEdit->setText(QString("%1 x %2").arg(right.size().width()).arg(right.size().height()));
}

void Widget::on_skipButton_clicked()
{
    qDebug() << "Image index " << QString::number(completedComparisons-1) << " skipped...";
    loadNextPair();
}

void Widget::on_deleteLeftButton_clicked()
{
    qDebug() << "Requested to delete " << ui->leftFilenameLineEdit->text();
    loadNextPair();
}

void Widget::on_deleteRightButton_clicked()
{
    qDebug() << "Requested to delete " << ui->rightFilenameLineEdit->text();
    loadNextPair();
}
