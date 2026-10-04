#include <QtTest>
#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QString>

#include <cstddef>
#include <string>
#include <utility>

#include "audio/Playlist.hpp"
#include "util/Color.hpp"
#include "util/FileUtils.hpp"

using namespace vc;

namespace {

// ── Helpers that measure rather than spot-check ───────────────────────────────

/// Length in bytes of the UTF-8 sequence starting at `text[offset]`.
///
/// Returns 0 for a truncated or malformed sequence, which is the point: a
/// caller asking this about a string sanitizeFilename() produced must never be
/// told "fine" for a split sequence.
std::size_t utf8SequenceLengthAt(const std::string& text, std::size_t offset) {
    if (offset >= text.size()) return 0;
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    std::size_t length = 0;
    if (lead < 0x80) {
        length = 1;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
    } else {
        return 0; // a continuation byte in lead position, or an invalid lead
    }
    if (offset + length > text.size()) return 0; // truncated
    for (std::size_t i = 1; i < length; ++i) {
        if ((static_cast<unsigned char>(text[offset + i]) & 0xC0) != 0x80) {
            return 0; // a missing continuation byte
        }
    }
    return length;
}

/// True when `text` is well-formed UTF-8 with no truncated or split sequence.
///
/// Written from the spec rather than delegating to QString on purpose: a
/// decoder REPAIRS a split sequence into U+FFFD and carries on, so it would
/// report "fine" for exactly the defect this suite exists to catch. The Qt
/// decode is asserted separately, and it is the one that proves the repair.
bool isWellFormedUtf8(const std::string& text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t length = utf8SequenceLengthAt(text, offset);
        if (length == 0) return false;
        offset += length;
    }
    return true;
}

/// Number of UTF-16 code units Qt decodes `text` into.
///
/// UTF-16 UNITS, not characters, and the distinction is load-bearing rather
/// than pedantic: Qt 6's QString is UTF-16, so a non-BMP character such as
/// U+1F600 occupies TWO units. 63 four-byte sequences therefore decode to 126
/// units, and a test that "fixed" that number would be measuring QString's
/// storage rather than this suite's subject. `decodedIsClean` is the assertion
/// that actually detects a split sequence.
qsizetype decodedUtf16Units(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())).size();
}

/// True when Qt's decoder had to REPAIR anything, i.e. the byte string was not
/// well-formed UTF-8. A split sequence survives a decode as U+FFFD, so this is
/// the decoder-level counterpart of isWellFormedUtf8() and the two together say
/// "no sequence was cut, and the decoder agreed".
bool decodedIsClean(const std::string& text) {
    return !QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()))
                 .contains(QChar(0xFFFD));
}

/// Does `dir / name` stay inside `dir`? The structural form of "sanitizeFilename
/// returned something that cannot traverse", checked with std::filesystem
/// rather than by searching for "../" -- because a name can leave its directory
/// without ever containing those three characters.
bool staysInside(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(dir / name, ec);
    if (ec) return false;
    const fs::path base = fs::weakly_canonical(dir, ec);
    if (ec) return false;
    return resolved.string().starts_with(base.string());
}

/// Printable, exact, and free of a trailing NUL so it is safe as a QVERIFY2
/// message. '#', '-', '_', '.', '(' , ')' and spaces are left readable; every
/// other byte is percent-encoded, which is how a multi-byte character is
/// compared exactly instead of through a lossy toStdString().
QByteArray show(const std::string& text) {
    return QByteArray::fromStdString(text).toPercentEncoding(" #-_.()");
}

/// A path for the name `n` 'a' characters. sanitizeFilename is the identity on
/// it (ASCII survives NFC unchanged and 'a' is not forbidden), so a test can
/// state an exact expected length.
std::string asciiName(std::size_t n) {
    return std::string(n, 'a');
}

} // namespace

class TestPathSafety : public QObject {
    Q_OBJECT

private slots:

    // ── Length budget ────────────────────────────────────────────────────────

