#include <iostream>
#include <cassert>

#include "fs.h"
#include "fat32.h"

int main() {
    assert(sizeof(FAT32::DirHeader_t) <= FAT32::CLUSTER_SIZE);
    assert(sizeof(FAT32::DirEntry_t) <= FAT32::CLUSTER_SIZE);

    IFS *fs = FAT32::getInstance();
    fs->mkdir("doc");
    fs->mkdir("tmp");
    fs->pwd();
    fs->ls();
    fs->cd("doc");
    fs->mkdir("test");
    fs->pwd();
    fs->ls();
    fs->cd("././././../doc");
    fs->pwd();
    fs->cd("/");
    fs->pwd();
    fs->ls();
    fs->rmdir("/doc/testA");

    return 0;
}