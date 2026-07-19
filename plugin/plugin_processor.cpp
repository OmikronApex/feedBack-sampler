#include "plugin_processor.h"

#include "plugin_editor.h"

#include "fbsampler/sfz_frontend.h"
#include "fbsampler/validate.h"
#include "fbsampler/version.h"

#include <algorithm>
#include <utility>

namespace fbsampler {

namespace {

// Enough for any realistic host block; beyond this, extra events in one block
// are dropped instead of allocating on the audio thread.
constexpr std::size_t kMaxEventsPerBlock = 4096;

std::string parentDirectory(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

// Real-world libraries can produce thousands of warnings (one per
// unsupported opcode); rendering them all into the status label stalls the
// message thread — the label re-lays-out the full text every paint, and the
// plugin shares that thread with the whole host UI. Show errors first, then
// warnings, hard-capped.
constexpr int kMaxDiagnosticLines = 12;

juce::String describeDiagnostics(const std::vector<Diagnostic>& diagnostics)
{
    juce::String out;
    int shown = 0;
    for (const bool errorsPass : {true, false}) {
        for (const Diagnostic& d : diagnostics) {
            if ((d.severity == Severity::Error) != errorsPass)
                continue;
            if (shown == kMaxDiagnosticLines) {
                out << "... and "
                    << static_cast<int>(diagnostics.size()) - shown
                    << " more diagnostics\n";
                return out.trimEnd();
            }
            out << (errorsPass ? "error" : "warning")
                << ": " << juce::String(d.message);
            if (!d.code.empty())
                out << " [" << juce::String(d.code) << "]";
            out << "\n";
            ++shown;
        }
    }
    return out.trimEnd();
}

} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      pool_(createAllRamSamplePool())
{
    eventScratch_.reserve(kMaxEventsPerBlock);
}

PluginProcessor::~PluginProcessor()
{
    joinLoadThread();
}

void PluginProcessor::joinLoadThread()
{
    if (loadThread_.joinable())
        loadThread_.join();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(sampleRate, samplesPerBlock);
    eventScratch_.reserve(kMaxEventsPerBlock);
}

void PluginProcessor::releaseResources() {}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Translate the host MIDI buffer preserving per-event sample positions
    // (FR-11/NFR-2: notes trigger mid-block, never quantized to block start).
    eventScratch_.clear();
    for (const juce::MidiMessageMetadata metadata : midi) {
        if (eventScratch_.size() >= kMaxEventsPerBlock)
            break; // never allocate on the audio thread
        const juce::MidiMessage msg = metadata.getMessage();
        EngineEvent e;
        e.delayFrames = metadata.samplePosition;
        if (msg.isNoteOn(true) && msg.getVelocity() == 0) {
            // MIDI convention: a note-on with velocity 0 is a note-off. Route it
            // there instead of into NoteOn, or the note would stick on.
            e.type = EngineEvent::Type::NoteOff;
            e.note = static_cast<std::uint8_t>(msg.getNoteNumber());
            e.velocity = 0;
        } else if (msg.isNoteOn(true)) {
            e.type = EngineEvent::Type::NoteOn;
            e.note = static_cast<std::uint8_t>(msg.getNoteNumber());
            e.velocity = static_cast<std::uint8_t>(msg.getVelocity());
        } else if (msg.isNoteOff(true)) {
            e.type = EngineEvent::Type::NoteOff;
            e.note = static_cast<std::uint8_t>(msg.getNoteNumber());
            e.velocity = 0;
        } else if (msg.isController()) {
            e.type = EngineEvent::Type::ControlChange;
            e.note = static_cast<std::uint8_t>(msg.getControllerNumber());
            e.velocity = static_cast<std::uint8_t>(msg.getControllerValue());
        } else if (msg.isPitchWheel()) {
            e.type = EngineEvent::Type::PitchBend;
            e.bendValue = msg.getPitchWheelValue() - 8192; // 0..16383 -> -8192..8191
        } else {
            continue;
        }
        eventScratch_.push_back(e);
    }