    /// Asserted with exact byte counts, never with "it got shorter". A
    /// length-only assertion passes on every version of the bug it guards.
    void filenameLengthIsCappedAtTheBudgetIncludingTheExtension() {
        const std::size_t budget = file::maxFilenameBytes;
        QCOMPARE(budget, std::size_t{255});

        // budget - 1 and budget: nothing to trim, so the name survives whole.
        QCOMPARE(file::sanitizeFilename(asciiName(budget - 1)).size(), budget - 1);
        QCOMPARE(file::sanitizeFilename(asciiName(budget)).size(), budget);

        // budget + 1: capped, not refused. Refusing the download because a
        // title is long is the worse outcome, and it is the one that was
        // waiting behind ENAMETOOLONG at open().
        const std::string over = file::sanitizeFilename(asciiName(budget + 1));
        QCOMPARE(over.size(), budget);
        QCOMPARE(over, asciiName(budget));

        // The extension is INSIDE the budget, not on top of it: 255 bytes of
        // stem plus ".mp3" is a 259-byte name before the cap.
        const std::string withExt = file::sanitizeFilename(asciiName(budget) + ".mp3");
        QCOMPARE(withExt.size(), budget);
        QCOMPARE(withExt.size() - 4, budget - 4);
        QVERIFY2(withExt.ends_with(".mp3"), show(withExt).constData());

        // A suffix this long is not an extension, it is a name containing dots.
        // Folding it back into the stem keeps the total bounded instead of
        // producing an over-budget result.
        const std::string silly =
            file::sanitizeFilename(std::string(300, 'a') + "." + std::string(300, 'b'));
        QVERIFY2(silly.size() <= budget, show(silly).constData());
        QCOMPARE(silly.size(), budget);
    }

    /// A byte-budget cut that lands mid-sequence produces not a shorter name but
    /// an INVALID one: the next decode turns the orphan bytes into U+FFFD.
    ///
    /// Every expectation below is a division with a remainder, which is the
    /// whole point -- 255 is not divisible by 2, 3 or 4, so each case lands a
    /// different number of bytes short of the budget. That shortfall is
    /// asserted, not tolerated.
    void truncationNeverSplitsAUtf8Sequence() {
        // 200 x U+00E9 = 400 bytes of 2-byte sequences. 255 holds 127, and the
        // last byte of the budget goes unused rather than half a character.
        std::string twoByte;
        for (int i = 0; i < 200; ++i) twoByte += "\xC3\xA9";
        const std::string out2 = file::sanitizeFilename(twoByte);
        QCOMPARE(out2.size(), std::size_t{254});
        QCOMPARE(decodedUtf16Units(out2), qsizetype{127});
        QVERIFY2(isWellFormedUtf8(out2), show(out2).constData());
        QVERIFY2(decodedIsClean(out2), show(out2).constData());

        // 100 x U+20AC = 300 bytes of 3-byte sequences. 255 holds 85 exactly.
        std::string threeByte;
        for (int i = 0; i < 100; ++i) threeByte += "\xE2\x82\xAC";
        const std::string out3 = file::sanitizeFilename(threeByte);
        QCOMPARE(out3.size(), std::size_t{255});
        QCOMPARE(decodedUtf16Units(out3), qsizetype{85});
        QVERIFY2(isWellFormedUtf8(out3), show(out3).constData());
        QVERIFY2(decodedIsClean(out3), show(out3).constData());

        // 100 x U+1F600 = 400 bytes of 4-byte sequences. 255 holds 63, which is
        // 126 UTF-16 units because a non-BMP character is a surrogate pair.
        std::string fourByte;
        for (int i = 0; i < 100; ++i) fourByte += "\xF0\x9F\x98\x80";
        const std::string out4 = file::sanitizeFilename(fourByte);
        QCOMPARE(out4.size(), std::size_t{252});
        QCOMPARE(decodedUtf16Units(out4), qsizetype{126});
        QVERIFY2(isWellFormedUtf8(out4), show(out4).constData());
        QVERIFY2(decodedIsClean(out4), show(out4).constData());

        // The cut is lossless in the sense that matters: what survives is a
        // prefix of the input, not a mangled version of it.
        QCOMPARE(out2, twoByte.substr(0, 254));
        QCOMPARE(out3, threeByte.substr(0, 255));
        QCOMPARE(out4, fourByte.substr(0, 252));
    }

    // ── Unicode normalization ───────────────────────────────────────────────

