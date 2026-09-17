// What com.palm.storage's erase methods erase, and what they must never touch.
//
// HP's service wiped partitions. This port has none: it owns its data directory
// and the files it downloaded into the user's Downloads folder, which is where
// /media/internal points. Everything else on the host belongs to the user.
//
// Verified by mutation: without the check that a path is the port's, with the
// downloads folder itself taken, or with EraseVar taking the download files,
// this turns red.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "erase.h"

namespace fs = std::filesystem;
using storaged::Erase;
using storaged::Layout;

static int failures = 0;

static void check(const char* what, bool ok, const std::string& detail = std::string())
{
    if (!ok)
        ++failures;
    std::printf("%-58s %s %s\n", what, ok ? "ok" : "FAILED", detail.c_str());
}

static void write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

static std::vector<std::string> entriesOf(const std::string& directory)
{
    std::vector<std::string> entries;
    std::error_code code;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, code))
        entries.push_back(entry.path().string());
    return entries;
}

static bool has(const std::vector<std::string>& paths, const fs::path& path)
{
    for (const std::string& candidate : paths)
        if (fs::path(candidate) == path)
            return true;
    return false;
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "storaged-erase-test";
    fs::remove_all(root);

    const fs::path var = root / "var";
    const fs::path downloads = root / "home" / "Downloads";
    const fs::path elsewhere = root / "home" / "Documents";

    write(var / "db" / "main" / "db.sqlite", "database");
    write(var / "luna" / "preferences" / "systemprefs.db", "preferences");
    write(var / "palm" / "data" / "com.palm.downloadmanager" / "history.json", "");
    write(downloads / "pdf_sample.pdf", "downloaded");
    write(downloads / "holiday.jpg", "the user's own");
    write(downloads / "pictures" / "a.jpg", "the user's own folder");
    write(elsewhere / "taxes.ods", "the user's own");

    const Layout layout{ var.string(), downloads.string(),
                         (var / "palm" / "data" / "com.palm.downloadmanager" / "history.json").string() };

    // The history as com.palm.downloadmanager writes it, with a file this port
    // downloaded, one that is gone, and one a caller aimed outside.
    const std::string history = R"({"nextTicket":4,"entries":[
        {"ticket":1,"owner":"com.palm.app.browser","state":"completed",
         "record":{"ticket":1,"target":")" + (downloads / "pdf_sample.pdf").string() + R"("}},
        {"ticket":2,"owner":"com.palm.app.browser","state":"completed",
         "record":{"ticket":2,"target":")" + (downloads / "gone.zip").string() + R"("}},
        {"ticket":3,"owner":"com.palm.app.browser","state":"completed",
         "record":{"ticket":3,"target":")" + (elsewhere / "taxes.ods").string() + R"("}}]})";

    const std::vector<std::string> downloaded = storaged::downloadedFiles(history);
    check("the history says which files this port downloaded", downloaded.size() == 3,
          std::to_string(downloaded.size()));
    check("a history that is not JSON says nothing", storaged::downloadedFiles("{not json").empty());
    check("no history at all says nothing", storaged::downloadedFiles("").empty());

    // What is the port's, and what is the user's.
    check("the data directory is the port's", storaged::ours((var / "db").string(), layout));
    check("a file it downloaded is the port's", storaged::ours((downloads / "pdf_sample.pdf").string(), layout));
    check("the downloads folder itself is not", !storaged::ours(downloads.string(), layout));
    check("nor a folder in it", !storaged::ours((downloads / "pictures").string(), layout));
    check("nor anything else of the user's", !storaged::ours((elsewhere / "taxes.ods").string(), layout));
    check("nor the host's own directories", !storaged::ours("/etc", layout) && !storaged::ours("/home", layout));
    check("and not a way out of the downloads folder",
          !storaged::ours((downloads / ".." / ".." / "etc").string(), layout));

    // EraseVar: the data directory's contents, nothing downloaded.
    const std::vector<std::string> varOnly =
        storaged::erasePaths(Erase::Var, layout, entriesOf(var.string()), history);
    check("EraseVar takes the data directory's contents", varOnly.size() == 3, std::to_string(varOnly.size()));
    check("including the database", has(varOnly, var / "db"));
    check("but not the data directory itself", !has(varOnly, var));
    check("and nothing that was downloaded", !has(varOnly, downloads / "pdf_sample.pdf"));

    // EraseMedia: what this port downloaded, and its record of it.
    const std::vector<std::string> media =
        storaged::erasePaths(Erase::Media, layout, entriesOf(var.string()), history);
    check("EraseMedia takes the file this port downloaded", has(media, downloads / "pdf_sample.pdf"));
    check("and the history that named it", has(media, fs::path(layout.historyFile)));
    check("not the user's own file in the same folder", !has(media, downloads / "holiday.jpg"));
    check("not a file a caller claimed outside it", !has(media, elsewhere / "taxes.ods"));
    check("and not the data directory", !has(media, var / "db"));

    // EraseAll and Wipe: both, and nothing more.
    const std::vector<std::string> all =
        storaged::erasePaths(Erase::Both, layout, entriesOf(var.string()), history);
    check("EraseAll takes both", has(all, var / "db") && has(all, downloads / "pdf_sample.pdf"));
    check("and still nothing of the user's",
          !has(all, downloads / "holiday.jpg") && !has(all, elsewhere / "taxes.ods")
              && !has(all, downloads / "pictures"));

    Erase erase = Erase::Var;
    check("Wipe erases what EraseAll erases", storaged::eraseOf("Wipe", &erase) && erase == Erase::Both);
    check("EraseVar and EraseMedia are their own", storaged::eraseOf("EraseVar", &erase) && erase == Erase::Var
              && storaged::eraseOf("EraseMedia", &erase) && erase == Erase::Media);
    check("anything else is not an erase", !storaged::eraseOf("enterMSM", &erase));
    check("and a recorded erase is read back as itself",
          std::string(storaged::methodName(Erase::Both)) == "EraseAll"
              && std::string(storaged::methodName(Erase::Var)) == "EraseVar"
              && std::string(storaged::methodName(Erase::Media)) == "EraseMedia");

    fs::remove_all(root);
    return failures == 0 ? 0 : 1;
}
