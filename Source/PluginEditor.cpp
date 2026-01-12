// --- START OF FILE PluginEditor.cpp ---

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

// --- Helper Functions ---

static juce::String utf8(const char* text) {
    return juce::String::fromUTF8(text);
}

juce::String AbletonKnob::getNoteStr(double hz) {
    if (hz <= 0) return "";
    double noteNum = 69.0 + 12.0 * std::log2(hz / 440.0);
    int noteInt = (int)(noteNum + 0.5);
    int cents = (int)((noteNum - noteInt) * 100.0);
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (noteInt / 12) - 1;
    int nameIdx = noteInt % 12;
    if (nameIdx < 0) nameIdx += 12;
    juce::String centStr = (cents >= 0 ? "+" : "") + juce::String(cents);
    return juce::String(noteNames[nameIdx]) + juce::String(octave) + " (" + centStr + " ct)";
}

void AbletonKnob::updateInfo() {
    if (onInfoUpdate) {
        juce::String valStr;
        if (textFromValueFunction) {
            valStr = textFromValueFunction(getValue());
        }
        else {
            valStr = juce::String(getValue(), 1) + getTextValueSuffix();
        }
        onInfoUpdate(nameJP + juce::String::fromUTF8((const char*)u8" : ") + valStr + juce::String::fromUTF8((const char*)u8"  ---  ") + description, false);
    }
}

// Double Click to Reset
void AbletonKnob::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) {
        setValue(getDoubleClickReturnValue());
    }
}

// FIX: Correct implementation of CallOutBox for Numeric Entry
void AbletonKnob::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isRightButtonDown() || e.mods.isAltDown()) {
        auto label = std::make_unique<juce::Label>("ValueEdit", juce::String(getValue()));
        label->setEditable(true);
        label->setSize(60, 20);
        label->setJustificationType(juce::Justification::centred);

        auto* labelPtr = label.get();
        label->onTextChange = [this, labelPtr]() {
            setValue(labelPtr->getText().getDoubleValue());
            };

        juce::CallOutBox::launchAsynchronously(
            std::move(label),
            getScreenBounds(),
            nullptr
        );
        return;
    }
    juce::Slider::mouseDown(e);
}

void InfoBarCombo::updateInfo() {
    if (onInfoUpdate) {
        int idx = getSelectedItemIndex();
        juce::String itemDesc = (idx >= 0 && idx < itemDescriptions.size()) ? itemDescriptions[idx] : "";
        onInfoUpdate(nameJP + juce::String::fromUTF8((const char*)u8" : ") + getText() + juce::String::fromUTF8((const char*)u8"  ---  ") + itemDesc);
    }
}

