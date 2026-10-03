// What com.palm.audio puts on the bus, and the policy behind it, checked without
// a sound server or an ls-hubd.
//
// NativeAlertManager (the volume HUD) and DisplayManager each drop a status
// payload that is missing a field they read -- a wrong name here is a HUD that
// never draws, not a wrong number -- so the fields are checked as strings, the
// way power-state.cpp checks com.palm.power's. The scenario names are matched
// with == on the consumer side, so they are pinned as exact strings too.
//
// MEASURED against the two consumers still in the tree:
//   NativeAlertManager::cbAudioControlsChanged pops the HUD only when "changed"
//   contains "volume" and not "scenario", reading action/scenario/volume/
//   "ringer switch".
//   DisplayManager::audiodCallback VALIDATE_SCHEMA-requires action(string),
//   scenario(string), active(boolean) on phone/status.
#include "audio_contract.h"
#include "system_sounds.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-70s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

int main()
{
    using namespace AudioContract;

    std::printf("the fields the HUD reads from a volume change\n");
    {
        // A media volume change: the shape NativeAlertManager pops the HUD on.
        const std::string p = statusPayload(defaultScenario(Category::Media), 56, false,
                                            /*withChangedVolume=*/true, /*withReturnValue=*/false);
        check(contains(p, "\"volume\":56"), "carries the volume as an int");
        check(contains(p, "\"scenario\":\"media_back_speaker\""), "carries a scenario name matched with ==");
        check(contains(p, "\"changed\":[\"volume\"]"), "changed is [\"volume\"], which pops the HUD");
        check(!contains(p, "\"scenario\"]"), "changed does not also carry scenario, which would suppress the HUD");
        check(contains(p, "\"action\":\"changed\""), "action is a string, and is not \"requested\"");
        check(!contains(p, "\"action\":\"requested\""), "action is never requested (the one value the HUD ignores)");
        check(contains(p, "\"ringer switch\":"), "carries ringer switch (note the space, as audiod spelled it)");
    }

    std::printf("\na state carry is not a HUD trigger\n");
    {
        // A fresh subscriber learning the level, or the host mixer moving our
        // node: state, not a key press. Must NOT pop the HUD.
        const std::string p = statusPayload(defaultScenario(Category::System), 40, false,
                                            /*withChangedVolume=*/false, /*withReturnValue=*/true);
        check(contains(p, "\"changed\":[]"), "a carry has an empty changed array");
        check(!contains(p, "\"volume\"]"), "a carry does not mark volume as changed");
        check(contains(p, "\"returnValue\":true"), "a direct reply carries returnValue");
    }

    std::printf("\nthe three fields DisplayManager's schema requires on phone/status\n");
    {
        const std::string p = statusPayload(defaultScenario(Category::Phone), 70, false,
                                            /*withChangedVolume=*/false, /*withReturnValue=*/false);
        check(contains(p, "\"action\":"), "action present (REQUIRED string)");
        check(contains(p, "\"scenario\":"), "scenario present (REQUIRED string)");
        check(contains(p, "\"active\":"), "active present (REQUIRED boolean)");
        check(!contains(p, "returnValue"), "an announcement carries no returnValue");
    }

    std::printf("\nmute shows in both the mute flag and the ringer switch\n");
    {
        const std::string muted = statusPayload(defaultScenario(Category::System), 30, true, true, false);
        check(contains(muted, "\"muted\":true"), "muted true is reported");
        // ringer switch true means not-silenced, so muted flips it to false.
        check(contains(muted, "\"ringer switch\":false"), "muted silences the ringer switch");
        const std::string unmuted = statusPayload(defaultScenario(Category::System), 30, false, true, false);
        check(contains(unmuted, "\"ringer switch\":true"), "unmuted leaves the ringer switch not-silenced");
    }

    std::printf("\nthe scenario name each category announces\n");
    {
        check(std::string(defaultScenario(Category::System)) == "system_default", "system -> system_default");
        check(std::string(defaultScenario(Category::Media)) == "media_back_speaker", "media -> media_back_speaker");
        check(std::string(defaultScenario(Category::Ringtone)) == "ringtone_default", "ringtone -> ringtone_default");
        check(std::string(categoryPath(Category::Phone)) == "phone", "phone category path is 'phone'");
    }

    std::printf("\nvolume keys: only 'up' is published, so a tap is one step\n");
    {
        check(applyVolumeKey(50, "volume_up") == 50 + volumeStep(), "volume_up adds one step");
        check(applyVolumeKey(50, "volume_down") == 50 - volumeStep(), "volume_down subtracts one step");
        check(applyVolumeKey(100, "volume_up") == 100, "volume_up clamps at 100");
        check(applyVolumeKey(0, "volume_down") == 0, "volume_down clamps at 0");
        check(applyVolumeKey(50, "mute") == 50, "an unknown key does nothing");
    }

    std::printf("\nthe one place webOS's 0..100 meets PipeWire's 0.0..1.0\n");
    {
        check(volumeToPercent(0.0) == 0, "0.0 -> 0%");
        check(volumeToPercent(1.0) == 100, "1.0 -> 100%");
        check(volumeToPercent(0.5) == 50, "0.5 -> 50%");
        check(volumeToPercent(1.5) == 100, "over-unity clamps to 100%");
        check(volumeToPercent(-0.2) == 0, "negative clamps to 0%");
        check(percentToVolume(50) > 0.49 && percentToVolume(50) < 0.51, "50% -> ~0.5");
        check(percentToVolume(150) == 1.0, "over 100% clamps to 1.0");
    }

    std::printf("\nsystem sounds: a name plays only if its file ships\n");
    {
        check(SystemSounds::fileForName("alert") == "alert.wav", "alert -> alert.wav (a file that ships)");
        check(SystemSounds::fileForName("notification") == "notification.wav", "notification -> notification.wav");
        check(SystemSounds::fileForName("ringtone") == "ringtone.mp3", "ringtone -> ringtone.mp3");
        // The UI-click set HP never shipped in the CE drop: no file, play nothing.
        check(SystemSounds::fileForName("key").empty(), "key has no shipped file (plays nothing, not a guess)");
        check(SystemSounds::fileForName("space").empty(), "space has no shipped file");
        check(SystemSounds::fileForName("carddrag").empty(), "carddrag has no shipped file");
        check(SystemSounds::pathForName("/usr/palm/sounds", "alert") == "/usr/palm/sounds/alert.wav",
              "a shipped name resolves to an absolute path under the sounds dir");
        check(SystemSounds::pathForName("/usr/palm/sounds", "key").empty(),
              "an unshipped name resolves to no path at all");
    }

    std::printf("\nescaping, for the device names the output chooser will route\n");
    {
        check(jsonEscape("Built-in Audio") == "Built-in Audio", "a plain name is unchanged");
        check(jsonEscape("He said \"hi\"") == "He said \\\"hi\\\"", "a quote is escaped");
        check(jsonEscape("a\\b") == "a\\\\b", "a backslash is escaped");
    }

    std::printf("\nthe output chooser's list entries\n");
    {
        const std::string e = outputEntry("1571", "Built-in Audio Analog Stereo", true);
        check(contains(e, "\"id\":\"1571\""), "carries the opaque id the chooser sends back");
        check(contains(e, "\"name\":\"Built-in Audio Analog Stereo\""), "carries the real device name");
        check(contains(e, "\"current\":true"), "marks the output webOS is on now");
        const std::string other = outputEntry("1568", "HDMI 3", false);
        check(contains(other, "\"current\":false"), "an output that is not current is marked false");
        // The names come from the host and can carry a quote; it must not break
        // the payload the chooser parses.
        const std::string quoted = outputEntry("92557", "J%27s \"Headset\"", false);
        check(contains(quoted, "\\\"Headset\\\""), "a device name with a quote is escaped in the entry");
    }

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
