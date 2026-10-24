#include "VeilLookAndFeel.h"
#include <cmath>
namespace veil::ui {
VeilLookAndFeel::VeilLookAndFeel() {
    setColour(juce::Slider::textBoxTextColourId, silver);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, burgundy);
    setColour(juce::ComboBox::textColourId, silver);
    setColour(juce::ComboBox::backgroundColourId, panel);
    setColour(juce::ComboBox::outlineColourId, bronze.withAlpha(.3f));
    setColour(juce::PopupMenu::backgroundColourId, ink);
    setColour(juce::PopupMenu::textColourId, silver);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, burgundy);
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::Label::textColourId, silver);
    setColour(juce::TextEditor::backgroundColourId, ink);
    setColour(juce::TextEditor::textColourId, silver);
    setColour(juce::TextEditor::highlightColourId, burgundy);
    setColour(juce::TooltipWindow::backgroundColourId, panel);
    setColour(juce::TooltipWindow::textColourId, silver);
    setColour(juce::TooltipWindow::outlineColourId, bronze);
}
void VeilLookAndFeel::drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float position,float start,float end,juce::Slider& slider) {
    const auto d = juce::jmin(float(w),float(h)) - 7.0f;
    const juce::Point<float> c (x+w*.5f,y+h*.5f);
    const auto outer = juce::Rectangle<float>(d,d).withCentre(c);
    const auto radius = d*.5f;
    const auto angle = start + position*(end-start);
    const bool hover = slider.isMouseOverOrDragging();
    g.setColour(juce::Colours::black.withAlpha(.5f)); g.fillEllipse(outer.translated(0,2));
    juce::Path path; path.addCentredArc(c.x,c.y,radius-1,radius-1,0,start,end,true);
    g.setColour(bronze.withAlpha(.16f)); g.strokePath(path,juce::PathStrokeType(2.0f));
    for (int i=0;i<=16;++i) {
        const auto a=start+(end-start)*float(i)/16.f;
        const juce::Point<float> p1(c.x+std::sin(a)*(radius-3),c.y-std::cos(a)*(radius-3));
        const juce::Point<float> p2(c.x+std::sin(a)*(radius-((i%4)==0?7:5)),c.y-std::cos(a)*(radius-((i%4)==0?7:5)));
        g.setColour(bronze.withAlpha(i/16.f<=position?.7f:.2f)); g.drawLine({p1,p2},.7f);
    }
    auto body=outer.reduced(9);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff343239),body.getTopLeft(),ink,body.getBottomRight(),false)); g.fillEllipse(body);
    g.setColour(bronze.withAlpha(hover?.85f:.38f));g.drawEllipse(body,.8f);
    g.setColour(juce::Colour(0xff494348).withAlpha(.6f));g.drawEllipse(body.reduced(3),.5f);
    path.clear();path.addCentredArc(c.x,c.y,radius-1,radius-1,0,start,angle,true);
    g.setColour(bronze.withAlpha(hover?1.f:.75f)); g.strokePath(path,juce::PathStrokeType(2.f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    const float r=juce::jmax(1.f,radius-13);
    g.setColour(silver);g.drawLine(c.x+std::sin(angle)*r*.5f,c.y-std::cos(angle)*r*.5f,c.x+std::sin(angle)*r,c.y-std::cos(angle)*r,1.7f);
    g.setColour(bronze.withAlpha(.35f));g.fillEllipse(c.x-1.5f,c.y-1.5f,3,3);
}
void VeilLookAndFeel::drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool hover,bool down) {
    auto r=b.getLocalBounds().toFloat().reduced(.5f);
    g.setColour(b.getToggleState()?burgundy.withAlpha(.55f):(down?bronze.withAlpha(.22f):panel.brighter(hover?.15f:0.f)));g.fillRoundedRectangle(r,3);
    g.setColour(bronze.withAlpha(hover?.7f:.28f));g.drawRoundedRectangle(r,3,.8f);
}
void VeilLookAndFeel::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool) {
    g.setColour(b.isEnabled()?silver:muted);g.setFont(getTextButtonFont(b,b.getHeight()));g.drawFittedText(b.getButtonText(),b.getLocalBounds().reduced(4),juce::Justification::centred,1);
}
juce::Font VeilLookAndFeel::getTextButtonFont(juce::TextButton&,int) { return juce::Font(juce::FontOptions(11.f)); }
void VeilLookAndFeel::drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool hover,bool) {
    const auto bounds=b.getLocalBounds().toFloat();
    const auto c=juce::Point<float>(bounds.getX()+9,bounds.getCentreY());
    g.setColour(bronze.withAlpha(hover?.9f:.5f));g.drawEllipse(c.x-5,c.y-5,10,10,.9f);
    if (b.getToggleState()) {g.setColour(bronze.withAlpha(.15f));g.fillEllipse(c.x-8,c.y-8,16,16);g.setColour(bronze);g.fillEllipse(c.x-2.5f,c.y-2.5f,5,5);}
    g.setColour(b.getToggleState()?silver:muted);g.setFont(juce::FontOptions(10.5f));g.drawFittedText(b.getButtonText(),b.getLocalBounds().withTrimmedLeft(21),juce::Justification::centredLeft,1);
}
void VeilLookAndFeel::drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox&) {
    auto r=juce::Rectangle<float>(.5f,.5f,float(w)-1,float(h)-1);
    g.setColour(panel);g.fillRoundedRectangle(r,3);
    g.setColour(bronze.withAlpha(.35f));g.drawRoundedRectangle(r,3,.8f);
    juce::Path p;p.startNewSubPath(w-14.f,h*.45f);p.lineTo(w-10.f,h*.6f);p.lineTo(w-6.f,h*.45f);g.setColour(bronze);g.strokePath(p,juce::PathStrokeType(1.f));
}
juce::Font VeilLookAndFeel::getComboBoxFont(juce::ComboBox&) {return juce::Font(juce::FontOptions(11.f));}
void ornament(juce::Graphics& g,juce::Rectangle<float> r,juce::Colour colour) {
    g.setColour(colour.withAlpha(.35f));g.drawRect(r,.7f);
    for (const auto p: {r.getTopLeft(),r.getTopRight(),r.getBottomLeft(),r.getBottomRight()}) {
        g.setColour(colour.withAlpha(.7f));g.fillRect(p.x-1.5f,p.y-1.5f,3.f,3.f);
    }
}
}
