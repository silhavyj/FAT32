#ifndef _FS_H_
#define _FS_H_

#include <string>

class IFS {
public:
    virtual void mkdir(std::string name) = 0;
    virtual void ls(std::string path) = 0;
    virtual void pwd() = 0;
    virtual void cd(std::string path) = 0;
    virtual void rmdir(std::string path) = 0;
    virtual void in(std::string path) = 0;
    virtual void out(std::string path) = 0;
    virtual void cat(std::string path) = 0;
    virtual void rm(std::string path) = 0;
    virtual void cp(std::string des, std::string src) = 0;
    virtual void mv(std::string des, std::string src) = 0;
    virtual void tree(std::string path) = 0;
    virtual std::string getPWD() = 0;
    virtual void info() = 0;
};

#endif