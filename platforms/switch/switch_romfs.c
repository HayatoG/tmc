/*
 * Switch romfs support: mount the .nro's embedded read-only filesystem and,
 * on a fresh install, seed sdmc:/switch/tmc/assets from the bundled cache so
 * the user only needs to drop baserom.gba in /switch/tmc/.
 *
 * Kept in its own TU because <switch.h> defines u8/u16/u32 etc. that clash
 * with the GBA game headers used elsewhere.
 */
#include <switch.h>

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

void Port_Switch_InitRomfs(void) {
    /* Harmless if the .nro was built without a romfs section. */
    romfsInit();
}

static void CopyFile(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) {
        return;
    }
    FILE* out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return;
    }
    static char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(out);
    fclose(in);
}

/*
 * If sdmc:/switch/tmc/assets/ has no build-state marker, populate it from the
 * .nro's embedded romfs:/assets (a flat dir of *.pak + *.json), plus the
 * romfs:/sounds.json. One-time; subsequent boots see the marker and skip.
 * cwd is already sdmc:/switch/tmc (port_main chdir'd before calling this).
 */
void Port_Switch_BootstrapAssetsFromRomfs(void) {
    struct stat st;
    if (stat("assets/.asset_build_state.json", &st) == 0) {
        return; /* assets already present on the SD card */
    }

    DIR* d = opendir("romfs:/assets");
    if (!d) {
        return; /* .nro has no bundled cache — fall back to SD/extraction */
    }

    mkdir("assets", 0777);

    struct dirent* e;
    char src[600];
    char dst[600];
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        snprintf(src, sizeof src, "romfs:/assets/%s", e->d_name);
        snprintf(dst, sizeof dst, "assets/%s", e->d_name);
        CopyFile(src, dst);
    }
    closedir(d);

    CopyFile("romfs:/sounds.json", "sounds.json");
}
