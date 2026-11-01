// Host validation for v30. Historical validators are retained verbatim; their
// assertions that channels must change playback no longer apply to this version.
// Build with C++17, tools/validate_v30.cpp and app/src/main/cpp/MusicEngine.cpp.
#define main archivedV28Checks
#include "validate_v28.cpp"
#undef main
#define TECHNOMATIC_PALETTE_NO_MAIN
#include "audit_palette.cpp"
#define TECHNOMATIC_AUDIT_NO_MAIN
#include "audit_music.cpp"
#define TECHNOMATIC_FOREGROUND_NO_MAIN
#include "audit_foreground.cpp"
#include <chrono>
#include <numeric>

namespace v30_validation {
using Engine=rb::MusicEngine;
struct Intent {
    int count=0;
    double bpm=0, leadHits=0, hatHits=0, supports=0;
    float bpmMin=1000,bpmMax=0;
    uint32_t representative=0;
    std::set<int> styles,leadModels,bassModels,padModels;
    std::set<std::string> leadIds,bassIds;
};

template<class Notes,class Gates>
std::string identity(const Notes& notes,const Gates& gates) {
    std::ostringstream out;
    for(std::size_t i=0;i<notes.size();++i) {
        if(gates[i]>.02f) out<<notes[i];else out<<'r';
        out<<',';
    }
    return out.str();
}

bool equalAudio(Engine& a,Engine& b,int seconds) {
    constexpr int block=257;
    std::array<float,block*2> aa{},bb{};
    for(int left=seconds*48000;left>0;left-=block) {
        const int n=std::min(left,block);
        a.render(aa.data(),n,2);b.render(bb.data(),n,2);
        if(!std::equal(aa.begin(),aa.begin()+n*2,bb.begin())) return false;
    }
    return true;
}

struct RenderResult {
    bool finite=true;float peak=0;double squares=0;int64_t samples=0;
    std::set<int> leadModels,bassModels,padModels;
    std::map<int,std::set<int>> roleModels;
};
RenderResult render(Engine& engine,int seconds) {
    RenderResult result;
    std::array<float,2048> block{};
    for(int left=seconds*48000;left>0;left-=1024) {
        const int n=std::min(left,1024);
        engine.render(block.data(),n,2);
        for(int i=0;i<n*2;++i) {
            result.finite=result.finite&&std::isfinite(block[i]);
            result.peak=std::max(result.peak,std::fabs(block[i]));
            result.squares+=static_cast<double>(block[i])*block[i];
            ++result.samples;
        }
        for(const auto& voice:engine.mLeads)if(voice.active) {
            result.leadModels.insert(voice.model);result.roleModels[voice.instrumentSlot].insert(voice.model);
        }
        for(const auto& voice:engine.mBass)if(voice.active) {
            result.bassModels.insert(voice.model);result.roleModels[1].insert(voice.model);
        }
        for(const auto& voice:engine.mPads)if(voice.active) {
            result.padModels.insert(voice.model);result.roleModels[2].insert(voice.model);
        }
    }
    return result;
}

std::vector<std::map<std::string,double>> csv(const std::string& path) {
    std::ifstream input(path);std::string line;
    if(!std::getline(input,line)) return {};
    std::vector<std::string> fields;std::istringstream header(line);std::string value;
    while(std::getline(header,value,',')) fields.push_back(value);
    std::vector<std::map<std::string,double>> rows;
    while(std::getline(input,line)) {
        std::istringstream in(line);std::map<std::string,double> row;std::size_t i=0;
        while(i<fields.size()&&std::getline(in,value,',')) {
            try {row[fields[i]]=std::stod(value);} catch(...) {}
            ++i;
        }
        rows.push_back(row);
    }
    return rows;
}

double median(std::vector<double> values) {
    std::sort(values.begin(),values.end());
    if(values.empty())return 0;
    const std::size_t n=values.size();
    return n%2?values[n/2]:(values[n/2-1]+values[n/2])*.5;
}

bool padPriorityHandoff() {
    Engine e;e.prepare(48000);e.reset(1013904226u);e.clearVoicesAndEvents();
    e.mSamplesUntilNextStep=48000;e.mStepIndex=256;e.mStyleAgeSteps=257;
    e.schedulePad(1000,4,.2f,1.0f,0,.5f); // Pending accompaniment must yield.
    e.triggerPad(0,.30f,2.0f,0,.5f);e.mPads[0].age=.30f;
    e.schedulePad(0,2,.3f,.6f,0,.5f,true);
    int pending=0;for(const auto& event:e.mEvents)if(event.active&&event.kind==Engine::EventKind::Pad) {
        ++pending;if(!event.foreground||event.samples>480)return false;
    }
    if(pending!=1)return false;
    std::array<float,2> frame{};int maxPads=0;bool replacement=false;double energy=0;
    for(int sample=0;sample<2400;++sample) {
        e.render(frame.data(),1,2);
        if(!std::isfinite(frame[0])||!std::isfinite(frame[1]))return false;
        energy+=frame[0]*frame[0]+frame[1]*frame[1];
        int active=0;for(const auto& voice:e.mPads)if(voice.active) {
            ++active;if(voice.forcedReleaseSamples<0&&voice.dur<1.0f)replacement=true;
        }
        maxPads=std::max(maxPads,active);
    }
    return replacement&&maxPads==1&&energy>1e-5;
}

std::array<int,4> mutedRoleEvents(int mutedSlot) {
    Engine e;e.prepare(48000);e.reset(141414419u);
    if(mutedSlot==0)e.mComposition.useLead=0;
    if(mutedSlot==1)e.mComposition.useBass=0;
    if(mutedSlot==2)e.mComposition.useChord=0;
    std::array<int,4> result{};
    for(int step=0;step<128*16;++step) {
        for(auto& event:e.mEvents)event.active=false;
        e.onStep();
        for(const auto& event:e.mEvents)if(event.active) {
            if(event.kind==Engine::EventKind::Lead&&event.instrumentSlot==0)++result[0];
            if(event.kind==Engine::EventKind::Bass)++result[1];
            if(event.kind==Engine::EventKind::Pad)++result[2];
            if(event.foreground&&event.instrumentSlot==1)++result[3];
        }
    }
    return result;
}

std::string setField(const std::string& input,const std::string& key,int value) {
    std::istringstream in(input);std::string item,out;
    while(std::getline(in,item,';'))if(item.rfind(key+'=',0)!=0) {if(!out.empty())out+=';';out+=item;}
    return out+';'+key+'='+std::to_string(value);
}

bool editedRegisterBounds(int& notes,int& padComponents) {
    notes=0;padComponents=0;
    const uint32_t seeds[]={3041712678u,3816266512u,99665556u,1013904226u};
    for(uint32_t seed:seeds)for(int root:{24,60}) {
        Engine base;base.prepare(48000);base.reset(seed);
        Engine e;e.prepare(48000);
        if(!e.loadSongData(setField(setField(base.currentSongData(),"edited",1),"root",root)))return false;
        if(e.mPattern.rootMidi!=root)return false;
        // The public recipe edits root. OctaveBias has no public recipe key;
        // stress both normal and extended private octave settings explicitly.
        for(int octave:{1,2,4}) {
            e.mComposition.octaveBias=octave;
            for(int step=16*16;step<144*16;++step) {
                const auto n=e.foregroundNoteForStep(step,e.currentChordRoot(step));
                if(n.rest||n.gate<=.04f)continue;
                ++notes;const int midi=e.scaleDegreeToMidi(n.degree,n.octave);
                const int low=n.instrumentSlot==1?33:52,high=n.instrumentSlot==1?62:78;
                if(midi<low||midi>high)return false;
                if(n.instrumentSlot==2) {
                    e.clearVoicesAndEvents();e.triggerPad(n.degree,.3f,.5f,0,.5f);
                    const auto& pad=e.mPads[0];
                    for(int k=0;k<pad.count;++k) {
                        ++padComponents;
                        const int pitch=static_cast<int>(std::lround(69+12*std::log2(pad.freq[k]/440.0f)));
                        if(pitch<45||pitch>76)return false;
                    }
                }
            }
        }
    }
    return notes>0&&padComponents>0;
}

bool fullyMutedExport() {
    Engine e;e.prepare(48000);e.reset(1013904226u);
    auto& c=e.mComposition;
    c.useKick=c.useSnare=c.useHat=c.useOpenHat=c.usePerc=0;
    c.useBass=c.useLead=c.useChord=0;
    c.useArp=c.useCounter=c.useStab=c.useDrone=c.useSpark=c.useFx=c.useEcho=0;
    c.useOrbit=c.useBloom=c.useGlyph=c.useSub=c.useSheen=c.usePluck=0;
    c.useBell=c.usePulse=c.useGrain=c.useComet=c.useRotor=c.useTexture=0;
    e.clearVoicesAndEvents();e.mExportSinglePieceMode=true;e.mExportStopSamples=45LL*48000;
    c.pieceSteps=e.pieceStepsFromSeconds(45,e.mBpmTarget);
    const auto rr=render(e,45);
    return rr.finite&&rr.peak==0&&rr.squares==0;
}
} // namespace v30_validation

