#ifndef _FS_H_
#define _FS_H_

#include <cstring>

class IFS {
public:
    virtual void mkdir(std::string name) = 0;
    virtual void ls() = 0;
    virtual void pwd() = 0;
    virtual void cd(std::string path) = 0;
    virtual void rmdir(std::string path) = 0;
    virtual void in(std::string path) = 0;
    virtual void out(std::string path) = 0;
    virtual void cat(std::string path) = 0;
    virtual void rm(std::string path) = 0;
};

#endif