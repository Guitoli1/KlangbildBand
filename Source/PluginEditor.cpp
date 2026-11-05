#include "PluginEditor.h"

using namespace vintage;

//==============================================================================
BandAudioProcessorEditor::BandAudioProcessorEditor (BandAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    addKnob ("input",   "EINGANG (DB)", true, juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "12|\xe2\x88\x92" "6|0|+6|+12|+18")));
    addKnob ("output",  "AUSGANG (DB)", true, juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "18|\xe2\x88\x92" "12|\xe2\x88\x92" "6|0|+6|+12")));
    addKnob ("speed",   "SPEED (IPS)",  false, "7,5|15|30");
    addKnob ("bias",    "BIAS",         false, juce::String (juce::CharPointer_UTF8 ("UNTER|\xe2\x80\x93|0|\xe2\x80\x93|\xc3\x9c" "BER")));
    addKnob ("wow",     "WOW",          false);
    addKnob ("flutter", "FLUTTER",      false);
    addKnob ("hiss",    "RAUSCHEN",     false);

    setupSwitch (onSwitch, "on", "AN", "AUS", lampRed);
    setupSwitch (autoSwitch, "autogain", "AN", "AUS", lampAmber);

    presetBox.getProperties().set ("plate", true);
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (vuLeft);
    addAndMakeVisible (vuRight);
    reels.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (reels);

    setSize (1100, 620);
    startTimerHz (60);
}

BandAudioProcessorEditor::~BandAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

BandAudioProcessorEditor::Knob& BandAudioProcessorEditor::addKnob (const juce::String& paramID, const juce::String& name, bool big, const juce::String& steps)
{
    auto k = std::make_unique<Knob>();
    k->name = name;
    k->big = big;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k->slider.setMouseDragSensitivity (big ? 320 : (steps.isNotEmpty() ? 140 : 220));
    if (steps.isNotEmpty())
        k->slider.getProperties().set ("stepLabels", steps);
    addAndMakeVisible (k->slider);
    k->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramID, k->slider);
    if (auto* rp = proc.apvts.getParameter (paramID))
        k->slider.setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void BandAudioProcessorEditor::setupSwitch (juce::ToggleButton& b, const juce::String& paramID,
                                            const juce::String& onText, const juce::String& offText, juce::Colour lamp)
{
    b.setButtonText ("x");
    b.getProperties().set ("onText", onText);
    b.getProperties().set ("offText", offText);
    b.getProperties().set ("lamp", (int) lamp.getARGB());
    addAndMakeVisible (b);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, paramID, b));
}

void BandAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    vuLeft.update (proc.vuL.load(), proc.peakL.exchange (0.0f), dt);
    vuRight.update (proc.vuR.load(), proc.peakR.exchange (0.0f), dt);

    // Spulen drehen sich nur, solange Audio durch das Band läuft
    const auto s = proc.samplesRun.load();
    if (s != lastSamples)
    {
        lastSamples = s;
        const float ips[] = { 7.5f, 15.0f, 30.0f };
        const int sp = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue ("speed")->load());
        const float step = dt * ips[sp] * 0.09f * juce::MathConstants<float>::twoPi;
        reels.angleL = std::fmod (reels.angleL + step, juce::MathConstants<float>::twoPi);
        reels.angleR = std::fmod (reels.angleR + step * Reels::packL / Reels::packR, juce::MathConstants<float>::twoPi);
        reels.repaint();
    }

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

