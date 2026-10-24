// VeilAUHarness: a small AudioToolbox host that loads aufx/Vel1/Veil the way
// real hosts do and checks that it actually produces sound and shows its view.
//
// It deliberately does not use JUCE's hosting code. Three hosting paths run:
//
//   1. AUv2 API, out-of-process  (AudioComponentInstantiate + LoadOutOfProcess)
//   2. AUv2 API, in-process      (AudioComponentInstantiate + LoadInProcess)
//   3. AUAudioUnit API, out-of-process (what modern hosts such as GarageBand
//      and Logic use on Apple Silicon, where AUv2 plug-ins are bridged into
//      AUHostingServiceXPC)
//
// Each path renders a 440 Hz sine and checks RMS, host bypass, parameter
// changes, factory presets, state round trip and the editor view.
//
// Usage:
//   VeilAUHarness [--strict] [--no-window] [--component aufx:Vel1:Veil]
//                 [--only v2-oop|v2-inproc|auaudiounit]
//
// Without --strict the harness reports and exits 0 (used to characterise an
// old build). With --strict any FAIL gives exit code 1. Exit code 2 means the
// component is not registered at all.

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <AudioUnit/AudioUnit.h>
#import <CoreAudioKit/CoreAudioKit.h>

#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <vector>

namespace {

//==============================================================================
std::string strf(const char* f, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, f);
    std::vsnprintf(buffer, sizeof(buffer), f, args);
    va_end(args);
    return buffer;
}

std::string statusText(OSStatus s) {
    const auto u = static_cast<UInt32>(s);
    char code[5] = { char(u >> 24), char(u >> 16), char(u >> 8), char(u), 0 };
    bool printable = true;
    for (int i = 0; i < 4; ++i) printable &= code[i] >= 32 && code[i] < 127;
    return printable ? strf("%d '%s'", int(s), code) : strf("%d", int(s));
}

std::string toString(NSString* s) { return s != nil ? std::string(s.UTF8String) : std::string("(nil)"); }

std::string toString(CFStringRef s) {
    if (s == nullptr) return "(null)";
    char buffer[512];
    return CFStringGetCString(s, buffer, sizeof(buffer), kCFStringEncodingUTF8) ? buffer : "(unconvertible)";
}

OSType fourCC(const std::string& s) {
    if (s.size() != 4) return 0;
    return (OSType(uint8_t(s[0])) << 24) | (OSType(uint8_t(s[1])) << 16) | (OSType(uint8_t(s[2])) << 8) | OSType(uint8_t(s[3]));
}

double toDb(double rms) { return 20.0 * std::log10(std::max(rms, 1.0e-12)); }

//==============================================================================
struct Report {
    int passed = 0, failed = 0, skipped = 0;
    std::string scope = "setup";

    void line(const char* tag, const std::string& name, const std::string& detail) {
        std::printf("%-4s [%s] %s%s%s\n", tag, scope.c_str(), name.c_str(), detail.empty() ? "" : " :: ", detail.c_str());
        std::fflush(stdout);
    }
    bool check(bool okay, const std::string& name, const std::string& detail = {}) {
        okay ? ++passed : ++failed;
        line(okay ? "PASS" : "FAIL", name, detail);
        return okay;
    }
    void skip(const std::string& name, const std::string& why) { ++skipped; line("SKIP", name, why); }
    void info(const std::string& name, const std::string& detail) { line("INFO", name, detail); }
};

//==============================================================================
// Spins the main run loop while waiting, so completion handlers delivered on
// the main queue (and JUCE's message thread work) can run.
bool waitFor(dispatch_semaphore_t semaphore, double seconds) {
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow: seconds];
    while (dispatch_semaphore_wait(semaphore, DISPATCH_TIME_NOW) != 0) {
        if ([deadline timeIntervalSinceNow] <= 0) return false;
        [[NSRunLoop currentRunLoop] runMode: NSDefaultRunLoopMode beforeDate: [NSDate dateWithTimeIntervalSinceNow: 0.01]];
    }
    return true;
}

void pump(double seconds) {
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow: seconds];
    while ([deadline timeIntervalSinceNow] > 0)
        [[NSRunLoop currentRunLoop] runMode: NSDefaultRunLoopMode beforeDate: [NSDate dateWithTimeIntervalSinceNow: 0.01]];
}

// True if a Mach-O image whose path contains `needle` is mapped into this process.
bool imageLoaded(const char* needle) {
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; ++i)
        if (const char* name = _dyld_get_image_name(i))
            if (std::strstr(name, needle) != nullptr) return true;
    return false;
}

