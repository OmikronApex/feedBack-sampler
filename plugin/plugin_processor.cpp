#include "plugin_processor.h"

#include "plugin_editor.h"
#include "ui/scan_banner_model.h"

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

std::string statusDetailFor(const std::vector<Diagnostic>& diagnostics)
{
    for (const Diagnostic& d : diagnostics)
        if (d.severity == Severity::Error)
            return d.code + ": " + d.message;
    return "load_failed: unknown";
}

} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      pool_(createAllRamSamplePool()),
      // Story 3.2 (AD-9): reads the shared per-user index at startup WITHOUT
      // rescanning — a second instance sees the first one's scan results.
      // First-run auto-scan is triggered on first folder add (Story 3.5),
      // never at startup.
      configService_(std::make_unique<ConfigService>())
{
    // Persisted voice limit applies to every snapshot this instance builds.
    engine_.setVoiceLimit(configService_->settings().voiceLimit);
    eventScratch_.reserve(kMaxEventsPerBlock);
    for (auto& slot : pendingCc_)
        slot.store(-1, std::memory_order_relaxed);

    // Story 2.4 (FR-9, AD-8): dedicated, stable-identity generic parameter
    // for soundfont preset selection. Fixed 0..255 range (host parameter
    // ranges are immutable); values are clamped to the open session's
    // preset count. Value-to-text surfaces "bank:program name" so generic
    // DAW parameter views show real preset names without any styled UI
    // (the visible selector row is Story 3.4's).
    presetParam_ = new juce::AudioParameterInt(
        juce::ParameterID { "presetIndex", 1 }, "Soundfont Preset", 0, 255, 0,
        juce::AudioParameterIntAttributes {}.withStringFromValueFunction(
            [this](int value, int) -> juce::String {
                std::shared_ptr<Sf2Session> session;
                {
                    const juce::ScopedLock lock(sessionLock_);
                    session = session_;
                }
                if (session != nullptr
                    && value >= 0
                    && static_cast<std::size_t>(value) < session->presets().size()) {
                    const auto& info =
                        session->presets()[static_cast<std::size_t>(value)];
                    return juce::String(info.bank) + ":" + juce::String(info.program)
                        + " " + juce::String(info.name);
                }
                return "preset " + juce::String(value);
            }));
    addParameter(presetParam_); // processor takes ownership
    presetParam_->addListener(this);
}

PluginProcessor::~PluginProcessor()
{
    cancelPendingUpdate();
    if (presetParam_ != nullptr)
        presetParam_->removeListener(this);
    joinLoadThread();
}

void PluginProcessor::joinLoadThread()
{
    if (loadThread_.joinable())
        loadThread_.join();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    smoothedGainL_.reset(sampleRate, 0.02);
    smoothedGainR_.reset(sampleRate, 0.02);
    engine_.prepare(sampleRate, samplesPerBlock);
    eventScratch_.reserve(kMaxEventsPerBlock);
    for (auto& slot : pendingCc_)
        slot.store(-1, std::memory_order_relaxed);
}