    /// APFS and NTFS both store filenames in a normalized form, so a composed
    /// and a decomposed spelling of one visible title are the SAME FILE there.
    /// Byte-identical output is therefore not cosmetic: it is what stops the
    /// second download from silently overwriting the first.
    void composedAndDecomposedSpellingsProduceTheSameName() {
        const std::string composed = "caf\xC3\xA9.mp3";    // U+00E9
        const std::string decomposed = "cafe\xCC\x81.mp3";  // U+0065 U+0301

        QVERIFY2(composed != decomposed,
                 "the inputs must differ as bytes or this test means nothing");

        const std::string a = file::sanitizeFilename(composed);
        const std::string b = file::sanitizeFilename(decomposed);

        QVERIFY2(a == b, show(a).constData());
        // Exact value, not merely equal: NFC, so the composed form survives and
        // the combining mark is folded in rather than dropped or reordered.
        QCOMPARE(show(a), QByteArray("caf%C3%A9.mp3"));
        QCOMPARE(a.size(), std::size_t{9});

        // Normalizing an already-normalized name is a no-op, which is what
        // makes the result stable across repeated calls and across a save/load.
        QCOMPARE(file::sanitizeFilename(a), a);
    }

    /// Normalization must not become a collision. Two titles differing ONLY by a
    /// combining mark are different visible strings and must stay different
    /// files; a normalizer that dropped combining marks would merge them.
    void titlesDifferingByACombiningMarkDoNotCollide() {
        const std::string outPlain = file::sanitizeFilename("a.mp3");
        const std::string outAccented = file::sanitizeFilename("a\xCC\x81.mp3");

        QVERIFY2(outPlain != outAccented, "the combining mark was dropped");
        QCOMPARE(show(outAccented), QByteArray("%C3%A1.mp3"));
        QCOMPARE(outPlain.size(), std::size_t{5});
        QCOMPARE(outAccented.size(), std::size_t{6});

        // A precomposed spelling of the same character is the same file, which
        // is the property the previous test asserts from the other direction.
        QCOMPARE(file::sanitizeFilename("a\xCC\x81.mp3"),
                 file::sanitizeFilename("\xC3\xA1.mp3"));
    }

    // ── Windows reserved names ──────────────────────────────────────────────

    /// "CON .mp3" is the case that used to slip through. Trimming only the end
    /// of the WHOLE string cannot see it, because the last character is '3';
    /// Win32 strips trailing spaces from a component before matching its
    /// reserved list, so the stem "CON " reaches the filesystem as "CON".
    void windowsReservedNamesAreRefusedInEveryCaseAndSpelling() {
        const std::pair<const char*, const char*> cases[] = {
            {"CON",       "_CON"},
            {"con",       "_con"},
            {"Con",       "_Con"},
            {"cOn",       "_cOn"},
            {"CON.mp3",   "_CON.mp3"},
            {"CON.txt",   "_CON.txt"},
            {"CON .mp3",  "_CON.mp3"},   // trailing space in the STEM
            {"con  .mp3", "_con.mp3"},   // two of them
            {"LPT1.mp3",  "_LPT1.mp3"},
            {"lpt1.mp3",  "_lpt1.mp3"},
            {"Lpt1.wav",  "_Lpt1.wav"},
            {"LPT1 .mp3", "_LPT1.mp3"},
            {"aux.mp3",   "_aux.mp3"},
            {"AUX.mp3",   "_AUX.mp3"},
            {"nul",       "_nul"},
            {"PRN  .flac", "_PRN.flac"},
            {"COM9",      "_COM9"},
            {"com1.wav",  "_com1.wav"},
        };
        for (const auto& [input, expected] : cases) {
            const std::string got = file::sanitizeFilename(input);
            QVERIFY2(got == expected,
                     qPrintable(QStringLiteral("%1 -> \"%2\", expected \"%3\"")
                                    .arg(QLatin1String(input), QString::fromStdString(got),
                                         QLatin1String(expected))));
        }
    }

