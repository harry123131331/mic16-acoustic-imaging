#include "xparameters.h"
#include "xparameters_ps.h"
#include "netif/xtopology.h"
struct xtopology_t xtopology[]={
    {XPAR_PS7_ETHERNET_0_BASEADDR,xemac_type_emacps,0,0,XPAR_SCUGIC_0_CPU_BASEADDR,XPAR_PS7_ETHERNET_0_INTR}
};
int xtopology_n_emacs=1;