void PluginProcessor::releaseResources() {}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Translate the host MIDI buffer preserving per-event sample positions
    // (FR-11/NFR-2: notes trigger mid-block, never quantized to block start).
    eventScratch_.clear();

    // Story 3.4: drain UI-generated CC values (atomic slots, no locks) as
    // block-start ControlChange events — same queue host MIDI uses.
    for (int cc = 0; cc < 128; ++cc) {
        const int pending = pendingCc_[static_cast<std::size_t>(cc)].exchange(
            -1, std::memory_order_acq_rel);
        if (pending >= 0 && eventScratch_.size() < kMaxEventsPerBlock) {
            EngineEvent e;
            e.type = EngineEvent::Type::ControlChange;
            e.delayFrames = 0;
            e.note = static_cast<std::uint8_t>(cc);
            e.velocity = static_cast<std::uint8_t>(pending);
            eventScratch_.push_back(e);
        }
    }

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

        // Story 3.4 generic set: post-engine volume/pan, smoothed to avoid
        // zipper noise. Equal-power pan law.
        const float volumeDb = masterVolumeDb_.load(std::memory_order_relaxed);
        const float pan = masterPan_.load(std::memory_order_relaxed);
        const float gain = juce::Decibels::decibelsToGain(volumeDb);
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi / 4.0f;
        smoothedGainL_.setTargetValue(gain * std::cos(angle)
                                      * juce::MathConstants<float>::sqrt2);
        smoothedGainR_.setTargetValue(gain * std::sin(angle)
                                      * juce::MathConstants<float>::sqrt2);
        for (int i = 0; i < numFrames; ++i) {
            out[0][i] *= smoothedGainL_.getNextValue();
            out[1][i] *= smoothedGainR_.getNextValue();
        }
    }
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void PluginProcessor::startLoaderThread(std::function<void()> body)
{
    joinLoadThread(); // reap the previous (finished) loader thread
    loadThread_ = std::thread([this, body = std::move(body)] {
        body();
        loadBusy_.store(false);
        sendChangeMessage(); // async + thread-safe; the editor hops to the
                             // message thread via ChangeListener delivery
    });
}

bool PluginProcessor::loadSfzFileAsync(const juce::String& sfzPath)
{
    bool expected = false;
    if (!loadBusy_.compare_exchange_strong(expected, true)) {
        setLoadStatusText("a load is already in progress");
        sendChangeMessage();
        return false;
    }

    setLoadStatusText("loading " + juce::File(sfzPath).getFileName() + "...");
    sendChangeMessage();

    startLoaderThread([this, path = sfzPath.toStdString()] { loadSfzFileSync(path); });
    return true;
}

bool PluginProcessor::loadSf2FileAsync(const juce::String& sf2Path)
{
    bool expected = false;
    if (!loadBusy_.compare_exchange_strong(expected, true)) {
        setLoadStatusText("a load is already in progress");
        sendChangeMessage();
        return false;
    }

    setLoadStatusText("loading " + juce::File(sf2Path).getFileName() + "...");
    sendChangeMessage();

    startLoaderThread([this, path = sf2Path.toStdString()] { loadSf2FileSync(path); });
    return true;
}

bool PluginProcessor::rescanLibrariesAsync()
{
    bool expected = false;
    if (!loadBusy_.compare_exchange_strong(expected, true)) {
        setLoadStatusText("a load is already in progress");
        sendChangeMessage();
        return false;
    }

    setLoadStatusText("scanning library folders...");
    sendChangeMessage();

    scanBusy_.store(true);
    startLoaderThread([this] {
        // Throttled progress relay (~4 Hz): the banner shows current path +
        // recognized count; the browser list refreshes on the same ticks.
        ui::ScanBannerModel banner;
        const auto diags = configService_->scan([this, &banner](
                                                    const ScanProgress& p) {
            const bool notify = banner.onProgress(
                p, static_cast<std::int64_t>(
                       juce::Time::getMillisecondCounter()));
            {
                const juce::ScopedLock lock(scanLock_);
                scanProgressText_ = banner.text;
            }
            if (notify)
                sendChangeMessage();
        });
        scanBusy_.store(false);
        const bool failed = std::any_of(
            diags.begin(), diags.end(), [](const Diagnostic& d) {
                return d.severity == Severity::Error;
            });
        setLoadStatusText(failed
                              ? juce::String("library scan finished with errors")
                              : juce::String("library scan complete: ")
                                    + juce::String(static_cast<int>(
                                        configService_->index().entries.size()))
                                    + " libraries");
    });
    return true;
}

juce::String PluginProcessor::scanProgressText() const
{
    const juce::ScopedLock lock(scanLock_);
    return scanProgressText_;
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
            if (ok) {
                const juce::ScopedLock lock(modelLock_);
                currentModel_ = *lowered.model;
                currentRoot_ = parentDirectory(sfzPath);
                hasModel_ = true;
            }
        }
    }

    // Story 3.6: persist success/failure against the index entry (no-op for
    // paths that are not in the index).
    configService_->setEntryStatus(sfzPath, ok,
                                   ok ? std::string()
                                      : statusDetailFor(diagnostics));

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