    /// The reserved check runs AFTER truncation and AFTER the stem trim, not
    /// before, and this is why. Neither step can create a reserved name on its
    /// own -- but the pair can, and each of the two ways is asserted separately
    /// so a future reordering fails loudly rather than quietly.
    void truncationCannotManufactureAReservedName() {
        // (a) Truncation cuts the padding off, and the stem trim that follows
        // exposes "LPT1". Checked before either step, this name is "LPT1" plus
        // 400 spaces plus an extension, which is not reserved.
        std::string lpt = "LPT1";
        lpt.append(400, ' ');
        lpt += ".mp3";
        const std::string got = file::sanitizeFilename(lpt);
        QCOMPARE(got, std::string("_LPT1.mp3"));
        QCOMPARE(got.size(), std::size_t{9});

        // (b) Truncation lands EXACTLY on a reserved name. The extension has to
        // be long enough to leave the stem a four-byte budget, because a stem
        // is never truncated to less than 240 bytes otherwise.
        std::string com = "COM1";
        com.append(300, 'y');
        com += ".";
        com.append(250, 'z'); // 251-byte extension => 4-byte stem budget
        const std::string got2 = file::sanitizeFilename(com);
        // The guard byte is the only thing that can push a result over budget,
        // so it is paid for out of the stem -- 5 bytes of "_COM1" would make
        // 256, so the last character is dropped and the total is exactly 255.
        QCOMPARE(got2.size(), file::maxFilenameBytes);
        QCOMPARE(got2.substr(0, 5), std::string("_COM."));
        QVERIFY(got2.ends_with('z'));

        // And the ordinary case still ends up in budget.
        std::string plain = "COM1";
        plain.append(400, 'x');
        QCOMPARE(file::sanitizeFilename(plain + ".mp3").size(), file::maxFilenameBytes);
    }

    // ── Traversal ───────────────────────────────────────────────────────────

    /// The guarantee is about the RESULT, not about how it was reached: a
    /// separator anywhere in the output means the caller can be escaped.
    void traversalInputYieldsABareFilename() {
        const char* inputs[] = {
            "../escape.mp3",
            "..\\escape.mp3",
            "..",
            "../",
            "..\\",
            "/etc/passwd",
            "\\etc\\passwd",
            "/",
            "C:\\Windows\\System32\\x.mp3",
            "C:/Windows/x.mp3",
            "....//....//etc/passwd",
            "../../../../../../etc/shadow",
            "song/../../escape.mp3",
        };
        for (const char* input : inputs) {
            const std::string got = file::sanitizeFilename(input);
            QVERIFY2(!got.empty(), input);
            QVERIFY2(got.find('/') == std::string::npos, input);
            QVERIFY2(got.find('\\') == std::string::npos, input);
            QVERIFY2(got.find('\0') == std::string::npos, input);
            QVERIFY2(!fs::path(got).is_absolute(), input);
            QVERIFY2(fs::path(got).parent_path().empty(), input);
            QVERIFY2(fs::path(got).filename().string() == got, input);
        }

        // Exact values for the shapes that used to be the dangerous ones, so a
        // regression names itself.
        QCOMPARE(file::sanitizeFilename("../x"), std::string("___x"));
        QCOMPARE(file::sanitizeFilename("..\\x"), std::string("___x"));
        QCOMPARE(file::sanitizeFilename(".."), std::string("__"));
        QCOMPARE(file::sanitizeFilename("/etc/passwd"), std::string("_etc_passwd"));
        QCOMPARE(file::sanitizeFilename("../../etc/passwd"),
                 std::string("______etc_passwd"));
    }

    /// An embedded NUL is data to std::string but a terminator to every POSIX
    /// open(), so a filename carrying one is a promise the filesystem will not
    /// keep. The result is a visible underscore, not a name the kernel silently
    /// cuts short -- which would be a collision waiting for two tracks.
    void anEmbeddedNulCannotTruncateTheNameSilently() {
        const std::string withNul("a\0b.mp3", 7);
        QCOMPARE(withNul.size(), std::size_t{7});

        const std::string got = file::sanitizeFilename(withNul);
        QVERIFY2(got.find('\0') == std::string::npos, show(got).constData());
        QCOMPARE(show(got), QByteArray("a_b.mp3"));
        QCOMPARE(got.size(), std::size_t{7});

        // A NUL as the entire name, and one immediately before the extension.
        QCOMPARE(file::sanitizeFilename(std::string("\0", 1)), std::string("_"));
        QCOMPARE(file::sanitizeFilename(std::string("a\0.mp3", 6)),
                 std::string("a_.mp3"));
    }

    /// Checked against std::filesystem rather than by searching for "../",
    /// because a name can leave its directory without containing those three
    /// characters.
    void everySanitizedNameStaysInsideItsDirectory() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path base(fs::path(dir.path().toStdString()) / "music");
        QVERIFY(static_cast<bool>(file::ensureDir(base)));

