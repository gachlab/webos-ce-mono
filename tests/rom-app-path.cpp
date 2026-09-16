// Which apps came with the system, under the rootfs this port runs from.
//
// HP took an app for a system one when its folder started with "/usr". Here
// every folder starts with the rootfs prefix, so every app, the system's own
// included, was taken for one the user installed and could remove.
#include "RomAppPath.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

int main()
{
    const std::string root = "/home/me/webos-ce/build/rootfs";

    std::printf("the prefix\n");
    check(rootfsPrefix(root + "/usr/lib/luna/system/luna-systemui") == root, "from the system UI path");
    check(rootfsPrefix(root + "/usr/lib/luna/system/luna-systemui/") == root, "with a trailing slash too");
    check(rootfsPrefix("/usr/lib/luna/system/luna-systemui/").empty(), "none on a device");
    check(rootfsPrefix("/opt/somewhere/else").empty(), "none from a path that is not the system UI's");

    std::printf("system apps\n");
    check(isRomAppPath(root + "/usr/palm/applications/com.palm.app.wifi", root), "the system's apps are in ROM");
    check(isRomAppPath(root + "/usr/lib/luna/applications/com.palm.app.phone", root), "wherever under /usr");
    check(!isRomAppPath(root + "/var/usr/palm/applications/com.example.app", root), "an app under /var is not");
    check(!isRomAppPath(root + "/media/cryptofs/apps/usr/palm/applications/com.example.app", root),
          "nor a downloaded one, though its path has /usr in it");
    check(!isRomAppPath(root + "/usrlocal/app", root), "nor a folder that only starts with the letters");
    check(!isRomAppPath("/usr/palm/applications/com.palm.app.wifi", root),
          "nor a path outside the rootfs");

    std::printf("on a device\n");
    check(isRomAppPath("/usr/palm/applications/com.palm.app.email", ""), "/usr is ROM, as HP had it");
    check(!isRomAppPath("/media/cryptofs/apps/usr/palm/applications/com.example.app", ""),
          "and an installed app is not");

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
