#include <cmath>
#include <memory>
#include <sstream>

#include "fat32.h"
#include "disk.h"

#include "debugger.h"

static std::vector<std::string> split(const std::string& s, char c);

FAT32 *FAT32::instance = nullptr;

FAT32 *FAT32::getInstance() {
    if (instance == nullptr)
        instance = new FAT32;
    return instance;
}

FAT32::FAT32() : disk(new Disk), workingDir(nullptr), rootDir(nullptr) {
    if (disk->diskExists(DISK_FILE_NAME) == false)
        initialize();
    disk->open(DISK_FILE_NAME);
    load();
}

FAT32::~FAT32() {
    if (workingDir != nullptr)
        delete workingDir;
    if (rootDir != nullptr)
        delete rootDir;
    disk->close();
    delete disk;
    delete instance;
}

FAT32::Dir_t::~Dir_t() {
    if (entries != nullptr) {
        delete[] entries;
        entries = nullptr;
    }
}

bool FAT32::DirEntry_t::operator==(const DirEntry_t other) const {
    return memcmp(reinterpret_cast<const void *>(this), reinterpret_cast<const void *>(&other), sizeof(DirEntry_t)) == 0;
}

bool FAT32::DirEntry_t::operator!=(const DirEntry_t other) const {
    return !(*this == other);
}

void FAT32::initialize() {
    disk->create(DISK_FILE_NAME, DISK_SIZE);
    disk->open(DISK_FILE_NAME);
    fat.fill(FREE_CLUSTER);
    rootDir = createEmptyDir("/", ROOT_DIR_CLUSTER_INDEX);
    save();
    disk->close();
    delete rootDir;
}

void FAT32::save() {
    saveDir(rootDir);
    saveFat(); // fat table must be stored after all changes has been done
}

inline void FAT32::saveFat() {
    disk->setAddr(FAT_TABLE_START_ADDR);
    disk->write(reinterpret_cast<const char *>(&fat), sizeof(fat));
}

inline void FAT32::loadFat() {
    disk->setAddr(FAT_TABLE_START_ADDR);
    disk->read(reinterpret_cast<char *>(&fat), sizeof(fat));
}

void FAT32::load() {
    loadFat();
    rootDir = loadDir(ROOT_DIR_CLUSTER_INDEX);
    workingDir = rootDir;
}

void FAT32::saveDir(Dir_t *dir) {
    assert(dir != nullptr && "dir is nullptr");
    freeAllOccupiedClusters(dir->header.startCluster);

    uint32_t entriesInFirstCluster = std::min(dir->header.entryCount, ENTRIES_IN_CLUSTER_AFTER_DIR_HEADER);
    bool allFitsInOneCluster = dir->header.entryCount <= ENTRIES_IN_CLUSTER_AFTER_DIR_HEADER; 

    if (allFitsInOneCluster) {
        // all we need is the end of file cluster (the first one is
        // already there as we skipped it in freeAllOccupiedClusters)
        assert(existsNumberOfFreeClusters(1) && "not enough free clusters");

        saveDirFirstCluster(dir, entriesInFirstCluster);

        // link up the last EOF cluster to the whole chain
        uint32_t eofCluster = getFreeCluster();
        fat[dir->header.startCluster] = eofCluster;
        fat[eofCluster] = EOF_CLUSTER;

        saveFat();
        return;
    }

    uint32_t remainingEntries = dir->header.entryCount - entriesInFirstCluster;
    uint32_t clustersNeeded = ceil(static_cast<double>(remainingEntries) / ENTRIES_IN_ONE_CLUSTER);

    // make sure we have enough free clusters (+1 is the final EOF cluster)
    assert(existsNumberOfFreeClusters(1 + clustersNeeded) && "not enough free clusters");

    // save all entries that fit into the first cluster
    saveDirFirstCluster(dir, entriesInFirstCluster);

    uint32_t prevCluster = dir->header.startCluster;
    uint32_t currCluster;
    uint32_t entryIndex = entriesInFirstCluster;

    // the very last cluster has to be handeled separately due to the reamaining space 
    for (uint32_t i = 0; i < clustersNeeded - 1; i++) {
        // create a link in the fat table
        currCluster = getFreeCluster();
        fat[prevCluster] = currCluster;
        prevCluster = currCluster;

        // store as many entries into one cluster as possible
        disk->setAddr(clusterAddr(currCluster));
        disk->write(reinterpret_cast<const char *>(&dir->entries[entryIndex]), ENTRIES_IN_ONE_CLUSTER * sizeof(DirEntry_t));
        entryIndex += ENTRIES_IN_ONE_CLUSTER;
    }

    uint32_t offset = 0;
    currCluster = getFreeCluster();
    fat[prevCluster] = currCluster;

    // store the very last entries
    for (; entryIndex < dir->header.entryCount; entryIndex++) {
        disk->setAddr(clusterAddr(currCluster) + offset);
        disk->write(reinterpret_cast<const char *>(&dir->entries[entryIndex]), sizeof(DirEntry_t));
        offset += sizeof(DirEntry_t);
    }

    // lastely we need to link up the EOF cluster
    prevCluster = currCluster;
    currCluster = getFreeCluster();
    fat[prevCluster] = currCluster;
    fat[currCluster] = EOF_CLUSTER;
}