        const char* inputs[] = {
            "../escape.mp3", "..", "/etc/passwd", "C:\\Windows\\x.mp3",
            "....//....//x", "song/../../escape.mp3", "\0", "",
            "CON .mp3", "a\0b.mp3", "///", "\\", "..\\..\\..", "CON",
        };
        for (const char* input : inputs) {
            const std::string got = file::sanitizeFilename(input);
            QVERIFY2(staysInside(base, got),
                     qPrintable(QStringLiteral("%1 -> \"%2\" escaped %3")
                                    .arg(QLatin1String(input), QString::fromStdString(got),
                                         QString::fromStdString(base.string()))));
        }
    }

    // ── fromHex ─────────────────────────────────────────────────────────────

    void hexColourRoundTripsForSixAndEightDigitForms() {
        struct Case {
            const char* input;
            vc::Color expected;
            const char* emitted; // what toHexString must produce
        };
        const Case cases[] = {
            {"#FF00FFFF", {255, 0, 255, 255},   "#FF00FFFF"},
            {"#11223344", {0x11, 0x22, 0x33, 0x44}, "#11223344"},
            {"#00FF88",   {0x00, 0xFF, 0x88, 255},   "#00FF88FF"},
            {"#00000000", {0, 0, 0, 0},          "#00000000"},
            {"#FFFFFFFF", {255, 255, 255, 255}, "#FFFFFFFF"},
            {"#abcdef01", {0xab, 0xcd, 0xef, 0x01}, "#ABCDEF01"}, // case folded
            {"#000000",   {0, 0, 0, 255},        "#000000FF"},
            {"#01020304", {1, 2, 3, 4},          "#01020304"},
        };
        for (const auto& [input, expected, emitted] : cases) {
            auto parsed = parseHexColor(input);
            QVERIFY2(parsed.has_value(), input);
            QCOMPARE(parsed->r, expected.r);
            QCOMPARE(parsed->g, expected.g);
            QCOMPARE(parsed->b, expected.b);
            QCOMPARE(parsed->a, expected.a);

            const std::string out = toHexString(*parsed);
            // Always '#' plus 8 uppercase digits -- asserted because
            // ConfigParsers round-trips through toHex() and a shape change here
            // would rewrite every colour in config.toml.
            QCOMPARE(out.size(), std::size_t{9});
            QCOMPARE(out[0], '#');
            QCOMPARE(out, std::string(emitted));

            // And it is an exact inverse.
            auto back = parseHexColor(out);
            QVERIFY(back.has_value());
            QCOMPARE(back->r, expected.r);
            QCOMPARE(back->g, expected.g);
            QCOMPARE(back->b, expected.b);
            QCOMPARE(back->a, expected.a);
        }

        // Every byte value survives the trip, which is the property that lets
        // default.toml and serialize() agree without a lossy step.
        for (int v = 0; v < 256; ++v) {
            const vc::Color c{static_cast<vc::u8>(v), static_cast<vc::u8>(255 - v),
                              static_cast<vc::u8>(v / 2), static_cast<vc::u8>(v)};
            auto back = parseHexColor(toHexString(c));
            QVERIFY(back.has_value());
            QCOMPARE(back->r, c.r);
            QCOMPARE(back->g, c.g);
            QCOMPARE(back->b, c.b);
            QCOMPARE(back->a, c.a);
        }
    }

    /// Every malformed shape gets a DISTINCT, named verdict. The old function
    /// had no failure value at all: a length other than 6 or 8 fell through to
    /// Color's default member initialisers, so `accent_color = "blue"` became
    /// opaque WHITE (#FFFFFFFF) with nothing to distinguish it from a colour the
    /// user actually chose.
    void hexColourRefusesEveryMalformedShape() {
        struct Case {
            const char* input;
            ColorParseError code;
            std::size_t offset;
        };
        const Case cases[] = {
            {"",           ColorParseError::Empty,       0},
            {"#",          ColorParseError::BadLength,   0},
            {"zzzzzz",     ColorParseError::MissingHash, 0},
            {"#gggggg",    ColorParseError::NotHex,      1},
            {"#FFF",       ColorParseError::BadLength,   0},
            {"#FFFFF",     ColorParseError::BadLength,   0},
            {"#FFFFFFF",   ColorParseError::BadLength,   0},
            {"#FFFFFFFFF", ColorParseError::BadLength,   0},
            {"FF00FF",     ColorParseError::MissingHash, 0},
            {"  #FF00FF",  ColorParseError::MissingHash, 0},
            {"#FF00FF ",   ColorParseError::BadLength,   0},
            {"#12345g",    ColorParseError::NotHex,      6},
            {"#g12345",    ColorParseError::NotHex,      1},
            // Eight digits is a valid SHAPE, so the bad digit is reported
            // rather than the length -- the length check runs first and passes.
            {"#1234567g",  ColorParseError::NotHex,      8},
        };
        for (const auto& [input, code, offset] : cases) {
            const auto parsed = parseHexColor(input);
            QVERIFY2(!parsed.has_value(), input);
            QVERIFY2(parsed.error().code == code,
                     qPrintable(QStringLiteral("\"%1\": got code %2, wanted %3")
                                    .arg(QLatin1String(input))
                                    .arg(static_cast<int>(parsed.error().code))
                                    .arg(static_cast<int>(code))));
            QCOMPARE(parsed.error().offset, offset);
            QVERIFY(parsed.error().message() != nullptr);
            QVERIFY(*parsed.error().message() != '\0');
        }

        // The three codes are genuinely distinct rather than one catch-all, so
        // a caller can report the actual defect.
        QVERIFY(ColorParseError::Empty != ColorParseError::MissingHash);
        QVERIFY(ColorParseError::MissingHash != ColorParseError::BadLength);
        QVERIFY(ColorParseError::BadLength != ColorParseError::NotHex);

        // The legacy shim still answers, still cannot throw, and is now loud.
        // Its answer is measured: the historical #FFFFFFFF, because that is
        // what Color's default member initialisers always produced.
        QCOMPARE(show(Color::fromHex("zzzzzz").toHex()), QByteArray("#FFFFFFFF"));
        QCOMPARE(show(Color::fromHex("blue").toHex()), QByteArray("#FFFFFFFF"));
        QCOMPARE(show(Color::fromHex("#00FF88").toHex()), QByteArray("#00FF88FF"));

        // A missing '#' used to be tolerated by fromHex and is now refused.
        // Every producer in this tree emits one -- QColor::name(), toHex(),
        // config/default.toml -- so nothing shipped relies on the tolerance.
        QVERIFY(!parseHexColor("FF00FF").has_value());
    }

    // ── loadM3U: the SSRF regression ────────────────────────────────────────

    /// The whole reason loadM3U has no remote branch.
    ///
    /// The sink is AudioEngine.cpp:425 and :436:
    ///
    ///     QUrl source = item->isRemote ? QUrl(QString::fromStdString(item->url))
    ///                                  : QUrl::fromLocalFile(QString::fromStdString(item->path.string()));
    ///
    /// so `isRemote == true` IS the request. Before the fix, loadM3U set it on
    /// any line beginning "http" while its path-traversal guard covered only the
    /// other branch, and the URL went straight into QMediaPlayer::setSource. A
    /// .m3u file -- a download, a shared drive, a forum post -- could therefore
    /// make the client issue a request to any host at all, and
    /// http://169.254.169.254/latest/meta-data/ is the well-known
    /// cloud-metadata credential-theft primitive.
    ///
    /// The local entry in the same fixture is what makes this a real assertion:
    /// a loader that refused everything would also produce zero remote items, so
    /// this suite would pass on a broken feature.
    void aRemoteM3uLineNeverBecomesATrack() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path root(dir.path().toStdString());

        // Two real files so the local branch has something to accept. Their
        // contents are not audio; loadM3U only requires fs::exists().
        //
        // "httpdemo.mp3" is here on purpose. The old code tested
        // line.starts_with("http"), so a local file with that name was
        // classified REMOTE and then failed confusingly -- a real usability bug
        // that the prefix sniff caused on the user's own files.
        const char* names[] = {"real.mp3", "httpdemo.mp3"};
        for (const char* name : names) {
            QFile f(QString::fromStdString((root / name).string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write("not audio"), qint64{9});
        }

        const fs::path m3u = root / "hostile.m3u";
        {
            QFile f(QString::fromStdString(m3u.string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            const QByteArray body =
                "#EXTM3U\n"
                "http://169.254.169.254/latest/meta-data/\n"
                "https://evil.example/payload.mp3\n"
                "https://169.254.169.254/latest/user-data\n"
                "https://localhost:8080/admin\n"
                "file:///etc/passwd\n"
                "ftp://internal.example/secret\n"
                "http://example.invalid\n"       // no path at all
                "http://\n"                      // nothing after the scheme
                "httpx://not-a-scheme-at-all\n"  // and the prefix-sniff bait
                "httpdemo.mp3\n"                 // the local file the old
                                                  // starts_with("http") test
                                                  // misclassified as remote
                "real.mp3\n";
            QCOMPARE(f.write(body), static_cast<qint64>(body.size()));
        }

        Playlist playlist;
        QVERIFY(playlist.loadM3U(m3u));

        // Exactly two tracks, and BOTH are local files. "httpdemo.mp3" is in the
        // fixture precisely because the old prefix test called it remote and
        // then failed confusingly; it must load as a local file.
        QCOMPARE(playlist.size(), std::size_t{2});
        QCOMPARE(QString::fromStdString(playlist.itemAt(0)->path.filename().string()),
                 QStringLiteral("httpdemo.mp3"));
        QCOMPARE(QString::fromStdString(playlist.itemAt(1)->path.filename().string()),
                 QStringLiteral("real.mp3"));

        // Nothing in the queue is remote and nothing carries a URL -- the
        // assertion that maps one-to-one onto the AudioEngine sink above.
        for (usize i = 0; i < playlist.size(); ++i) {
            const auto it = playlist.itemAt(i);
            QVERIFY(it.has_value());
            QVERIFY2(!it->isRemote,
                     qPrintable(QStringLiteral("item %1 is remote").arg(i)));
            QVERIFY2(it->url.empty(),
                     qPrintable(QStringLiteral("item %1 carries the URL \"%2\"")
                                    .arg(i).arg(QString::fromStdString(it->url))));
        }

        // And no selection exists, so the engine is handed nothing to open.
        QVERIFY(!playlist.currentIndex().has_value());
        QVERIFY(!playlist.currentItem().has_value());
    }

    /// A playlist containing ONLY hostile lines loads to nothing, and loadM3U
    /// still reports success: the file was read, it simply held nothing legal.
    void aPlaylistOfOnlyRemoteLinesLoadsNothing() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path m3u = fs::path(dir.path().toStdString()) / "all-remote.m3u";
        {
            QFile f(QString::fromStdString(m3u.string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#EXTM3U\nhttp://a.example/1.mp3\nhttps://b.example/2.mp3\n"
                    "http://169.254.169.254/latest/meta-data/\n");
        }

        Playlist playlist;
        QVERIFY(playlist.loadM3U(m3u));
        QCOMPARE(playlist.size(), std::size_t{0});
        QVERIFY(playlist.empty());
    }

    // ── #EXTINF round-trip ──────────────────────────────────────────────────

    /// saveM3U has always written `#EXTINF:<seconds>,<artist> - <title>` and
    /// loadM3U used to skip every '#' line, so the session file recorded the
    /// metadata and then discarded it -- it was neither round-tripped nor a
    /// valid interchange file.
    ///
    /// Which fields the payload actually restores is measured, not assumed:
    /// TagLib 2.3.2 returns a SUCCESSFUL read for a .mp3 holding nine bytes of
    /// non-audio, backfilling the title from the filename stem
    /// (MediaMetadata.cpp:89-92) and leaving artist and duration blank. So the
    /// artist and the duration come from #EXTINF, while the title comes from
    /// the file -- which is the right way round, since the file is the fresher
    /// record of what it is.
    void extInfMetadataIsReadBackForAFileWithNoReadableTags() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path root(dir.path().toStdString());

        const fs::path untagged = root / "untagged.mp3";
        {
            QFile f(QString::fromStdString(untagged.string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write("not audio"), qint64{9});
        }

        const fs::path m3u = root / "with-metadata.m3u";
        {
            QFile f(QString::fromStdString(m3u.string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            const QByteArray body =
                "#EXTM3U\n"
                "#EXTINF:212,Some Artist - Some Title\n"
                "untagged.mp3\n"
                // A second entry with no #EXTINF at all, so the pairing is
                // proven rather than assumed to persist forever.
                "untagged.mp3\n";
            QCOMPARE(f.write(body), static_cast<qint64>(body.size()));
        }

        Playlist playlist;
        QVERIFY(playlist.loadM3U(m3u));
        QCOMPARE(playlist.size(), std::size_t{2});

        const auto first = playlist.itemAt(0);
        QVERIFY(first.has_value());
        QCOMPARE(QString::fromStdString(first->metadata.artist),
                 QStringLiteral("Some Artist"));
        QCOMPARE(first->metadata.duration.count(), i64{212000});
        // The title comes from the FILE, not from #EXTINF -- see above.
        QCOMPARE(QString::fromStdString(first->metadata.title),
                 QStringLiteral("untagged"));

        // No #EXTINF: nothing is invented.
        const auto second = playlist.itemAt(1);
        QVERIFY(second.has_value());
        QVERIFY(second->metadata.artist.empty());
        QCOMPARE(second->metadata.duration.count(), i64{0});

        // "#EXTINF:<seconds>" is mandatory, and a title containing the " - "
        // separator survives because the split is on the FIRST occurrence:
        // artist "A", title "B - C". Splitting on the last would have produced
        // artist "A - B" and thrown the real title away.
        const fs::path m3u2 = root / "tricky.m3u";
        {
            QFile f(QString::fromStdString(m3u2.string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#EXTM3U\n#EXTINF:5,A - B - C\nuntagged.mp3\n");
        }
        Playlist tricky;
        QVERIFY(tricky.loadM3U(m3u2));
        QCOMPARE(tricky.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(tricky.itemAt(0)->metadata.artist),
                 QStringLiteral("A"));
        QCOMPARE(tricky.itemAt(0)->metadata.duration.count(), i64{5000});
        // The title comes from the FILE here, because the file's own tags read
        // successfully -- see the note at the top of this test.
        QCOMPARE(QString::fromStdString(tricky.itemAt(0)->metadata.title),
                 QStringLiteral("untagged"));

        // A #EXTINF with no comma at all is not fatal; it just carries nothing.
        const fs::path m3u3 = root / "malformed-extinf.m3u";
        {
            QFile f(QString::fromStdString(m3u3.string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#EXTM3U\n#EXTINF\n#EXTINF:-5,x - y\nuntagged.mp3\n");
        }
        Playlist malformed;
        QVERIFY(malformed.loadM3U(m3u3));
        QCOMPARE(malformed.size(), std::size_t{1});
        QCOMPARE(malformed.itemAt(0)->metadata.duration.count(), i64{0});
        QCOMPARE(QString::fromStdString(malformed.itemAt(0)->metadata.artist),
                 QStringLiteral("x"));
    }

    /// The full loop the session file actually takes: saveM3U writes it and
    /// loadM3U reads it back, and the metadata that comes out is the metadata
    /// that went in.
    void aSessionFileWrittenBySaveM3URoundTripsItsTitlesAndDurations() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const fs::path root(dir.path().toStdString());

        const fs::path track = root / "Round Trip.mp3";
        {
            QFile f(QString::fromStdString(track.string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write("not audio"), qint64{9});
        }

        const fs::path session = root / "last_session.m3u";
        {
            Playlist written;
            written.addFile(track);
            QCOMPARE(written.size(), std::size_t{1});
            QVERIFY(static_cast<bool>(written.saveM3U(session)));
        }

        Playlist reloaded;
        QVERIFY(reloaded.loadM3U(session));
        QCOMPARE(reloaded.size(), std::size_t{1});

        const auto item = reloaded.itemAt(0);
        QVERIFY(item.has_value());
        QVERIFY2(!item->isRemote, "a local file came back marked remote");
        QVERIFY(item->url.empty());
        QCOMPARE(QString::fromStdString(item->path.filename().string()),
                 QStringLiteral("Round Trip.mp3"));
        QCOMPARE(QString::fromStdString(item->metadata.title),
                 QStringLiteral("Round Trip"));

        // saveM3U wrote displayArtist() == "Unknown Artist" for the empty tag;
        // the loader maps that display sentinel back to empty rather than
        // inventing an artist the file does not have.
        QVERIFY2(item->metadata.artist.empty(),
                 qPrintable(QString::fromStdString(item->metadata.artist)));
        QCOMPARE(item->metadata.duration.count(), i64{0});
    }
};

#include "test_PathSafety.moc"

int runTestPathSafety(int argc, char** argv) {
    TestPathSafety tc;
    return QTest::qExec(&tc, argc, argv);
}
