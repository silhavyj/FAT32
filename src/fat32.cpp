#include <cmath>
#include <memory>
#include <sstream>

#include "fat32.h"
#include "disk.h"

#include "debugger.h"

FAT32 *FAT32::instance = nullptr;

FAT32 *FAT32::getInstance() {
    if (instance == nullptr)
        instance = new FAT32;
    return instance;
}

FAT32::FAT32() : disk(new Disk), workingDir(nullptr) {
    if (disk->diskExists(DISK_FILE_NAME) == false)
        initialize();
    disk->open(DISK_FILE_NAME);
    load();
}

void FAT32::initialize() {
    disk->create(DISK_FILE_NAME, DISK_SIZE);
    disk->open(DISK_FILE_NAME);
    fat.fill(FREE_CLUSTER);
    workingDir = createEmptyDir("/", ROOT_DIR_CLUSTER_INDEX);
    save();
    disk->close();
}

void FAT32::save() {
    saveDir(workingDir);
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
    workingDir = loadDir(ROOT_DIR_CLUSTER_INDEX);
    currPath.push_back(workingDir->header.name);
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

FAT32::DirEntry_t *FAT32::createEntry(Dir_t *dir) {
    assert(dir != nullptr && "dir is null");
    DirEntry_t *entry = new DirEntry_t;
    strcpy(entry->name, dir->header.name);
    entry->startCluster = dir->header.startCluster;
    entry->parentStartCluster = dir->header.parentStartCluster;
    entry->size = sizeof(Dir_t);
    entry->directory = true;
    return entry;
}

bool FAT32::nameTaken(std::string name, Dir_t *dir) {
    assert(dir != nullptr && "dir is null");
    if (dir->header.entryCount == 0)
        return false;
    for (uint32_t i = 0; i < dir->header.entryCount; i++) {
        if (strcmp(dir->entries[i].name, name.c_str()) == 0)
            return true;
    }
    return false;
}

void FAT32::addEntryIntoDir(Dir_t *dir, DirEntry_t *entry) {
    assert(dir != nullptr && "dir is null");
    assert(entry != nullptr && "entry is null");
    assert(!nameTaken(entry->name, dir) && "names is already taken");
    
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

FAT32::DirEntry_t *FAT32::getEntry(std::string path) {
    // TODO
}

void FAT32::mkdir(std::string name) {
    std::unique_ptr<Dir_t> dir(createEmptyDir(name, 0));
    std::unique_ptr<DirEntry_t> entry(createEntry(dir.get()));
    addEntryIntoDir(workingDir, entry.get());
    saveDir(dir.get());
}

void FAT32::ls() {
    printDir(workingDir);
}

void FAT32::pwd() {
    std::stringstream ss;
    for (auto &dir : currPath) {
        ss << dir;
        if (dir != "/") // dont print // (root)
            ss << "/";
    }
    std::string out = ss.str();
    if (out.length() > 1)
        out.pop_back(); // pop out the last '/'
    std::cout << out << "\n";
}

void FAT32::cd(std::string path) {
    assert(path.length() > 0 && "invalid path");
    
    if (path == "..") {
        Dir_t *prevDir = workingDir;
        workingDir = loadDir(workingDir->header.parentStartCluster);
        delete prevDir;

        if (currPath.size() > 1)
            currPath.pop_back();
        return;
    }

    bool absolute = path[0] == '/';
}