// --- Ableton LookAndFeel Implementation ---
AbletonLookAndFeel::AbletonLookAndFeel() {
    setColour(juce::Label::textColourId, colorText);
    setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    setColour(juce::ComboBox::outlineColourId, colorOutline);
    setColour(juce::ComboBox::arrowColourId, colorText);
    setColour(juce::ComboBox::textColourId, colorText);

    setColour(juce::ToggleButton::textColourId, colorText);
    setColour(juce::ToggleButton::tickColourId, colorAccent);
    setColour(juce::ToggleButton::tickDisabledColourId, juce::Colours::grey);

    setColour(juce::PopupMenu::backgroundColourId, colorBg);
    setColour(juce::PopupMenu::textColourId, colorText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colorAccent);
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void AbletonLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
    const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider)
{
    if (width <= 0 || height <= 0) return;

    const float labelHeight = 24.0f;
    const float rotaryAreaHeight = (float)height - labelHeight;
    const float radius = juce::jmin((float)width / 2.0f, rotaryAreaHeight / 2.0f) - 4.0f;
    const float centreX = (float)x + (float)width * 0.5f;
    const float centreY = (float)y + (rotaryAreaHeight * 0.5f);
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float arcThickness = 5.0f;

    g.setColour(colorKnobTrack);
    juce::Path bgArc;
    bgArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.strokePath(bgArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(colorAccent);
    juce::Path valArc;
    valArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, angle, true);
    g.strokePath(valArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(colorText);
    g.setFont(juce::Font("Meiryo UI", 16.0f, juce::Font::bold));

    juce::String valText = slider.getTextFromValue(slider.getValue());
    g.drawText(valText,
        (int)(centreX - radius), (int)(centreY - 10), (int)(radius * 2), 20,
        juce::Justification::centred, false);

    g.setColour(juce::Colours::darkgrey);
    g.setFont(juce::Font("Meiryo UI", 15.0f, juce::Font::bold));
    g.drawText(slider.getName(), x, (int)(y + height - labelHeight), width, (int)labelHeight, juce::Justification::centred, false);
}

void AbletonLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
    int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
{
    juce::ignoreUnused(isButtonDown, buttonX, buttonY, buttonW, buttonH);
    if (width <= 0 || height <= 0) return;

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(0.0f, 0.0f, (float)width, (float)height, 4.0f);

    g.setColour(colorOutline);
    g.drawRoundedRectangle(0.0f, 0.0f, (float)width, (float)height, 3.0f, 1.0f);

    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    auto arrowZone = juce::Rectangle<int>(width - 20, 0, 20, height).toFloat();

    juce::Path path;
    path.addTriangle(arrowZone.getCentreX() - 4, arrowZone.getCentreY() - 2,
        arrowZone.getCentreX() + 4, arrowZone.getCentreY() - 2,
        arrowZone.getCentreX(), arrowZone.getCentreY() + 3);
    g.fillPath(path);
}

void AbletonLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    if (bounds.isEmpty()) return;

    juce::Colour bgCol = shouldDrawButtonAsDown ? colorAccent.withAlpha(0.5f) : (shouldDrawButtonAsHighlighted ? juce::Colours::white : juce::Colour(0xFFE0E0E0));

    if (button.getName() == "Auto Gain" && button.getToggleState()) {
        bgCol = colorAccent;
    }
    else if (button.getName() == "Bypass" && button.getToggleState()) {
        bgCol = juce::Colours::yellow.withAlpha(0.8f);
    }
    else if (button.getToggleState()) {
        bgCol = colorAccent;
    }

    g.setColour(bgCol);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(colorOutline);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    g.setColour(colorText);
    g.setFont(juce::Font("Meiryo UI", 13.0f, juce::Font::bold));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4), juce::Justification::centred, 1);
}

void AbletonLookAndFeel::drawTickBox(juce::Graphics& g, juce::Component& component,
    float x, float y, float w, float h,
    bool ticked, bool isEnabled,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
}

void AbletonLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(backgroundColour);
    auto bounds = button.getLocalBounds().toFloat();
    if (bounds.isEmpty()) return;

    g.setColour(shouldDrawButtonAsDown ? colorAccent.withAlpha(0.5f) : (shouldDrawButtonAsHighlighted ? juce::Colours::white : juce::Colour(0xFFE0E0E0)));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(colorOutline);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

// --- Editor Implementation ---

