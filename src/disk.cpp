#include <cassert>

#include <unistd.h>

#include "disk.h"

Disk::Disk() : file(NULL) {
}

Disk::~Disk() {
    close();
}

bool Disk::diskExists(std::string name) {
    file = fopen(name.c_str(), "rb+");
    bool success = file != nullptr;
    if (success)
        close();
    return success;
}

void Disk::open(std::string name) {
    file = fopen(name.c_str(), "rb+");
    assert(file != NULL && "could not open the disk");
}

void Disk::close() {
    if (file != NULL) {
        fclose(file);
        file = NULL;
    }
}

void Disk::setAddr(uint32_t addr) {
    assert(file != NULL && "disk is null");
    fseek(file, addr, SEEK_SET);
}

void Disk::create(std::string name, uint32_t size) {
    file = fopen(name.c_str(), "wb");
    assert(file != NULL && "disk is NULL");
    assert(ftruncate(fileno(file), size) == 0 && "creating disk failed");
    rewind(file);
    fclose(file);
}

void Disk::write(const char *data, size_t size) {
    assert(file != NULL && "disk is NULL");
    (void)fwrite(data, size, 1, file);
}

void Disk::read(char *buffer, size_t size) {
    assert(file != NULL && "disk is NULL");
    (void)fread(buffer, size, 1, file);
}