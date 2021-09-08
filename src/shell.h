#ifndef _SHELL_H_
#define _SHELL_H_

#include <vector>

#include "fs.h"

class Shell {
private:
    static Shell *instance;
    IFS *fs;

private:
    Shell();
    Shell(Shell &) = delete;
    void operator=(Shell &) = delete;

private:
    std::vector<std::string> split(std::string str, char separator);
    void printPrompt();

public:
    static Shell *getInstance();
    void setFS(IFS *fs);
    void run();
};

#endif