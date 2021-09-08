#include <cmath>
#include <memory>
#include <sstream>
#include <iomanip>

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

FAT32::FAT32() : disk(new Disk), workingDirStartCluster(ROOT_DIR_CLUSTER_INDEX) {
    if (disk->diskExists(DISK_FILE_NAME) == false)
        initialize();
    disk->open(DISK_FILE_NAME);
    load();
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
    std::unique_ptr<Dir_t> rootDir(createEmptyDir("/", ROOT_DIR_CLUSTER_INDEX));
    saveDir(rootDir.get());
    saveFat();
    disk->close();
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
    workingDirStartCluster = ROOT_DIR_CLUSTER_INDEX;
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
    saveFat();
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
    
    entry->parentStartCluster = dir->header.startCluster;

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
    if (dir->header.entryCount == 0)
        return;
    std::cout << "type"   << std::setw(LS_SPACING)
              << "size"   << std::setw(LS_SPACING)
              << "parent" << std::setw(LS_SPACING)
              << "start"  << std::setw(LS_SPACING)
              << "name\n";

    for (uint32_t i = 0; i < dir->header.entryCount; i++)
        printDirEntry(&dir->entries[i]);
}

void FAT32::printDirEntry(DirEntry_t *entry) {
    assert(entry != nullptr && "entry is null");
    std::cout << (entry->directory ? "[+]" : "[-]") << std::setw(LS_SPACING)
              << entry->size << std::setw(LS_SPACING)
              << entry->parentStartCluster << std::setw(LS_SPACING)
              << entry->startCluster << std::setw(LS_SPACING)
              <<  entry->name << '\n';
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
    std::unique_ptr<Dir_t> workingDir(loadDir(workingDirStartCluster));
    if (path == ".") {
        return createEntry(workingDir.get());
    }
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
        if (entry == NULL_DIR_ENTRY || (i < (tokens.size() - 1) && entry.directory == false)) {
            delete currDir;
            return NULL_DIR_ENTRY;
        }
        if (i == tokens.size() - 1 && entry.directory == false)
            break;
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
    DirEntry_t entry = getEntry(name);
    assert(entry == NULL_DIR_ENTRY && "name is already taken");
    uint32_t dirStartCluster;

    size_t pos = name.find_last_of('/');
    if (pos == std::string::npos) {
        dirStartCluster = workingDirStartCluster;
    } else {
        std::string parDir = name.substr(0, pos + 1);
        entry = getEntry(parDir);
        assert(entry != NULL_DIR_ENTRY && "target parent dir is NULL");
        assert(entry.directory == true && "cannot insert into a file");
        dirStartCluster = entry.startCluster;
    }

    std::unique_ptr<Dir_t> workingDir(loadDir(dirStartCluster));
    std::unique_ptr<Dir_t> dir(createEmptyDir(name.substr(pos + 1, name.length() - 1 - pos), workingDir->header.startCluster));
    entry = createEntry(dir.get());
    addEntryIntoDir(workingDir.get(), &entry);
    saveDir(dir.get());
}

void FAT32::ls(std::string path) {
    DirEntry_t entry = getEntry(path);
    assert(entry != NULL_DIR_ENTRY && "entry is NULL");

    if (entry.directory) {
        std::unique_ptr<Dir_t> workingDir(loadDir(entry.startCluster));
        printDir(workingDir.get());
    } else {
        printDirEntry(&entry);
    }
}

void FAT32::pwd() {
    std::cout << getPWD() << "\n";
}

std::string FAT32::getPWD() {
    std::unique_ptr<Dir_t> workingDir(loadDir(workingDirStartCluster));
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
    delete dir;
    return path;
}

void FAT32::cd(std::string path) {
    DirEntry_t entry = getEntry(path);
    assert(entry != NULL_DIR_ENTRY && "entry is null");
    assert(entry.directory == true && "entry is not a directory");
    workingDirStartCluster = entry.startCluster;
}

void FAT32::rmdir(std::string path) {
    DirEntry_t entry = getEntry(path);
    assert(entry != NULL_DIR_ENTRY && "entry is null");
    assert(entry.directory == true && "entry is not a directory");
    
    std::unique_ptr<Dir_t> dir(loadDir(entry.startCluster));
    assert(dir->header.entryCount == 0 && "dir is not empty");
    std::unique_ptr<Dir_t> parentDir(loadDir(entry.parentStartCluster));
    removeEntryFromDir(parentDir.get(), &entry);
    freeAllOccupiedClusters(entry.startCluster);
    fat[entry.startCluster] = FREE_CLUSTER;
}

inline uint32_t FAT32::getFileSize(FILE *file) const {
    fseek(file, 0, SEEK_END);
    uint32_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    return fileSize;
}

