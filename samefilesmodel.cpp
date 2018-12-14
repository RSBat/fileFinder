#include "samefilesmodel.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <iostream>

SameFilesModel::SameFilesModel() :
    QAbstractItemModel(nullptr),
    worker_thread(),
    unique_group(new Node("Unique files", QByteArray())),
    total_files(0)
{
    worker = new HashingWorker();
    worker->moveToThread(&worker_thread);
    connect(this, &SameFilesModel::scan_directory, worker, &HashingWorker::process);
    connect(worker, &HashingWorker::file_processed, this, &SameFilesModel::add_file);
    connect(worker, &HashingWorker::scan_ended, this, &SameFilesModel::no_more_files);
    worker_thread.start();
}

SameFilesModel::~SameFilesModel() {
    delete unique_group;
    for (auto ptr : grouped_files) {
        delete ptr;
    }
}

QVariant SameFilesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid())
        return QVariant();

    if (role != Qt::DisplayRole)
        return QVariant();

    auto ptr = static_cast<Node*>(index.internalPointer());
    if (ptr == unique_group) {
        return QString::number(ptr->children.size()) + " unique files";
    } else if (ptr->isFile) {
        return ptr->name;
    } else {
        return QString::number(ptr->children.size()) + " same files";
    }
}

QVariant SameFilesModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) {
        return QVariant();
    }

    if (orientation != Qt::Horizontal) {
        return QVariant();
    }

    if (section == 0) {
        return "Name";
    } else {
        return QVariant();
    }
}

QModelIndex SameFilesModel::index(int row, int column, const QModelIndex &parent) const {
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    if (!parent.isValid()) {
        if (row == grouped_files.size()) {
            return createIndex(row, column, unique_group);
        } else {
            return createIndex(row, column, grouped_files[row]);
        }
    }

    auto parent_ptr = static_cast<Node*>(parent.internalPointer());
    return createIndex(row, column, parent_ptr->children[row]);
}

QModelIndex SameFilesModel::parent(const QModelIndex &index) const {
    auto ptr = static_cast<Node*>(index.internalPointer());
    auto parent_ptr = ptr->parent;

    if (parent_ptr == nullptr) {
        return QModelIndex();
    }

    if (parent_ptr == unique_group) {
        return createIndex(grouped_files.size(), 0, unique_group);
    } else {
        int parents_row = hash_to_id[parent_ptr->hash];
        return createIndex(parents_row, 0, grouped_files[parents_row]);
    }
}

int SameFilesModel::rowCount(const QModelIndex &parent) const {
    if (!parent.isValid()) {
        return grouped_files.size() + 1;
    }
    return static_cast<Node*>(parent.internalPointer())->children.size();
}

int SameFilesModel::columnCount(const QModelIndex &parent) const {
    return 1;
}

void SameFilesModel::add_file(Node* file) {
    auto pos = hash_to_id.find(file->hash);
    int parent_pos;
    Node* group;

    if (pos == hash_to_id.end()) {
        auto unique_pos = unique_id.find(file->hash);
        if (unique_pos == unique_id.end()) {
            group = unique_group;
            parent_pos = grouped_files.size();

            unique_id[file->hash] = unique_group->children.size();
        } else {
            group = new Node(QString(), file->hash);
            group->isFile = false;
            parent_pos = grouped_files.size();
            hash_to_id[file->hash] = parent_pos;

            beginInsertRows(QModelIndex(), parent_pos, parent_pos);
            grouped_files.push_back(group);
            endInsertRows();

            // move file from unique to group
            // some files in front of us could be deleted, need to recalculate position
            int old_file_pos = unique_pos.value();
            if (old_file_pos >= unique_group->children.size()) { old_file_pos = unique_group->children.size() - 1; }
            while (unique_group->children[old_file_pos]->hash != file->hash) { old_file_pos--; }

            Node* old_file = unique_group->children[old_file_pos];
            old_file->parent = group;
            beginRemoveRows(index(grouped_files.size(), 0, QModelIndex()), old_file_pos, old_file_pos);
            unique_group->children.erase(unique_group->children.begin() + old_file_pos);
            unique_id.erase(unique_pos);
            endRemoveRows();

            beginInsertRows(index(parent_pos, 0, QModelIndex()), 0, 0);
            group->children.push_back(old_file);
            endInsertRows();
        }
    } else {
        parent_pos = pos.value();
        group = grouped_files[parent_pos];
    }

    file->parent = group;
    auto parent_index = index(parent_pos, 0, QModelIndex());
    beginInsertRows(parent_index, group->children.size(), group->children.size());
    group->children.push_back(file);
    endInsertRows();

    emit dataChanged(parent_index, parent_index); // need to update group title

    total_files++;
    emit scan_update(total_files);
}

