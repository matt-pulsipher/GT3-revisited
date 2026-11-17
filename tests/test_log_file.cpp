// The log file's name hiding and size limits (base/log_file.h). No game data.

#include "test_framework.h"

#include "base/log_file.h"

#include <string>

using namespace gt3r;

TEST(log_redact_home_and_user) {
    const LogRedactor win("C:\\Users\\Ana", "Ana");
    CHECK(win.apply("Disc: C:\\Users\\Ana\\Downloads\\gt3.iso") == "Disc: ~\\Downloads\\gt3.iso");
    // Case and separators don't matter on the way in; the rest of the path stays as written.
    CHECK(win.apply("c:\\users/ANA/Documents/x") == "~/Documents/x");
    CHECK(win.apply("C:\\Users\\Ana") == "~");
    // A longer name isn't the home folder.
    CHECK(win.apply("C:\\Users\\Anabel\\x") == "C:\\Users\\Anabel\\x");
    // The name as a whole folder name elsewhere.
    CHECK(win.apply("D:\\ana\\Games\\gt3.iso") == "D:\\<user>\\Games\\gt3.iso");
    CHECK(win.apply("D:\\Games\\Ana") == "D:\\Games\\<user>");
    CHECK(win.apply("'E:\\Ana' opened") == "'E:\\<user>' opened");
    // Not as part of a name, nor as a word outside a path.
    CHECK(win.apply("D:\\Banana\\x D:\\Ana.bak\\y") == "D:\\Banana\\x D:\\Ana.bak\\y");
    CHECK(win.apply("Ana's car") == "Ana's car");

    const LogRedactor nix("/home/ana/", "ana");
    CHECK(nix.apply("User data: /home/ana/.local/share/GT3Revisited/GT3Revisited/") ==
          "User data: ~/.local/share/GT3Revisited/GT3Revisited/");
    CHECK(nix.apply("/Volumes/ana/gt3.iso and /home/ana") == "/Volumes/<user>/gt3.iso and ~");

    // Non-ASCII names match byte for byte.
    const LogRedactor utf("C:\\Users\\Jo\xC3\xA3o", "Jo\xC3\xA3o");
    CHECK(utf.apply("C:\\Users\\Jo\xC3\xA3o\\x") == "~\\x");
    CHECK(utf.apply("D:\\Jo\xC3\xA3o\\x") == "D:\\<user>\\x");
    CHECK(utf.apply("D:\\Jo\xC3\xA3ozinho\\x") == "D:\\Jo\xC3\xA3ozinho\\x");
}

TEST(log_redact_short_or_empty) {
    // Names shorter than 3 characters would hit ordinary folders: only the home folder goes.
    const LogRedactor r("/home/al", "al");
    CHECK(r.apply("/home/al/x /data/al/y") == "~/x /data/al/y");
    // No home folder (a phone): nothing changes.
    const LogRedactor none("", "");
    CHECK(none.empty());
    CHECK(none.apply("/data/user/0/org.gt3revisited.gt3r/files/") == "/data/user/0/org.gt3revisited.gt3r/files/");
    // A root alone would hide every path.
    CHECK(LogRedactor("/", "").empty());
}

TEST(log_limiter_repeats) {
    LogLimiter lim;
    auto feed = [&](LogLevel lv, const std::string& text) {
        return lim.feed(lv, text, log_file::format_line(1.0, lv, text));
    };
    CHECK(feed(LogLevel::Info, "a") == log_file::format_line(1.0, LogLevel::Info, "a"));
    CHECK(feed(LogLevel::Info, "a").empty());
    CHECK(feed(LogLevel::Warn, "a").empty());
    const std::string next = feed(LogLevel::Info, "b");
    CHECK(next == "        (repeated 2 more times)\n" + log_file::format_line(1.0, LogLevel::Info, "b"));
    CHECK(feed(LogLevel::Info, "b").empty());
    CHECK(lim.finish() == "        (repeated 1 more time)\n");
    CHECK(lim.finish().empty());
}

TEST(log_limiter_size_limits) {
    LogLimiter lim(100, 400);
    const std::string info(30, 'i'), warn(30, 'w');
    int n = 0;
    auto feed = [&](LogLevel lv, const std::string& text) {
        const std::string t = text + std::to_string(n++); // no repeats
        return lim.feed(lv, t, t + "\n");
    };
    for (int i = 0; i < 3; ++i) CHECK(!feed(LogLevel::Info, info).empty()); // 96 bytes
    const std::string trimmed = feed(LogLevel::Info, info);
    CHECK(trimmed.find("only warnings and errors") != std::string::npos);
    CHECK(trimmed.find(info) == std::string::npos);
    CHECK(feed(LogLevel::Info, info).empty());     // dropped, no second note
    CHECK(!feed(LogLevel::Warn, warn).empty());     // warnings still go in
    CHECK(!feed(LogLevel::Error, warn).empty());
    std::string last;
    while ((last = feed(LogLevel::Error, warn)).find("full") == std::string::npos) CHECK(!last.empty());
    CHECK(feed(LogLevel::Error, warn).empty()); // nothing after the full note
    CHECK(lim.written() < 400 + 100);
}

TEST(log_format_line) {
    CHECK(log_file::format_line(12.3456, LogLevel::Warn, "Could not read x\n") == "   12.346 WARN  Could not read x\n");
    CHECK(log_file::format_line(0, LogLevel::Error, "e") == "    0.000 ERROR e\n");
}