std::string shell(const char* command) {
    std::string out;
    if (FILE* pipe = popen(command, "r")) {
        char buffer[512];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) out += buffer;
        pclose(pipe);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

std::string hostingServices() {
    auto list = shell("/usr/bin/pgrep -l AUHostingService 2>/dev/null | /usr/bin/tr '\\n' ';'");
    return list.empty() ? "none" : list;
}

const char* harnessArchitecture() {
   #if defined(__arm64__)
    return "arm64";
   #elif defined(__x86_64__)
    int translated = 0;
    size_t size = sizeof(translated);
    if (sysctlbyname("sysctl.proc_translated", &translated, &size, nullptr, 0) == 0 && translated == 1)
        return "x86_64 (Rosetta)";
    return "x86_64";
   #else
    return "unknown";
   #endif
}

constexpr const char* pluginImageNeedle = "VEIL.component/Contents/MacOS/";

//==============================================================================
AudioStreamBasicDescription floatFormat(double sampleRate, UInt32 channels) {
    AudioStreamBasicDescription f {};
    f.mSampleRate = sampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = sizeof(float);
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = sizeof(float);
    f.mChannelsPerFrame = channels;
    f.mBitsPerChannel = 32;
    return f;
}

// Deterministic sine source. It also records what it handed to the plug-in so
// the output can be compared sample-by-sample (bypass, dry path).
struct Source {
    double sampleRate = 48000.0, frequency = 440.0, phase = 0.0;
    float amplitude = 0.25f;
    std::vector<float> captured;
    size_t capturedCount = 0;
    UInt64 pulls = 0;

    void reset(size_t capacity) {
        captured.assign(capacity, 0.0f);
        capturedCount = 0;
        pulls = 0;
    }
    void fill(AudioBufferList* io, UInt32 frames) {
        ++pulls;
        const double increment = 2.0 * M_PI * frequency / sampleRate;
        for (UInt32 n = 0; n < frames; ++n) {
            const float v = amplitude * float(std::sin(phase));
            phase += increment;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
            for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
                if (io->mBuffers[b].mData != nullptr) static_cast<float*>(io->mBuffers[b].mData)[n] = v;
            if (capturedCount < captured.size()) captured[capturedCount++] = v;
        }
    }
};

OSStatus pullInput(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* io) {
    static_cast<Source*>(ref)->fill(io, frames);
    return noErr;
}

struct RenderResult {
    OSStatus status = noErr;
    double outRms[2] {};
    double inRms = 0, diffRms = 0;   // diff = output channel 0 minus input, sample aligned
    float peak = 0;
    bool finite = true;
    UInt64 pulls = 0;
    UInt32 outChannels = 0;

    double rmsDb() const { return toDb(outChannels > 1 ? std::sqrt(0.5 * (outRms[0] * outRms[0] + outRms[1] * outRms[1])) : outRms[0]); }
    double diffDb() const { return toDb(diffRms) - toDb(inRms); }   // 0 dB = as loud as the input
    std::string describe() const {
        return strf("status=%s out=%.1f dBFS (L %.1f / R %.1f) in=%.1f dBFS diff-vs-input=%.1f dB peak=%.3f finite=%d input-pulls=%llu",
                      statusText(status).c_str(), rmsDb(), toDb(outRms[0]), toDb(outRms[1]), toDb(inRms), diffDb(),
                      double(peak), int(finite), (unsigned long long) pulls);
    }
};

// Accumulates statistics for output written into `channels`, comparing channel 0 to
// the source's captured input at the same sample index.
struct Meter {
    double sum[2] {}, inSum = 0, diffSum = 0;
    size_t counted = 0;
    float peak = 0;
    bool finite = true;

    void add(const float* const* channels, UInt32 numChannels, UInt32 frames, size_t firstIndex, size_t skip, const Source& source) {
        for (UInt32 n = 0; n < frames; ++n) {
            const size_t index = firstIndex + n;
            for (UInt32 c = 0; c < numChannels; ++c) {
                const float v = channels[c][n];
                if (!std::isfinite(v)) { finite = false; continue; }
                peak = std::max(peak, std::abs(v));
                if (index >= skip && c < 2) sum[c] += double(v) * v;
            }
            if (index >= skip) {
                const float in = index < source.capturedCount ? source.captured[index] : 0.0f;
                const float out = std::isfinite(channels[0][n]) ? channels[0][n] : 0.0f;
                inSum += double(in) * in;
                diffSum += double(out - in) * (out - in);
                ++counted;
            }
        }
    }
    void finish(RenderResult& r, UInt32 numChannels) const {
        const double n = double(std::max<size_t>(counted, 1));
        r.outRms[0] = std::sqrt(sum[0] / n);
        r.outRms[1] = numChannels > 1 ? std::sqrt(sum[1] / n) : r.outRms[0];
        r.inRms = std::sqrt(inSum / n);
        r.diffRms = std::sqrt(diffSum / n);
        r.peak = peak;
        r.finite = finite;
        r.outChannels = numChannels;
    }
};

//==============================================================================
// AUv2 API host.
struct V2Host {
    AudioUnit unit = nullptr;
    Source source;
    double sampleRate = 48000.0;
    UInt32 inChannels = 2, outChannels = 2, maxFrames = 512;
    Float64 sampleTime = 0;
    std::vector<std::vector<float>> outputs;

    OSStatus configure(double sr, UInt32 in, UInt32 out, UInt32 frames = 512) {
        sampleRate = sr; inChannels = in; outChannels = out; maxFrames = frames;
        source.sampleRate = sr;
        AudioUnitUninitialize(unit);
        auto inFormat = floatFormat(sr, in), outFormat = floatFormat(sr, out);
        OSStatus s = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &inFormat, sizeof(inFormat));
        if (s != noErr) return s;
        s = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &outFormat, sizeof(outFormat));
        if (s != noErr) return s;
        s = AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof(maxFrames));
        if (s != noErr) return s;
        AURenderCallbackStruct callback { pullInput, &source };
        s = AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback));
        if (s != noErr) return s;
        outputs.assign(out, std::vector<float>(maxFrames, 0.0f));
        return AudioUnitInitialize(unit);
    }

    RenderResult render(double seconds, double skipSeconds) {
        RenderResult result;
        const auto total = size_t(seconds * sampleRate), skip = size_t(skipSeconds * sampleRate);
        source.reset(total);
        std::vector<uint8_t> storage(offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer) * outChannels);
        auto* abl = reinterpret_cast<AudioBufferList*>(storage.data());
        std::vector<const float*> channels(outChannels);
        Meter meter;
        size_t done = 0;
        while (done < total) {
            const auto frames = UInt32(std::min<size_t>(maxFrames, total - done));
            abl->mNumberBuffers = outChannels;
            for (UInt32 c = 0; c < outChannels; ++c) {
                std::fill(outputs[c].begin(), outputs[c].end(), 0.0f);
                abl->mBuffers[c].mNumberChannels = 1;
                abl->mBuffers[c].mDataByteSize = frames * sizeof(float);
                abl->mBuffers[c].mData = outputs[c].data();
            }
            AudioUnitRenderActionFlags flags = 0;
            AudioTimeStamp ts {};
            ts.mFlags = kAudioTimeStampSampleTimeValid;
            ts.mSampleTime = sampleTime;
            result.status = AudioUnitRender(unit, &flags, &ts, 0, frames, abl);
            if (result.status != noErr) break;
            for (UInt32 c = 0; c < outChannels; ++c)
                channels[c] = abl->mBuffers[c].mData != nullptr ? static_cast<const float*>(abl->mBuffers[c].mData) : outputs[c].data();
            meter.add(channels.data(), outChannels, frames, done, skip, source);
            sampleTime += frames;
            done += frames;
        }
        meter.finish(result, outChannels);
        result.pulls = source.pulls;
        return result;
    }
};

