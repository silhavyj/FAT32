#include <cassert>

#include "fat32.h"
#include "shell.h"

int main() {
    assert(sizeof(FAT32::DirHeader_t) <= FAT32::CLUSTER_SIZE);
    assert(sizeof(FAT32::DirEntry_t) <= FAT32::CLUSTER_SIZE);

    Shell::getInstance()->setFS(FAT32::getInstance());
    Shell::getInstance()->run();

    return 0;
}