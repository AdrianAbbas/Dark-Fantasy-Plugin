// Listening proof: renders a bright sawtooth chord and a piano-like phrase through
// the real processBlock path (dry, defaults, each module alone, the signature
// features, DREAD extremes and every factory preset) and writes 16-bit WAV files
// plus an index with level and brightness measurements for each file.
#include "TestSignals.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

using namespace veiltest;
using veil::P;

namespace {
constexpr double demoRate = 44100;
constexpr int demoBlock = 256;

enum class Source { saw, piano };
struct Demo {
    std::string file, description;
    Source source;
    std::function<void(VeilAudioProcessor&)> setup;
    std::function<void(VeilAudioProcessor&, double seconds)> automation;   // called before each block
    double seconds = 8.0;
};

Signal makeInput(Source source, double seconds) {
    return source == Source::saw ? sawChord(demoRate, seconds, 3.0, 0.5f) : pianoPhrase(demoRate, seconds, 0.55f);
}
const char* sourceName(Source source) { return source == Source::saw ? "saw-chord" : "piano-phrase"; }
// The window in which the input is sounding, used for the level comparison.
std::pair<double, double> sustainWindow(Source source) { return source == Source::saw ? std::pair{ 0.5, 3.0 } : std::pair{ 0.2, 4.0 }; }

bool writeWav(const juce::File& file, const Signal& signal) {
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    if (!static_cast<juce::FileOutputStream*>(stream.get())->openedOk()) return false;
    auto writer = juce::WavAudioFormat().createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(demoRate).withNumChannels(2).withBitsPerSample(16));
    if (writer == nullptr) return false;
    // TPDF dither at 16 bits so quiet reverb tails fade smoothly.
    juce::AudioBuffer<float> buffer(2, static_cast<int>(signal.size()));
    veil::dsp::Random random; random.seed(77);
    const float lsb = 1.0f / 32768.0f;
    for (std::size_t n = 0; n < signal.size(); ++n) {
        buffer.setSample(0, static_cast<int>(n), signal.l[n] + 0.5f * lsb * (random.bipolar() + random.bipolar()));
        buffer.setSample(1, static_cast<int>(n), signal.r[n] + 0.5f * lsb * (random.bipolar() + random.bipolar()));
    }
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

std::vector<Demo> demoList() {
    std::vector<Demo> demos;
    const auto none = [](VeilAudioProcessor&) {};
    const auto settings = [](Settings s) {
        std::vector<std::pair<P, float>> copy(s);
        return [copy](VeilAudioProcessor& p) { for (const auto& [id, value] : copy) set(p, id, value); };
    };
    const auto alone = [](P module, std::vector<std::pair<P, float>> extra = {}) {
        return [module, extra](VeilAudioProcessor& p) { onlyModule(p, module); for (const auto& [id, value] : extra) set(p, id, value); };
    };
    for (auto source : { Source::saw, Source::piano }) {
        const std::string s = sourceName(source);
        demos.push_back({ "00-dry-" + s, "Unprocessed input (VEIL bypassed).", source, [](VeilAudioProcessor& p) { set(p, veil::Bypass, 1); } });
        demos.push_back({ "01-defaults-" + s, "VEIL as it loads: every module at its default, DREAD 50%.", source, none });
        demos.push_back({ "02-abyss-only-" + s, "Abyss alone: dark resonant low-pass and high-frequency shelf.", source, alone(veil::AbyssOn) });
        demos.push_back({ "03-blood-only-" + s, "Ancient Blood alone: drive, wow and age.", source, alone(veil::BloodOn) });
        demos.push_back({ "04-choir-only-" + s, "Choir alone: voices an octave below with formants and a drone.", source, alone(veil::ChoirOn) });
        demos.push_back({ "05-wraith-only-" + s, "Wraith alone: blurred, detuned spectrum and an octave ghost.", source, alone(veil::WraithOn) });
        demos.push_back({ "06-echoes-only-" + s, "Echoes alone: diffused ping-pong delay.", source, alone(veil::EchoesOn) });
        demos.push_back({ "07-crypt-only-" + s, "Crypt alone: seven-second cathedral reverb.", source, alone(veil::CryptOn) });
        demos.push_back({ "08-dread-0-" + s, "Defaults with DREAD at 0%: the light touch.", source, settings({ { veil::Dread, 0.0f } }) });
        demos.push_back({ "09-dread-100-" + s, "Defaults with DREAD at 100%: fully transformed.", source, settings({ { veil::Dread, 1.0f } }) });
    }
    demos.push_back({ "10-feature-echoes-descent-octave-piano-phrase", "Echoes alone, Descent = Octave: each repeat drops an octave.",
                      Source::piano, alone(veil::EchoesOn, { { veil::Descent, 3 }, { veil::Feedback, 0.7f }, { veil::DelayMix, 0.6f } }) });
    demos.push_back({ "11-feature-echoes-descent-semitone-saw-chord", "Echoes alone, Descent = Semitone: repeats sink a semitone at a time.",
                      Source::saw, alone(veil::EchoesOn, { { veil::Descent, 1 }, { veil::Feedback, 0.75f }, { veil::DelaySync, 2 }, { veil::DelayMix, 0.55f } }) });
    demos.push_back({ "12-feature-crypt-rising-piano-phrase", "Crypt alone, Rising mode: a reversed swell rises into each note.",
                      Source::piano, alone(veil::CryptOn, { { veil::CryptMode, 1 }, { veil::RiseTime, 1.2f }, { veil::ReverbMix, 0.6f } }) });
    demos.push_back({ "13-feature-crypt-eternal-saw-chord", "Crypt alone; Eternal switched on at 2.5 s holds the tail indefinitely.",
                      Source::saw, alone(veil::CryptOn, { { veil::ReverbMix, 0.6f } }),
                      [](VeilAudioProcessor& p, double t) { if (t >= 2.5) set(p, veil::Eternal, 1); }, 12.0 });
    demos.push_back({ "14-feature-abyss-tomb-saw-chord", "Abyss alone, Tomb Resonator tuned to C2: the chord rings like stone.",
                      Source::saw, alone(veil::AbyssOn, { { veil::Tomb, 0.7f }, { veil::TombNote, 0 }, { veil::TombOctave, 1 }, { veil::TombRing, 0.8f } }) });
    demos.push_back({ "15-feature-abyss-tomb-piano-phrase", "Abyss alone, Tomb Resonator tuned to A2.",
                      Source::piano, alone(veil::AbyssOn, { { veil::Tomb, 0.7f }, { veil::TombNote, 9 }, { veil::TombOctave, 1 }, { veil::TombRing, 0.75f } }) });
    demos.push_back({ "16-feature-choir-drone-piano-phrase", "Choir alone, Drone at 90%: a sub-octave choir holds after the notes end.",
                      Source::piano, alone(veil::ChoirOn, { { veil::Drone, 0.9f }, { veil::DroneHold, 10 } }), {}, 10.0 });
    demos.push_back({ "17-feature-wraith-freeze-saw-chord", "Wraith alone; Freeze switched on at 1.5 s captures the spectrum.",
                      Source::saw, alone(veil::WraithOn, { { veil::WraithMix, 0.7f } }),
                      [](VeilAudioProcessor& p, double t) { if (t >= 1.5) set(p, veil::Freeze, 1); } });
    for (int preset = 0; preset < static_cast<int>(veil::factoryPresetTotal); ++preset) {
        for (auto source : { Source::saw, Source::piano }) {
            std::ostringstream file;
            file << "preset-" << std::setw(2) << std::setfill('0') << preset + 1 << "-"
                 << juce::File::createLegalFileName(veil::factoryPresets()[static_cast<std::size_t>(preset)].name).replaceCharacter(' ', '-').toLowerCase()
                 << "-" << sourceName(source);
            const auto& info = veil::factoryPresets()[static_cast<std::size_t>(preset)];
            demos.push_back({ file.str(), std::string(info.name) + ": " + info.description, source,
                              [preset](VeilAudioProcessor& p) { p.loadFactoryPreset(preset); }, {}, 10.0 });
        }
    }
    return demos;
}
}