//==============================================================================
struct Param {
    AudioUnitParameterID id = 0;
    std::string name;
    AudioUnitParameterInfo info {};
};

std::vector<Param> listParameters(AudioUnit unit) {
    UInt32 size = 0;
    Boolean writable = false;
    if (AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, &writable) != noErr || size == 0)
        return {};
    std::vector<AudioUnitParameterID> ids(size / sizeof(AudioUnitParameterID));
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &size) != noErr)
        return {};
    std::vector<Param> result;
    for (auto id : ids) {
        Param p;
        p.id = id;
        UInt32 infoSize = sizeof(p.info);
        if (AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &p.info, &infoSize) != noErr)
            continue;
        p.name.assign(p.info.name, strnlen(p.info.name, sizeof(p.info.name)));
        if ((p.info.flags & kAudioUnitParameterFlag_HasCFNameString) != 0 && p.info.cfNameString != nullptr) {
            p.name = toString(p.info.cfNameString);
            if ((p.info.flags & kAudioUnitParameterFlag_CFNameRelease) != 0) CFRelease(p.info.cfNameString);
            p.info.cfNameString = nullptr;
        }
        result.push_back(p);
    }
    return result;
}

const Param* findParameter(const std::vector<Param>& params, std::initializer_list<const char*> names) {
    for (const char* name : names)
        for (const auto& p : params)
            if (strcasecmp(p.name.c_str(), name) == 0) return &p;
    return nullptr;
}

float getParameter(AudioUnit unit, AudioUnitParameterID id) {
    AudioUnitParameterValue v = 0;
    AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &v);
    return v;
}

OSStatus setParameter(AudioUnit unit, AudioUnitParameterID id, float value) {
    return AudioUnitSetParameter(unit, id, kAudioUnitScope_Global, 0, value, 0);
}

bool isDiscrete(const AudioUnitParameterInfo& info) {
    return info.unit == kAudioUnitParameterUnit_Indexed || info.unit == kAudioUnitParameterUnit_Boolean;
}

//==============================================================================
AudioComponentInstance instantiate(AudioComponent component, bool outOfProcess, OSStatus& status) {
    __block AudioComponentInstance instance = nullptr;
    __block OSStatus result = noErr;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    AudioComponentInstantiate(component,
                              outOfProcess ? kAudioComponentInstantiation_LoadOutOfProcess : kAudioComponentInstantiation_LoadInProcess,
                              ^(AudioComponentInstance i, OSStatus e) {
                                  instance = i;
                                  result = e;
                                  dispatch_semaphore_signal(done);
                              });
    if (!waitFor(done, 30.0)) {
        status = kAudioUnitErr_FailedInitialization;
        return nullptr;
    }
    status = result;
    return instance;
}

