// --- START OF FILE PluginEditor.h ---

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ... (AbletonLookAndFeel, InfoBarCombo, InfoBarButton, VisualizerComponent are same as before) ...
// ... (Please refer to previous PluginEditor.h for these classes) ...

// ==============================================================================
// 1. Custom LookAndFeel (Ableton Style)
// ==============================================================================
class AbletonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AbletonLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
        const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
        int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawTickBox(juce::Graphics& g, juce::Component& component,
        float x, float y, float w, float h,
        bool ticked, bool isEnabled,
        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont(juce::Label&) override { return getCustomFont(); }
    juce::Font getCustomFont() { return juce::Font("Meiryo UI", 13.0f, juce::Font::plain); }

private:
    const juce::Colour colorBg = juce::Colour(0xFFF0F0F0);
    const juce::Colour colorPanel = juce::Colour(0xFFE1E1E1);
    const juce::Colour colorAccent = juce::Colour(0xFFFF764D);
    const juce::Colour colorText = juce::Colour(0xFF1A1A1A);
    const juce::Colour colorKnobBg = juce::Colour(0xFFDCDCDC);
    const juce::Colour colorKnobTrack = juce::Colour(0xFFAAAAAA);
    const juce::Colour colorOutline = juce::Colour(0xFF888888);
};

// ==============================================================================
// 2. InfoBar Components
// ==============================================================================

class AbletonKnob : public juce::Slider {
public:
    juce::String nameEN, nameJP, description, unit;
    bool isFreq = false;
    bool isNote = false;
    bool requiresKeyTrackInfo = false;

    std::function<void(const juce::String&, bool)> onInfoUpdate;
    std::function<void()> onInfoClear;

    AbletonKnob() {
        setSliderStyle(juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        setPopupDisplayEnabled(true, false, nullptr);
    }

    void setup(const juce::String& name, const juce::String& suffix) {
        setName(name);
        setTextValueSuffix(suffix);
    }

    void mouseEnter(const juce::MouseEvent& e) override {
        if (onInfoUpdate) updateInfo();
        juce::Slider::mouseEnter(e);
    }
    void mouseExit(const juce::MouseEvent& e) override {
        if (onInfoClear) onInfoClear();
        juce::Slider::mouseExit(e);
    }
    void valueChanged() override {
        if (isMouseOverOrDragging() && onInfoUpdate) updateInfo();
        juce::Slider::valueChanged();
    }

    // Added: Mouse interaction for Reset and Numeric Entry
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

    void updateInfo();
private:
    juce::String getNoteStr(double hz);
};

class InfoBarCombo : public juce::ComboBox {
public:
    juce::String nameEN, nameJP, description;
    juce::StringArray itemDescriptions;
    std::function<void(const juce::String&)> onInfoUpdate;
    std::function<void()> onInfoClear;

    void mouseEnter(const juce::MouseEvent& e) override { if (onInfoUpdate) updateInfo(); juce::ComboBox::mouseEnter(e); }
    void mouseExit(const juce::MouseEvent& e) override { if (onInfoClear) onInfoClear(); juce::ComboBox::mouseExit(e); }
    void updateInfo();
};

class InfoBarButton : public juce::ToggleButton {
public:
    juce::String nameJP, description;
    std::function<void(const juce::String&)> onInfoUpdate;
    std::function<void()> onInfoClear;

    void mouseEnter(const juce::MouseEvent& e) override { if (onInfoUpdate) onInfoUpdate(nameJP + ": " + description); juce::ToggleButton::mouseEnter(e); }
    void mouseExit(const juce::MouseEvent& e) override { if (onInfoClear) onInfoClear(); juce::ToggleButton::mouseExit(e); }
};

// ==============================================================================
// 3. Visualizer
// ==============================================================================
class VisualizerComponent : public juce::Component {
public:
    void setProcessor(NextGenSaturationAudioProcessor* p) { processor = p; }

