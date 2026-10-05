#include "ps5_apps.h"
#include "launcher.h"
#include "log.h"

#include <stddef.h>
#include <sys/stat.h>

#include <ps5/kernel.h>

extern const unsigned char icon_png_data[];
extern const unsigned int icon_png_data_len;

int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallAll(void *reserved);
int sceAppInstUtilAppUnInstall(const char *title_id);

static int install_title_dir(const char *title_id, const char *dir)
{
    int (*install_fn)(const char *, const char *, void *) = NULL;
    static const char nid[] = "Wudg3Xe3heE";
    uint32_t handle;

    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) == 0)
        install_fn = (void *)kernel_dynlib_resolve(-1, handle, nid);

    if (install_fn) return install_fn(title_id, dir, 0);
    return sceAppInstUtilAppInstallAll(0);
}

int apps_install_launcher(void)
{
    struct stat st;
    int w, r;

    w = launcher_write(LAUNCHER_APP_ROOT, icon_png_data, icon_png_data_len);
    if (w < 0) {
        log_line("launcher: could not write the files under %s", LAUNCHER_APP_ROOT);
        return -1;
    }
    if (w == 0 && stat(LAUNCHER_APP_ROOT "/" LAUNCHER_TITLE_ID "/sce_sys/icon0.png", &st) == 0) {
        log_line("launcher: already installed and up to date");
        return 0;
    }
    if ((r = sceAppInstUtilInitialize()) != 0) {
        log_line("launcher: sceAppInstUtilInitialize -> %#x", (unsigned)r);
        return -1;
    }
    r = install_title_dir(LAUNCHER_TITLE_ID, LAUNCHER_APP_ROOT "/");
    sceAppInstUtilTerminate();
    log_line("launcher: installed %s in the media row -> %#x", LAUNCHER_TITLE_ID, (unsigned)r);
    return r == 0 ? 0 : -1;
}

int apps_remove_launcher(void)
{
    int r = sceAppInstUtilInitialize();

    if (r == 0) {
        r = sceAppInstUtilAppUnInstall(LAUNCHER_TITLE_ID);
        sceAppInstUtilTerminate();
    }
    log_line("launcher: removed %s -> %#x", LAUNCHER_TITLE_ID, (unsigned)r);
    launcher_remove_files(LAUNCHER_APP_ROOT);
    return r == 0 ? 0 : -1;
}