//==============================================================================
// Editor via the AUv2 Cocoa view factory. Must run in the plug-in's process.
void cocoaViewTest(Report& r, AudioUnit unit, bool useWindow) {
    UInt32 size = 0;
    Boolean writable = false;
    OSStatus s = AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable);
    if (!r.check(s == noErr && size >= sizeof(AudioUnitCocoaViewInfo), "kAudioUnitProperty_CocoaUI is published",
                 strf("status=%s size=%u", statusText(s).c_str(), unsigned(size))))
        return;
    std::vector<uint8_t> storage(size, 0);
    auto* info = reinterpret_cast<AudioUnitCocoaViewInfo*>(storage.data());
    s = AudioUnitGetProperty(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &size);
    if (!r.check(s == noErr && info->mCocoaAUViewBundleLocation != nullptr, "kAudioUnitProperty_CocoaUI readable", statusText(s)))
        return;
    const auto classes = UInt32((size - sizeof(CFURLRef)) / sizeof(CFStringRef));
    NSURL* url = CFBridgingRelease(info->mCocoaAUViewBundleLocation);
    NSString* className = classes > 0 ? (NSString*) CFBridgingRelease(info->mCocoaAUViewClass[0]) : nil;
    for (UInt32 i = 1; i < classes; ++i) CFRelease(info->mCocoaAUViewClass[i]);
    r.info("Cocoa view factory", strf("class=%s bundle=%s", toString(className).c_str(), toString(url.path).c_str()));

    // Hosts resolve the class through the bundle the property names.
    NSBundle* bundle = url != nil ? [NSBundle bundleWithURL: url] : nil;
    Class factoryClass = bundle != nil && className != nil ? [bundle classNamed: className] : nil;
    const char* how = "[NSBundle classNamed:]";
    if (factoryClass == nil && className != nil) { factoryClass = NSClassFromString(className); how = "NSClassFromString (bundle lookup failed)"; }
    if (!r.check(factoryClass != nil, "view factory class resolves", how)) return;
    if (!r.check([factoryClass conformsToProtocol: @protocol(AUCocoaUIBase)]
                     && [factoryClass instancesRespondToSelector: @selector(uiViewForAudioUnit:withSize:)],
                 "view factory implements AUCocoaUIBase"))
        return;

    NSView* view = nil;
    @autoreleasepool {
        id<AUCocoaUIBase> factory = [[factoryClass alloc] init];
        view = [factory uiViewForAudioUnit: unit withSize: NSMakeSize(0, 0)];
    }
    if (!r.check(view != nil, "uiViewForAudioUnit:withSize: returns an NSView")) return;
    const NSSize size0 = view.frame.size;
    r.check(size0.width >= 300 && size0.height >= 200, "editor view has a sensible size", strf("%.0f x %.0f", size0.width, size0.height));

    if (!useWindow) {
        r.skip("editor hosted in a window", "--no-window");
    } else {
        NSWindow* window = [[NSWindow alloc] initWithContentRect: NSMakeRect(80, 80, size0.width, size0.height)
                                                       styleMask: NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                                         backing: NSBackingStoreBuffered
                                                           defer: NO];
        window.releasedWhenClosed = NO;
        NSView* container = [[NSView alloc] initWithFrame: NSMakeRect(0, 0, size0.width, size0.height)];
        window.contentView = container;
        [container addSubview: view];
        [window orderFront: nil];
        pump(1.5);
        r.check(view.window == window && NSWidth(view.frame) > 0, "editor stays alive in a window for 1.5 s",
                strf("%.0f x %.0f", NSWidth(view.frame), NSHeight(view.frame)));
        [view removeFromSuperview];
        [window orderOut: nil];
        [window close];
    }
    view = nil;
    pump(0.3);   // let JUCE delete its editor before the unit goes away
}

// Editor via kAudioUnitProperty_RequestViewController (the AUv2-API route to a remote
// view). Apple does not promise this property for every AUv2 instance, so when it is
// absent that is reported, not failed; when present it must deliver a usable view.
// The AUAudioUnit suite checks the modern hosts' route, which is required.
void requestViewControllerTest(Report& r, AudioUnit unit, bool useWindow, bool required) {
    typedef void (^ViewControllerBlock)(AUViewControllerBase*);
    UInt32 size = 0;
    Boolean writable = false;
    OSStatus s = AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_RequestViewController, kAudioUnitScope_Global, 0, &size, &writable);
    const bool available = s == noErr && size == sizeof(ViewControllerBlock);
    const auto detail = strf("status=%s size=%u", statusText(s).c_str(), unsigned(size));
    if (!required && !available) {
        r.info("kAudioUnitProperty_RequestViewController not offered for this instance", detail);
        return;
    }
    if (!r.check(available, "kAudioUnitProperty_RequestViewController available", detail))
        return;
    __block AUViewControllerBase* controller = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    ViewControllerBlock callback = ^(AUViewControllerBase* vc) {
        controller = vc;
        dispatch_semaphore_signal(done);
    };
    s = AudioUnitSetProperty(unit, kAudioUnitProperty_RequestViewController, kAudioUnitScope_Global, 0, &callback, sizeof(callback));
    if (!r.check(s == noErr, "view controller requested", statusText(s))) return;
    const bool answered = waitFor(done, 20.0);
    if (!r.check(answered && controller != nil, "view controller delivered", !answered ? "timed out" : (controller == nil ? "callback returned nil" : "")))
        return;
    NSView* view = controller.view;
    const NSSize preferred = controller.preferredContentSize;
    r.check(view != nil, "view controller has a view",
            strf("class=%s view=%.0f x %.0f preferred=%.0f x %.0f", toString(NSStringFromClass([controller class])).c_str(),
                   NSWidth(view.frame), NSHeight(view.frame), preferred.width, preferred.height));
    if (view != nil && useWindow) {
        const NSSize sz = NSMakeSize(std::max<CGFloat>(NSWidth(view.frame), preferred.width), std::max<CGFloat>(NSHeight(view.frame), preferred.height));
        NSWindow* window = [[NSWindow alloc] initWithContentRect: NSMakeRect(80, 80, std::max<CGFloat>(sz.width, 100), std::max<CGFloat>(sz.height, 100))
                                                       styleMask: NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                                         backing: NSBackingStoreBuffered
                                                           defer: NO];
        window.releasedWhenClosed = NO;
        window.contentViewController = controller;
        [window orderFront: nil];
        pump(2.0);
        r.check(NSWidth(view.frame) >= 300 && NSHeight(view.frame) >= 200, "remote editor has a sensible size after 2 s",
                strf("%.0f x %.0f", NSWidth(view.frame), NSHeight(view.frame)));
        [window orderOut: nil];
        window.contentViewController = nil;
        [window close];
    }
    controller = nil;
    pump(0.3);
}