//==============================================================================
void BandAudioProcessorEditor::renderTextures()
{
    tolexImage = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), false);
    {
        juce::Image::BitmapData data (tolexImage, juce::Image::BitmapData::writeOnly);
        juce::Random rnd (7);
        for (int y = 0; y < tolexImage.getHeight(); ++y)
            for (int x = 0; x < tolexImage.getWidth(); ++x)
            {
                const int weave = ((x + y) % 4 == 0 || (x - y + 4096) % 4 == 0) ? -5 : 0;
                const int n = rnd.nextInt (13) - 6 + weave;
                data.setPixelColour (x, y, juce::Colour ((juce::uint8) juce::jlimit (0, 255, 34 + n),
                                                         (juce::uint8) juce::jlimit (0, 255, 28 + n),
                                                         (juce::uint8) juce::jlimit (0, 255, 24 + n)));
            }
    }
    if (faceplate.isEmpty())
        return;
    faceplateImage = juce::Image (juce::Image::ARGB, faceplate.getWidth(), faceplate.getHeight(), true);
    juce::Graphics g (faceplateImage);
    const auto r = faceplateImage.getBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (cream.brighter (0.06f), 0, 0, creamDark, 0, r.getHeight(), false));
    g.fillRoundedRectangle (r, 6.0f);
    juce::Random rnd (3);
    for (int y = 0; y < faceplate.getHeight(); ++y)
    {
        g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colour (0xff6b5534)).withAlpha (0.015f + rnd.nextFloat() * 0.035f));
        g.drawHorizontalLine (y, 0.0f, r.getWidth());
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                             juce::Colour (0x30694a1c), 0, 0, true));
    g.fillRoundedRectangle (r, 6.0f);
}

void BandAudioProcessorEditor::drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                                             juce::Font f, juce::Justification j)
{
    g.setFont (f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawFittedText (text, r.translated (0.0f, 1.0f).toNearestInt(), j, 2, 1.0f);
    g.setColour (ink);
    g.drawFittedText (text, r.toNearestInt(), j, 2, 1.0f);
}

void BandAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (tolexImage, 0, 0);

    // Logo
    const auto scriptF = lnf.scriptFont (64.0f);
    const auto logoArea = juce::Rectangle<float> (40.0f, 8.0f, 400.0f, 84.0f);
    g.setFont (scriptF);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText ("Klangbild", logoArea.translated (2.0f, 3.0f), juce::Justification::centredLeft);
    g.setColour (logo);
    g.drawText ("Klangbild", logoArea, juce::Justification::centredLeft);
    const float sw = (float) juce::GlyphArrangement::getStringWidthInt (scriptF, "Klangbild");
    g.setColour (gold);
    g.setFont (lnf.labelFont (26.0f, 0.35f));
    g.drawText ("BAND", juce::Rectangle<float> (logoArea.getX() + sw + 14.0f, logoArea.getY() + 30.0f, 140.0f, 40.0f), juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.55f));
    g.setFont (lnf.labelFont (12.0f, 0.3f));
    g.drawText (juce::CharPointer_UTF8 ("BANDMASCHINE \xc2\xb7 S\xc3\x84TTIGUNG \xc2\xb7 WOW & FLUTTER"), juce::Rectangle<float> (44.0f, 90.0f, 420.0f, 16.0f), juce::Justification::centredLeft);

    const auto pa = presetArea.toFloat();
    g.setColour (logo.withAlpha (0.6f));
    g.setFont (lnf.labelFont (10.0f, 0.3f));
    g.drawText ("PROGRAMM", pa.withY (pa.getY() - 15.0f).withHeight (13.0f), juce::Justification::centredLeft);

    // Frontplatte
    const auto fp = faceplate.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (fp.expanded (4.0f).translated (0, 2.0f), 9.0f);
    g.setGradientFill (juce::ColourGradient (gold.brighter (0.3f), 0, fp.getY(), gold.darker (0.6f), 0, fp.getBottom(), false));
    g.fillRoundedRectangle (fp.expanded (3.0f), 8.0f);
    g.drawImageAt (faceplateImage, faceplate.getX(), faceplate.getY());
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (fp, 6.0f, 1.0f);
    for (auto [x, y, a] : { std::tuple { fp.getX() + 12.0f, fp.getY() + 12.0f, 0.4f }, std::tuple { fp.getRight() - 12.0f, fp.getY() + 12.0f, 1.3f },
                            std::tuple { fp.getX() + 12.0f, fp.getBottom() - 12.0f, 2.2f }, std::tuple { fp.getRight() - 12.0f, fp.getBottom() - 12.0f, 0.9f } })
        VintageLookAndFeel::drawScrew (g, { x, y }, 5.0f, a);

    // Rahmen um die beiden VU-Meter
    for (auto vbi : { vuBezelL, vuBezelR })
    {
        const auto vb = vbi.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (vb.translated (2.0f, 3.0f), 12.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2520), 0, vb.getY(), juce::Colour (0xff120f0c), 0, vb.getBottom(), false));
        g.fillRoundedRectangle (vb, 12.0f);
    }

    // Trennlinie zwischen Haupt- und Nebenreglern
    for (int dy : { 0, 1 })
    {
        g.setColour (dy == 0 ? ink.withAlpha (0.4f) : juce::Colours::white.withAlpha (0.5f));
        g.drawHorizontalLine (dividerArea.getY() + dy, (float) dividerArea.getX(), (float) dividerArea.getRight());
    }

    for (auto& k : knobs)
    {
        const auto sb = k->slider.getBounds().toFloat();
        drawEngraved (g, k->name, { sb.getX() - 20.0f, sb.getBottom() - 4.0f, sb.getWidth() + 40.0f, k->big ? 24.0f : 18.0f },
                      lnf.labelFont (k->big ? 18.0f : 13.0f, k->big ? 0.2f : 0.14f), juce::Justification::centred);
    }

    const juce::String names[] = { "BAND", "AUTO-GAIN" };
    for (size_t i = 0; i < 2; ++i)
        drawEngraved (g, names[i], switchLabels[i].toFloat(), lnf.labelFont (12.0f, 0.14f), juce::Justification::centred);

    const auto tag = tagArea.toFloat();
    g.setColour (ink.withAlpha (0.85f));
    g.fillRoundedRectangle (tag, 3.0f);
    g.setColour (gold);
    g.setFont (lnf.labelFont (11.0f, 0.2f));
    g.drawText ("MODELL KB-1500", tag, juce::Justification::centred);
}

