#include <gtest/gtest.h>

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <limits>

#include "app/settings.h"

// Settings persistence. Every test uses its own INI file in a temporary directory: the user's
// real configuration (~/.config/lotus-jam) is never read nor written.

namespace {

struct TempIni {
    QTemporaryDir dir;
    QString path() const { return dir.filePath("lotus-jam.conf"); }
};

}  // namespace

TEST(Settings, DefaultsMatchPrototype) {
    TempIni t;
    ASSERT_TRUE(t.dir.isValid());
    QSettings s(t.path(), QSettings::IniFormat);
    const Settings d = loadSettings(s);
    EXPECT_EQ(d.bpm, 80.0);
    EXPECT_EQ(d.gainDb, 0.0);
    EXPECT_EQ(d.compensationMs, 0.0);
    EXPECT_EQ(d.a4, 440.0);
    EXPECT_EQ(d.instrumentId, "baixo4");
    EXPECT_EQ(d.tuningName, "Padrão");
    EXPECT_EQ(d.countInBeats, 4);
    EXPECT_EQ(d.subdivision, 1);
    // No saved devices: device_select picks PCM2902 / HDMI (see UiSmoke.DeviceDefaults).
    EXPECT_TRUE(d.inputNode.empty());
    EXPECT_TRUE(d.outputNode.empty());
    // The struct's own defaults are the same values.
    const Settings z;
    EXPECT_EQ(z.bpm, 80.0);
    EXPECT_EQ(z.countInBeats, 4);
    EXPECT_EQ(z.instrumentId, "baixo4");
}

TEST(Settings, RoundTrip) {
    TempIni t;
    Settings in;
    in.inputNode = "alsa_input.usb-BurrBrown_from_Texas_Instruments_USB_AUDIO_CODEC-00.analog-stereo";
    in.outputNode = "alsa_output.pci-0000_01_00.1.hdmi-stereo";
    in.bpm = 132;
    in.compensationMs = -17.5;
    in.gainDb = 6.5;
    in.a4 = 442;
    in.instrumentId = "violao";
    in.tuningName = "Drop D";
    in.countInBeats = 8;
    in.subdivision = 4;
    {
        QSettings s(t.path(), QSettings::IniFormat);
        saveSettings(s, in);
    }  // written to disk
    QSettings s(t.path(), QSettings::IniFormat);
    const Settings out = loadSettings(s);
    EXPECT_EQ(out.inputNode, in.inputNode);
    EXPECT_EQ(out.outputNode, in.outputNode);
    EXPECT_EQ(out.bpm, 132.0);
    EXPECT_EQ(out.compensationMs, -17.5);
    EXPECT_EQ(out.gainDb, 6.5);
    EXPECT_EQ(out.a4, 442.0);
    EXPECT_EQ(out.instrumentId, "violao");
    EXPECT_EQ(out.tuningName, "Drop D");
    EXPECT_EQ(out.countInBeats, 8);
    EXPECT_EQ(out.subdivision, 4);
    // The compensation is the saved pair's.
    EXPECT_EQ(compensationFor(s, in.inputNode, in.outputNode), -17.5);
}

// Compensation is per (input, output) pair. An unknown pair returns the default 0 ms (not the
// value of another pair: the latency depends on both devices).
TEST(Settings, CompensationPerDevicePair) {
    TempIni t;
    const std::string usb = "alsa_input.usb-BurrBrown_from_Texas_Instruments_USB_AUDIO_CODEC-00.analog-stereo";
    const std::string mic = "alsa_input.pci-0000_00_1f.3.analog-stereo";
    const std::string hdmi = "alsa_output.pci-0000_01_00.1.hdmi-stereo";
    const std::string odd = "weird/node|with%chars=and spaces";
    {
        QSettings s(t.path(), QSettings::IniFormat);
        setCompensationFor(s, usb, hdmi, 23.0);
        setCompensationFor(s, mic, hdmi, -4.5);
        setCompensationFor(s, odd, hdmi, 7.0);
        setCompensationFor(s, hdmi, odd, 9.0);
        EXPECT_EQ(compensationFor(s, usb, hdmi), 23.0);
        EXPECT_EQ(compensationFor(s, mic, hdmi), -4.5);
    }
    QSettings s(t.path(), QSettings::IniFormat);
    EXPECT_EQ(compensationFor(s, usb, hdmi), 23.0);
    EXPECT_EQ(compensationFor(s, mic, hdmi), -4.5);
    EXPECT_EQ(compensationFor(s, odd, hdmi), 7.0);
    EXPECT_EQ(compensationFor(s, hdmi, odd), 9.0);
    EXPECT_EQ(compensationFor(s, hdmi, usb), 0.0);  // order matters: (input, output)
    EXPECT_EQ(compensationFor(s, usb, mic), 0.0);   // unknown pair
    setCompensationFor(s, usb, hdmi, 30.0);  // overwrite
    EXPECT_EQ(compensationFor(s, usb, hdmi), 30.0);
    EXPECT_EQ(compensationFor(s, mic, hdmi), -4.5);
    // Setting a pair's compensation does not change the saved device selection.
    EXPECT_TRUE(loadSettings(s).inputNode.empty());
}

