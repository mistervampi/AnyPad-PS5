#include "ps5_ui.h"
#include "log.h"

int sceSystemServiceLaunchWebBrowser(const char *uri, void *reserved);

int ui_open_browser(const char *url)
{
    int r = sceSystemServiceLaunchWebBrowser(url, 0);

    log_line("browser: %s -> %#x", url, (unsigned)r);
    return r;
}