std::string FAT32::getFileName(std::string path) const {
    if (path.back() == '/')
        path.pop_back();

    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return path;
    return path.substr(pos + 1);
}

FAT32::DirEntry_t FAT32::createFileEntry(Dir_t *dir, const char *name, uint32_t size) {
    assert(existsNumberOfFreeClusters(1) && "not enough free clusters");
    DirEntry_t entry;
    entry.startCluster = getFreeCluster();
    entry.parentStartCluster = dir->header.startCluster;
    entry.directory = false;
    entry.size = size;
    strcpy(entry.name, name);
    return entry;
}

void FAT32::in(std::string path) {
    FILE *file = fopen(path.c_str(), "rb+");
    assert(file != nullptr && "file was not found");

    uint32_t size = getFileSize(file);
    uint32_t clustersNeeded = ceil(static_cast<double>(size) / CLUSTER_SIZE);
    uint32_t lastClusterSize = size % CLUSTER_SIZE;
    std::string name = getFileName(path);
    std::unique_ptr<Dir_t> workingDir(loadDir(workingDirStartCluster));

    assert(getEntry(name, workingDir.get()) == NULL_DIR_ENTRY && "name is already taken");
    // +1 is because of the entry itself, the rest is taken up by the content
    assert(existsNumberOfFreeClusters(1 + clustersNeeded) && "not enough free clusters");

    DirEntry_t entry = createFileEntry(workingDir.get(), name.c_str(), size);
    addEntryIntoDir(workingDir.get(), &entry);

    uint32_t prevCluster;
    uint32_t currCluster = entry.startCluster;
    uint32_t fileOffset = 0;
    std::unique_ptr<char> buffer(new char[CLUSTER_SIZE]);

    // the last cluster needs to be handeled separately 
    for (uint32_t i = 0; i < clustersNeeded - 1; i++) {
        // read one junk of data from the input file
        fseek(file, fileOffset, SEEK_SET);
        fread(buffer.get(), CLUSTER_SIZE, 1, file);
        fileOffset += CLUSTER_SIZE;

        // store the junk into the current cluster
        disk->setAddr(clusterAddr(currCluster));
        disk->write(buffer.get(), CLUSTER_COUNT);

        // move on to the next cluster
        prevCluster = currCluster;
        currCluster = getFreeCluster();
        fat[prevCluster] = currCluster;
    }
    // the very last cluster
    fseek(file, fileOffset, SEEK_SET);
    fread(buffer.get(), lastClusterSize, 1, file);
    disk->setAddr(clusterAddr(currCluster));
    disk->write(buffer.get(), lastClusterSize);

    // the eof cluster
    prevCluster = currCluster;
    currCluster = getFreeCluster();
    fat[prevCluster] = currCluster;
    fat[currCluster] = EOF_CLUSTER;

    fclose(file);
    saveFat();
}

void FAT32::out(std::string path) {
    DirEntry_t entry = getEntry(path);

    assert(entry != NULL_DIR_ENTRY && "file not found");
    assert(entry.directory == false && "target is not a file");

    std::string name = getFileName(path);
    FILE *file = fopen(name.c_str(), "wb");
    assert(file != nullptr && "could not open the output file");

    std::unique_ptr<char> buffer(new char[CLUSTER_SIZE]);
    std::uint32_t currCluster = entry.startCluster;
    std::uint32_t clusterCount = ceil(static_cast<double>(entry.size) / CLUSTER_SIZE);
    uint32_t lastClusterSize = entry.size % CLUSTER_SIZE;

    for (uint32_t i = 0; i < clusterCount - 1; i++) {
        disk->setAddr(clusterAddr(currCluster));
        disk->read(buffer.get(), CLUSTER_SIZE);
        fwrite(buffer.get(), CLUSTER_SIZE, 1, file);
        currCluster = fat[currCluster];
    }

    disk->setAddr(clusterAddr(currCluster));
    disk->read(buffer.get(), lastClusterSize);
    buffer.get()[lastClusterSize] = '\0';
    fwrite(buffer.get(), lastClusterSize, 1, file);

    currCluster = fat[currCluster];
    assert(fat[currCluster] == EOF_CLUSTER && "file was not read properly");
    fclose(file);
}