void  FAT32::saveDirFirstCluster(Dir_t *dir, uint32_t entryCount) {
    // store the directory header
    disk->setAddr(clusterAddr(dir->header.startCluster));
    disk->write(reinterpret_cast<const char *>(&dir->header), sizeof(DirHeader_t));
    
    // skip the header (offstet = sizeof(DirHeader_t))
    // and store all entries into the first cluster
    disk->setAddr(clusterAddr(dir->header.startCluster) + sizeof(DirHeader_t));
    disk->write(reinterpret_cast<const char *>(dir->entries), entryCount * sizeof(DirEntry_t));
}

inline uint32_t FAT32::clusterAddr(uint32_t index) {
    return CLUSTERS_START_ADDR + (index * CLUSTER_SIZE);
}

FAT32::Dir_t *FAT32::loadDir(uint32_t startCluster) {
    Dir_t *dir = new Dir_t;

    // read the dir's header - contains basic info
    disk->setAddr(clusterAddr(startCluster));
    disk->read(reinterpret_cast<char *>(&dir->header), sizeof(DirHeader_t));

    uint32_t entriesInFirstCluster = std::min(dir->header.entryCount, ENTRIES_IN_CLUSTER_AFTER_DIR_HEADER);
    dir->entries = new DirEntry_t[dir->header.entryCount];

    // set the address to the very first entry (skip the header)
    disk->setAddr(clusterAddr(startCluster) + sizeof(DirHeader_t));
    disk->read(reinterpret_cast<char *>(dir->entries), entriesInFirstCluster * sizeof(DirEntry_t));

    // all entries fitted into the first cluster
    if (dir->header.entryCount <= ENTRIES_IN_CLUSTER_AFTER_DIR_HEADER)
        return dir;

    uint32_t remainingEntries = dir->header.entryCount - entriesInFirstCluster;
    uint32_t clustersNeeded = ceil(static_cast<double>(remainingEntries) / ENTRIES_IN_ONE_CLUSTER);
    uint32_t entryIndex = entriesInFirstCluster;
    
    // skip the first cluster (that one has been already handeled)
    uint32_t currCluster = fat[dir->header.startCluster];

    // process all clusters except the very last one (that one has to be handeled separately)
    for (uint32_t i = 0; i < clustersNeeded - 1; i++) {
        disk->setAddr(clusterAddr(currCluster));
        disk->read(reinterpret_cast<char *>(&dir->entries[entryIndex]), ENTRIES_IN_ONE_CLUSTER * sizeof(DirEntry_t));
        
        // move on to the next cluster
        currCluster = fat[currCluster];
        entryIndex += ENTRIES_IN_ONE_CLUSTER;
    }

    uint32_t offset = 0;

    // read the remaining entries from the very last cluster
    for (; entryIndex < dir->header.entryCount; entryIndex++) {
        disk->setAddr(clusterAddr(currCluster) + offset);
        disk->read(reinterpret_cast<char *>(&dir->entries[entryIndex]), sizeof(DirEntry_t));
        offset += sizeof(DirEntry_t);
    }

    // check point - make sure we've reached the end
    currCluster = fat[currCluster];
    assert(fat[currCluster] == EOF_CLUSTER && "dir has not been read properly");

    return dir;
}

uint32_t FAT32::getFreeCluster() {
    for (uint32_t i = 0; i < CLUSTER_COUNT; i++)
        if (fat[i] == FREE_CLUSTER) {
            fat[i] = TAKEN_CLUSTER;
            return i;
        }
    return ALL_CLUSTERS_TAKEN;
}

