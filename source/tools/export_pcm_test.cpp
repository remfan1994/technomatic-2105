#include <cstdio>
#include <cstdlib>
#include <string>
#include "../app/src/main/cpp/MusicEngine.h"
int main(int argc, char** argv){
  const char* out=argc>1?argv[1]:"out.pcm";
  int seconds=argc>2?std::atoi(argv[2]):30;
  unsigned seed=argc>3?static_cast<unsigned>(std::strtoul(argv[3],nullptr,0)):1379932468u;
  int trackSeconds=argc>4?std::atoi(argv[4]):seconds;
  std::string data="technomatic2105-v1;seed="+std::to_string(seed)+";seconds="+std::to_string(trackSeconds)+";edited=0;gmask=0;gblend=0";
  bool ok=rb::MusicEngine::exportPcm16File(data, seconds, out, nullptr);
  std::printf("ok=%d data=%s\n", ok?1:0, data.c_str());
}
