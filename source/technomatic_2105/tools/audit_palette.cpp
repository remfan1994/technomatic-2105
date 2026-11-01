// Records which synthesis models the real trigger functions select for events.
// This is a scheduler/trigger simulation, not a PCM render: voices/events are
// drained per step. Reported families must not be confused with concurrency.
#define TECHNOMATIC_DEVELOPMENT_NO_MAIN
#include "audit_development.cpp"

namespace palette_audit {
struct Metrics {
    std::set<int> bass, lead, pad, drum;
    std::map<int, std::set<int>> roleModels;
    int events = 0;
    bool finiteEvents = true;
};

Metrics scheduled(rb::MusicEngine& e, int phrases) {
    Metrics m;
    for (int step = 0; step < phrases * 16; ++step) {
        e.clearVoicesAndEvents();
        e.onStep();
        const auto events = e.mEvents;
        for (const auto& event : events) {
            if (!event.active) continue;
            ++m.events;
            m.finiteEvents = m.finiteEvents && std::isfinite(event.amp) &&
                std::isfinite(event.freq) && std::isfinite(event.dur) && event.dur > 0;
            e.clearVoicesAndEvents();
            switch (event.kind) {
                case rb::MusicEngine::EventKind::Bass:
                    e.triggerBass(event.freq,event.amp,event.dur,event.pan,event.aux);
                    m.bass.insert(e.mBass[0].model);
                    m.roleModels[1].insert(e.mBass[0].model); break;
                case rb::MusicEngine::EventKind::Lead:
#ifdef TECHNOMATIC_AUDIT_V29
                    e.triggerLead(event.freq,event.amp,event.dur,event.pan,event.aux);
                    m.roleModels[0].insert(e.mLeads[0].model);
#else
                    e.triggerLead(event.freq,event.amp,event.dur,event.pan,event.aux,event.instrumentSlot);
                    m.roleModels[event.instrumentSlot].insert(e.mLeads[0].model);
#endif
                    m.lead.insert(e.mLeads[0].model); break;
                case rb::MusicEngine::EventKind::Pad:
                    e.triggerPad(event.degree,event.amp,event.dur,event.pan,event.aux);
                    m.pad.insert(e.mPads[0].model);
                    m.roleModels[2].insert(e.mPads[0].model); break;
                default:
                    m.drum.insert(static_cast<int>(event.drumType)); break;
            }
        }
    }
    return m;
}

std::string ids(const std::set<int>& values) {
    std::ostringstream out;
    bool first=true;
    for(int value:values) { if(!first) out << ':'; out << value; first=false; }
    return out.str();
}
} // namespace palette_audit

#ifndef TECHNOMATIC_PALETTE_NO_MAIN
int main(int argc,char** argv) {
    const int count=argc>1?std::max(1,std::atoi(argv[1])):256;
    const int phrases=argc>2?std::max(1,std::atoi(argv[2])):128;
    std::cout << "seed,intent,style,support_count,bass_model,lead_model,pad_model,drum_model,"
        "bass_families,lead_families,pad_families,bass_ids,lead_ids,pad_ids,events,finite_events,max_families_per_role\n";
    for(int i=1;i<=count;++i) {
        const uint32_t seed=static_cast<uint32_t>(i)*2654435761u;
        rb::MusicEngine e;e.prepare(48000);e.reset(seed);
        const auto c=e.mComposition;
        const auto m=palette_audit::scheduled(e,phrases);
        std::size_t maxRole=0;
        for (const auto& role:m.roleModels) maxRole=std::max(maxRole,role.second.size());
        std::cout << seed << ',' << e.mPattern.intent << ',' << static_cast<int>(e.mPattern.style) << ','
            << development_audit::supportCount(c) << ',' << c.bassModel << ',' << c.leadModel << ','
            << c.padModel << ',' << c.drumKit << ',' << m.bass.size() << ',' << m.lead.size() << ','
            << m.pad.size() << ',' << palette_audit::ids(m.bass) << ',' << palette_audit::ids(m.lead) << ','
            << palette_audit::ids(m.pad) << ',' << m.events << ',' << m.finiteEvents << ',' << maxRole << '\n';
    }
}
#endif