bool FAT32::existsNumberOfFreeClusters(uint32_t n) const {
    uint32_t freeClusters = 0;
    for (auto &cluster : fat) {
        freeClusters += (cluster == FREE_CLUSTER);
        if (freeClusters == n)
            return true;
    }
    return false;
}

void FAT32::freeAllOccupiedClusters(uint32_t startCluster) {
    uint32_t currCluster = startCluster;
    uint32_t prevCluster;

    // skip the first cluster so the entries will always
    // have the same firstCluster once they're created
    currCluster = fat[currCluster]; 
    
    while (fat[currCluster] != EOF_CLUSTER && fat[currCluster] != FREE_CLUSTER) {
        prevCluster = currCluster;
        currCluster = fat[currCluster];
        fat[prevCluster] = FREE_CLUSTER;
    }
    fat[currCluster] = FREE_CLUSTER;
}

FAT32::Dir_t *FAT32::createEmptyDir(std::string name, uint32_t parentStartCluster) {
    // the dir's header will defo take one cluster
    // also there must bu an ending cluster => min clusters required = 2
    assert(existsNumberOfFreeClusters(2) && "not enough free clusters");
    
    Dir_t *dir = new Dir_t;
    strcpy(dir->header.name, name.c_str());

    dir->header.entryCount = 0;
    dir->header.startCluster = getFreeCluster();
    dir->header.parentStartCluster = parentStartCluster;
    dir->entries = nullptr;

    uint32_t eofCluster = getFreeCluster();
    fat[dir->header.startCluster] = eofCluster;
    fat[eofCluster] = EOF_CLUSTER;
    return dir;
}

FAT32::DirEntry_t FAT32::createEntry(Dir_t *dir) {
    assert(dir != nullptr && "dir is null");
    DirEntry_t entry;
    strcpy(entry.name, dir->header.name);
    entry.startCluster = dir->header.startCluster;
    entry.parentStartCluster = dir->header.parentStartCluster;
    entry.size = sizeof(Dir_t);
    entry.directory = true;
    return entry;
}

FAT32::DirEntry_t FAT32::getEntry(std::string name, Dir_t *dir) {
    assert(dir != nullptr && "dir is null");
    if (dir->header.entryCount == 0)
        return NULL_DIR_ENTRY;
    for (uint32_t i = 0; i < dir->header.entryCount; i++) {
        if (strcmp(dir->entries[i].name, name.c_str()) == 0)
            return dir->entries[i];
    }
    return NULL_DIR_ENTRY;
}

void FAT32::addEntryIntoDir(Dir_t *dir, DirEntry_t *entry) {
    assert(dir != nullptr && "dir is null");
    assert(entry != nullptr && "entry is null");
    assert(getEntry(entry->name, dir) == NULL_DIR_ENTRY && "names is already taken");
    
    if (dir->header.entryCount == 0) {
        dir->header.entryCount++;
        dir->entries = new DirEntry_t[1];
        dir->entries[0] = *entry;
    } else {
        uint32_t n = dir->header.entryCount;
        DirEntry_t *entries = new DirEntry_t[n+1];

        // copy all previous entries
        for (uint32_t i = 0; i < n; i++)
            entries[i] = dir->entries[i];

        // add the new one to the last position
        entries[n] = *entry;

        dir->header.entryCount++;
        delete[] dir->entries;
        dir->entries = entries;
    }
    saveDir(dir);
}

void FAT32::printDir(Dir_t *dir) {
    assert(dir != nullptr && "dir is null");
    for (uint32_t i = 0; i < dir->header.entryCount; i++)
        printDirEntry(&dir->entries[i]);
}

void FAT32::printDirEntry(DirEntry_t *entry) {
    assert(entry != nullptr && "entry is null");
    std::cout << (entry->directory ? "[+]" : "[-]");
    std::cout << ' ' << entry->size;
    std::cout << ' ' << entry->parentStartCluster;
    std::cout << ' ' << entry->startCluster;
    std::cout << ' ' << entry->size;
    std::cout << ' ' << entry->name << '\n';
}

void FAT32::printFAT() {
    for (uint32_t i = 0; i < fat.size(); i++) {
        cout << i << " | ";
        switch (fat[i]) {
            case FREE_CLUSTER:
                std::cout << "FREE\n";
                break;
            case EOF_CLUSTER:
                std::cout << "EOF\n";
                break;
            case TAKEN_CLUSTER:
                std::cout << "TAKEN!!\n";
                break;
            default:
                std::cout << fat[i] << "\n";
        }
    }
}