int renderDemos(const juce::String& directory) {
    juce::ScopedJuceInitialiser_GUI juce;
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile(directory);
    if (!folder.createDirectory()) { std::cerr << "Cannot create " << folder.getFullPathName() << '\n'; return 1; }
    std::ostringstream index;
    index << "VEIL audio demos - " << demoRate << " Hz, 16-bit stereo WAV, rendered offline through processBlock.\n"
          << "Columns: file | peak dBFS | level vs dry while the input sounds (dB) | brightness (spectral centroid, Hz) vs dry | description\n"
          << "Nothing is normalised: the level column shows VEIL's own gain staging.\n\n";
    std::map<int, double> dryLevel, dryCentroid;
    int failures = 0, written = 0;
    for (const auto& demo : demoList()) {
        auto p = std::make_unique<VeilAudioProcessor>();
        demo.setup(*p);
        p->prepareToPlay(demoRate, demoBlock);
        const auto input = makeInput(demo.source, demo.seconds);
        const auto rendered = render(*p, input, demoRate, demoBlock, [&](std::size_t position) {
            if (demo.automation) demo.automation(*p, static_cast<double>(position) / demoRate);
        });
        const auto [from, to] = sustainWindow(demo.source);
        const double level = toDb(meanSquare(rendered.out, at(demoRate, from), at(demoRate, to)));
        const double bright = centroid(rendered.out, demoRate, at(demoRate, from), at(demoRate, to));
        const int key = static_cast<int>(demo.source);
        if (!dryLevel.count(key)) {
            dryLevel[key] = toDb(meanSquare(input, at(demoRate, from), at(demoRate, to)));
            dryCentroid[key] = centroid(input, demoRate, at(demoRate, from), at(demoRate, to));
        }
        const double peakDb = 20.0 * std::log10(std::max(1.0e-9f, peak(rendered.out)));
        const bool okay = finite(rendered.out) && writeWav(folder.getChildFile(demo.file + ".wav"), rendered.out);
        if (!okay) ++failures; else ++written;
        std::ostringstream line;
        line << std::fixed << std::setprecision(1) << demo.file << ".wav | " << peakDb << " dBFS | "
             << std::showpos << level - dryLevel[key] << std::noshowpos << " dB | "
             << std::setprecision(0) << bright << " Hz (dry " << dryCentroid[key] << ") | " << demo.description
             << (okay ? "" : "  [WRITE FAILED]");
        index << line.str() << '\n';
        std::cout << "[demo] " << line.str() << std::endl;
    }
    folder.getChildFile("index.txt").replaceWithText(index.str());
    std::cout << "Rendered " << written << " demo WAV files to " << folder.getFullPathName() << '\n';
    return failures == 0 ? 0 : 1;
}