NextGenSaturationAudioProcessorEditor::NextGenSaturationAudioProcessorEditor(NextGenSaturationAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&abletonLnF);
    setSize(800, 350);

    addKnob(inputGainSlider, "inputGain", "Input", " dB", juce::String::fromUTF8((const char*)u8"入力レベルを調整します。"));

    addButton(autoGainButton, "autoGain", "Auto Gain", juce::String::fromUTF8((const char*)u8"3秒間計測し、出力音量を入力音量に合わせます。"));
    autoGainButton.setName("Auto Gain");

    addButton(bypassButton, "bypass", "Bypass", juce::String::fromUTF8((const char*)u8"エフェクトをスルーして原音と比較します。"));
    bypassButton.setName("Bypass");

    autoGainButton.onClick = [this]() {
        if (autoGainButton.getToggleState()) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(audioProcessor.apvts.getParameter("inputGain"))) {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->convertTo0to1(0.0f));
                p->endChangeGesture();
            }
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(audioProcessor.apvts.getParameter("outputGain"))) {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->convertTo0to1(0.0f));
                p->endChangeGesture();
            }
        }
        };

    addKnob(preLowCutSlider, "preLowCut", "Low Cut", " Hz", juce::String::fromUTF8((const char*)u8"歪ませる前の低域をカットします。"));
    addKnob(preHighCutSlider, "preHighCut", "High Cut", " Hz", juce::String::fromUTF8((const char*)u8"歪ませる前の高域をカットします。"));

    addCombo(satTypeCombo, "satType", juce::String::fromUTF8((const char*)u8"歪みのアルゴリズムを選択します。"));

    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Analog Tape】磁気テープのヒステリシスと高域減衰。Char: テープ速度"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Tube Triode】三極管の温かみのある非対称歪み。Char: バイアス調整"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Tube Pentode】五極管の鋭く攻撃的な歪み。Char: 硬さ調整"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Transformer】鉄心の磁気飽和による低域の密度。Char: コア飽和度"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Console】ヴィンテージ卓のスルーレート制限。Char: なまり具合"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【JFET】真空管に近いトランジスタのクランチ感。Char: 動作点"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【BJT】毛羽立った激しいファズサウンド。Char: フィードバック"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Diode】対称的で密度の高い歪み（ペダル系）。Char: ニー特性"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Soft Tanh】標準的なソフトクリップ。Char: 非対称性"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Hard Clip】デジタルで攻撃的な歪み。Char: 角の丸み(Knee)"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Wavefold】波形を折り畳む金属的な変調歪み。Char: 折り畳み回数"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Rectify】全波整流によるオクターブファズ効果。Char: ブレンド率"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Bitcrush】解像度を下げる破壊的エフェクト。Char: ビット深度"));
    satTypeCombo.itemDescriptions.add(juce::String::fromUTF8((const char*)u8"【Exciter】高域の倍音を強調し煌びやかにします。Char: 周波数シフト"));

    addKnob(driveSlider, "drive", "Drive", " dB", juce::String::fromUTF8((const char*)u8"歪みの深さを調整します。"));
    addKnob(charSlider, "character", "Char", "", juce::String::fromUTF8((const char*)u8"アルゴリズムごとの特性（非対称性など）を調整します。"));
    addCombo(qualityCombo, "quality", juce::String::fromUTF8((const char*)u8"オーバーサンプリング倍率を設定します。"));

    visualizer.setProcessor(&audioProcessor);
    addAndMakeVisible(visualizer);

    addKnob(postLowCutSlider, "postLowCut", "Low Cut", " Hz", juce::String::fromUTF8((const char*)u8"最終的な低域を調整します。"));
    addKnob(postHighCutSlider, "postHighCut", "High Cut", " Hz", juce::String::fromUTF8((const char*)u8"最終的な高域を調整します。"));
    addCombo(postSlopeCombo, "postSlope", juce::String::fromUTF8((const char*)u8"ポストフィルタのスロープ（急峻さ）を設定します。"));

    addKnob(mixSlider, "mix", "Mix", " %", juce::String::fromUTF8((const char*)u8"原音とエフェクト音のバランスを調整します。"));
    addKnob(outputGainSlider, "outputGain", "Output", " dB", juce::String::fromUTF8((const char*)u8"最終出力レベルを調整します。"));
    addButton(safetyClipButton, "safetyClip", "Safe", juce::String::fromUTF8((const char*)u8"0dBを超えないようにクリッピングします。"));

    addAndMakeVisible(infoBar);
    infoBar.setColour(juce::Label::backgroundColourId, juce::Colour(0xFFE0E0E0));
    infoBar.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
    infoBar.setJustificationType(juce::Justification::centred);
    infoBar.setFont(juce::Font("Meiryo UI", 20.0f, juce::Font::bold));
    infoBar.setText(juce::String::fromUTF8((const char*)u8"Ready."), juce::dontSendNotification);

    // Logo Button Setup
    auto logoImage = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);
    logoButton.setImages(false, true, true,
        logoImage, 1.0f, juce::Colours::transparentBlack,
        logoImage, 1.0f, juce::Colours::transparentBlack,
        logoImage, 0.8f, juce::Colours::transparentBlack);
    logoButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    logoButton.onClick = []() {
        juce::URL("https://github.com/OTODESK4193").launchInDefaultBrowser();
        };
    addAndMakeVisible(logoButton);

    startTimer(30);
}

NextGenSaturationAudioProcessorEditor::~NextGenSaturationAudioProcessorEditor() {
    setLookAndFeel(nullptr);
    stopTimer();
}

void NextGenSaturationAudioProcessorEditor::addKnob(AbletonKnob& knob, const juce::String& paramID, const juce::String& labelText, const juce::String& suffix, const juce::String& descJP) {
    addAndMakeVisible(knob);
    knob.setup(labelText, suffix);
    knob.nameJP = labelText;
    knob.description = descJP;

    knob.onInfoUpdate = [this](const juce::String& text, bool) {
        updateInfoBar(text);
        infoBarHoldCounter = 100;
        };

    sliderAtts.push_back(std::make_unique<SliderAtt>(audioProcessor.apvts, paramID, knob));
}