static std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string token;

    while (getline(ss, token, c))
        if (token != "")
            tokens.emplace_back(token);
    return move(tokens);
}

FAT32::DirEntry_t FAT32::getEntry(std::string path) {
    assert(path.length() > 0 && "invalid path");
    if (path == ".")
        return createEntry(workingDir);
    if (path == "..") {
        std::unique_ptr<Dir_t> parentDir(loadDir(workingDir->header.parentStartCluster));
        return createEntry(parentDir.get());
    }
    
    DirEntry_t entry;
    Dir_t *currDir;
    Dir_t *tmpDir;
    Dir_t *parentDir;
    bool absolute = path[0] == '/';

    if (absolute) {
        currDir = loadDir(ROOT_DIR_CLUSTER_INDEX);
    } else {
        currDir = loadDir(workingDir->header.startCluster);
    }
    entry = createEntry(currDir);

    std::vector<std::string> tokens = split(path, '/');
    for (uint32_t i = 0; i < tokens.size(); i++) {
        if (tokens[i] == ".") {
            continue;
        } else if (tokens[i] == "..") {
            parentDir = loadDir(currDir->header.parentStartCluster);
            entry = createEntry(parentDir);
            delete parentDir;
        } else {
            entry = getEntry(tokens[i], currDir);
        }
        if (entry == NULL_DIR_ENTRY || entry.directory == false) {
            delete currDir;
            return NULL_DIR_ENTRY;
        }
        tmpDir = currDir;
        currDir = loadDir(entry.startCluster);
        delete tmpDir;
    }
    delete currDir;
    return entry;
}

void FAT32::removeEntryFromDir(Dir_t*dir, DirEntry_t *entry) {
    assert(dir != nullptr && "dir is null");
    assert(entry != nullptr && "entry is null");
    
    // find the possition of the entry to delete
    uint32_t p = 0;
    for (; p < dir->header.entryCount; p++)
        if (strcmp(dir->entries[p].name, entry->name) == 0)
            break;

    uint32_t index = 0;
    uint32_t n = dir->header.entryCount;
    DirEntry_t *prevEntries = dir->entries;
    DirEntry_t *entries = new DirEntry_t[n-1];

    // copy all entries except the one to be deleted
    for (uint32_t i = 0; i < n; i++)
        if (i != p)
            entries[index++] = dir->entries[i];
    
    // update the parent dir
    dir->header.entryCount--;
    dir->entries = entries;
    delete[] prevEntries;

    saveDir(dir);
}

void FAT32::mkdir(std::string name) {
    std::unique_ptr<Dir_t> dir(createEmptyDir(name, workingDir->header.startCluster));
    DirEntry_t entry = createEntry(dir.get());
    addEntryIntoDir(workingDir, &entry);
    saveDir(dir.get());
}

void FAT32::ls() {
    printDir(workingDir);
}

void FAT32::pwd() {
    std::string path = "";
    Dir_t *prevDir;
    Dir_t *dir = loadDir(workingDir->header.startCluster);

    while (dir->header.startCluster != ROOT_DIR_CLUSTER_INDEX) {
        path = "/" + std::string(dir->header.name) + path;
        prevDir = dir;
        dir = loadDir(dir->header.parentStartCluster);
        delete prevDir;
    }
    if (path == "")
        path = "/";
    std::cout << path << '\n';
    delete dir;
}

void FAT32::cd(std::string path) {
    DirEntry_t entry = getEntry(path);
    assert(entry != NULL_DIR_ENTRY && "entry is null");
    assert(entry.directory == true && "entry is not a directory");

    // change the current working directory and delete the old one
    Dir_t *prevWorkingDir = workingDir;
    workingDir = loadDir(entry.startCluster);
    delete prevWorkingDir;
}

void FAT32::rmdir(std::string path) {
    DirEntry_t entry = getEntry(path);
    assert(entry != NULL_DIR_ENTRY && "entry is null");
    assert(entry.directory == true && "entry is not a directory");
    
    std::unique_ptr<Dir_t> dir(loadDir(entry.startCluster));
    assert(dir->header.entryCount == 0 && "dir is not empty");
    std::unique_ptr<Dir_t> parentDir(loadDir(entry.parentStartCluster));
    removeEntryFromDir(parentDir.get(), &entry);
}

void FAT32::in(std::string path) {
    // TODO
}

void FAT32::out(std::string path) {
    // TODO
}

void FAT32::cat(std::string path) {
    // TODO
}

void FAT32::rm(std::string path) {
    // TODO
}