// Developer probe: VeilTests --probe <saw|piano|impulse|noise|sine:HZ> <seconds> [id=value ...] [wav=path]
// Prints level, brightness and a coarse envelope so settings can be tuned quickly.
int probe(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc < 4) { std::cerr << "usage: --probe <signal> <seconds> [id=value ...] [wav=path]\n"; return 2; }
    const juce::String kind(argv[2]);
    const double seconds = juce::String(argv[3]).getDoubleValue(), rate = 48000;
    Signal input;
    double inputEnd = seconds;
    if (kind == "saw") { input = sawChord(rate, seconds, 3.0, 0.5f); inputEnd = 3.3; }
    else if (kind == "piano") { input = pianoPhrase(rate, seconds, 0.55f); inputEnd = 4.2; }
    else if (kind == "impulse") { input = impulse(rate, seconds, 0.8f); inputEnd = 0.001; }
    else if (kind == "noise") { input = noise(rate, seconds, 0.3f, 0.08); inputEnd = 0.08; }
    else if (kind.startsWith("sine:")) { input = sine(rate, seconds, kind.fromFirstOccurrenceOf(":", false, false).getDoubleValue(), 0.3f, 1.5); inputEnd = 1.5; }
    else { std::cerr << "unknown signal\n"; return 2; }
    auto p = std::make_unique<VeilAudioProcessor>();
    juce::String wav;
    for (int a = 4; a < argc; ++a) {
        const juce::String arg(argv[a]);
        const auto key = arg.upToFirstOccurrenceOf("=", false, false), value = arg.fromFirstOccurrenceOf("=", false, false);
        if (key == "wav") { wav = value; continue; }
        if (key == "only") { for (std::size_t i = 0; i < veil::Count; ++i) if (value == veil::specs[i].id) onlyModule(*p, static_cast<P>(i)); continue; }
        if (key == "preset") { p->loadFactoryPreset(value.getIntValue()); continue; }
        bool found = false;
        for (std::size_t i = 0; i < veil::Count; ++i) if (key == veil::specs[i].id) { set(*p, static_cast<P>(i), value.getFloatValue()); found = true; }
        if (!found) { std::cerr << "unknown parameter " << key << '\n'; return 2; }
    }
    p->prepareToPlay(rate, 512);
    const auto r = render(*p, input, rate);
    const double from = std::min(0.5, inputEnd * 0.3), to = std::max(from + 0.05, inputEnd - 0.1);
    std::cout << std::fixed << std::setprecision(2)
              << "level vs input " << toDb(meanSquare(r.out, at(rate, from), at(rate, to))) - toDb(meanSquare(input, at(rate, from), at(rate, to))) << " dB"
              << "  peak " << 20.0 * std::log10(std::max(1.0e-9f, peak(r.out))) << " dBFS"
              << "  centroid " << centroid(input, rate, at(rate, from), at(rate, to)) << " -> " << centroid(r.out, rate, at(rate, from), at(rate, to)) << " Hz"
              << "  T60 " << decaySeconds(r.out, rate, at(rate, inputEnd)) << " s\n";
    const double reference = toDb(meanSquare(input, at(rate, from), at(rate, to)));
    std::cout << "envelope (dB vs input level, 0.25 s steps):";
    for (double t = 0; t + 0.25 <= seconds; t += 0.25) std::cout << ' ' << std::setprecision(0) << toDb(meanSquare(r.out, at(rate, t), at(rate, t + 0.25))) - reference;
    std::cout << '\n';
    if (wav.isNotEmpty()) writeWav(juce::File::getCurrentWorkingDirectory().getChildFile(wav), r.out);
    return 0;
}
