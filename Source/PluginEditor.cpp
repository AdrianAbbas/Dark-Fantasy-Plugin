#include "PluginEditor.h"
#include <cmath>
using namespace veil;
using namespace veil::ui;

VeilAudioProcessorEditor::VeilAudioProcessorEditor(VeilAudioProcessor& p)
:juce::AudioProcessorEditor(p),processor(p),sigil(p.parameters,p.ritualDisplayX,p.ritualDisplayY,p.inputLevel) {
    setLookAndFeel(&look);
    setOpaque(true);
    addAndMakeVisible(sigil);
    auto& state=p.parameters;
    modules[0]=std::make_unique<ModulePanel>(state,"I   THE ABYSS","DARK FILTERING",AbyssOn,std::initializer_list<P>{Cutoff,Resonance,HighPass,Darkness,FilterMovement,FilterRate,Slope,FilterSync});
    modules[1]=std::make_unique<ModulePanel>(state,"II   THE CRYPT","CATHEDRAL REVERB",CryptOn,std::initializer_list<P>{Space,Decay,PreDelay,Damping,ReverbLowCut,Shimmer,ReverbWidth,ReverbMix,Character});
    modules[2]=std::make_unique<ModulePanel>(state,"III   THE WRAITH","SPECTRAL HAUNTING",WraithOn,std::initializer_list<P>{Blur,WraithDetune,Drift,Ghost,WraithMix,Freeze});
    modules[3]=std::make_unique<ModulePanel>(state,"IV   ANCIENT BLOOD","SATURATION & DECAY",BloodOn,std::initializer_list<P>{Drive,Texture,Age,Dust,Instability,BloodMix});
    modules[4]=std::make_unique<ModulePanel>(state,"V   THE ECHOES","MYSTICAL DELAY",EchoesOn,std::initializer_list<P>{DelayTime,Feedback,DelayDamping,Diffusion,Spread,DelayMix,DelaySync,Reverse});
    modules[5]=std::make_unique<ModulePanel>(state,"VI   THE CHOIR","PHANTOM HARMONICS",ChoirOn,std::initializer_list<P>{Voices,ChoirDetune,Formant,ChoirMovement,ChoirBlend,Interval});
    for(int i=0;i<6;++i){addAndMakeVisible(*modules[std::size_t(i)]);modules[std::size_t(i)]->onSelect=[this,i]{selectModule(i/3,i%3);};}
    const std::array<P,3> globalIds{{InputGain,GlobalMix,OutputGain}};
    for(std::size_t i=0;i<globals.size();++i){globals[i]=std::make_unique<ParameterControl>(state,globalIds[i]);addAndMakeVisible(*globals[i]);}
    const std::array<P,4> motionIds{{MotionRate,MotionDepth,MotionPattern,MotionSync}};
    for(std::size_t i=0;i<motion.size();++i){motion[i]=std::make_unique<ParameterControl>(state,motionIds[i]);addAndMakeVisible(*motion[i]);}
    for(int i=0;i<p.factoryPresetCount();++i)presets.addItem(p.factoryPresetName(i),i+1);
    presets.setTextWhenNothingSelected("CUSTOM RITUAL");presets.setTooltip("Factory rituals - each recalls a complete set of parameters.");
    presets.onChange=[this]{if(presets.getSelectedId()>0)processor.loadFactoryPreset(presets.getSelectedId()-1);};
    addAndMakeVisible(presets);
    for(auto* b:{&previous,&next,&save,&load,&settings,&recallA,&recallB,&storeA,&storeB})addAndMakeVisible(b);
    previous.onClick=[this]{int count=processor.factoryPresetCount();if(count>0)processor.loadFactoryPreset((juce::jmax(0,processor.currentFactoryPreset())+count-1)%count);};
    next.onClick=[this]{int count=processor.factoryPresetCount();if(count>0)processor.loadFactoryPreset((processor.currentFactoryPreset()+1+count)%count);};
    save.onClick=[this]{choosePresetFile(true);};load.onClick=[this]{choosePresetFile(false);};settings.onClick=[this]{showSettings();};
    storeA.onClick=[this]{processor.storeA();};storeB.onClick=[this]{processor.storeB();};
    recallA.onClick=[this]{processor.recallA();};recallB.onClick=[this]{processor.recallB();};
    storeA.setTooltip("Capture the current sound in comparison slot A.");storeB.setTooltip("Capture the current sound in comparison slot B.");
    recallA.setTooltip("Recall comparison slot A.");recallB.setTooltip("Recall comparison slot B.");
    for(auto* b:{&bypass,&ritualOn,&motionOn})addAndMakeVisible(b);
    bypassAttach=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,"bypass",bypass);
    ritualAttach=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,"ritualOn",ritualOn);
    motionAttach=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,"motionOn",motionOn);
    ritualOn.setTooltip("Morph marked parameters with the sigil. Your manual settings remain the starting point.");
    motionOn.setTooltip("Animate the Ritual orb with a bounded, smoothly evolving motion.");
    bypass.setTooltip("Smooth global bypass. The original input passes through unchanged.");
    selectModule(0,0);selectModule(1,1);
    setResizable(true,true);setResizeLimits(1040,760,1800,1200);setSize(1280,860);
    startTimerHz(30);
}
VeilAudioProcessorEditor::~VeilAudioProcessorEditor(){stopTimer();setLookAndFeel(nullptr);}
void VeilAudioProcessorEditor::selectModule(int side,int value){selected[std::size_t(side)]=value;for(int i=0;i<3;++i)if(modules[std::size_t(side*3+i)])modules[std::size_t(side*3+i)]->setExpanded(i==value);if(getWidth()>0 && getHeight()>0)resized();}
void VeilAudioProcessorEditor::resized(){
    const int w=getWidth(),h=getHeight();
    if(w<=0 || h<=0 || !globals[0] || !motion[0] || !modules[5]) return;
    previous.setBounds(28,44,26,29);presets.setBounds(59,44,244,29);next.setBounds(308,44,26,29);
    save.setBounds(59,81,70,23);load.setBounds(135,81,70,23);
    bypass.setBounds(w-232,43,100,29);settings.setBounds(w-126,44,98,28);
    recallA.setBounds(w-258,81,32,23);storeA.setBounds(w-221,81,64,23);recallB.setBounds(w-147,81,32,23);storeB.setBounds(w-110,81,82,23);
    middle={28,126,w-56,h-264};
    const int gap=18,side=juce::jlimit(270,360,int(w*.255f));
    centre={middle.getX()+side+gap,middle.getY(),middle.getWidth()-2*side-2*gap,middle.getHeight()};
    for(int s=0;s<2;++s){int x=s==0?middle.getX():middle.getRight()-side;int y=middle.getY();for(int i=0;i<3;++i){int height=i==selected[std::size_t(s)]?middle.getHeight()-112:48;modules[std::size_t(s*3+i)]->setBounds(x,y,side,height);y+=height+8;}}
    ritualOn.setBounds(centre.getX()+8,centre.getY()+34,128,25);
    const int motionHeight=juce::jlimit(170,205,centre.getHeight()/3);
    sigil.setBounds(centre.getX()-8,centre.getY()+45,centre.getWidth()+16,centre.getHeight()-motionHeight-42);
    motionArea={centre.getX()+4,centre.getBottom()-motionHeight,centre.getWidth()-8,motionHeight-8};
    motionOn.setBounds(motionArea.getX()+11,motionArea.getY()+5,110,25);
    const int half=(motionArea.getWidth()-22)/2;
    motion[0]->setBounds(motionArea.getX()+11,motionArea.getY()+38,half,89);
    motion[1]->setBounds(motionArea.getX()+11+half,motionArea.getY()+38,half,89);
    motion[2]->setBounds(motionArea.getX()+11,motionArea.getY()+129,half,44);
    motion[3]->setBounds(motionArea.getX()+11+half,motionArea.getY()+129,half,44);
    bottom={28,h-116,w-56,96};
    const int globalWidth=94;
    for(int i=0;i<3;++i)globals[std::size_t(i)]->setBounds(w/2-globalWidth*3/2+i*globalWidth,bottom.getY()-1,globalWidth,94);
}
void VeilAudioProcessorEditor::drawMeter(juce::Graphics& g,juce::Rectangle<float> b,float level,const juce::String& label){
    g.setColour(muted);g.setFont(juce::FontOptions(10.f));g.drawText(label,b.removeFromTop(18),juce::Justification::centredLeft);
    auto meter=b.removeFromTop(7);g.setColour(ink);g.fillRoundedRectangle(meter,1.5f);
    float db=juce::Decibels::gainToDecibels(level,-60.f);float amount=juce::jlimit(0.f,1.f,(db+60.f)/60.f);
    auto fill=meter.withWidth(meter.getWidth()*amount);
    g.setGradientFill(juce::ColourGradient(bronze.darker(.35f),fill.getTopLeft(),bronze,fill.getTopRight(),false));g.fillRoundedRectangle(fill,1.5f);
    if(level>=.9999f){g.setColour(juce::Colour(0xffbd5463));g.fillRect(meter.removeFromRight(3));}
    g.setColour(bronze.withAlpha(.25f));g.drawRoundedRectangle(meter,1.5f,.5f);
    g.setColour(muted.withAlpha(.65f));g.setFont(juce::FontOptions(8.5f));
    auto labels=b.removeFromTop(14);g.drawText("-60",labels,juce::Justification::centredLeft);g.drawText("-24",labels,juce::Justification::centred);g.drawText("0 dB",labels,juce::Justification::centredRight);
    g.setColour(silver.withAlpha(.8f));g.setFont(juce::FontOptions(11.f));g.drawText(level>0.00003f?juce::String(db,1)+" dBFS":"-inf dBFS",b.removeFromTop(24),juce::Justification::centredLeft);
}
void VeilAudioProcessorEditor::paint(juce::Graphics& g){
    auto r=getLocalBounds().toFloat();g.fillAll(ink);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff202026),{r.getCentreX(),0},ink,{r.getCentreX(),r.getBottom()*.6f},false));g.fillRect(r);
    // Procedural mineral grain: deterministic, original and cheap to redraw.
    juce::Random random(0x7665696c);
    for(int i=0;i<1800;++i){float x=random.nextFloat()*getWidth(),y=random.nextFloat()*getHeight();g.setColour(juce::Colours::white.withAlpha(random.nextFloat()*.022f));g.fillRect(x,y,1.f,1.f);}
    ornament(g,r.reduced(12),bronze);
    g.setColour(bronze.withAlpha(.23f));g.drawLine(28,116,float(getWidth()-28),116,.7f);g.drawLine(28,float(getHeight()-127),float(getWidth()-28),float(getHeight()-127),.7f);
    g.setColour(silver);auto logo=juce::Font(juce::FontOptions(43.f).withName(juce::Font::getDefaultSerifFontName()));logo.setExtraKerningFactor(.32f);g.setFont(logo);g.drawText("VEIL",getWidth()/2-150,21,300,51,juce::Justification::centred);
    g.setColour(bronze.withAlpha(.9f));auto subtitle=juce::Font(juce::FontOptions(9.5f));subtitle.setExtraKerningFactor(.22f);g.setFont(subtitle);g.drawText("DARK FANTASY PROCESSOR",getWidth()/2-170,78,340,17,juce::Justification::centred);
    g.setColour(muted);g.setFont(juce::FontOptions(9.f));g.drawText("FACTORY RITUAL",59,24,220,15,juce::Justification::centredLeft);
    g.drawText("COMPARE",getWidth()-258,62,65,14,juce::Justification::centredLeft);
    g.setColour(bronze);g.setFont(juce::FontOptions(12.f).withStyle("Bold"));g.drawText("THE RITUAL ENGINE",centre.getX(),centre.getY()+3,centre.getWidth(),21,juce::Justification::centred);
    g.setColour(muted);g.setFont(juce::FontOptions(9.f));g.drawText("DRAG THE LIGHT. ALTER THE WORLD.",centre.getX(),centre.getY()+23,centre.getWidth(),13,juce::Justification::centred);
    g.setColour(bronze.withAlpha(.17f));g.drawRoundedRectangle(motionArea.toFloat(),4,.7f);
    g.setColour(muted);g.setFont(juce::FontOptions(9.f));
    g.drawText("X "+juce::String(processor.ritualDisplayX.load(),2)+"   Y "+juce::String(processor.ritualDisplayY.load(),2),centre.getRight()-115,centre.getY()+38,110,15,juce::Justification::centredRight);
    g.setColour(bronze.withAlpha(.7f));g.fillEllipse(float(motionArea.getX()+132),float(motionArea.getY()+17),3.f,3.f);
    g.setColour(muted);g.drawText(processor.parameters.getRawParameterValue("ritualOn")->load()>.5f?"RITUAL MODULATES MARKED DIALS":"RITUAL OFF | MANUAL DIALS",motionArea.getX()+141,motionArea.getY()+10,motionArea.getWidth()-148,18,juce::Justification::centredLeft);
    const float meterWidth=juce::jlimit(145.f,240.f,getWidth()*.18f);
    drawMeter(g,{48.f,float(bottom.getY()+8),meterWidth,80.f},displayedInput,"INPUT SIGNAL");
    drawMeter(g,{getWidth()-48.f-meterWidth,float(bottom.getY()+8),meterWidth,80.f},displayedOutput,"OUTPUT SIGNAL");
    g.setColour(muted);g.setFont(juce::FontOptions(9.f));
    float cpu=processor.cpuLoad.load(std::memory_order_relaxed)*100.f;
    g.drawText("DSP "+juce::String(cpu,1)+" %",bottom.getX()+int(meterWidth)+40,bottom.getY()+23,100,18,juce::Justification::centredLeft);
    auto width=processor.parameters.getRawParameterValue("reverbWidth")->load();
    g.drawText("CRYPT WIDTH "+juce::String(juce::roundToInt(width*100))+" %",bottom.getRight()-int(meterWidth)-157,bottom.getY()+23,130,18,juce::Justification::centredRight);
    g.setColour(bronze.withAlpha(displayedInput>.0001f?.9f:.2f));g.fillEllipse(float(bottom.getX()+int(meterWidth)+41),float(bottom.getY()+50),4,4);
    g.setColour(muted);g.drawText("SIGNAL ACTIVITY",bottom.getX()+int(meterWidth)+51,bottom.getY()+44,110,17,juce::Justification::centredLeft);
}
void VeilAudioProcessorEditor::timerCallback(){
    displayedInput=juce::jmax(processor.inputLevel.load(std::memory_order_relaxed),displayedInput*.88f);
    displayedOutput=juce::jmax(processor.outputLevel.load(std::memory_order_relaxed),displayedOutput*.88f);
    int current=processor.currentFactoryPreset();if(current!=observedPreset){observedPreset=current;presets.setSelectedId(current>=0?current+1:0,juce::dontSendNotification);}
    sigil.tick();repaint();
}
void VeilAudioProcessorEditor::choosePresetFile(bool saving){
    auto directory=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    chooser=std::make_unique<juce::FileChooser>(saving?"Save VEIL ritual":"Load VEIL ritual",directory.getChildFile("Untitled.veilpreset"),"*.veilpreset",true);
    auto flags=juce::FileBrowserComponent::canSelectFiles|(saving?juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting:juce::FileBrowserComponent::openMode);
    juce::Component::SafePointer<VeilAudioProcessorEditor> safe(this);
    chooser->launchAsync(flags,[safe,saving](const juce::FileChooser& fc){if(!safe)return;auto file=fc.getResult();if(file==juce::File())return;if(saving)file=file.withFileExtension("veilpreset");bool okay=saving?safe->processor.savePreset(file):safe->processor.loadPreset(file);if(!okay)juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"VEIL",saving?"The ritual could not be saved. Choose a writable folder.":"This file is not a valid VEIL preset.");});
}
void VeilAudioProcessorEditor::showSettings(){
    juce::PopupMenu menu;menu.addSectionHeader("VEIL | First Edition");menu.addItem(1,"Control guide");menu.addItem(2,"Signal path & latency");menu.addItem(3,"About VEIL");
    juce::Component::SafePointer<VeilAudioProcessorEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(settings),[safe](int result){if(!safe||result==0)return;juce::String message;
        if(result==1)message="Select a module header to reveal its controls. Drag a dial; double-click to reset, or type a value below it.\n\nThe sigil morphs between Shadow, Spirit and Ruin. Bronze dots mark parameters affected by the Ritual. Your manual settings remain the starting point. Disable Ritual to hear them directly.\n\nSTORE A/B captures comparison slots; A/B recalls them. SAVE and LOAD use .veilpreset files. Host automation is available for every parameter.";
        if(result==2)message="Abyss -> Ancient Blood -> Echoes -> Crypt -> Wraith -> Choir.\n\nDry audio passes without algorithmic delay (reported latency: 0 samples). Granular, pitch-shifted, reverse and reverb layers have intentional internal delays. Use Global Wet / Dry to blend the complete chain.\n\nFree time controls apply when their sync menu is set to Free. Host synchronization falls back to 120 BPM when no tempo is supplied.";
        if(result==3)message="VEIL\nDark Fantasy Processor\n\nAn original six-module audio effect with a two-dimensional Ritual Engine. All interface artwork is drawn procedurally.\n\nBuilt with JUCE. See the project README and dependency notices for licensing and known limitations.";
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,result==1?"VEIL | Control guide":result==2?"VEIL | Signal path":"About VEIL",message);
    });
}