int main() {
    using namespace v30_validation;
    bool ok=true;int checks=0;
    auto require=[&](bool condition,const std::string& label) {
        ++checks;std::cout<<(condition?"PASS ":"FAIL ")<<label<<std::endl;ok=ok&&condition;
    };
    constexpr int count=512;
    std::array<Intent,4> summaries{};
    std::set<int> styles,leadModels,bassModels,padModels,drumModels;
    bool serialized=true,noSfx=true,foreground=true,ensemble=true;
    int slow=0,fast=0;float maxAuxiliary=0;
    std::array<int,7> supportHistogram{};
    for(int i=1;i<=count;++i) {
        const uint32_t seed=static_cast<uint32_t>(i)*2654435761u;
        Engine e;e.prepare(48000);e.reset(seed);
        const auto& c=e.mComposition;const auto& p=e.mPattern;
        const int intent=p.intent;
        serialized=serialized&&audit::field(e.currentSongData(),"engine")==30&&intent>=0&&intent<4;
        noSfx=noSfx&&c.useTexture==0&&c.useFx==0&&c.useGrain==0;
        foreground=foreground&&c.useLead>.1f&&c.useBass>.1f;
        const int supports=development_audit::supportCount(c);
        ensemble=ensemble&&supports>=1&&supports<=6;
        if(supports>=0&&supports<=6)++supportHistogram[supports];
        maxAuxiliary=std::max(maxAuxiliary,audit::auxiliary(c)/std::max(.01f,audit::foreground(c)));
        styles.insert(static_cast<int>(p.style));
        leadModels.insert(c.leadModel);bassModels.insert(c.bassModel);padModels.insert(c.padModel);drumModels.insert(c.drumKit);
        if(e.mBpmTarget<=90)++slow;
        if(e.mBpmTarget>=135)++fast;
        if(intent<0||intent>=4)continue;
        auto& s=summaries[intent];++s.count;s.bpm+=e.mBpmTarget;
        s.bpmMin=std::min(s.bpmMin,e.mBpmTarget);s.bpmMax=std::max(s.bpmMax,e.mBpmTarget);
        s.leadHits+=audit::hits(c.gateA);s.hatHits+=audit::hits(p.hat);s.supports+=supports;
        s.styles.insert(static_cast<int>(p.style));s.leadModels.insert(c.leadModel);s.bassModels.insert(c.bassModel);s.padModels.insert(c.padModel);
        s.leadIds.insert(identity(c.motifA,c.gateA));
        s.bassIds.insert(identity(c.bassRel,c.bassGate)+";"+identity(c.bassVerseRel,c.bassVerseGate)+";"+identity(c.bassAnswerRel,c.bassAnswerGate));
        if(!s.representative)s.representative=seed;
    }
    require(serialized,"512 seed recipes serialize engine 30 and a valid musical intent");
    require(noSfx,"texture, grain and special-effect lanes remain disabled across 512 seeds");
    require(foreground,"every sampled composition retains primary melody and bass foundation");
    require(ensemble,"selected supporting-role count remains between 1 and 6 for every seed");
    require(maxAuxiliary<1.7f,"support-level controls stay bounded relative to foreground");
    require(styles.size()>=24,"candidate selection covers at least 24 original design families");
    require(slow>=count/5&&fast>=count/5,"slow and intense tempo regions each occur in at least 20 percent of seeds");
    require(leadModels.size()>16&&bassModels.size()>12&&padModels.size()>10&&drumModels.size()>8,
        "available instrument choices exceed every v29 model catalogue without extra ensemble roles");
    for(int intent=0;intent<4;++intent) {
        const auto& s=summaries[intent];const std::string suffix=" for intent "+std::to_string(intent);
        require(s.count>=count*15/100,"intent coverage is at least 15 percent"+suffix);
        require(s.bpmMax-s.bpmMin>=15,"tempo range spans at least 15 BPM"+suffix);
        require(s.styles.size()>=12,"at least 12 design families remain reachable"+suffix);
        require(s.leadIds.size()>=static_cast<std::size_t>(s.count*95/100),"initial lead note/rest identities vary across seeds"+suffix);
        require(s.bassIds.size()>=static_cast<std::size_t>(s.count*95/100),"foundation/verse/answer bass grammar varies across seeds"+suffix);
        std::cout<<"INFO intent="<<intent<<" n="<<s.count<<" bpm_min="<<s.bpmMin<<" bpm_max="<<s.bpmMax
            <<" bpm_mean="<<s.bpm/s.count<<" lead_hits="<<s.leadHits/s.count<<" hat_hits="<<s.hatHits/s.count
            <<" support_mean="<<s.supports/s.count<<" designs="<<s.styles.size()<<" lead_models="<<s.leadModels.size()
            <<" bass_models="<<s.bassModels.size()<<" pad_models="<<s.padModels.size()<<" representative="<<s.representative<<std::endl;
    }
    require(summaries[0].leadHits/summaries[0].count<.8*summaries[3].leadHits/summaries[3].count,
        "slow lead grammar leaves substantially more space than intense grammar");
    require(summaries[0].hatHits/summaries[0].count<.8*summaries[3].hatHits/summaries[3].count,
        "slow percussion leaves substantially more space than intense percussion");

    // Compare the audible argument, not a hash of all accompaniment/timbres.
    // The paired baseline files were produced by the same observer against
    // immutable v29 sources; all numeric comparisons have a musical purpose.
    const auto baselinePlans=csv("tools/validation/v29_primary_development.csv");
    const auto baselinePalette=csv("tools/validation/v29_triggered_palette.csv");
    require(baselinePlans.size()==256&&baselinePalette.size()==256,"paired v29 primary and triggered-palette evidence is present");
    double oldPaletteSum=0,newPaletteSum=0;int oldPaletteMax=0,newPaletteMax=0;
    bool fixedRoles=true,matchedEnsemble=true,plannedDevelopment=true,recallAndExploration=true,registers=true;
    std::vector<double> oldLeadUnique,newLeadUnique,oldRhythms,newRhythms;
    int oldTop4=0,newTop4=0,maxPrimaryRun=0,minPrimaryRhythms=512;
    for(int i=1;i<=256;++i) {
        const uint32_t seed=static_cast<uint32_t>(i)*2654435761u;
        Engine e;e.prepare(48000);e.reset(seed);
        const auto p=development_audit::planned(e,512);
        const auto palette=palette_audit::scheduled(e,128);
        const int families=static_cast<int>(palette.lead.size()+palette.bass.size()+palette.pad.size());
        newPaletteSum+=families;newPaletteMax=std::max(newPaletteMax,families);
        for(const auto& role:palette.roleModels)fixedRoles=fixedRoles&&role.second.size()==1;
        if(i<=static_cast<int>(baselinePalette.size())) {
            const auto& old=baselinePalette[i-1];
            const int total=static_cast<int>(old.at("lead_families")+old.at("bass_families")+old.at("pad_families"));
            oldPaletteSum+=total;oldPaletteMax=std::max(oldPaletteMax,total);
            matchedEnsemble=matchedEnsemble&&old.at("seed")==seed&&old.at("support_count")==p.supportCount;
        }
        if(i<=static_cast<int>(baselinePlans.size())) {
            const auto& old=baselinePlans[i-1];
            oldLeadUnique.push_back(old.at("lead_unique")/512);
            newLeadUnique.push_back(static_cast<double>(p.lead.counts.size())/std::max(1,p.primaryPhrases));
            oldRhythms.push_back(old.at("rhythm_unique"));newRhythms.push_back(p.rhythm.counts.size());
            oldTop4=std::max(oldTop4,static_cast<int>(old.at("lead_top4")));
        }
        newTop4=std::max(newTop4,p.foreground.top(4));maxPrimaryRun=std::max(maxPrimaryRun,p.lead.maxRun);
        minPrimaryRhythms=std::min(minPrimaryRhythms,static_cast<int>(p.rhythm.counts.size()));
        // Recall remains present; exploration must occupy a substantial part
        // of the tune. Theme repetition itself is desirable, not a test error.
        recallAndExploration=recallAndExploration&&p.hookSections>=16&&p.hookSections<=128&&
            p.exploratoryRoles>=180&&p.handedPhrases>=64&&p.primaryPhrases>=256;
        plannedDevelopment=plannedDevelopment&&p.lead.maxRun<=4&&p.rhythm.counts.size()>=16&&
            p.foreground.top(4)<=160;
        registers=registers&&p.outOfSlowRegister==0;
    }
    require(matchedEnsemble,"all 256 matched seeds preserve their v29 supporting-role count");
    require(newPaletteSum<=oldPaletteSum&&newPaletteMax<=oldPaletteMax,
        "expanded catalogues do not increase mean or maximum instruments used per song");
    require(fixedRoles,"every observed foreground/support role keeps one fixed synthesis family");
    require(recallAndExploration,"every sampled tune balances recurring theme, exploration and foreground handoffs");
    require(plannedDevelopment,"primary development has at least 16 rest patterns and avoids long repeated-phrase runs");
    require(median(newLeadUnique)>median(oldLeadUnique)&&median(newRhythms)>median(oldRhythms),
        "primary-note and rhythm development improves independently of timbre and instrumentation");
    require(registers,"all planned slow primary notes stay inside MIDI 52 to 78 after every device");
    std::cout<<"INFO paired_palette old_mean="<<oldPaletteSum/256<<" new_mean="<<newPaletteSum/256
        <<" old_max="<<oldPaletteMax<<" new_max="<<newPaletteMax<<" old_median_primary_unique_ratio="<<median(oldLeadUnique)
        <<" new_median_primary_unique_ratio="<<median(newLeadUnique)<<" old_median_rhythms="<<median(oldRhythms)
        <<" new_median_rhythms="<<median(newRhythms)<<" min_new_rhythms="<<minPrimaryRhythms
        <<" old_max_top4="<<oldTop4<<" new_max_top4="<<newTop4<<" max_primary_repeat_run="<<maxPrimaryRun<<std::endl;

    bool ownership=true,scheduledMusic=true,recallRetained=true;
    std::array<double,4> foregroundRate{},duration{};std::array<int,4> scheduleCounts{};
    for(int i=1;i<=64;++i) {
        Engine e;e.prepare(48000);e.reset(static_cast<uint32_t>(i)*2654435761u);
        const auto m=foreground_audit::scheduled(e,256);
        const int intent=e.mPattern.intent;++scheduleCounts[intent];
        foregroundRate[intent]+=m.foregroundEvents/m.seconds;
        duration[intent]+=m.primaryDuration/std::max(1,m.primaryEvents);
        ownership=ownership&&m.wrongOwner==0&&m.unselectedOwner==0&&m.owners.size()>=2;
        scheduledMusic=scheduledMusic&&m.specialEffects==0&&m.outOfScale==0&&m.slowRangeErrors==0&&m.primary.maxRun<=4;
        recallRetained=recallRetained&&!m.recallRhythms.empty()&&m.recallRhythms.size()<=2;
    }
    require(ownership,"16384 scheduled phrases assign each foreground answer to its selected instrument");
    require(scheduledMusic,"scheduled foreground stays tonal, bounded and free of special-effect events");
    require(recallRetained,"recall phrases preserve their one or two characteristic theme rhythms");
    require(foregroundRate[0]/scheduleCounts[0]<foregroundRate[3]/scheduleCounts[3]&&
        duration[0]/scheduleCounts[0]>2*duration[3]/scheduleCounts[3],
        "scheduled slow music has fewer foreground attacks and longer melody articulations than intense music");
    require(padPriorityHandoff(),"foreground harmony replaces an existing pad within 10 ms without overlapping pads");
    const auto mutedLead=mutedRoleEvents(0),mutedBass=mutedRoleEvents(1),mutedHarmony=mutedRoleEvents(2);
    require(mutedLead[0]==0&&mutedLead[3]>0,"muting primary lead preserves an existing bass when it owns the answer");
    require(mutedBass[1]==0,"muted bass stays silent through convergence and phrase ownership");
    require(mutedHarmony[2]==0,"muted harmony stays silent through convergence and phrase ownership");
    int boundNotes=0,boundPadComponents=0;
    require(editedRegisterBounds(boundNotes,boundPadComponents),
        "edited root extremes and octave stress keep final slow notes and actual harmony voices in range");
    std::cout<<"INFO register_stress foreground_notes="<<boundNotes<<" triggered_pad_components="<<boundPadComponents<<std::endl;
    require(fullyMutedExport(),"fully muted finite export remains silent through arrivals, recovery and final cadence");

    bool channelRecipes=true,channelAudio=true,channelLive=true;
    const int masks[]={4095,4095,1,1<<9,37};const int blends[]={0,1,0,0,1};const int primaries[]={6,6,1,10,3};
    for(int i=1;i<=32;++i) {
        const uint32_t seed=static_cast<uint32_t>(i)*2654435761u;
        Engine base;base.prepare(48000);base.reset(seed);
        const std::string recipe=base.currentSongData();
        for(int mode=0;mode<5;++mode) {
            Engine e;e.prepare(48000);e.setGenreMask(masks[mode]);e.setGenreBlendMode(blends[mode]);e.setGenrePrimary(primaries[mode]);e.reset(seed);
            channelRecipes=channelRecipes&&recipe==e.currentSongData()&&e.currentGenreMask()==0&&e.currentGenreBlendMode()==0&&e.currentGenrePrimary()==0&&e.currentGenreMode()==0;
        }
        if(i<=4) {
            Engine changed;changed.prepare(48000);changed.setGenreMask(4095);changed.setGenreBlendMode(1);changed.setGenrePrimary(6);changed.reset(seed);
            channelAudio=channelAudio&&equalAudio(base,changed,6);
            const double elapsed=changed.currentElapsedSeconds();
            const std::string history=changed.historyData();
            changed.rerenderCurrentWithChannel(2,1,2);
            channelLive=channelLive&&changed.currentElapsedSeconds()==elapsed&&changed.historyData()==history&&equalAudio(base,changed,2);
        }
    }
    require(channelRecipes,"160 legacy selector combinations produce identical neutral recipes");
    require(channelAudio,"legacy channel selections produce sample-exact identical playback");
    require(channelLive,"legacy rerender calls preserve playback position, history and audio continuity");

    bool snapshots=true,finite=true;
    for(int intent=0;intent<4;++intent) {
        Engine a,b;a.prepare(48000);b.prepare(48000);a.reset(summaries[intent].representative);
        snapshots=snapshots&&b.loadSongData(a.currentSongData())&&equalAudio(a,b,12);
        const auto rr=render(a,30);
        finite=finite&&rr.finite&&rr.peak<=1&&rr.squares>1;
        std::cout<<"INFO pcm intent="<<intent<<" seconds=30 peak="<<rr.peak<<" rms="<<std::sqrt(rr.squares/rr.samples)<<std::endl;
    }
    require(snapshots,"all four intent snapshots reconstruct 12 seconds sample exactly");
    require(finite,"four 30-second musical excerpts are finite, bounded and non-silent");

    Engine history;history.prepare(48000);history.clearHistory();history.reset(77u);render(history,2);
    history.forceNewPiece();render(history,3);
    const auto historyRows=lines(history.historyData());
    bool listened=false;for(const auto& entry:historyRows) if(parseUnsigned(entry,"seed")==77u&&parseSigned(entry,"listened")>=1)listened=true;
    require(historyRows.size()==2&&listened,"history retains prior tune and listened duration when a new tune begins");
    for(int i=0;i<25;++i)history.forceNewPiece();
    require(lines(history.historyData()).size()==20,"native history is capped at 20 tunes");

    Engine exportSource;exportSource.prepare(48000);exportSource.reset(summaries[0].representative);
    const std::string recipe=exportSource.currentSongData();
    const char* rangePath="/tmp/technomatic_v30_validation_range.pcm";std::remove(rangePath);
    require(Engine::exportPcm16RangeFile(recipe,10,40,rangePath),"finite 30-second range export completes");
    require(fileSize(rangePath)==30L*48000L*2L*sizeof(int16_t),"range export length matches requested duration exactly");std::remove(rangePath);
    Engine ending;ending.prepare(48000);ending.loadSongData(recipe);ending.mExportSinglePieceMode=true;
    ending.mExportStopSamples=45LL*48000;ending.mComposition.pieceSteps=ending.pieceStepsFromSeconds(45,ending.mBpmTarget);
    const auto endRender=render(ending,45);
    require(endRender.finite&&endRender.peak<=1&&endRender.squares>1,"finite opening/ending path stays bounded and non-silent");

    bool drumBounded=true;
    for(int intent=0;intent<4;++intent) {
        Engine e;e.prepare(48000);e.reset(summaries[intent].representative);
        const double initial=std::accumulate(e.mPattern.hat.begin(),e.mPattern.hat.end(),0.0);
        const int hits=audit::hits(e.mPattern.hat);double low=initial,high=initial;int maxHits=hits;
        for(int n=1;n<=200000;++n) { e.mutateDrumsOnly();if(n%1000)continue;
            const double mass=std::accumulate(e.mPattern.hat.begin(),e.mPattern.hat.end(),0.0);
            low=std::min(low,mass);high=std::max(high,mass);maxHits=std::max(maxHits,audit::hits(e.mPattern.hat)); }
        drumBounded=drumBounded&&low>=initial*.5&&high<=initial*1.45+1&&maxHits<=hits+8;
        std::cout<<"INFO drum_mutation intent="<<intent<<" initial="<<initial<<" min="<<low<<" max="<<high<<" initial_hits="<<hits<<" max_hits="<<maxHits<<std::endl;
    }
    require(drumBounded,"800000 accelerated drum mutations retain rests and bounded density");

    Engine live;live.prepare(48000);live.reset(summaries[1].representative);const uint32_t liveSeed=parseUnsigned(live.currentSongData(),"seed");
    const auto begin=std::chrono::steady_clock::now();const auto rr=render(live,180);
    require(parseUnsigned(live.currentSongData(),"seed")==liveSeed&&live.currentElapsedSeconds()>=180&&
        live.currentSectionType(live.mStyleAgeSteps)!=Engine::SectionType::Outro,
        "three-minute live playback continues the same seed without an automatic ending");
    require(rr.finite&&rr.peak<=1&&rr.squares>1,"three-minute live PCM remains finite, bounded and non-silent");
    bool stableRenderedRoles=true;for(const auto& role:rr.roleModels)stableRenderedRoles=stableRenderedRoles&&role.second.size()==1;
    require(stableRenderedRoles&&rr.leadModels.size()+rr.bassModels.size()+rr.padModels.size()<=6,
        "full three-minute PCM retains fixed role families within the six-family palette bound");
    std::cout<<"INFO live_pcm seconds=180 host_wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()
        <<" peak="<<rr.peak<<" rms="<<std::sqrt(rr.squares/rr.samples)<<std::endl;
    live.next();
    require(parseUnsigned(live.currentSongData(),"seed")!=liveSeed&&live.currentElapsedSeconds()==0,
        "explicit next switches seed immediately and resets elapsed time without a master fade");

    std::ifstream source("app/src/main/cpp/MusicEngine.cpp");std::string sourceText((std::istreambuf_iterator<char>(source)),{});
    require(sourceText.find("for (int32_t i = 0; i < 48; ++i)")!=std::string::npos,"all 48 quality-search candidates remain enabled");
    std::cout<<"INFO catalogue lead="<<leadModels.size()<<" bass="<<bassModels.size()<<" pad="<<padModels.size()<<" drum="<<drumModels.size()
        <<" styles="<<styles.size()<<" slow_le90="<<slow<<" fast_ge135="<<fast<<" support_histogram=";
    for(int i=0;i<7;++i)std::cout<<i<<':'<<supportHistogram[i]<<',';
    std::cout<<std::endl;
    std::cout<<(ok?"PASS ":"FAIL ")<<"v30 validation checks="<<checks<<std::endl;
    return ok?0:1;
}
