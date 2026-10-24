#include "ParameterControls.h"
#include <cstring>
namespace veil::ui {
namespace {
bool ritualMapped(P p) {
    switch(p) {case Cutoff:case Darkness:case FilterMovement:case Space:case Decay:case Damping:case Shimmer:case ReverbWidth:case ReverbMix:case Blur:case WraithDetune:case Drift:case Ghost:case WraithMix:case Drive:case Texture:case Age:case Instability:case BloodMix:case Diffusion:case Spread:case ChoirBlend:case ChoirMovement:return true;default:return false;}
}
}
ParameterControl::ParameterControl(juce::AudioProcessorValueTreeState& state,P p,bool ritualMarked):parameter(p),marked(ritualMarked) {
    const auto& s=specs[p];
    ritualEnabled=state.getRawParameterValue("ritualOn");
    juce::String choices(s.choices);
    if(choices.isNotEmpty()) {
        auto tokens=juce::StringArray::fromTokens(choices,"|","");
        if(tokens.size()==2 && s.min==0 && s.max==1 && tokens[0]=="Off" && tokens[1]=="On") {
            kind=1;toggle.setButtonText(s.name);addAndMakeVisible(toggle);buttonAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,s.id,toggle);
        } else {
            kind=2;menu.addItemList(tokens,1);addAndMakeVisible(menu);comboAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state,s.id,menu);
        }
    } else {
        dial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        dial.setRotaryParameters(juce::MathConstants<float>::pi*1.23f,juce::MathConstants<float>::pi*2.77f,true);
        dial.setColour(juce::Slider::textBoxTextColourId,silver);
        dial.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);
        dial.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        dial.setColour(juce::Slider::textBoxHighlightColourId,burgundy);
        dial.setTextBoxStyle(juce::Slider::TextBoxBelow,false,90,17);
        dial.setDoubleClickReturnValue(true,s.def);
        dial.setPopupDisplayEnabled(false,false,nullptr);
        dial.setTooltip(juce::String(s.name)+" - drag to adjust; double-click to reset. Shift-drag for precision.");
        dial.setVelocityBasedMode(false);dial.setMouseDragSensitivity(180);
        addAndMakeVisible(dial);sliderAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state,s.id,dial);
        dial.textFromValueFunction=[s](double v) {
            if(s.max<=1.f && s.min>=0 && std::strlen(s.unit)==0) return juce::String(juce::roundToInt(v*100))+" %";
            if(juce::String(s.unit)=="Hz" && v>=1000) return juce::String(v/1000.,1)+" kHz";
            int decimals=(s.max<=30 && s.max>4)?1:((s.max<=4 && s.min!=1)?2:0);
            return juce::String(v,decimals)+(std::strlen(s.unit)>0?" "+juce::String(s.unit):juce::String());
        };
        dial.valueFromTextFunction=[s](const juce::String& text) {auto value=text.getDoubleValue();if(text.containsIgnoreCase("k"))value*=1000.;if(text.contains("%") && s.max<=1.f)value*=.01;return value;};
        dial.updateText();
    }
    setTitle(s.name);setDescription(juce::String("VEIL ")+s.name);
    if(marked) dial.setTooltip(juce::String(s.name)+" - Ritual Engine adds an archetype offset to this manual setting. Double-click to reset.");
}
void ParameterControl::paint(juce::Graphics& g) {
    if(kind==1)return;
    g.setColour(silver.withAlpha(.85f));g.setFont(juce::FontOptions(10.4f));
    auto text=juce::String(specs[parameter].name);
    g.drawFittedText(text,getLocalBounds().removeFromTop(15).reduced(2,0),juce::Justification::centred,1);
    if(marked){g.setColour(bronze.withAlpha(ritualEnabled && ritualEnabled->load()>.5f?.8f:.15f));g.fillEllipse(float(getWidth())-7,4,3,3);}
}
void ParameterControl::resized() {
    if(kind==0)dial.setBounds(getLocalBounds().withTrimmedTop(13));
    else if(kind==1)toggle.setBounds(getLocalBounds().withSizeKeepingCentre(getWidth()-8,26));
    else menu.setBounds(getLocalBounds().withTrimmedTop(18).reduced(4,2));
}
ModulePanel::ModulePanel(juce::AudioProcessorValueTreeState& state,const juce::String& t,const juce::String& sub,P enable,std::initializer_list<P> params):title(t),subtitle(sub) {
    select.setButtonText("");select.setTitle(title+" details");select.setTooltip("Show "+title+" controls");select.setAlpha(0.f);select.onClick=[this]{if(onSelect)onSelect();};addAndMakeVisible(select);
    on.setButtonText("");on.setTooltip("Enable "+title);on.setTitle(title+" enable");addAndMakeVisible(on);
    onAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,specs[enable].id,on);
    for(auto p:params){auto c=std::make_unique<ParameterControl>(state,p,ritualMapped(p));addChildComponent(*c);controls.push_back(std::move(c));}
}
void ModulePanel::setExpanded(bool value){expanded=value;for(auto& c:controls)c->setVisible(value);resized();repaint();}
void ModulePanel::paint(juce::Graphics& g){
    auto r=getLocalBounds().toFloat().reduced(.5f);
    g.setGradientFill(juce::ColourGradient(panel.brighter(.015f),r.getTopLeft(),ink.brighter(.02f),r.getBottomRight(),false));g.fillRoundedRectangle(r,4);
    g.setColour(bronze.withAlpha(expanded?.48f:.23f));g.drawRoundedRectangle(r,4,.8f);
    g.setColour(bronze);g.setFont(juce::FontOptions(13.f).withStyle("Bold"));g.drawText(title,14,7,getWidth()-58,19,juce::Justification::centredLeft);
    g.setColour(muted);g.setFont(juce::FontOptions(9.f));g.drawText(subtitle,14,26,getWidth()-55,13,juce::Justification::centredLeft);
    if(expanded){g.setColour(bronze.withAlpha(.2f));g.drawLine(14,47,float(getWidth()-14),47,.7f);}
    else {g.setColour(bronze.withAlpha(.6f));g.setFont(juce::FontOptions(15.f));g.drawText("+",getWidth()-52,13,18,18,juce::Justification::centred);}
}
void ModulePanel::resized(){
    if(getWidth()<30 || getHeight()<48)return;
    select.setBounds(0,0,getWidth()-34,47);on.setBounds(getWidth()-29,10,23,27);
    if(!expanded)return;
    const int top=52,bottom=7;
    std::vector<ParameterControl*> dials,compact;
    for(auto& c:controls)(c->isCompact()?compact:dials).push_back(c.get());
    // Panels with many dials use three columns so every dial stays readable.
    const int columns=dials.size()>6 ? 3 : 2;
    const int compactRows=(int(compact.size())+1)/2;
    const int dialRows=(int(dials.size())+columns-1)/columns;
    const int available=getHeight()-top-bottom;
    const int ch=juce::jmin(45,available/juce::jmax(1,dialRows+compactRows));
    const int dh=juce::jmin(128,(available-ch*compactRows)/juce::jmax(1,dialRows));
    const int cw=(getWidth()-20)/2, dw=(getWidth()-20)/columns;
    for(std::size_t i=0;i<dials.size();++i)dials[i]->setBounds(10+int(i%std::size_t(columns))*dw,top+int(i/std::size_t(columns))*dh,dw,dh);
    const auto ct=top+dh*dialRows;
    for(std::size_t i=0;i<compact.size();++i){const bool lastWide=(i==compact.size()-1 && compact.size()%2==1);compact[i]->setBounds(10+int(i%2)*cw,ct+int(i/2)*ch,lastWide?cw*2:cw,ch);}
}
}