//==============================================================================
// Common AUv2 render/parameter/preset/state checks for one instance.
void v2Suite(Report& r, AudioComponent component, bool outOfProcess, bool useWindow) {
    r.scope = outOfProcess ? "v2 out-of-process" : "v2 in-process";
    const bool imageBefore = imageLoaded(pluginImageNeedle);
    OSStatus s = noErr;
    AudioComponentInstance unit = instantiate(component, outOfProcess, s);
    if (!r.check(unit != nullptr && s == noErr, "instantiate", statusText(s))) return;
    const bool imageAfter = imageLoaded(pluginImageNeedle);
    r.info("where the plug-in code runs",
           strf("plug-in image in harness process: before=%d after=%d; AUHostingService processes: %s",
                  int(imageBefore), int(imageAfter), hostingServices().c_str()));
    if (outOfProcess && !imageBefore && imageAfter)
        r.info("note", "out-of-process was requested but the plug-in loaded into this process");

    V2Host host;
    host.unit = unit;

    // 1. Stereo render at 48 kHz.
    s = host.configure(48000.0, 2, 2);
    if (r.check(s == noErr, "stereo 2->2 @48k initialise", statusText(s))) {
        auto res = host.render(3.0, 1.0);
        r.check(res.status == noErr, "stereo render calls succeed", res.describe());
        r.check(res.pulls > 0, "plug-in pulls its input");
        r.check(res.finite, "output is finite");
        r.check(res.rmsDb() > -40.0, "default settings are audible (> -40 dBFS for a -15 dBFS sine)", strf("%.1f dBFS", res.rmsDb()));
        r.check(toDb(res.outRms[1]) > -40.0, "right channel is written", strf("R %.1f dBFS", toDb(res.outRms[1])));
        r.info("default processing changes the signal", strf("output minus input = %.1f dB relative to input", res.diffDb()));

        // 2. Host bypass (kAudioUnitProperty_BypassEffect) passes the input through.
        UInt32 on = 1, off = 0;
        s = AudioUnitSetProperty(unit, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &on, sizeof(on));
        if (r.check(s == noErr, "set kAudioUnitProperty_BypassEffect", statusText(s))) {
            auto bypassed = host.render(1.5, 0.5);
            r.check(bypassed.status == noErr && bypassed.diffDb() < -40.0, "host bypass passes input through (residual < -40 dB)", bypassed.describe());
            AudioUnitSetProperty(unit, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &off, sizeof(off));
        }

        // 3. Parameters set through AudioUnitSetParameter change the output.
        const auto params = listParameters(unit);
        r.check(!params.empty(), "parameter list is published", strf("%zu parameters", params.size()));
        if (const auto* output = findParameter(params, { "Output", "Output Gain" })) {
            const float original = getParameter(unit, output->id);
            auto before = host.render(1.5, 0.5);
            s = setParameter(unit, output->id, output->info.minValue);
            auto after = host.render(1.5, 0.5);
            r.check(s == noErr && before.rmsDb() - after.rmsDb() > 12.0, "AudioUnitSetParameter(Output -> minimum) is audible",
                    strf("%.1f dBFS -> %.1f dBFS", before.rmsDb(), after.rmsDb()));
            setParameter(unit, output->id, original);
        } else {
            r.skip("AudioUnitSetParameter(Output) is audible", "no parameter named Output");
        }
        if (const auto* cutoff = findParameter(params, { "Cutoff" })) {
            const float original = getParameter(unit, cutoff->id);
            auto before = host.render(1.5, 0.5);
            setParameter(unit, cutoff->id, cutoff->info.minValue);
            auto after = host.render(1.5, 0.5);
            r.check(std::abs(before.rmsDb() - after.rmsDb()) > 1.0, "AudioUnitSetParameter(Cutoff -> minimum) is audible",
                    strf("%.1f dBFS -> %.1f dBFS", before.rmsDb(), after.rmsDb()));
            setParameter(unit, cutoff->id, original);
        }

        // 4. Wet/Dry 0 with unity output gain: output equals input.
        const auto* mix = findParameter(params, { "Wet / Dry", "Mix", "Global Mix" });
        const auto* output = findParameter(params, { "Output", "Output Gain" });
        if (mix != nullptr && output != nullptr) {
            const float mixWas = getParameter(unit, mix->id), outWas = getParameter(unit, output->id);
            setParameter(unit, mix->id, mix->info.minValue);
            setParameter(unit, output->id, 0.0f);
            auto dry = host.render(1.5, 0.5);
            r.check(dry.diffDb() < -40.0, "Wet/Dry 0 + Output 0 dB passes input through (residual < -40 dB)", dry.describe());
            setParameter(unit, mix->id, mixWas);
            setParameter(unit, output->id, outWas);
        }

        // 5. Factory presets via kAudioUnitProperty_PresentPreset.
        CFArrayRef presets = nullptr;
        UInt32 size = sizeof(presets);
        s = AudioUnitGetProperty(unit, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &presets, &size);
        if (r.check(s == noErr && presets != nullptr && CFArrayGetCount(presets) > 0, "factory presets are published",
                    strf("status=%s count=%ld", statusText(s).c_str(), presets ? long(CFArrayGetCount(presets)) : 0L))) {
            int audible = 0;
            const CFIndex count = CFArrayGetCount(presets);
            for (CFIndex i = 0; i < count; ++i) {
                AUPreset preset = *static_cast<const AUPreset*>(CFArrayGetValueAtIndex(presets, i));
                s = AudioUnitSetProperty(unit, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &preset, sizeof(preset));
                auto res = host.render(1.5, 0.5);
                const bool okay = s == noErr && res.status == noErr && res.finite && res.rmsDb() > -40.0;
                audible += okay ? 1 : 0;
                r.check(okay, strf("preset %ld '%s' is audible", long(preset.presetNumber), toString(preset.presetName).c_str()),
                        strf("%.1f dBFS", res.rmsDb()));
            }
            r.info("presets audible", strf("%d of %ld", audible, long(count)));
        }
        if (presets != nullptr) CFRelease(presets);

        // 6. ClassInfo round trip into a fresh instance. Change a few parameters
        // (never bypass) so the restore has something to prove, then put them back.
        std::vector<std::pair<AudioUnitParameterID, float>> changed;
        for (const auto& p : params) {
            if ((p.info.flags & kAudioUnitParameterFlag_IsWritable) == 0 || changed.size() >= 6) continue;
            if (strcasestr(p.name.c_str(), "bypass") != nullptr) continue;
            float v = p.info.minValue + 0.37f * (p.info.maxValue - p.info.minValue);
            if (isDiscrete(p.info)) v = std::round(v) == p.info.defaultValue ? p.info.maxValue : std::round(v);
            if (std::abs(v - p.info.defaultValue) < 1.0e-4f) continue;
            const float original = getParameter(unit, p.id);
            if (setParameter(unit, p.id, v) == noErr) changed.push_back({ p.id, original });
        }
        CFPropertyListRef state = nullptr;
        size = sizeof(state);
        s = AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &size);
        if (r.check(s == noErr && state != nullptr, "kAudioUnitProperty_ClassInfo readable", statusText(s))) {
            OSStatus s2 = noErr;
            AudioComponentInstance fresh = instantiate(component, outOfProcess, s2);
            if (r.check(fresh != nullptr, "second instance for state restore", statusText(s2))) {
                s2 = AudioUnitSetProperty(fresh, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof(state));
                int mismatches = 0;
                std::string firstMismatch;
                for (const auto& p : params) {
                    const float a = getParameter(unit, p.id), b = getParameter(fresh, p.id);
                    if (std::abs(a - b) > 1.0e-3f * std::max(1.0f, std::abs(a))) {
                        if (mismatches++ == 0) firstMismatch = strf("%s: %g vs %g", p.name.c_str(), double(a), double(b));
                    }
                }
                r.check(s2 == noErr && mismatches == 0, "state restores every parameter in a fresh instance",
                        strf("set=%s changed=%zu mismatches=%d %s", statusText(s2).c_str(), changed.size(), mismatches, firstMismatch.c_str()));
                AudioComponentInstanceDispose(fresh);
            }
        }
        if (state != nullptr) CFRelease(state);
        for (const auto& [id, original] : changed) setParameter(unit, id, original);
    }

    // 7. Other rates and block sizes.
    for (double rate : { 44100.0, 96000.0 }) {
        s = host.configure(rate, 2, 2, rate > 50000 ? 4096 : 64);
        if (r.check(s == noErr, strf("stereo @%.0f initialise (max %u frames)", rate, unsigned(host.maxFrames)), statusText(s))) {
            auto res = host.render(2.0, 1.0);
            r.check(res.status == noErr && res.finite && res.rmsDb() > -40.0, strf("stereo @%.0f is audible", rate), res.describe());
        }
    }

    // 8. Mono -> mono and mono -> stereo.
    s = host.configure(48000.0, 1, 1);
    if (r.check(s == noErr, "mono 1->1 initialise", statusText(s))) {
        auto res = host.render(2.0, 1.0);
        r.check(res.status == noErr && res.rmsDb() > -40.0, "mono 1->1 is audible", res.describe());
    }
    s = host.configure(48000.0, 1, 2);
    if (r.check(s == noErr, "mono 1->2 initialise", statusText(s))) {
        auto res = host.render(2.0, 1.0);
        r.check(res.status == noErr && toDb(res.outRms[0]) > -40.0 && toDb(res.outRms[1]) > -40.0, "mono 1->2 is audible on both outputs", res.describe());
    }
    AudioUnitUninitialize(unit);

    // 9. Editor.
    if (outOfProcess && !imageAfter) {
        requestViewControllerTest(r, unit, useWindow, false);
    } else {
        host.configure(48000.0, 2, 2);
        cocoaViewTest(r, unit, useWindow);
        requestViewControllerTest(r, unit, useWindow, false);
        AudioUnitUninitialize(unit);
    }

    AudioComponentInstanceDispose(unit);
    pump(0.2);
}