void BandAudioProcessorEditor::resized()
{
    const int W = getWidth();
    presetArea = { 44, 128, 260, 32 };
    presetBox.setBounds (presetArea);

    faceplate = { 24, 178, W - 48, 420 };
    const auto in = faceplate.reduced (30, 20);

    // obere Reihe: Eingang | VU L | VU R | Ausgang
    const int topH = 250;
    knobs[0]->slider.setBounds (in.getX() - 10, in.getY() + 10, 230, 210);
    knobs[1]->slider.setBounds (in.getRight() - 220, in.getY() + 10, 230, 210);
    vuBezelL = juce::Rectangle<int> (282, 214).withCentre ({ in.getCentreX() - 148, in.getY() + 118 });
    vuBezelR = juce::Rectangle<int> (282, 214).withCentre ({ in.getCentreX() + 148, in.getY() + 118 });
    vuLeft.setBounds (vuBezelL.reduced (9));
    vuRight.setBounds (vuBezelR.reduced (9));
    dividerArea = { in.getX() + 10, in.getY() + topH + 6, in.getWidth() - 20, 2 };

    // untere Reihe: Speed, Bias, Wow, Flutter, Rauschen | Schalter
    const int rowY = in.getY() + topH + 14;
    for (int i = 2; i < 7; ++i)
        knobs[(size_t) i]->slider.setBounds (in.getX() + (i - 2) * 124, rowY, 124, 108);

    juce::ToggleButton* sws[] = { &onSwitch, &autoSwitch };
    const int restX = in.getX() + 5 * 124, cx = (restX + in.getRight()) / 2;
    for (int i = 0; i < 2; ++i)
    {
        const int x = cx - 140 + i * 140;
        switchLabels[(size_t) i] = { x, rowY + 8, 140, 16 };
        sws[i]->setBounds (x + 10, rowY + 30, 120, 40);
    }
    tagArea = { cx - 100, rowY + 84, 200, 24 };

    reels.setBounds (W - 470, 4, 440, 170);

    renderTextures();
}

