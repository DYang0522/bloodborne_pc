// Static AppImage entry point. Clear host-injected libraries before GTK/Python starts.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void drop(const char* name) {
    const char* value = getenv(name);
    if (value && *value) {
        char saved[128];
        snprintf(saved, sizeof saved, "BB_HOST_%s", name);
        setenv(saved, value, 1);
    }
    unsetenv(name);
}

int main(int argc, char** argv) {
    (void)argc;
    static const char* const host_only[] = {
        "LD_PRELOAD", "LD_LIBRARY_PATH", "LD_AUDIT", "GTK_PATH", "GTK_MODULES",
        "GTK_EXE_PREFIX", "GIO_EXTRA_MODULES", "GDK_PIXBUF_MODULEDIR",
        "PYTHONHOME", "PYTHONPATH"
    };
    for (size_t i = 0; i < sizeof host_only / sizeof host_only[0]; ++i) drop(host_only[i]);
    setenv("DISABLE_VK_LAYER_VALVE_steam_overlay_1", "1", 1);
    setenv("DISABLE_VK_LAYER_VALVE_steam_fossilize_1", "1", 1);

    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof executable - 1);
    if (length < 0 || (size_t)length >= sizeof executable - 1) {
        perror("bbport: cannot locate AppRun");
        return 127;
    }
    executable[length] = '\0';
    char* slash = strrchr(executable, '/');
    if (!slash || (size_t)(slash - executable) + sizeof("/AppRun.sh") > sizeof executable) {
        fprintf(stderr, "bbport: invalid AppRun path\n");
        return 127;
    }
    strcpy(slash, "/AppRun.sh");
    argv[0] = executable;
    execv(executable, argv);
    perror("bbport: AppRun.sh");
    return 127;
}
