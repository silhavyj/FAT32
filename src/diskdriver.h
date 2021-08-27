#ifndef _DISK_DRIVER_H_
#define _DISK_DRIVER_H_

#include <string>
#include <cstdint>

class IDiskDriver {
public:
    virtual ~IDiskDriver() = 0;

    virtual bool diskExists(std::string name) = 0;
    virtual void open(std::string name) = 0;
    virtual void close() = 0;
    virtual void create(std::string name, uint32_t size) = 0;
    virtual void setAddr(uint32_t addr) = 0;
    virtual void write(const char *data, size_t size) = 0;
    virtual void read(char *buffer, size_t size) = 0;
};

#endif