void SameFilesModel::no_more_files() {
    emit scan_ended(total_files);
}

void SameFilesModel::start_scan(QString const& directory) {
    // cleanup
    beginResetModel();
    for (auto ptr: unique_group->children) {
        delete ptr;
    }
    unique_group->children.clear();
    unique_id.clear();

    for (auto ptr: grouped_files) {
        delete ptr;
    }
    grouped_files.clear();
    hash_to_id.clear();

    total_files = 0;
    endResetModel();

    emit scan_directory(directory);
}

void SameFilesModel::stop_scan() {
    worker->stop_scan();
}

// delete file
// if only one file remains in group, move that file to unique
void SameFilesModel::delete_file(QModelIndex const& index) {
    if (!index.isValid()) { return; }

    auto ptr = static_cast<Node*>(index.internalPointer());
    auto parent_ptr = ptr->parent;
    auto parent_index = index.parent();
    if (!ptr->isFile) { return; }

    // delete file
    beginRemoveRows(parent_index, index.row(), index.row());
    QFile::remove(ptr->name);
    parent_ptr->children.erase(parent_ptr->children.begin() + index.row());
    delete ptr;
    endRemoveRows();

    if (parent_ptr == unique_group) { return; }
    if (parent_ptr->children.size() > 1) { return; }

    // delete other child
    beginRemoveRows(parent_index, 0, 0);
    auto tmp = parent_ptr->children.back();
    parent_ptr->children.clear();
    endRemoveRows();

    // delete parent
    beginRemoveRows(QModelIndex(), parent_index.row(), parent_index.row());
    grouped_files.erase(grouped_files.begin() + parent_index.row());
    delete parent_ptr;
    endRemoveRows();

    // fix indexes
    auto iter = hash_to_id.find(parent_ptr->hash);
    hash_to_id.erase(iter);
    for (auto& x : hash_to_id) {
        if (x > parent_index.row()) {
            x--;
        }
    }

    // move other child to unique
    beginInsertRows(this->index(rowCount() - 1, 0, QModelIndex()), unique_group->children.size(), unique_group->children.size());
    tmp->parent = unique_group;
    unique_group->children.push_back(tmp);
    endInsertRows();
}

// deletes all files that have same hash as ours except our
// and moves it to unique files
void SameFilesModel::delete_same(QModelIndex const& index) {
    if (!index.isValid()) { return; }

    auto ptr = static_cast<Node*>(index.internalPointer());
    auto parent_ptr = ptr->parent;
    if (!ptr->isFile) { return; }
    if (parent_ptr == unique_group) { return; }

    auto parent_index = index.parent();

    // delete other files
    beginRemoveRows(parent_index, 0, parent_ptr->children.size());
    while (!parent_ptr->children.empty()) {
        auto tmp = parent_ptr->children.back();
        parent_ptr->children.pop_back();
        if (tmp != ptr) {
            QFile::remove(tmp->name);
            delete tmp;
        }
    }
    endRemoveRows();

    // delete parent
    beginRemoveRows(QModelIndex(), parent_index.row(), parent_index.row());
    grouped_files.erase(grouped_files.begin() + parent_index.row());
    delete parent_ptr;
    endRemoveRows();

    // actual position changed, recalculate them
    auto iter = hash_to_id.find(ptr->hash);
    hash_to_id.erase(iter);
    for (auto& x : hash_to_id) {
        if (x > parent_index.row()) {
            x--;
        }
    }

    // move file to unique
    beginInsertRows(this->index(rowCount() - 1, 0, QModelIndex()), unique_group->children.size(), unique_group->children.size());
    ptr->parent = unique_group;
    unique_group->children.push_back(ptr);
    endInsertRows();

}
