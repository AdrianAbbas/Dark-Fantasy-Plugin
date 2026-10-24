#include "RitualSigil.h"
#include <cmath>
namespace veil::ui {
RitualSigil::RitualSigil(juce::AudioProcessorValueTreeState& s,std::atomic<float>& dx,std::atomic<float>& dy,std::atomic<float>& l):state(s),x(dx),y(dy),signal(l) {
    xParameter=state.getParameter("ritualX");yParameter=state.getParameter("ritualY");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTitle("Ritual Engine");setDescription("Drag the orb to morph between Shadow, Spirit and Ruin.");
}
juce::Rectangle<float> RitualSigil::triangleBounds() const {
    const float side=juce::jmax(20.f,juce::jmin(float(getWidth())-100.f,juce::jmin(float(getHeight())-92.f,(float(getHeight())-44.f)/1.2f)));
    const float height=side*.8660254f;
    // Move the triangle's bounding box up by h/6: its centroid lies at y=2h/3.
    return juce::Rectangle<float>(side,height).withCentre({getWidth()*.5f,getHeight()*.5f+10.f-height/6.f});
}
void RitualSigil::paint(juce::Graphics& g) {
    auto b=triangleBounds();auto centre=b.getCentre();centre.y+=b.getHeight()*.1666667f;
    float radius=b.getWidth()*.60f;
    const auto ring=juce::Rectangle<float>(radius*2,radius*2).withCentre(centre);
    float amount=juce::jlimit(0.f,1.f,glow*2.f);
    g.setGradientFill(juce::ColourGradient(burgundy.withAlpha(.14f+amount*.1f),centre,ink.withAlpha(0.f),{centre.x+radius,centre.y},true));g.fillEllipse(ring.expanded(25));
    g.setColour(bronze.withAlpha(.13f));g.drawEllipse(ring,1);
    g.setColour(bronze.withAlpha(.35f));g.drawEllipse(ring.reduced(7),.65f);
    g.setColour(bronze.withAlpha(.13f));g.drawEllipse(ring.reduced(17),.5f);
    for(int i=0;i<72;++i){float angle=i*juce::MathConstants<float>::twoPi/72.f+animation*.03f;float inner=radius-((i%6)==0?14.f:9.f);g.setColour(bronze.withAlpha(i%6==0?.65f:.24f));g.drawLine(centre.x+std::cos(angle)*inner,centre.y+std::sin(angle)*inner,centre.x+std::cos(angle)*(radius-4),centre.y+std::sin(angle)*(radius-4),.7f);}
    // These original geometric inscriptions are vectors, independent of the audio clock.
    for(int i=0;i<12;++i){float a=i*juce::MathConstants<float>::twoPi/12.f-animation*.02f;juce::Point<float> p(centre.x+std::cos(a)*(radius-24),centre.y+std::sin(a)*(radius-24));juce::Path rune;rune.startNewSubPath(-3,-5);rune.lineTo(3,0);rune.lineTo(-3,5);rune.startNewSubPath(0,-5);rune.lineTo(0,5);g.setColour(bronze.withAlpha(.45f));g.strokePath(rune,juce::PathStrokeType(.7f),juce::AffineTransform::rotation(a+juce::MathConstants<float>::halfPi).translated(p));}
    const juce::Point<float> shadow=b.getBottomLeft(), spirit(b.getCentreX(),b.getY()),ruin=b.getBottomRight();
    juce::Path triangle;triangle.startNewSubPath(shadow);triangle.lineTo(spirit);triangle.lineTo(ruin);triangle.closeSubPath();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff472234).withAlpha(.2f),shadow,bronze.withAlpha(.035f),spirit,false));g.fillPath(triangle);
    g.setColour(bronze.withAlpha(.5f));g.strokePath(triangle,juce::PathStrokeType(.9f));
    for(auto p:{shadow,spirit,ruin}){g.setColour(bronze.withAlpha(.4f));g.drawLine({centre,p},.5f);g.setColour(ink);g.fillEllipse(p.x-5,p.y-5,10,10);g.setColour(bronze);g.drawEllipse(p.x-4,p.y-4,8,8,.8f);}
    g.setColour(bronze.withAlpha(.18f));g.drawEllipse(juce::Rectangle<float>(b.getWidth()*.5f,b.getWidth()*.5f).withCentre(centre),.7f);
    const bool useMotion=!dragging && state.getRawParameterValue("motionOn")->load()>.5f && state.getRawParameterValue("ritualOn")->load()>.5f;
    float nx=juce::jlimit(0.f,1.f,useMotion?x.load(std::memory_order_relaxed):state.getRawParameterValue("ritualX")->load());float ny=juce::jlimit(0.f,1.f,useMotion?y.load(std::memory_order_relaxed):state.getRawParameterValue("ritualY")->load());
    nx=juce::jlimit(.5f-.5f*ny,.5f+.5f*ny,nx);
    auto orb=juce::Point<float>(b.getX()+nx*b.getWidth(),b.getY()+ny*b.getHeight());
    float spiritWeight=1.f-ny, ruinWeight=nx-.5f+.5f*ny, shadowWeight=1.f-spiritWeight-ruinWeight;
    g.setColour(bronze.withAlpha(.13f));g.drawLine({orb,shadow},.7f);g.drawLine({orb,spirit},.7f);g.drawLine({orb,ruin},.7f);
    g.setGradientFill(juce::ColourGradient(bronze.withAlpha(.35f+amount*.25f),orb,bronze.withAlpha(0.f),orb.translated(24,0),true));g.fillEllipse(orb.x-24,orb.y-24,48,48);
    g.setColour(bronze.withAlpha(.4f));g.drawEllipse(orb.x-10,orb.y-10,20,20,.8f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff1e7d1),orb.translated(-2,-3),bronze.darker(.1f),orb.translated(5,6),false));g.fillEllipse(orb.x-6,orb.y-6,12,12);
    g.setColour(juce::Colours::white.withAlpha(.65f));g.fillEllipse(orb.x-2,orb.y-3,3,3);
    auto label=[&](juce::String name,int percent,juce::Point<float> p,bool upper){g.setColour(silver.withAlpha(.9f));g.setFont(juce::FontOptions(11.f).withStyle("Bold"));g.drawText(name,int(p.x)-65,int(p.y)+(upper?-33:14),130,17,juce::Justification::centred);g.setColour(muted);g.setFont(juce::FontOptions(9.f));g.drawText(juce::String(percent)+" %",int(p.x)-45,int(p.y)+(upper?-17:30),90,14,juce::Justification::centred);};
    label("SHADOW",juce::roundToInt(shadowWeight*100),shadow,false);label("SPIRIT",juce::roundToInt(spiritWeight*100),spirit,true);label("RUIN",juce::roundToInt(ruinWeight*100),ruin,false);
}
void RitualSigil::setPosition(juce::Point<float> p){auto b=triangleBounds();auto ny=juce::jlimit(0.f,1.f,(p.y-b.getY())/b.getHeight());auto nx=juce::jlimit(.5f-.5f*ny,.5f+.5f*ny,(p.x-b.getX())/b.getWidth());if(xParameter)xParameter->setValueNotifyingHost(nx);if(yParameter)yParameter->setValueNotifyingHost(ny);repaint();}
void RitualSigil::mouseDown(const juce::MouseEvent& e){dragging=true;if(xParameter)xParameter->beginChangeGesture();if(yParameter)yParameter->beginChangeGesture();setPosition(e.position);}
void RitualSigil::mouseDrag(const juce::MouseEvent& e){if(dragging)setPosition(e.position);}
void RitualSigil::mouseUp(const juce::MouseEvent&){if(dragging){if(xParameter)xParameter->endChangeGesture();if(yParameter)yParameter->endChangeGesture();dragging=false;}}
void RitualSigil::tick(){animation+=.0333333f;glow=.86f*glow+.14f*juce::jlimit(0.f,1.f,signal.load(std::memory_order_relaxed));repaint();}
}