bool PluginProcessor::loadSf2FileSync(const std::string& sf2Path, int presetIndex)
{
    auto opened = openSf2Session(sf2Path);
    if (!opened.session) {
        configService_->setEntryStatus(sf2Path, false,
                                       statusDetailFor(opened.diagnostics));
        setLoadStatusText("load failed\n" + describeDiagnostics(opened.diagnostics)
                          + "\n(previous instrument, if any, is still active)");
        return false;
    }
    if (opened.session->presets().empty()) {
        setLoadStatusText("load failed: soundfont has no presets");
        return false;
    }
    {
        const juce::ScopedLock lock(sessionLock_);
        session_ = opened.session;
    }
    const int count = static_cast<int>(opened.session->presets().size());
    const int index = presetIndex < 0 ? 0 : std::min(presetIndex, count - 1);
    return applyPresetIndexSync(index);
}

bool PluginProcessor::applyPresetIndexSync(int presetIndex)
{
    std::shared_ptr<Sf2Session> session;
    {
        const juce::ScopedLock lock(sessionLock_);
        session = session_;
    }
    if (!session) {
        setLoadStatusText("no soundfont session open");
        return false;
    }
    const int count = static_cast<int>(session->presets().size());
    if (count == 0)
        return false;
    const int index = std::clamp(presetIndex, 0, count - 1);

    // Lower from the parsed hydra (in-memory transforms only) and rebind
    // through the shared pool: already-pooled sf2:// references refcount-hit,
    // so a preset switch performs zero new sample-file reads for shared
    // samples. The AD-3 snapshot swap keeps the old preset audible until the
    // new snapshot is live; sounding notes finish under the old snapshot.
    LowerResult lowered = session->lowerPreset(static_cast<std::size_t>(index));
    std::vector<Diagnostic> diagnostics = std::move(lowered.diagnostics);
    bool ok = false;
    if (lowered.model) {
        std::vector<Diagnostic> ed =
            engine_.load(*lowered.model, pool_, parentDirectory(session->path()));
        ok = std::none_of(ed.begin(), ed.end(),
            [](const Diagnostic& d) { return d.severity == Severity::Error; });
        diagnostics.insert(diagnostics.end(), ed.begin(), ed.end());
    }

    configService_->setEntryStatus(session->path(), ok,
                                   ok ? std::string()
                                      : statusDetailFor(diagnostics));
    if (ok) {
        {
            const juce::ScopedLock lock(modelLock_);
            currentModel_ = *lowered.model;
            currentRoot_ = parentDirectory(session->path());
            hasModel_ = true;
        }
        currentPresetIndex_.store(index);
        if (presetParam_ != nullptr
            && static_cast<int>(presetParam_->get()) != index) {
            // Reflect the effective (clamped) selection back to the host.
            presetParam_->beginChangeGesture();
            *presetParam_ = index;
            presetParam_->endChangeGesture();
        }
        const auto& info = session->presets()[static_cast<std::size_t>(index)];
        juce::String status = "loaded " + juce::File(juce::String(session->path())).getFileName()
            + " [" + juce::String(info.bank) + ":" + juce::String(info.program) + " "
            + juce::String(info.name) + "]";
        if (!diagnostics.empty())
            status += "\n" + describeDiagnostics(diagnostics);
        setLoadStatusText(status);
    } else {
        setLoadStatusText("preset switch failed\n" + describeDiagnostics(diagnostics)
                          + "\n(previous preset is still active)");
    }
    return ok;
}

std::vector<Sf2PresetInfo> PluginProcessor::currentPresets() const
{
    const juce::ScopedLock lock(sessionLock_);
    return session_ ? session_->presets() : std::vector<Sf2PresetInfo> {};
}

std::vector<ControlMapEntry> PluginProcessor::currentControls() const
{
    const juce::ScopedLock lock(modelLock_);
    return hasModel_ ? currentModel_.controls : std::vector<ControlMapEntry> {};
}