void NextGenSaturationAudioProcessorEditor::addCombo(InfoBarCombo& box, const juce::String& paramID, const juce::String& descJP) {
    addAndMakeVisible(box);
    box.addItemList(audioProcessor.apvts.getParameter(paramID)->getAllValueStrings(), 1);
    box.nameJP = paramID;
    box.description = descJP;

    box.onInfoUpdate = [this](const juce::String& text) {
        updateInfoBar(text);
        infoBarHoldCounter = 100;
        };

    comboAtts.push_back(std::make_unique<ComboAtt>(audioProcessor.apvts, paramID, box));
}

void NextGenSaturationAudioProcessorEditor::addButton(InfoBarButton& btn, const juce::String& paramID, const juce::String& nameJP, const juce::String& descJP) {
    addAndMakeVisible(btn);
    btn.setButtonText(nameJP);
    btn.nameJP = nameJP;
    btn.description = descJP;

    btn.onInfoUpdate = [this](const juce::String& text) {
        updateInfoBar(text);
        infoBarHoldCounter = 100;
        };

    buttonAtts.push_back(std::make_unique<ButtonAtt>(audioProcessor.apvts, paramID, btn));
}

void NextGenSaturationAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFFF0F0F0));

    g.setColour(juce::Colours::grey.withAlpha(0.3f));
    int w = getWidth();
    int h = getHeight();
    int secW = w / 5;

    // Adjusted vertical lines (first line shortened for logo area)
    for (int i = 1; i < 5; ++i) {
        float lineEndY = (i == 1) ? static_cast<float>(h) - 140.0f : static_cast<float>(h) - 45.0f;
        g.drawLine(static_cast<float>(secW * i), 15.0f, static_cast<float>(secW * i), lineEndY, 1.0f);
    }

    g.setColour(juce::Colours::darkgrey);
    g.setFont(juce::Font("Meiryo UI", 13.0f, juce::Font::bold));
    g.drawText("INPUT", 0, 5, secW, 20, juce::Justification::centred);
    g.drawText("PRE FILTER", secW, 5, secW, 20, juce::Justification::centred);
    g.drawText("SATURATION", secW * 2, 5, secW, 20, juce::Justification::centred);
    g.drawText("POST FILTER", secW * 3, 5, secW, 20, juce::Justification::centred);
    g.drawText("OUTPUT", secW * 4, 5, secW, 20, juce::Justification::centred);

    g.setFont(juce::Font("Meiryo UI", 11.0f, juce::Font::bold));
    g.setColour(juce::Colours::grey);
    g.drawText("QUALITY", secW * 2, h - 45, secW, 20, juce::Justification::centred);
}

void NextGenSaturationAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto footer = area.removeFromBottom(30);
    infoBar.setBounds(footer);

    auto mainArea = area.reduced(10);
    mainArea.removeFromTop(20);
    int secW = mainArea.getWidth() / 5;

    auto rInput = mainArea.removeFromLeft(secW).reduced(5);
    inputGainSlider.setBounds(rInput.removeFromTop(110));
    autoGainButton.setBounds(rInput.removeFromTop(30).reduced(20, 0));
    bypassButton.setBounds(rInput.removeFromTop(30).reduced(20, 0));

    auto rPre = mainArea.removeFromLeft(secW).reduced(5);
    preLowCutSlider.setBounds(rPre.removeFromTop(110));
    preHighCutSlider.setBounds(rPre.removeFromTop(110));

    // Logo placement (below INPUT and PRE FILTER sections)
    // Calculate logo area: spans 2 sections width, from below Bypass button to above InfoBar
    int logoAreaX = 10;
    int logoAreaY = bypassButton.getBottom() + 8;
    int logoAreaW = secW * 2;
    int logoAreaH = footer.getY() - logoAreaY - 5;

    auto logoImage = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);
    if (logoImage.isValid() && logoAreaH > 0 && logoAreaW > 0) {
        float imgAspect = static_cast<float>(logoImage.getWidth()) / static_cast<float>(logoImage.getHeight());

        // Fit width first (logo is horizontal)
        int fitWidth = logoAreaW - 10;
        int fitHeight = static_cast<int>(static_cast<float>(fitWidth) / imgAspect);

        // If too tall, fit by height instead
        if (fitHeight > logoAreaH) {
            fitHeight = logoAreaH;
            fitWidth = static_cast<int>(static_cast<float>(fitHeight) * imgAspect);
        }

        // Center in logo area
        int logoX = logoAreaX + (logoAreaW - fitWidth) / 2;
        int logoY = logoAreaY + (logoAreaH - fitHeight) / 2;

        logoButton.setBounds(logoX, logoY, fitWidth, fitHeight);
    }

    auto rSat = mainArea.removeFromLeft(secW).reduced(5);
    satTypeCombo.setBounds(rSat.removeFromTop(25));
    rSat.removeFromTop(5);
    visualizer.setBounds(rSat.removeFromTop(70));
    rSat.removeFromTop(5);
    auto rSatKnobs = rSat.removeFromTop(110);
    driveSlider.setBounds(rSatKnobs.removeFromLeft(rSatKnobs.getWidth() / 2));
    charSlider.setBounds(rSatKnobs);
    qualityCombo.setBounds(rSat.removeFromBottom(25));

    auto rPost = mainArea.removeFromLeft(secW).reduced(5);
    postLowCutSlider.setBounds(rPost.removeFromTop(110));
    postHighCutSlider.setBounds(rPost.removeFromTop(110));
    postSlopeCombo.setBounds(rPost.removeFromTop(25));

    auto rOut = mainArea.reduced(5);
    mixSlider.setBounds(rOut.removeFromTop(110));
    outputGainSlider.setBounds(rOut.removeFromTop(110));
    safetyClipButton.setBounds(rOut.removeFromTop(30).reduced(20, 0));
}

void NextGenSaturationAudioProcessorEditor::timerCallback() {
    visualizer.update();

    auto* hovered = dynamic_cast<juce::Component*>(juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse());
    bool isHovering = false;

    if (hovered != nullptr) {
        if (auto* knob = dynamic_cast<AbletonKnob*>(hovered)) {
            knob->updateInfo();
            isHovering = true;
        }
        else if (hovered == &satTypeCombo) {
            int idx = satTypeCombo.getSelectedItemIndex();
            juce::String desc = (idx >= 0 && idx < satTypeCombo.itemDescriptions.size()) ? satTypeCombo.itemDescriptions[idx] : "";
            updateInfoBar(juce::String::fromUTF8((const char*)u8"Algorithm : ") + satTypeCombo.getText() + juce::String::fromUTF8((const char*)u8"  ---  ") + desc);
            isHovering = true;
        }
        else if (dynamic_cast<juce::ComboBox*>(hovered)) {
            updateInfoBar(juce::String::fromUTF8((const char*)u8"Select Option"));
            isHovering = true;
        }
        else if (dynamic_cast<juce::ToggleButton*>(hovered)) {
            if (hovered == &autoGainButton) {
                if (autoGainButton.getToggleState()) {
                    updateInfoBar(juce::String::fromUTF8((const char*)u8"Learning... (Please wait)"));
                }
                else {
                    updateInfoBar(juce::String::fromUTF8((const char*)u8"Auto Gain : Click to start learning"));
                }
            }
            else if (hovered == &bypassButton) {
                updateInfoBar(juce::String::fromUTF8((const char*)u8"Bypass : Compare with original signal"));
            }
            else {
                updateInfoBar(juce::String::fromUTF8((const char*)u8"Switch On/Off"));
            }
            isHovering = true;
        }
    }

    if (isHovering) {
        infoBarHoldCounter = 100;
    }
    else {
        if (infoBarHoldCounter > 0) {
            infoBarHoldCounter--;
        }
        else {
            if (audioProcessor.isAutoGainLearning.load()) {
                infoBar.setText(juce::String::fromUTF8((const char*)u8"Learning..."), juce::dontSendNotification);
            }
            else {
                infoBar.setText(juce::String::fromUTF8((const char*)u8"Ready."), juce::dontSendNotification);
            }
        }
    }

    int currentSatType = satTypeCombo.getSelectedItemIndex();
    if (currentSatType != lastSatType) {
        lastSatType = currentSatType;
        updateKnobProperties(currentSatType);
    }
}