//==============================================================================
// AUAudioUnit API, out-of-process: the hosting route used by modern hosts.
void auAudioUnitSuite(Report& r, AudioComponentDescription description, bool useWindow) {
    r.scope = "AUAudioUnit out-of-process";
    __block AUAudioUnit* unit = nil;
    __block NSError* error = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [AUAudioUnit instantiateWithComponentDescription: description
                                             options: kAudioComponentInstantiation_LoadOutOfProcess
                                   completionHandler: ^(AUAudioUnit* u, NSError* e) {
                                       unit = u;
                                       error = e;
                                       dispatch_semaphore_signal(done);
                                   }];
    const bool answered = waitFor(done, 30.0);
    if (!r.check(answered && unit != nil, "instantiate", answered ? toString(error.description) : "timed out")) return;
    r.info("instance", strf("class=%s name=%s; plug-in image in harness process=%d; AUHostingService processes: %s",
                              toString(NSStringFromClass([unit class])).c_str(), toString(unit.audioUnitName).c_str(),
                              int(imageLoaded(pluginImageNeedle)), hostingServices().c_str()));

    AVAudioFormat* format48 = [[AVAudioFormat alloc] initStandardFormatWithSampleRate: 48000.0 channels: 2];
    NSError* e = nil;
    const bool inOkay = unit.inputBusses.count > 0 && [unit.inputBusses[0] setFormat: format48 error: &e];
    const bool outOkay = unit.outputBusses.count > 0 && [unit.outputBusses[0] setFormat: format48 error: &e];
    r.check(inOkay && outOkay, "set stereo 48 kHz bus formats", toString(e.description));
    unit.maximumFramesToRender = 512;
    e = nil;
    if (!r.check([unit allocateRenderResourcesAndReturnError: &e], "allocateRenderResources", toString(e.description))) return;

    // Buffers live here; blocks capture raw pointers only.
    Source source;
    source.sampleRate = 48000.0;
    std::vector<float> inScratch0(512), inScratch1(512), out0(512), out1(512);
    Source* src = &source;
    float* in0 = inScratch0.data();
    float* in1 = inScratch1.data();
    AURenderPullInputBlock pull = ^AUAudioUnitStatus(AudioUnitRenderActionFlags*, const AudioTimeStamp*, AUAudioFrameCount frames,
                                                       NSInteger, AudioBufferList* input) {
        for (UInt32 b = 0; b < input->mNumberBuffers; ++b) {
            if (input->mBuffers[b].mData == nullptr) {
                input->mBuffers[b].mData = b == 0 ? in0 : in1;
                input->mBuffers[b].mDataByteSize = frames * sizeof(float);
            }
        }
        src->fill(input, frames);
        return noErr;
    };
    AURenderBlock renderBlock = unit.renderBlock;
    Float64 sampleTime = 0;
    auto render = [&](double seconds, double skipSeconds) {
        RenderResult result;
        const auto total = size_t(seconds * 48000.0), skip = size_t(skipSeconds * 48000.0);
        source.reset(total);
        Meter meter;
        std::vector<uint8_t> storage(offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer) * 2);
        auto* abl = reinterpret_cast<AudioBufferList*>(storage.data());
        size_t doneFrames = 0;
        while (doneFrames < total) {
            const auto frames = UInt32(std::min<size_t>(512, total - doneFrames));
            abl->mNumberBuffers = 2;
            float* outs[2] = { out0.data(), out1.data() };
            for (UInt32 c = 0; c < 2; ++c) {
                abl->mBuffers[c].mNumberChannels = 1;
                abl->mBuffers[c].mDataByteSize = frames * sizeof(float);
                abl->mBuffers[c].mData = outs[c];
            }
            AudioUnitRenderActionFlags flags = 0;
            AudioTimeStamp ts {};
            ts.mFlags = kAudioTimeStampSampleTimeValid;
            ts.mSampleTime = sampleTime;
            result.status = renderBlock(&flags, &ts, frames, 0, abl, pull);
            if (result.status != noErr) break;
            const float* channels[2] = { static_cast<const float*>(abl->mBuffers[0].mData), static_cast<const float*>(abl->mBuffers[1].mData) };
            meter.add(channels, 2, frames, doneFrames, skip, source);
            sampleTime += frames;
            doneFrames += frames;
        }
        meter.finish(result, 2);
        result.pulls = source.pulls;
        return result;
    };

    auto res = render(3.0, 1.0);
    r.check(res.status == noErr, "renderBlock calls succeed", res.describe());
    r.check(res.pulls > 0, "plug-in pulls its input");
    r.check(res.finite && res.rmsDb() > -40.0, "default settings are audible", strf("%.1f dBFS", res.rmsDb()));
    r.check(toDb(res.outRms[1]) > -40.0, "right channel is written", strf("R %.1f dBFS", toDb(res.outRms[1])));

    // Parameter through the AUParameterTree (what generic UIs and automation use).
    AUParameter* output = nil;
    for (AUParameter* p in unit.parameterTree.allParameters)
        if ([p.displayName caseInsensitiveCompare: @"Output"] == NSOrderedSame) output = p;
    r.info("parameter tree", strf("%lu parameters", (unsigned long) unit.parameterTree.allParameters.count));
    if (output != nil) {
        const AUValue original = output.value;
        auto before = render(1.5, 0.5);
        output.value = output.minValue;
        auto after = render(1.5, 0.5);
        r.check(before.rmsDb() - after.rmsDb() > 12.0, "AUParameter(Output -> minimum) is audible",
                strf("%.1f dBFS -> %.1f dBFS", before.rmsDb(), after.rmsDb()));
        output.value = original;
    } else {
        r.skip("AUParameter(Output) is audible", "no parameter named Output");
    }

    unit.shouldBypassEffect = YES;
    auto bypassed = render(1.5, 0.5);
    r.check(bypassed.status == noErr && bypassed.diffDb() < -40.0, "shouldBypassEffect passes input through", bypassed.describe());
    unit.shouldBypassEffect = NO;

    NSArray<AUAudioUnitPreset*>* presets = unit.factoryPresets;
    r.check(presets.count > 0, "factoryPresets are published", strf("%lu", (unsigned long) presets.count));
    int audible = 0;
    for (AUAudioUnitPreset* preset in presets) {
        unit.currentPreset = preset;
        auto p = render(1.5, 0.5);
        const bool okay = p.status == noErr && p.finite && p.rmsDb() > -40.0;
        audible += okay ? 1 : 0;
        r.check(okay, strf("preset '%s' is audible", toString(preset.name).c_str()), strf("%.1f dBFS", p.rmsDb()));
    }
    r.info("presets audible", strf("%d of %lu", audible, (unsigned long) presets.count));

    NSDictionary<NSString*, id>* state = unit.fullState;
    r.check(state != nil, "fullState readable", strf("%lu keys", (unsigned long) state.count));
    [unit deallocateRenderResources];

    if (useWindow) {
        __block AUViewControllerBase* controller = nil;
        dispatch_semaphore_t viewDone = dispatch_semaphore_create(0);
        [unit requestViewControllerWithCompletionHandler: ^(AUViewControllerBase* vc) {
            controller = vc;
            dispatch_semaphore_signal(viewDone);
        }];
        const bool got = waitFor(viewDone, 20.0);
        if (r.check(got && controller != nil, "requestViewController delivers a controller", got ? "nil controller" : "timed out")) {
            NSWindow* window = [[NSWindow alloc] initWithContentRect: NSMakeRect(80, 80, 400, 300)
                                                           styleMask: NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                                             backing: NSBackingStoreBuffered
                                                               defer: NO];
            window.releasedWhenClosed = NO;
            window.contentViewController = controller;
            [window orderFront: nil];
            pump(2.0);
            NSView* view = controller.view;
            r.check(view != nil && NSWidth(view.frame) >= 300 && NSHeight(view.frame) >= 200, "remote editor has a sensible size after 2 s",
                    strf("class=%s view=%.0f x %.0f preferred=%.0f x %.0f", toString(NSStringFromClass([controller class])).c_str(),
                           NSWidth(view.frame), NSHeight(view.frame), controller.preferredContentSize.width, controller.preferredContentSize.height));
            [window orderOut: nil];
            window.contentViewController = nil;
            [window close];
        }
        controller = nil;
    } else {
        r.skip("requestViewController delivers a controller", "--no-window");
    }
    unit = nil;
    pump(0.3);
}

