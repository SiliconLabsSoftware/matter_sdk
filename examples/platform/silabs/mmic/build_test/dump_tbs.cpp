#include "chip_tool_storage.h"
#include "matter_cert_issuer.h"
#include <cstdio>
int main(){ ChipToolStorage s; if(!loadChipToolStorage(s))return 1;
    auto i=issueNoc(s,0x000000000001B669ULL); if(!i.ok)return 2;
    FILE*f=fopen("tbs.bin","wb"); fwrite(i.tbsDer.data(),1,i.tbsDer.size(),f); fclose(f);
    FILE*n=fopen("noc.bin","wb"); fwrite(i.nocTlv.data(),1,i.nocTlv.size(),n); fclose(n);
    printf("wrote tbs.bin (%zu) noc.bin (%zu)\n", i.tbsDer.size(), i.nocTlv.size());
    return 0; }
