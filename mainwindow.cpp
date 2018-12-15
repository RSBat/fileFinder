#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "samefilesmodel.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>
#include <QFileDialog>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    isScanning(false),
    ui(new Ui::MainWindow),
    no_directory_message(new QErrorMessage(this))
{
    ui->setupUi(this);
    SameFilesModel* model = new SameFilesModel();
    ui->treeView->setModel(model);
    //ui->treeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents); // looks ugly
    //ui->treeView->header()->setStretchLastSection(false);
    connect(model, &SameFilesModel::scan_ended, this, &MainWindow::set_progress_complete);
    connect(model, &SameFilesModel::scan_update, this, &MainWindow::set_progress_update);
    connect(this, &MainWindow::scan_directory, model, &SameFilesModel::start_scan);
    connect(this, &MainWindow::abort_scan, model, &SameFilesModel::stop_scan);
    connect(this, &MainWindow::delete_file, model, &SameFilesModel::delete_file);
    connect(this, &MainWindow::delete_same, model, &SameFilesModel::delete_same);

    ui->treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeView, &QTreeView::customContextMenuRequested, this, &MainWindow::getContextMenu);

    ui->progressBar->reset();

    total_label = new QLabel(statusBar());
    statusBar()->addWidget(total_label);

    connect(ui->btn_start, &QPushButton::clicked, this, &MainWindow::click_start);
    connect(ui->lineEdit, &QLineEdit::returnPressed, this, &MainWindow::click_start);
    connect(ui->btn_stop, &QPushButton::clicked, this, &MainWindow::click_stop);

    ui->btn_dir_dialog->setIcon(QIcon::fromTheme("document-open"));
    connect(ui->btn_dir_dialog, &QPushButton::clicked, this, [this](){
        QString tmp = QFileDialog::getExistingDirectory(this, "Directory to scan");
        this->ui->lineEdit->setText(tmp);
    });

    ui->btn_stop->setEnabled(false);

    ui->lineEdit->setText(QDir::currentPath());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::enable_buttons(bool state) {
    ui->btn_start->setEnabled(state);
    ui->lineEdit->setEnabled(state);
    ui->btn_dir_dialog->setEnabled(state);

    ui->btn_stop->setEnabled(!state);
}

void MainWindow::set_progress_complete(int count) {
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(1);
    ui->progressBar->setValue(1);

    enable_buttons(true);

    total_label->setText("Files scanned: " + QString::number(count));

    isScanning = false;
}

void MainWindow::set_progress_update(int count) {
    total_label->setText("Files scanned: " + QString::number(count));
}

void MainWindow::click_start() {
    QString dir = ui->lineEdit->text();

    if (QDir(dir).exists()) {
        ui->progressBar->setMinimum(0);
        ui->progressBar->setMaximum(0);

        total_label->setText("Files scanned: 0");
        enable_buttons(false);

        emit scan_directory(ui->lineEdit->text());
        isScanning = true;
    } else {
        no_directory_message->showMessage("No such directory");
    }
}

void MainWindow::click_stop() {
    emit abort_scan();
    ui->progressBar->setMaximum(1); // otherwise reset won't work
    ui->progressBar->reset();

    enable_buttons(true);

    isScanning = false;
}

void MainWindow::getContextMenu(QPoint const& pos) {
    QModelIndex index = ui->treeView->indexAt(pos);
    Node* ptr = static_cast<Node*>(index.internalPointer());
    if (!ptr->isFile) { return; }

    QString filename = ptr->name;

    QMenu* menu = new QMenu(ui->treeView);
    QAction* act_open = menu->addAction("Open file");
    connect(act_open, &QAction::triggered, this, [filename](){
        QDesktopServices::openUrl(QUrl(filename));
    });

    QAction* act_folder = menu->addAction("Open folder");
    connect(act_folder, &QAction::triggered, this, [filename](){
        QFileInfo info(filename);
        QDesktopServices::openUrl(QUrl(info.absolutePath())); // or info.path() because filename includes absolute path
    });

    QAction* act_delete = menu->addAction("Delete file");
    connect(act_delete, &QAction::triggered, this, [this, index]() {
        emit this->delete_file(index);
    });


    QAction* act_delete_same = menu->addAction("Delete same except this");
    connect(act_delete_same, &QAction::triggered, this, [this, index]() {
        emit this->delete_same(index);
    });

    if (isScanning) {
        act_delete->setEnabled(false);
        act_delete_same->setEnabled(false);
    }

    menu->exec(ui->treeView->mapToGlobal(pos));
}