    void update() {
        if (!processor) return;

        int start1, size1, start2, size2;
        processor->scopeFifo.prepareToRead(1024, start1, size1, start2, size2);

        if (size1 + size2 > 0) {
            if (size1 > 0) {
                for (int i = 0; i < size1; ++i) {
                    inputBuffer.add(processor->scopeDataInput[start1 + i]);
                    outputBuffer.add(processor->scopeDataOutput[start1 + i]);
                }
            }
            if (size2 > 0) {
                for (int i = 0; i < size2; ++i) {
                    inputBuffer.add(processor->scopeDataInput[start2 + i]);
                    outputBuffer.add(processor->scopeDataOutput[start2 + i]);
                }
            }
            processor->scopeFifo.finishedRead(size1 + size2);

            while (inputBuffer.size() > 256) inputBuffer.remove(0);
            while (outputBuffer.size() > 256) outputBuffer.remove(0);

            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        if (getWidth() <= 0 || getHeight() <= 0) return;

        g.setColour(juce::Colour(0xFF222222));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
        g.setColour(juce::Colours::grey);
        g.drawRect(getLocalBounds(), 1);

        if (inputBuffer.isEmpty()) return;

        float w = (float)getWidth();
        float h = (float)getHeight();
        float mid = h * 0.5f;
        float scaleX = w / (float)inputBuffer.size();

        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        juce::Path pIn;
        pIn.startNewSubPath(0, mid - inputBuffer[0] * mid * 0.8f);
        for (int i = 1; i < inputBuffer.size(); ++i) {
            pIn.lineTo((float)i * scaleX, mid - inputBuffer[i] * mid * 0.8f);
        }
        g.strokePath(pIn, juce::PathStrokeType(1.0f));

        g.setColour(juce::Colour(0xFFFF764D));
        juce::Path pOut;
        pOut.startNewSubPath(0, mid - outputBuffer[0] * mid * 0.8f);
        for (int i = 1; i < outputBuffer.size(); ++i) {
            pOut.lineTo((float)i * scaleX, mid - outputBuffer[i] * mid * 0.8f);
        }
        g.strokePath(pOut, juce::PathStrokeType(2.0f));
    }

private:
    NextGenSaturationAudioProcessor* processor = nullptr;
    juce::Array<float> inputBuffer;
    juce::Array<float> outputBuffer;
};

// ==============================================================================
// 4. Main Editor Class
// ==============================================================================
class NextGenSaturationAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    NextGenSaturationAudioProcessorEditor(NextGenSaturationAudioProcessor&);
    ~NextGenSaturationAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateInfoBar(const juce::String& text);
    void updateKnobProperties(int satType);

    NextGenSaturationAudioProcessor& audioProcessor;
    AbletonLookAndFeel abletonLnF;

    // --- Components ---
    AbletonKnob inputGainSlider;
    InfoBarButton autoGainButton;
    InfoBarButton bypassButton;

    AbletonKnob preLowCutSlider;
    AbletonKnob preHighCutSlider;

    InfoBarCombo satTypeCombo;
    AbletonKnob driveSlider;
    AbletonKnob charSlider;
    InfoBarCombo qualityCombo;
    VisualizerComponent visualizer;

    AbletonKnob postLowCutSlider;
    AbletonKnob postHighCutSlider;
    InfoBarCombo postSlopeCombo;

    AbletonKnob mixSlider;
    AbletonKnob outputGainSlider;
    InfoBarButton safetyClipButton;

    juce::Label infoBar;
    int infoBarHoldCounter = 0;

    juce::ImageButton logoButton;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::vector<std::unique_ptr<SliderAtt>> sliderAtts;
    std::vector<std::unique_ptr<ComboAtt>> comboAtts;
    std::vector<std::unique_ptr<ButtonAtt>> buttonAtts;

    void addKnob(AbletonKnob& knob, const juce::String& paramID, const juce::String& labelText, const juce::String& suffix, const juce::String& descJP);
    void addCombo(InfoBarCombo& box, const juce::String& paramID, const juce::String& descJP);
    void addButton(InfoBarButton& btn, const juce::String& paramID, const juce::String& nameJP, const juce::String& descJP);

    juce::Component* lastHoveredComp = nullptr;
    int lastSatType = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NextGenSaturationAudioProcessorEditor)
};