//==============================================================================
void describeComponent(Report& r, AudioComponent component) {
    CFStringRef name = nullptr;
    AudioComponentCopyName(component, &name);
    UInt32 version = 0;
    AudioComponentGetVersion(component, &version);
    AudioComponentDescription d {};
    AudioComponentGetDescription(component, &d);
    r.info("component", strf("name='%s' version=0x%x flags=0x%x sandboxSafe=%d isV3=%d canLoadInProcess=%d",
                               toString(name).c_str(), unsigned(version), unsigned(d.componentFlags),
                               int((d.componentFlags & kAudioComponentFlag_SandboxSafe) != 0),
                               int((d.componentFlags & kAudioComponentFlag_IsV3AudioUnit) != 0),
                               int((d.componentFlags & kAudioComponentFlag_CanLoadInProcess) != 0)));
    if (name != nullptr) CFRelease(name);
}

} // namespace

//==============================================================================
int main(int argc, const char* argv[]) {
    @autoreleasepool {
        bool strict = false, useWindow = true;
        std::string only, componentText = "aufx:Vel1:Veil";
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--strict") strict = true;
            else if (a == "--no-window") useWindow = false;
            else if (a == "--only" && i + 1 < argc) only = argv[++i];
            else if (a == "--component" && i + 1 < argc) componentText = argv[++i];
            else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 64; }
        }
        if (componentText.size() != 14 || componentText[4] != ':' || componentText[9] != ':') {
            std::fprintf(stderr, "--component expects type:subtype:manufacturer, e.g. aufx:Vel1:Veil\n");
            return 64;
        }

        // Watchdog: a hung load must fail the job, not stall it.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, int64_t(600) * NSEC_PER_SEC), dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
            std::printf("FAIL [watchdog] harness exceeded 600 s\n");
            std::fflush(stdout);
            _exit(3);
        });

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy: NSApplicationActivationPolicyAccessory];

        Report r;
        NSOperatingSystemVersion os = [[NSProcessInfo processInfo] operatingSystemVersion];
        r.info("environment", strf("macOS %ld.%ld.%ld, harness architecture %s",
                                     long(os.majorVersion), long(os.minorVersion), long(os.patchVersion), harnessArchitecture()));

        AudioComponentDescription description {};
        description.componentType = fourCC(componentText.substr(0, 4));
        description.componentSubType = fourCC(componentText.substr(5, 4));
        description.componentManufacturer = fourCC(componentText.substr(10, 4));
        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (!r.check(component != nullptr, "component registered", componentText)) {
            std::printf("SUMMARY passed=%d failed=%d skipped=%d\n", r.passed, r.failed, r.skipped);
            return 2;
        }
        int matches = 0;
        for (AudioComponent c = nullptr; (c = AudioComponentFindNext(c, &description)) != nullptr;) ++matches;
        r.check(matches == 1, "exactly one registered component matches", strf("%d", matches));
        describeComponent(r, component);

        // Out-of-process first, before the in-process load maps the plug-in into this process.
        if (only.empty() || only == "v2-oop") v2Suite(r, component, true, useWindow);
        if (only.empty() || only == "auaudiounit") auAudioUnitSuite(r, description, useWindow);
        if (only.empty() || only == "v2-inproc") v2Suite(r, component, false, useWindow);

        r.scope = "summary";
        std::printf("SUMMARY passed=%d failed=%d skipped=%d\n", r.passed, r.failed, r.skipped);
        std::fflush(stdout);
        return strict && r.failed > 0 ? 1 : 0;
    }
}