bool PluginProcessor::isSoundfontLoaded() const
{
    const juce::ScopedLock lock(sessionLock_);
    return session_ != nullptr;
}

void PluginProcessor::setControlValue(int ccNumber, int value0to127)
{
    if (ccNumber < 0 || ccNumber > 127)
        return;
    pendingCc_[static_cast<std::size_t>(ccNumber)].store(
        std::clamp(value0to127, 0, 127), std::memory_order_release);
}

void PluginProcessor::setMasterVolumeDb(float volumeDb)
{
    masterVolumeDb_.store(std::clamp(volumeDb, -60.0f, 12.0f),
                          std::memory_order_relaxed);
}

void PluginProcessor::setMasterPan(float pan)
{
    masterPan_.store(std::clamp(pan, -1.0f, 1.0f), std::memory_order_relaxed);
}

bool PluginProcessor::applyModelOffsetsAsync(float tuningCents,
                                             float attackSeconds,
                                             float releaseSeconds)
{
    // Route (b), documented tradeoff: re-lower the stored model with offsets
    // and swap snapshots via the proven loader path — zero engine changes,
    // but nothing changes audibly mid-note (2.4 swap semantics). Debounce is
    // gesture-level: the UI calls this on drag end, not per mouse move.
    InstrumentModel model;
    std::string root;
    {
        const juce::ScopedLock lock(modelLock_);
        if (!hasModel_)
            return false;
        model = currentModel_;
        root = currentRoot_;
    }

    bool expected = false;
    if (!loadBusy_.compare_exchange_strong(expected, true))
        return false;

    for (auto& region : model.regions) {
        region.tuningCents += tuningCents;
        if (attackSeconds >= 0.0f)
            region.amplitudeEnvelope.attackSeconds = attackSeconds;
        if (releaseSeconds >= 0.0f)
            region.amplitudeEnvelope.releaseSeconds = releaseSeconds;
    }

    startLoaderThread([this, model = std::move(model), root = std::move(root)] {
        const auto ed = engine_.load(model, pool_, root);
        const bool ok = std::none_of(ed.begin(), ed.end(),
            [](const Diagnostic& d) { return d.severity == Severity::Error; });
        if (!ok)
            setLoadStatusText("control change failed\n"
                              + describeDiagnostics(ed));
    });
    return true;
}

