/* Finite offline R7 CPU replay candidate. No UDP socket, PL/HLS or live claim.
 * Keep every source, including boundary audio halos, separate in DDR.
 * Source slots become visible only after whole output-window success.
 */
static m16_enh_workspace enhancement_workspace;
static m16_cont_workspace continuous_workspace;
static m16_cont_prediction current_prediction;
static float continuous_audio[3][24][2][16778] __attribute__((aligned(64)));
typedef struct {
    u32 signature,version,committed[3],counts[3],source_ids[3][24],current[3][24],samples[3];
    u32 wire_packets[3],wire_bytes[3],wire_digest[3];
    u64 output_start[3],output_end[3],compute_ticks[3];
    int code[3];
} continuous_result;
volatile continuous_result g_mic16_continuous_result;
static unsigned wire_window;
static int source_datagram_receipt(void *context,const unsigned char *data,unsigned length){
    unsigned i;(void)context;
    if(wire_window>=3 || length>1084 || length<60)return -1;
    /* Encoding is exercised, but no network transmission is claimed here. */
    g_mic16_continuous_result.wire_packets[wire_window]++;
    g_mic16_continuous_result.wire_bytes[wire_window]+=length;
    for(i=0;i<length;i++)g_mic16_continuous_result.wire_digest[wire_window]=
        g_mic16_continuous_result.wire_digest[wire_window]*16777619u^data[i];
    return 0;
}
static int continuous_sink(void *context,const m16_cont_info *info,const double *a,const double *b,unsigned samples){
    unsigned i,c=info->window,s=info->ordinal;(void)context;
    if(c>=3 || info->stream_count>24 || samples>16778)return -1;
    if(info->source_id){
        if(s>=24)return -1;
        for(i=0;i<samples;i++){continuous_audio[c][s][0][i]=(float)a[i];continuous_audio[c][s][1][i]=(float)b[i];}
        g_mic16_continuous_result.source_ids[c][s]=info->source_id;
        g_mic16_continuous_result.current[c][s]=info->current_confirmed;
    }
    g_mic16_continuous_result.counts[c]=info->stream_count;
    g_mic16_continuous_result.samples[c]=samples;
    g_mic16_continuous_result.output_start[c]=info->output_start;
    g_mic16_continuous_result.output_end[c]=info->output_end;
    wire_window=c;
    return m16_source_encode(info,a,b,samples,source_datagram_receipt,0)<0;
}
static int collect_prediction(void *context,unsigned id,unsigned candidate,const float *features,const float *mask,
    const double *spectrum,const double *wdas,const double *enhanced,unsigned samples){
    unsigned j,n=current_prediction.count;(void)context;(void)features;(void)spectrum;(void)wdas;(void)enhanced;
    if(n>=8 || candidate>=16 || samples!=16778)return -1;
    current_prediction.ids[n]=id;
    current_prediction.directions[n][0]=observation.directions[candidate*2];
    current_prediction.directions[n][1]=observation.directions[candidate*2+1];
    for(j=0;j<67*64;j++)current_prediction.masks[n][j]=mask[j];
    current_prediction.count++;return 0;
}
static void commit_continuous_output(unsigned output){
    unsigned count=g_mic16_continuous_result.counts[output];
    if(count)Xil_DCacheFlushRange((INTPTR)continuous_audio[output],count*2u*16778u*4u);
    __asm__ volatile("dmb sy" ::: "memory");
    g_mic16_continuous_result.committed[output]=1;
    Xil_DCacheFlushRange((INTPTR)&g_mic16_continuous_result,sizeof(g_mic16_continuous_result));
}
static int advance_continuous(unsigned cycle){
    int count,code;unsigned i,previous;XTime begin,end;
    if(cycle>=3)return -1;
    if(!cycle){
        g_mic16_continuous_result.signature=0x4d434f4eu;
        g_mic16_continuous_result.version=0x20261019u;
        if(m16_cont_init(&continuous_workspace,20261019u))return -1;
    }
    XTime_GetTime(&begin);current_prediction.count=0;
    count=m16_enh_window(pcm,quiet,observation.directions,observation.valid,&event,&workspace.extended,
        &enhancement_workspace,collect_prediction,0);
    if(count<0 || (unsigned)count!=current_prediction.count)return -2;
    if(count)for(i=0;i<320;i++)current_prediction.weights[i]=enhancement_workspace.weights[i];
    previous=continuous_workspace.emitted;
    code=m16_cont_push(&continuous_workspace,pcm,&current_prediction,(u64)cycle*40960,continuous_sink,0);
    if(code>=0 && continuous_workspace.emitted>previous)commit_continuous_output(previous);
    XTime_GetTime(&end);g_mic16_continuous_result.compute_ticks[cycle]=end-begin;
    g_mic16_continuous_result.code[cycle]=code;
    if(code<0)return -3;
    printf("AV7_CONTINUOUS_INPUT cycle=%u current=%d emitted=%u NO_LIVE_UDP_OR_REALTIME_ACCEPTANCE\r\n",
        cycle,count,(unsigned)continuous_workspace.emitted);
    return 0;
}
static int finish_continuous(void){
    unsigned previous=continuous_workspace.emitted;
    int code=m16_cont_finish(&continuous_workspace,continuous_sink,0);
    if(code<0 || previous>=3)return -1;
    if(continuous_workspace.emitted>previous)commit_continuous_output(previous);
    printf("AV7_CONTINUOUS_FINISHED windows=%u samples=%lu encoder_only_NO_UDP\r\n",
        (unsigned)continuous_workspace.emitted,(unsigned long)g_mic16_continuous_result.output_end[2]);
    return 0;
}