void FAT32::cat(std::string path) {
    DirEntry_t entry = getEntry(path);
    assert(entry != NULL_DIR_ENTRY && "file not found");
    assert(entry.directory == false && "target is not a file");

    std::unique_ptr<char> buffer(new char[CLUSTER_SIZE]);
    std::uint32_t currCluster = entry.startCluster;
    std::uint32_t clusterCount = ceil(static_cast<double>(entry.size) / CLUSTER_SIZE);
    uint32_t lastClusterSize = entry.size % CLUSTER_SIZE;

    for (uint32_t i = 0; i < clusterCount - 1; i++) {
        disk->setAddr(clusterAddr(currCluster));
        disk->read(buffer.get(), CLUSTER_SIZE);
        std::cout << buffer.get();
        currCluster = fat[currCluster];
    }
    disk->setAddr(clusterAddr(currCluster));
    disk->read(buffer.get(), lastClusterSize);
    buffer.get()[lastClusterSize] = '\0';
    std::cout << buffer.get();

    currCluster = fat[currCluster];
    assert(fat[currCluster] == EOF_CLUSTER && "file was not read properly");
}

void FAT32::rm(std::string path) {
    DirEntry_t entry = getEntry(path);
    assert(entry != NULL_DIR_ENTRY && "file not found");
    assert(entry.directory == false && "target is not a file");
    
    std::unique_ptr<Dir_t> dir(loadDir(entry.parentStartCluster));

    removeEntryFromDir(dir.get(), &entry);
    freeAllOccupiedClusters(entry.startCluster);

    // also we must not forget to delete the very first cluster
    fat[entry.startCluster] = FREE_CLUSTER;
    saveFat();
}

void FAT32::cp(std::string des, std::string src) {
    // TODO
    if (des == src)
        return;

    DirEntry_t file = getEntry(src);
    assert(file != NULL_DIR_ENTRY && "file not found");
    assert(file.directory == false && "cannot move a directory");

    DirEntry_t destination = getEntry(des);
}

void FAT32::mv(std::string des, std::string src) { 
    /* 
       POSSIBLE OPTIONS:
       (1) /data       <- mv into a folder (under the same name) 
       (1) /data/      <- mv into a folder (under the same name) 
       (2) /data/file  <- mv into a folder (under a new name) 
       (3) /data/file1 <- mv into a folder (overwrite an existing file) 
    */
    
    DirEntry_t file = getEntry(src);
    assert(file != NULL_DIR_ENTRY && "file not found");
    assert(file.directory == false && "cannot move a directory");

    // delete the file entirely from its original location
    Dir_t *dir = loadDir(file.parentStartCluster);
    removeEntryFromDir(dir, &file);
    delete dir;

    // load the destination entry
    DirEntry_t destEntry = getEntry(des);
    std::string fileName;
    std::string parentDirPath;
    size_t lastSlashPos;

    if (destEntry == NULL_DIR_ENTRY) {
        // (2)
        fileName = getFileName(des);
        lastSlashPos = des.find_last_of('/');
        DirEntry_t dirEntry;
        if (lastSlashPos == std::string::npos) {
            dirEntry = getEntry(getPWD());
        } else {
            dirEntry = getEntry(des.substr(0, lastSlashPos + 1));
        }
        assert(dirEntry.directory == true && "error when loading target dir");
        
        strcpy(file.name, fileName.c_str());
        dir = loadDir(dirEntry.startCluster);
        addEntryIntoDir(dir, &file);
        delete dir;
    } else if (destEntry.directory == true) {
        // (1)
        fileName = getFileName(src);
        dir = loadDir(destEntry.startCluster);
        DirEntry_t prevEntry = getEntry(fileName, dir);

        // if there's a file with the same name it will be overwritten
        if (prevEntry != NULL_DIR_ENTRY) {
            removeEntryFromDir(dir, &prevEntry);
            freeAllOccupiedClusters(prevEntry.startCluster);

            // also we must not forget to delete the very first cluster
            fat[prevEntry.startCluster] = FREE_CLUSTER;
            saveFat();

            // reload the directory after the file has been deleted
            delete dir;
            dir = loadDir(destEntry.startCluster);  
        }
        // move the file into the new dir
        addEntryIntoDir(dir, &file);
        delete dir;
    } else {
        // (3)
        rm(des);
        dir = loadDir(destEntry.parentStartCluster);
        strcpy(file.name, destEntry.name);
        addEntryIntoDir(dir, &file);
        delete dir;
    }
}

void FAT32::info() {
    uint32_t freeClusters = 0;
    for (uint32_t i = 0; i < CLUSTER_COUNT; i++)
        freeClusters += fat[i] == FREE_CLUSTER;

    size_t totalSize = CLUSTER_COUNT * CLUSTER_SIZE;
    size_t freeSize = freeClusters * CLUSTER_SIZE;

    std::cout << "total clusters   : " << CLUSTER_COUNT << '\n';
    std::cout << "free clusters    : " << freeClusters << '\n';
    std::cout << "cluster size [B] : " << CLUSTER_SIZE << '\n';
    std::cout << "total size   [B] : " << totalSize << '\n';
    std::cout << "free size    [B] : " << freeSize << '\n';
    std::cout << "free size    [%] : " << ((freeSize * 100.0) / totalSize) << '\n';
}