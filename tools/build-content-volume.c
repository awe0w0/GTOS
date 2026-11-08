#define _POSIX_C_SOURCE 200809L

#include "lfs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    SectorSize = 512,
    BlockSize = 4096,
    VolumeOffset = 8 * SectorSize,
    MaximumAssets = 32
};

struct Asset {
    char name[LFS_NAME_MAX + 1];
    FILE* source;
};

static void require(int condition, const char* operation, const char* subject) {
    if (condition) return;
    fprintf(stderr, "content volume: %s: %s\n", operation, subject);
    exit(1);
}

static int block_seek(const struct lfs_config* cfg, lfs_block_t block,
                      lfs_off_t offset, lfs_size_t length) {
    if (block >= cfg->block_count || offset > BlockSize ||
        length > BlockSize - offset) return LFS_ERR_INVAL;
    const off_t address = VolumeOffset + (off_t)block * BlockSize + offset;
    return fseeko(cfg->context, address, SEEK_SET) ? LFS_ERR_IO : 0;
}

static int block_read(const struct lfs_config* cfg, lfs_block_t block,
                      lfs_off_t offset, void* buffer, lfs_size_t length) {
    const int result = block_seek(cfg, block, offset, length);
    if (result) return result;
    return fread(buffer, 1, length, cfg->context) == length ? 0 : LFS_ERR_IO;
}

static int block_write(const struct lfs_config* cfg, lfs_block_t block,
                       lfs_off_t offset, const void* buffer, lfs_size_t length) {
    const int result = block_seek(cfg, block, offset, length);
    if (result) return result;
    return fwrite(buffer, 1, length, cfg->context) == length ? 0 : LFS_ERR_IO;
}

static int block_erase(const struct lfs_config* cfg, lfs_block_t block) {
    unsigned char erased[BlockSize];
    memset(erased, 0xff, sizeof(erased));
    return block_write(cfg, block, 0, erased, sizeof(erased));
}

static int block_sync(const struct lfs_config* cfg) {
    return fflush(cfg->context) ? LFS_ERR_IO : 0;
}

static void asset_path(char* path, const struct Asset* asset) {
    snprintf(path, sizeof("/content/") + LFS_NAME_MAX, "/content/%.*s",
             LFS_NAME_MAX, asset->name);
}

static void copy_asset(lfs_t* store, const struct Asset* asset) {
    char path[sizeof("/content/") + LFS_NAME_MAX];
    asset_path(path, asset);
    lfs_file_t file;
    require(lfs_file_open(store, &file, path,
                          LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL) == 0,
            "cannot create asset", path);
    unsigned char buffer[BlockSize];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), asset->source)) > 0) {
        require(lfs_file_write(store, &file, buffer, count) == (lfs_ssize_t)count,
                "cannot write asset", path);
    }
    require(!ferror(asset->source), "cannot read input", path);
    require(lfs_file_close(store, &file) == 0, "cannot close asset", path);
}

static void verify_asset(lfs_t* store, const struct Asset* asset) {
    char path[sizeof("/content/") + LFS_NAME_MAX];
    asset_path(path, asset);
    lfs_file_t file;
    require(lfs_file_open(store, &file, path, LFS_O_RDONLY) == 0,
            "cannot reopen asset", path);
    require(fseeko(asset->source, 0, SEEK_SET) == 0, "cannot rewind input", path);
    unsigned char expected[BlockSize], actual[BlockSize];
    size_t count;
    do {
        count = fread(expected, 1, sizeof(expected), asset->source);
        const lfs_ssize_t size = lfs_file_read(store, &file, actual, sizeof(actual));
        require(size == (lfs_ssize_t)count && !memcmp(actual, expected, count),
                "asset readback differs", path);
    } while (count);
    require(!ferror(asset->source), "cannot re-read input", path);
    require(lfs_file_close(store, &file) == 0, "cannot close asset", path);
    require(fclose(asset->source) == 0, "cannot close input", path);
    printf("CONTENT ASSET VERIFIED %s\n", path);
}

int main(int argc, char** argv) {
    if (argc < 4 || argc > MaximumAssets + 3) {
        fprintf(stderr, "usage: %s NEW_IMAGE VOLUME_MIB NAME=INPUT ...\n", argv[0]);
        return 2;
    }
    errno = 0;
    char* end;
    const unsigned long mib = strtoul(argv[2], &end, 10);
    require(!errno && end != argv[2] && !*end && mib && mib <= 1024,
            "invalid volume size in MiB", argv[2]);
    struct Asset assets[MaximumAssets];
    const int count = argc - 3;
    for (int i = 0; i < count; ++i) {
        const char* spec = argv[i + 3];
        const char* equal = strchr(spec, '=');
        require(equal && equal != spec && equal[1] && equal - spec <= LFS_NAME_MAX,
                "expected NAME=INPUT", spec);
        memcpy(assets[i].name, spec, (size_t)(equal - spec));
        assets[i].name[equal - spec] = '\0';
        const char* name = assets[i].name;
        require(!strpbrk(name, "/\\") && strcmp(name, ".") && strcmp(name, ".."),
                "invalid asset name", name);
        for (int j = 0; j < i; ++j)
            require(strcmp(name, assets[j].name), "duplicate asset name", name);
        assets[i].source = fopen(equal + 1, "rb");
        struct stat info;
        require(assets[i].source && !fstat(fileno(assets[i].source), &info) &&
                S_ISREG(info.st_mode) && info.st_size <= LFS_FILE_MAX,
                "expected a readable regular input", equal + 1);
    }
    const int descriptor = open(argv[1], O_RDWR | O_CREAT | O_EXCL, 0600);
    require(descriptor >= 0, "cannot create fresh image", argv[1]);
    FILE* image = fdopen(descriptor, "r+b");
    const off_t volume_size = (off_t)mib * 1024 * 1024;
    require(image && ftruncate(descriptor, VolumeOffset + volume_size) == 0,
            "cannot size fresh image", argv[1]);
    const struct lfs_config config = {
        .context = image, .read = block_read, .prog = block_write,
        .erase = block_erase, .sync = block_sync, .read_size = SectorSize,
        .prog_size = SectorSize, .block_size = BlockSize,
        .block_count = (lfs_size_t)(volume_size / BlockSize), .block_cycles = 500,
        .cache_size = SectorSize, .lookahead_size = 128
    };
    lfs_t store;
    require(lfs_format(&store, &config) == 0, "cannot format", argv[1]);
    require(lfs_mount(&store, &config) == 0, "cannot mount", argv[1]);
    require(lfs_mkdir(&store, "/content") == 0, "cannot create /content", argv[1]);
    for (int i = 0; i < count; ++i) copy_asset(&store, &assets[i]);
    require(lfs_unmount(&store) == 0 && fflush(image) == 0 && fsync(descriptor) == 0,
            "cannot flush image", argv[1]);
    require(lfs_mount(&store, &config) == 0, "cannot remount", argv[1]);
    for (int i = 0; i < count; ++i) verify_asset(&store, &assets[i]);
    require(lfs_unmount(&store) == 0 && fclose(image) == 0,
            "cannot close image", argv[1]);
    printf("CONTENT VOLUME VERIFIED assets=%d first_sector=8 sectors=%ld\n",
           count, (long)(volume_size / SectorSize));
    return 0;
}
