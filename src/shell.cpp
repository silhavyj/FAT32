#include <cassert>
#include <iostream>
#include <sstream>

#include "shell.h"

Shell *Shell::instance = nullptr;

Shell *Shell::getInstance() {
    if (instance == nullptr)
        instance = new Shell;
    return instance;
}

Shell::Shell() {
}

void Shell::setFS(IFS *fs) {
    assert(fs != nullptr && "fs is NULL");
    this->fs = fs;
}

void Shell::printPrompt() {
    std::string pwd = fs->getPWD();
    std::cout << pwd << "> ";
}

std::vector<std::string> Shell::split(std::string str, char separator) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, separator))
        if (token != "")
            tokens.emplace_back(token);
    return move(tokens);
}

void Shell::run() {
    std::string line;
    std::vector<std::string> args;

    while (true) {
        printPrompt();

        std::getline(std::cin, line);
        args = split(line, ' ');

        if (args.empty())
            continue;

        if (args[0] == "ls") {
            if (args.size() > 1) {
                fs->ls(args[1]);
            } else {
                fs->ls(".");
            }
        } else if (args[0] == "mkdir") {
            if (args.size() < 2) {
                std::cout << "missing folder name\n";
            } else {
                fs->mkdir(args[1]);
            }
        } else if (args[0] == "pwd") {
            fs->pwd();
        } else if (args[0] == "cd") {
            if (args.size() < 2) {
                std::cout << "missing path\n";
            } else {
                fs->cd(args[1]);
            }
        } else if (args[0] == "rmdir") {
            if (args.size() < 2) {
                std::cout << "missing folder\n";
            } else {
                fs->rmdir(args[1]);
            }
        } else if (args[0] == "in") {
            if (args.size() < 2) {
                std::cout << "missing path\n";
            } else {
                fs->in(args[1]);
            }
        } else if (args[0] == "out") {
            if (args.size() < 2) {
                std::cout << "missing path\n";
            } else {
                fs->out(args[1]);
            }
        } else if (args[0] == "cat") {
            if (args.size() < 2) {
                std::cout << "missing file\n";
            } else {
                fs->cat(args[1]);
            }
        } else if (args[0] == "rm") {
            if (args.size() < 2) {
                std::cout << "missing file\n";
            } else {
                fs->rm(args[1]);
            } 
        } else if (args[0] == "cp") {
            // TODO
        } else if (args[0] == "mv") {
            if (args.size() == 1) {
                std::cout << "missing source file\n";
            } else if (args.size() == 2) {
                std::cout << "missing destination folder\n";
            } else {
                fs->mv(args[2], args[1]);
            } 
        } else if (args[0] == "tree") {
            if (args.size() < 2) {
                std::cout << "missing directory\n";
            } else {
                fs->tree(args[1]);
            }
        } else if (args[0] == "info") {
            fs->info();
        } else {
            std::cout << "invalid command\n";
        }
    }
}