TEST(Settings, CorruptOrMissingValuesFallBackToDefaults) {
    // Values of the wrong type or out of range.
    {
        TempIni t;
        {
            QSettings s(t.path(), QSettings::IniFormat);
            s.setValue("training/bpm", "abc");
            s.setValue("training/countInBeats", 3);
            s.setValue("training/subdivision", "x");
            s.setValue("audio/gainDb", 99);
            s.setValue("tuner/a4", -1);
            s.setValue("tuner/instrument", "theremin");
            s.setValue("tuner/tuning", "Nope");
            s.setValue("devices/input", "alsa_input.x");
        }
        QSettings s(t.path(), QSettings::IniFormat);
        const Settings d = loadSettings(s);
        EXPECT_EQ(d.bpm, 80.0);
        EXPECT_EQ(d.countInBeats, 4);
        EXPECT_EQ(d.subdivision, 1);
        EXPECT_EQ(d.gainDb, 0.0);
        EXPECT_EQ(d.a4, 440.0);
        EXPECT_EQ(d.instrumentId, "baixo4");
        EXPECT_EQ(d.tuningName, "Padrão");
        EXPECT_EQ(d.inputNode, "alsa_input.x");  // valid values are kept
    }
    // A tuning that does not exist for the (valid) instrument falls back to Padrão.
    {
        TempIni t;
        QSettings s(t.path(), QSettings::IniFormat);
        s.setValue("tuner/instrument", "baixo5");
        s.setValue("tuner/tuning", "DADGAD");
        const Settings d = loadSettings(s);
        EXPECT_EQ(d.instrumentId, "baixo5");
        EXPECT_EQ(d.tuningName, "Padrão");
    }
    // Non-finite and out-of-range compensation.
    {
        TempIni t;
        QSettings s(t.path(), QSettings::IniFormat);
        setCompensationFor(s, "a", "b", std::numeric_limits<double>::quiet_NaN());
        EXPECT_EQ(compensationFor(s, "a", "b"), 0.0);
        setCompensationFor(s, "a", "b", 5000.0);
        EXPECT_EQ(compensationFor(s, "a", "b"), 0.0);
    }
    // A file that is not INI at all (binary garbage) and a missing file.
    {
        TempIni t;
        QFile f(t.path());
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        const char junk[] = "\x00\xff\xfe[[[\n=\x01\x02 bpm\n[training\nbpm==\xff\n";
        f.write(junk, sizeof junk);
        f.close();
        QSettings s(t.path(), QSettings::IniFormat);
        const Settings d = loadSettings(s);
        EXPECT_EQ(d.bpm, 80.0);
        EXPECT_EQ(d.instrumentId, "baixo4");
        EXPECT_EQ(compensationFor(s, "a", "b"), 0.0);
        QSettings m(t.dir.filePath("does-not-exist/lotus-jam.conf"), QSettings::IniFormat);
        EXPECT_EQ(loadSettings(m).bpm, 80.0);
    }
}

// An unwritable configuration file is reported by QSettings::status() and changes nothing else:
// the values still load (defaults) and nothing throws or crashes.
TEST(Settings, UnwritableFileIsHarmless) {
    TempIni t;
    QFile f(t.path());
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("[training]\nbpm=100\n");
    f.close();
    ASSERT_TRUE(QFile::setPermissions(t.path(), QFile::ReadOwner));
    QSettings s(t.path(), QSettings::IniFormat);
    Settings v = loadSettings(s);
    EXPECT_EQ(v.bpm, 100.0);
    v.bpm = 150;
    saveSettings(s, v);
    s.sync();
    if (QFile(t.path()).open(QIODevice::WriteOnly)) GTEST_SKIP() << "running with write access anyway (root?)";
    EXPECT_EQ(s.status(), QSettings::AccessError);
    QFile r(t.path());  // the file is untouched (another QSettings would share the cache)
    ASSERT_TRUE(r.open(QIODevice::ReadOnly));
    EXPECT_EQ(r.readAll(), QByteArray("[training]\nbpm=100\n"));
    QFile::setPermissions(t.path(), QFile::ReadOwner | QFile::WriteOwner);
}
