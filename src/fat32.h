#ifndef _FAT32_H_
#define _FAT32_H_

#include <climits>
#include <cstdint>
#include <array>

#include "fs.h"
#include "diskdriver.h"

#define KB(x) ((x) * (1 << 10))
#define MB(x) ((x) * (1 << 20))
#define GB(x) ((x) * (1 << 30))

class FAT32 : public IFS {
public:
    static constexpr uint32_t MAX_NAME_LEN = 16;
    static constexpr uint32_t LS_SPACING = MAX_NAME_LEN - 1;
    static constexpr const char *DISK_FILE_NAME  = "disk.dat";

    static constexpr uint32_t DISK_SIZE    = MB(50);
    static constexpr uint32_t CLUSTER_SIZE = 128;
    static constexpr uint8_t ADDR_SIZE = sizeof(uint32_t);

    static constexpr uint32_t CLUSTER_COUNT = DISK_SIZE / (ADDR_SIZE + CLUSTER_SIZE);
    static constexpr uint32_t FAT_TABLE_START_ADDR = 0;
    static constexpr uint32_t CLUSTERS_START_ADDR = FAT_TABLE_START_ADDR + (CLUSTER_COUNT * ADDR_SIZE);

    static constexpr uint32_t FREE_CLUSTER  = (1L << 32) - 1;
    static constexpr uint32_t EOF_CLUSTER   = (1L << 32) - 2;
    static constexpr uint32_t TAKEN_CLUSTER = (1L << 32) - 3;
    static constexpr uint32_t ALL_CLUSTERS_TAKEN = (1L << 32) - 4;

    static constexpr uint32_t ROOT_DIR_CLUSTER_INDEX = 0;

    struct DirEntry_t {
        char name[MAX_NAME_LEN];
        uint32_t startCluster;
        uint32_t parentStartCluster;
        uint32_t size;
        bool directory;
        bool operator==(const DirEntry_t other) const;
        bool operator!=(const DirEntry_t other) const;
    } __attribute__((packed));

    struct DirHeader_t {
        char name[MAX_NAME_LEN];
        uint32_t startCluster;
        uint32_t parentStartCluster;
        uint32_t entryCount;
    } __attribute__((packed));

    struct Dir_t {
        DirHeader_t header;
        DirEntry_t *entries;
        ~Dir_t();
    } __attribute__((packed));

    DirEntry_t NULL_DIR_ENTRY;

    static constexpr uint32_t ENTRIES_IN_ONE_CLUSTER = CLUSTER_SIZE / sizeof(DirEntry_t);
    static constexpr uint32_t ENTRIES_IN_CLUSTER_AFTER_DIR_HEADER = (CLUSTER_SIZE - sizeof(DirHeader_t)) / sizeof(DirEntry_t);

private:
    IDiskDriver *disk;
    std::array<uint32_t, CLUSTER_COUNT> fat;
    uint32_t workingDirStartCluster;
    
    static FAT32 *instance;

private:
    FAT32();
    FAT32(FAT32 &) = delete;
    void operator=(FAT32 &) = delete;

    void initialize();
    void load();
    inline void saveFat();
    inline void loadFat();
    void saveDir(Dir_t *dir);
    void saveDirFirstCluster(Dir_t *dir, uint32_t entryCount);
    Dir_t *loadDir(uint32_t startCluster);
    uint32_t getFreeCluster();
    bool existsNumberOfFreeClusters(uint32_t n) const;
    void freeAllOccupiedClusters(uint32_t startCluster);
    Dir_t *createEmptyDir(std::string name, uint32_t parentStartCluster);
    inline uint32_t clusterAddr(uint32_t index);
    void addEntryIntoDir(Dir_t *dir, DirEntry_t *entry);
    void removeEntryFromDir(Dir_t*dir, DirEntry_t *entry);
    DirEntry_t getEntry(std::string name, Dir_t *dir);
    DirEntry_t getEntry(std::string path);
    DirEntry_t createFileEntry(Dir_t *dir, const char *name, uint32_t size);
    DirEntry_t createEntry(Dir_t *dir);
    inline uint32_t getFileSize(FILE *file) const;
    std::string getFileName(std::string path) const;
    void copyClusters(uint32_t srcStartCluster, uint32_t desStartCluster);

    void printDir(Dir_t *dir);
    void printDirEntry(DirEntry_t *entry);
    void printFAT();
    void printTree(Dir_t *dir, uint32_t space);

public:
    static FAT32 *getInstance();

    void mkdir(std::string name) override;
    void ls(std::string path) override;
    void pwd() override;
    void cd(std::string path) override;
    void rmdir(std::string path) override;
    void in(std::string path) override;
    void out(std::string path) override;
    void cat(std::string path) override;
    void rm(std::string path) override;
    void cp(std::string des, std::string src) override;
    void mv(std::string des, std::string src) override;
    std::string getPWD() override;
    void info() override;
    void tree(std::string path) override;
};

#endif