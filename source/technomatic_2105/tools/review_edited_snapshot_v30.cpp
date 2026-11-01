#include "MusicEngine.h"
// Original two-case review diagnostic, retained for quick reproduction.
// The complete assertion gate is edited_snapshot_v30.cpp, run by
// python3 tools/test_edited_snapshots.py; this diagnostic has no pass count.
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
std::string field(const std::string& s,const std::string& key) {
    auto p=s.find(";"+key+"="); if(p==std::string::npos) return "<missing>";
    p+=key.size()+2; auto e=s.find(';',p); return s.substr(p,e==std::string::npos?e:e-p);
}
int main() {
    for (const auto& recipe : {
      "technomatic2105-v1;seed=141414419;seconds=180;edited=1;gmask=4095;gblend=1;gprimary=6;gmode=6;cand=17;root=43;scale=2;tempo=88;brightness=80;lead=70;bass=65;chords=60;arp=90;counter=75;motif=70;drama=65",
      "technomatic2105-v1;seed=987654321;seconds=180;edited=1;gmask=0;gblend=0;gprimary=0;gmode=0;cand=7;root=36;scale=6;tempo=104;lead=0;bass=100;brightness=25;diversity=90"
    }) {
        rb::MusicEngine a,b; a.prepare(48000); b.prepare(48000);
        a.loadSongData(recipe); auto sa=a.currentSongData();
        b.loadSongData(sa); auto sb=b.currentSongData();
        std::cout << "input seed=" << field(sa,"seed") << " engine=" << field(sa,"engine") << '\n';
        for(auto key : {"style","root","scale","tempo","brightness","lead","bass","chords","arp","counter","motif","drama","planseed","leadmodel","bassmodel","support0","support1"})
            if(field(sa,key)!=field(sb,key)) std::cout << key << ' ' << field(sa,key) << " -> " << field(sb,key) << '\n';
        std::vector<float> pa(48000*2),pb(48000*2);
        a.render(pa.data(),48000,2); b.render(pb.data(),48000,2);
        size_t diff=0; double squared=0;
        for(size_t i=0;i<pa.size();i++) { if(pa[i]!=pb[i])diff++; double d=double(pa[i])-pb[i]; squared+=d*d; }
        std::cout << "PCM differing samples=" << diff << "/" << pa.size() << " mse=" << squared/pa.size() << '\n';
    }
}
