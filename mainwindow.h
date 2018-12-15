#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QErrorMessage>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void set_progress_complete(int count);
    void set_progress_update(int count);
    void click_start();
    void click_stop();
    void getContextMenu(QPoint const& pos);

signals:
    void scan_directory(QString const& directory);
    void abort_scan();
    void delete_file(QModelIndex const& index);
    void delete_same(QModelIndex const& index);

private:
    bool isScanning;
    Ui::MainWindow *ui;
    QLabel* total_label;
    QErrorMessage* no_directory_message;

    void enable_buttons(bool state);
};

#endif // MAINWINDOW_H