    const int numFrames = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() >= 2 && numFrames > 0) {
        float* out[2] = { buffer.getWritePointer(0), buffer.getWritePointer(1) };
        engine_.process(eventScratch_.data(), eventScratch_.size(),
                        out, static_cast<std::size_t>(numFrames));
    }
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

bool PluginProcessor::loadSfzFileAsync(const juce::String& sfzPath)
{
    bool expected = false;
    if (!loadBusy_.compare_exchange_strong(expected, true)) {
        setLoadStatusText("a load is already in progress");
        sendChangeMessage();
        return false;
    }
    joinLoadThread(); // reap the previous (finished) loader thread

    setLoadStatusText("loading " + juce::File(sfzPath).getFileName() + "...");
    sendChangeMessage();

    loadThread_ = std::thread([this, path = sfzPath.toStdString()] {
        loadSfzFileSync(path);
        loadBusy_.store(false);
        sendChangeMessage(); // async + thread-safe; the editor hops to the
                             // message thread via ChangeListener delivery
    });
    return true;
}

bool PluginProcessor::loadSfzFileSync(const std::string& sfzPath)
{
    // Full load pipeline off the audio thread: lower -> validate -> pool bind
    // + engine snapshot swap (Story 1.4 mechanism). On any error the previous
    // snapshot keeps sounding (FR-5 seed).
    LowerResult lowered = lowerSfzFile(sfzPath);
    std::vector<Diagnostic> diagnostics = std::move(lowered.diagnostics);

    bool ok = false;
    if (lowered.model) {
        std::vector<Diagnostic> vd = validate(*lowered.model);
        const bool invalid = std::any_of(vd.begin(), vd.end(),
            [](const Diagnostic& d) { return d.severity == Severity::Error; });
        diagnostics.insert(diagnostics.end(), vd.begin(), vd.end());
        if (!invalid) {
            std::vector<Diagnostic> ed =
                engine_.load(*lowered.model, pool_, parentDirectory(sfzPath));
            ok = std::none_of(ed.begin(), ed.end(),
                [](const Diagnostic& d) { return d.severity == Severity::Error; });
            diagnostics.insert(diagnostics.end(), ed.begin(), ed.end());
        }
    }

    juce::String status;
    if (ok) {
        status = "loaded " + juce::File(juce::String(sfzPath)).getFileName();
        if (!diagnostics.empty())
            status += "\n" + describeDiagnostics(diagnostics);
    } else {
        status = "load failed";
        if (!diagnostics.empty())
            status += "\n" + describeDiagnostics(diagnostics);
        status += "\n(previous instrument, if any, is still active)";
    }
    setLoadStatusText(status);
    return ok;
}

juce::String PluginProcessor::getLoadStatusText() const
{
    const juce::ScopedLock lock(statusLock_);
    return statusText_;
}

void PluginProcessor::setLoadStatusText(const juce::String& text)
{
    const juce::ScopedLock lock(statusLock_);
    statusText_ = text;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

bool PluginProcessor::hasEditor() const { return true; }

const juce::String PluginProcessor::getName() const { return "feedBack Sampler"; }
bool PluginProcessor::acceptsMidi() const { return true; }
bool PluginProcessor::producesMidi() const { return false; }
bool PluginProcessor::isMidiEffect() const { return false; }
double PluginProcessor::getTailLengthSeconds() const { return 0.0; }

int PluginProcessor::getNumPrograms() { return 1; }
int PluginProcessor::getCurrentProgram() { return 0; }
void PluginProcessor::setCurrentProgram(int) {}
const juce::String PluginProcessor::getProgramName(int) { return {}; }
void PluginProcessor::changeProgramName(int, const juce::String&) {}

void PluginProcessor::getStateInformation(juce::MemoryBlock&) {}
void PluginProcessor::setStateInformation(const void*, int) {}

} // namespace fbsampler

#ifndef FBSAMPLER_NO_PLUGIN_FILTER_ENTRY
// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new fbsampler::PluginProcessor();
}
#endif