void NextGenSaturationAudioProcessorEditor::updateKnobProperties(int satType) {
    juce::String charName = "Char";
    juce::String charDesc = "";
    juce::String suffix = "";
    std::function<juce::String(double)> textFunc = nullptr;

    switch (satType) {
    case 0: // Analog Tape
        charName = "Speed";
        charDesc = juce::String::fromUTF8((const char*)u8"テープ速度。左で遅く(ローファイ)、右で速く(クリア)なります。");
        suffix = " ips";
        textFunc = [](double v) { return juce::String(3.75 + v * (30.0 - 3.75), 1) + " ips"; };
        break;
    case 1: // Tube Triode
        charName = "Bias";
        charDesc = juce::String::fromUTF8((const char*)u8"バイアス電圧。回すほど非対称性が増し、偶数次倍音が強調されます。");
        suffix = " %";
        textFunc = [](double v) { return juce::String(v * 100.0, 0) + " %"; };
        break;
    case 2: // Tube Pentode
        charName = "Hard";
        charDesc = juce::String::fromUTF8((const char*)u8"クリップの硬さ。右に回すほど鋭角的な歪みになります。");
        suffix = " %";
        break;
    case 3: // Transformer
        charName = "Core";
        charDesc = juce::String::fromUTF8((const char*)u8"コアの飽和度。右に回すほど低域の密度とコンプ感が増します。");
        suffix = " %";
        break;
    case 4: // Console
        charName = "Slew";
        charDesc = juce::String::fromUTF8((const char*)u8"スルーレート制限。右に回すほどトランジェントが太く丸くなります。");
        suffix = " %";
        break;
    case 5: // JFET
        charName = "Bias";
        charDesc = juce::String::fromUTF8((const char*)u8"動作点。倍音構成を変化させます。");
        suffix = " %";
        break;
    case 6: // BJT
        charName = "Fdbk";
        charDesc = juce::String::fromUTF8((const char*)u8"内部フィードバック。右に回すほどサステインが伸び、暴れます。");
        suffix = " %";
        break;
    case 7: // Diode
        charName = "Knee";
        charDesc = juce::String::fromUTF8((const char*)u8"クリップの膝特性。右に回すほどハードな壁にぶつかる音になります。");
        suffix = " %";
        break;
    case 8: // Soft Tanh
        charName = "Asym";
        charDesc = juce::String::fromUTF8((const char*)u8"非対称性。右に回すと偶数次倍音が付加されます。");
        suffix = " %";
        break;
    case 9: // Hard Clip
        charName = "Knee";
        charDesc = juce::String::fromUTF8((const char*)u8"角の丸み。右に回すほど純粋なデジタルクリップに近づきます。");
        suffix = " dB";
        textFunc = [](double v) { return juce::String(v * 6.0, 1) + " dB"; };
        break;
    case 10: // Wavefold
        charName = "Fold";
        charDesc = juce::String::fromUTF8((const char*)u8"折り畳み回数。右に回すほど金属的な響きが増します。");
        suffix = " x";
        textFunc = [](double v) { return juce::String(0.5 + v * 3.5, 1) + " x"; };
        break;
    case 11: // Rectify
        charName = "Mix";
        charDesc = juce::String::fromUTF8((const char*)u8"原音と整流音のブレンド率。");
        suffix = " %";
        break;
    case 12: // Bitcrush
        charName = "Bits";
        charDesc = juce::String::fromUTF8((const char*)u8"ビット深度とサンプルレート。右に回すほど破壊されます。");
        suffix = " bit";
        textFunc = [](double v) { return juce::String(16.0 - v * 14.0, 1) + " bit"; };
        break;
    case 13: // Exciter
        charName = "Freq";
        charDesc = juce::String::fromUTF8((const char*)u8"エキサイターが反応する周波数帯域。");
        suffix = " Hz";
        textFunc = [](double v) { return juce::String(1000.0 + v * 9000.0, 0) + " Hz"; };
        break;
    }

    charSlider.setName(charName);
    charSlider.description = charDesc;
    charSlider.setTextValueSuffix(suffix);
    charSlider.textFromValueFunction = textFunc;
    charSlider.repaint();
}

void NextGenSaturationAudioProcessorEditor::updateInfoBar(const juce::String& text) {
    infoBar.setText(text, juce::dontSendNotification);
}