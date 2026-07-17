#include "plugin_processor.h"

#include "fbsampler/version.h"

namespace fbsampler {

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void PluginProcessor::prepareToPlay(double, int) {}
void PluginProcessor::releaseResources() {}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
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

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new fbsampler::PluginProcessor();
}
