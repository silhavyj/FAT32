#ifndef _DISK_H_
#define _DISK_H_

#include <string>
#include <cstdint>
#include <cstdio>

#include "diskdriver.h"

class Disk : public IDiskDriver {
private:
    FILE *file;

public:
    Disk();
    ~Disk();

    bool diskExists(std::string name) override;
    void open(std::string name) override;
    void close() override;
    void create(std::string name, uint32_t size) override;
    void setAddr(uint32_t addr) override;
    void write(const char *data, size_t size) override;
    void read(char *buffer, size_t size) override;
};

#endif