//==============================================================================
void BandAudioProcessorEditor::Reels::drawReel (juce::Graphics& g, juce::Point<float> c, float radius, float packRadius, float angle)
{
    // Schatten
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2, radius * 2).withCentre (c.translated (3.0f, 5.0f)));
    // Bandwickel (braunes Band)
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a3a24), c.x, c.y - packRadius, juce::Colour (0xff2a190e), c.x, c.y + packRadius, false));
    g.fillEllipse (juce::Rectangle<float> (packRadius * 2, packRadius * 2).withCentre (c));
    g.setColour (juce::Colour (0x22ffffff));
    for (float rr = radius * 0.3f; rr < packRadius; rr += 3.0f)
        g.drawEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre (c), 0.6f);

    // Metallspule mit drei Aussparungen
    juce::Path flange;
    flange.addEllipse (juce::Rectangle<float> (radius * 2, radius * 2).withCentre (c));
    for (int k = 0; k < 3; ++k)
    {
        const float a0 = angle + (float) k * juce::MathConstants<float>::twoPi / 3.0f;
        juce::Path hole;
        hole.addCentredArc (c.x, c.y, radius * 0.80f, radius * 0.80f, 0.0f, a0 + 0.25f, a0 + 1.65f, true);
        hole.addCentredArc (c.x, c.y, radius * 0.36f, radius * 0.36f, 0.0f, a0 + 1.50f, a0 + 0.42f, false);
        hole.closeSubPath();
        flange.addPath (hole);
    }
    flange.setUsingNonZeroWinding (false);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xe0d9dcdf), c.x - radius, c.y - radius, juce::Colour (0xe07d8287), c.x + radius, c.y + radius, false));
    g.fillPath (flange);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.strokePath (flange, juce::PathStrokeType (1.0f));

    // Nabe
    g.setColour (juce::Colour (0xff3a3632));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.34f, radius * 0.34f).withCentre (c));
    g.setColour (juce::Colour (0xffb9bcc0));
    for (int k = 0; k < 3; ++k)
    {
        const float a = angle + (float) k * juce::MathConstants<float>::twoPi / 3.0f;
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c.getPointOnCircumference (radius * 0.1f, a)));
    }
}

void BandAudioProcessorEditor::Reels::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float radius = reelRadius;
    const juce::Point<float> cl (b.getX() + 90.0f, b.getCentreY() - 4.0f), cr (b.getRight() - 90.0f, b.getCentreY() - 4.0f);

    // Bandweg unten über den Tonkopf
    const juce::Point<float> head (b.getCentreX(), b.getBottom() - 10.0f);
    juce::Path tape;
    tape.startNewSubPath (cl.x, cl.y + packL);
    tape.lineTo (head.x - 26.0f, head.y);
    tape.lineTo (head.x + 26.0f, head.y);
    tape.lineTo (cr.x, cr.y + packR);
    g.setColour (juce::Colour (0xff4a2f1c));
    g.strokePath (tape, juce::PathStrokeType (3.0f));
    // Tonkopf
    const auto hr = juce::Rectangle<float> (36.0f, 16.0f).withCentre (head.translated (0.0f, 6.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd5d7da), hr.getX(), hr.getY(), juce::Colour (0xff6d7175), hr.getX(), hr.getBottom(), false));
    g.fillRoundedRectangle (hr, 4.0f);

    // Wickelteller laufen mit gleicher Bandgeschwindigkeit -> kleiner Wickel dreht schneller
    drawReel (g, cl, radius, packL, angleL);
    drawReel (g, cr, radius, packR, angleR);
}