bool PluginProcessor::applyVoiceLimitSync(int maxVoices)
{
    engine_.setVoiceLimit(maxVoices);
    InstrumentModel model;
    std::string root;
    {
        const juce::ScopedLock lock(modelLock_);
        if (!hasModel_)
            return true; // stored; applies on next load
        model = currentModel_;
        root = currentRoot_;
    }
    const auto ed = engine_.load(model, pool_, root);
    return std::none_of(ed.begin(), ed.end(),
        [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

bool PluginProcessor::applyVoiceLimitAsync(int maxVoices)
{
    bool expected = false;
    if (!loadBusy_.compare_exchange_strong(expected, true))
        return false;
    startLoaderThread([this, maxVoices] { applyVoiceLimitSync(maxVoices); });
    return true;
}

void PluginProcessor::selectPreset(int presetIndex)
{
    // Message-thread UI entry: drive the EXISTING presetIndex parameter; the
    // 2.4 deferral (parameter listener -> async updater -> loader thread)
    // does the actual switch.
    if (presetParam_ == nullptr)
        return;
    presetParam_->beginChangeGesture();
    *presetParam_ = presetIndex;
    presetParam_->endChangeGesture();
}

void PluginProcessor::parameterValueChanged(int, float)
{
    // May arrive on the audio thread (host automation): never lower or load
    // here — defer to the message thread, which hands off to the loader
    // thread (AD-3 command path).
    presetParamPending_.store(true);
    triggerAsyncUpdate();
}

void PluginProcessor::handleAsyncUpdate()
{
    if (statePending_.exchange(false)) {
        bool expected = false;
        if (loadBusy_.compare_exchange_strong(expected, true))
            startLoaderThread([this] { applyPendingStateSyncBody(); });
        else
            statePending_.store(true); // retry after the current load
        return;
    }
    if (presetParamPending_.exchange(false)) {
        std::shared_ptr<Sf2Session> session;
        {
            const juce::ScopedLock lock(sessionLock_);
            session = session_;
        }
        if (!session)
            return; // no soundfont open; parameter is inert (sfz instrument)
        const int target = presetParam_ != nullptr ? presetParam_->get() : 0;
        if (target == currentPresetIndex_.load())
            return;
        bool expected = false;
        if (loadBusy_.compare_exchange_strong(expected, true))
            startLoaderThread([this, target] { applyPresetIndexSync(target); });
        else {
            presetParamPending_.store(true); // coalesce; retry on next update
            triggerAsyncUpdate();
        }
    }
}

bool PluginProcessor::applyPendingStateSync()
{
    bool expected = false;
    if (!loadBusy_.compare_exchange_strong(expected, true))
        return false;
    const bool ok = applyPendingStateSyncBody();
    loadBusy_.store(false);
    return ok;
}

bool PluginProcessor::applyPendingStateSyncBody()
{
    PendingState state;
    {
        const juce::ScopedLock lock(pendingStateLock_);
        state = pendingState_;
    }
    if (state.path.isEmpty())
        return false;
    if (!state.isSf2)
        return loadSfzFileSync(state.path.toStdString());

    // Restore by bank/program first (stable across file edits), index as
    // fallback (AD-3 state chunk contract).
    auto opened = openSf2Session(state.path.toStdString());
    if (!opened.session || opened.session->presets().empty()) {
        setLoadStatusText("state restore failed: cannot reopen "
                          + juce::File(state.path).getFileName());
        return false;
    }
    int index = -1;
    const auto& presets = opened.session->presets();
    for (std::size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].bank == state.bank && presets[i].program == state.program) {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0)
        index = state.presetIndex >= 0 ? state.presetIndex : 0;
    {
        const juce::ScopedLock lock(sessionLock_);
        session_ = opened.session;
    }
    return applyPresetIndexSync(index);
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

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // AD-3 state chunk (v0 slice): library reference + soundfont preset
    // identity (bank/program preferred, index fallback). Epic 6 owns the
    // full robustness story; this keeps preset selection round-trippable.
    juce::XmlElement xml("fbsampler-state");
    xml.setAttribute("version", 1);

    std::shared_ptr<Sf2Session> session;
    {
        const juce::ScopedLock lock(sessionLock_);
        session = session_;
    }
    const int index = currentPresetIndex_.load();
    if (session != nullptr && index >= 0
        && static_cast<std::size_t>(index) < session->presets().size()) {
        const auto& info = session->presets()[static_cast<std::size_t>(index)];
        xml.setAttribute("type", "sf2");
        xml.setAttribute("path", juce::String(session->path()));
        xml.setAttribute("bank", info.bank);
        xml.setAttribute("program", info.program);
        xml.setAttribute("presetIndex", index);
    } else {
        xml.setAttribute("type", "none");
    }
    copyXmlToBinary(xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName("fbsampler-state"))
        return;
    if (xml->getStringAttribute("type") != "sf2")
        return;

    PendingState state;
    state.path = xml->getStringAttribute("path");
    state.isSf2 = true;
    state.bank = xml->getIntAttribute("bank", -1);
    state.program = xml->getIntAttribute("program", -1);
    state.presetIndex = xml->getIntAttribute("presetIndex", -1);
    if (state.path.isEmpty())
        return;
    {
        const juce::ScopedLock lock(pendingStateLock_);
        pendingState_ = state;
    }
    // Never load on the calling thread (hosts may call this from odd
    // threads): defer through the async updater -> loader thread. Tests use
    // applyPendingStateSync().
    statePending_.store(true);
    triggerAsyncUpdate();
}

} // namespace fbsampler

#ifndef FBSAMPLER_NO_PLUGIN_FILTER_ENTRY
// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new fbsampler::PluginProcessor();
}
#endif
