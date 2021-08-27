#include <iostream>
#include <cassert>

#include "fs.h"
#include "fat32.h"

int main() {
    assert(sizeof(FAT32::DirHeader_t) <= FAT32::CLUSTER_SIZE);
    assert(sizeof(FAT32::DirEntry_t) <= FAT32::CLUSTER_SIZE);

    IFS *fs = FAT32::getInstance();
    fs->ls();
    fs->pwd();
    return 0;
}