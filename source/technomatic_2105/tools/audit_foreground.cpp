// v30 scheduler audit. Foreground ownership is explicit; secondary notes and
// timbre changes cannot inflate primary-melody novelty. Events are drained at
// each sixteenth, so this is not a substitute for the separate full-PCM tests.
#define TECHNOMATIC_DEVELOPMENT_NO_MAIN
#include "audit_development.cpp"

namespace foreground_audit {
using Engine=rb::MusicEngine;
struct Metrics {
    int phrases=0,primaryPhrases=0,foregroundEvents=0,primaryEvents=0,bassEvents=0,padEvents=0;
    int specialEffects=0,wrongOwner=0,outOfScale=0,slowRangeErrors=0,unselectedOwner=0;
    int totalEvents=0,supportEvents=0,hatEvents=0;
    int minForegroundMidi=127,maxForegroundMidi=0,minPrimaryMidi=127,maxPrimaryMidi=0;
    double seconds=0,primaryDuration=0,bassDuration=0,padDuration=0;
    double foregroundDuration=0,primaryMidi=0;
    development_audit::IdentitySeries primary,foreground,bass;
    std::set<int> owners;
    std::set<std::string> recallRhythms,recallPitchClasses;
};

int modulo(int value,int modulus) {return (value%modulus+modulus)%modulus;}

bool inScale(const Engine& e,int midi) {
    for(int degree=0;degree<e.scaleLength();++degree)
        if(modulo(e.scaleDegreeToMidi(degree,0),12)==modulo(midi,12))return true;
    return false;
}

Metrics scheduled(Engine& e,int phrases,int prefix=16) {
    Metrics m;m.phrases=phrases;
    std::string primary,foreground,bass,recallRhythm,recallPitch;
    bool primaryPhrase=false;
    for(int step=0;step<(prefix+phrases)*16;++step) {
        for(auto& event:e.mEvents)event.active=false;
        e.onStep();
        if(step<prefix*16)continue;
        const auto plan=e.phrasePlanForStep(step);
        if((step&15)==0)primaryPhrase=plan.instrumentSlot==0;
        m.seconds+=e.stepDurationSeconds();
        const int root=e.currentChordRoot(step);
        const int rootMidi=e.scaleDegreeToMidi(root,0);
        std::vector<int> primaryNotes,foregroundNotes,bassNotes;
        for(const auto& event:e.mEvents) {
            if(!event.active)continue;
            ++m.totalEvents;
            if(event.kind==Engine::EventKind::Drum) {
                if(event.drumType==Engine::DrumType::Zap||event.drumType==Engine::DrumType::Noise)++m.specialEffects;
                if(event.drumType==Engine::DrumType::HatClosed||event.drumType==Engine::DrumType::HatOpen)++m.hatEvents;
                continue;
            }
            int midi=event.kind==Engine::EventKind::Pad?e.scaleDegreeToMidi(event.degree,1):
                static_cast<int>(std::lround(69+12*std::log2(std::max(1.0f,event.freq)/440.0f)));
            if(event.kind!=Engine::EventKind::Pad&&!inScale(e,midi))++m.outOfScale;
            if(event.kind==Engine::EventKind::Bass) {++m.bassEvents;m.bassDuration+=event.dur;bassNotes.push_back(midi-rootMidi);}
            if(event.kind==Engine::EventKind::Pad) {++m.padEvents;m.padDuration+=event.dur;}
            if(!event.foreground) {++m.supportEvents;continue;}
            ++m.foregroundEvents;m.foregroundDuration+=event.dur;m.owners.insert(event.instrumentSlot);
            if(event.instrumentSlot!=plan.instrumentSlot)++m.wrongOwner;
            if(event.instrumentSlot>=3&&e.supportLaneWeight(event.instrumentSlot-3)<=.02f)++m.unselectedOwner;
            if(event.kind!=Engine::EventKind::Pad) {
                m.minForegroundMidi=std::min(m.minForegroundMidi,midi);m.maxForegroundMidi=std::max(m.maxForegroundMidi,midi);
                const int lo=event.kind==Engine::EventKind::Bass?33:52;
                const int hi=event.kind==Engine::EventKind::Bass?62:78;
                if(e.mPattern.intent==0&&(midi<lo||midi>hi))++m.slowRangeErrors;
            }
            // Pad root is represented once. Actual component voicings are
            // tested through triggerPad separately, so doubling is not novelty.
            foregroundNotes.push_back(midi-rootMidi);
            if(event.instrumentSlot==0) {
                ++m.primaryEvents;m.primaryDuration+=event.dur;m.primaryMidi+=midi;
                m.minPrimaryMidi=std::min(m.minPrimaryMidi,midi);m.maxPrimaryMidi=std::max(m.maxPrimaryMidi,midi);
                primaryNotes.push_back(midi-rootMidi);
            }
        }
        auto append=[](std::string& out,std::vector<int>& notes) {
            std::sort(notes.begin(),notes.end());
            for(int n:notes)out+=std::to_string(n)+',';
            out+='|';
        };
        append(primary,primaryNotes);append(foreground,foregroundNotes);append(bass,bassNotes);
        if(plan.section==Engine::SectionType::Hook) {
            recallRhythm+=foregroundNotes.empty()?'r':'x';
            for(int note:foregroundNotes)recallPitch+=std::to_string(modulo(note,12))+',';
            recallPitch+='|';
        }
        if((step&15)==15) {
            m.foreground.add(foreground);m.bass.add(bass);
            if(primaryPhrase) {++m.primaryPhrases;m.primary.add(primary);}
            if(plan.section==Engine::SectionType::Hook) {m.recallRhythms.insert(recallRhythm);m.recallPitchClasses.insert(recallPitch);}
            primary.clear();foreground.clear();bass.clear();recallRhythm.clear();recallPitch.clear();
        }
    }
    return m;
}

void header() {
    std::cout<<"seed,intent,bpm,phrases,primary_phrases,foreground_unique,primary_unique,bass_unique,primary_top4,"
        "primary_max_run,foreground_max_run,foreground_events,primary_events,bass_events,pad_events,hat_events,"
        "seconds,primary_mean_duration,bass_mean_duration,pad_mean_duration,foreground_events_per_second,"
        "primary_mean_midi,primary_min_midi,primary_max_midi,foreground_min_midi,foreground_max_midi,"
        "special_effects,wrong_owner,unselected_owner,out_of_scale,slow_range_errors,owner_count,recall_rhythms,recall_pitch_class_phrases\n";
}
void row(const Engine& e,const Metrics& m,uint32_t seed) {
    std::cout<<seed<<','<<e.mPattern.intent<<','<<e.mBpmTarget<<','<<m.phrases<<','<<m.primaryPhrases<<','
        <<m.foreground.counts.size()<<','<<m.primary.counts.size()<<','<<m.bass.counts.size()<<','<<m.primary.top(4)<<','
        <<m.primary.maxRun<<','<<m.foreground.maxRun<<','<<m.foregroundEvents<<','<<m.primaryEvents<<','<<m.bassEvents<<','
        <<m.padEvents<<','<<m.hatEvents<<','<<m.seconds<<','<<m.primaryDuration/std::max(1,m.primaryEvents)<<','
        <<m.bassDuration/std::max(1,m.bassEvents)<<','<<m.padDuration/std::max(1,m.padEvents)<<','
        <<m.foregroundEvents/m.seconds<<','<<m.primaryMidi/std::max(1,m.primaryEvents)<<','<<m.minPrimaryMidi<<','
        <<m.maxPrimaryMidi<<','<<m.minForegroundMidi<<','<<m.maxForegroundMidi<<','<<m.specialEffects<<','
        <<m.wrongOwner<<','<<m.unselectedOwner<<','<<m.outOfScale<<','<<m.slowRangeErrors<<','<<m.owners.size()<<','
        <<m.recallRhythms.size()<<','<<m.recallPitchClasses.size()<<'\n';
}
} // namespace foreground_audit

#ifndef TECHNOMATIC_FOREGROUND_NO_MAIN
int main(int argc,char** argv) {
    const int count=argc>1?std::max(1,std::atoi(argv[1])):64;
    const int phrases=argc>2?std::max(16,std::atoi(argv[2])):256;
    foreground_audit::header();std::cout<<std::fixed<<std::setprecision(6);
    for(int i=1;i<=count;++i) {
        const uint32_t seed=static_cast<uint32_t>(i)*2654435761u;
        rb::MusicEngine e;e.prepare(48000);e.reset(seed);
        const auto m=foreground_audit::scheduled(e,phrases);
        foreground_audit::row(e,m,seed);
    }
}
#endif
