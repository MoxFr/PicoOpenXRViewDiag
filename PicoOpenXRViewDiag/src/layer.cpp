#include <openxr/openxr.h>
#include <openxr_loader_negotiation.h>
#include <windows.h>
#include <mutex>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <unordered_map>
#include <vector>

static PFN_xrGetInstanceProcAddr g_nextGIPA = nullptr;
static PFN_xrLocateViews g_nextLocateViews = nullptr;
static PFN_xrEndFrame g_nextEndFrame = nullptr;
static PFN_xrDestroyInstance g_nextDestroyInstance = nullptr;
static std::mutex g_mutex;
static std::ofstream g_log;
static uint64_t g_locateSeq = 0, g_endSeq = 0;

struct ViewSnap { XrTime t{}; XrSpace space{}; std::vector<XrView> views; };
static std::vector<ViewSnap> g_recent;

static void OpenLog() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_log.is_open()) return;
    char tmp[MAX_PATH]{}; GetTempPathA(MAX_PATH, tmp);
    std::string p = std::string(tmp) + "PicoOpenXRViewDiag.log";
    g_log.open(p, std::ios::out | std::ios::app);
    if (g_log) g_log << "\n=== PicoOpenXRViewDiag loaded PID=" << GetCurrentProcessId() << " ===\n";
}
static void Line(const std::string& s) { OpenLog(); std::lock_guard<std::mutex> lock(g_mutex); if(g_log){g_log<<s<<"\n"; g_log.flush();} }
static std::string Pose(const XrPosef& p){ std::ostringstream o; o<<std::fixed<<std::setprecision(6)<<"pos("<<p.position.x<<","<<p.position.y<<","<<p.position.z<<") quat("<<p.orientation.x<<","<<p.orientation.y<<","<<p.orientation.z<<","<<p.orientation.w<<")"; return o.str(); }
static std::string Fov(const XrFovf& f){ std::ostringstream o; o<<std::fixed<<std::setprecision(6)<<"fov(L="<<f.angleLeft<<" R="<<f.angleRight<<" U="<<f.angleUp<<" D="<<f.angleDown<<")"; return o.str(); }

static XRAPI_ATTR XrResult XRAPI_CALL Layer_xrLocateViews(XrSession session,const XrViewLocateInfo* info,XrViewState* state,uint32_t cap,uint32_t* count,XrView* views){
    XrResult r=g_nextLocateViews(session,info,state,cap,count,views);
    uint64_t seq=++g_locateSeq;
    std::ostringstream h; h<<"LOCATE #"<<seq<<" result="<<r;
    if(info) h<<" displayTime="<<info->displayTime<<" space=0x"<<std::hex<<(uintptr_t)info->space<<std::dec<<" viewConfig="<<info->viewConfigurationType;
    if(state) h<<" flags=0x"<<std::hex<<state->viewStateFlags<<std::dec;
    h<<" count="<<(count?*count:0); Line(h.str());
    if(XR_SUCCEEDED(r)&&views&&count){ uint32_t n=(*count<cap?*count:cap); for(uint32_t i=0;i<n;i++){ std::ostringstream o;o<<"  VIEW["<<i<<"] "<<Pose(views[i].pose)<<" "<<Fov(views[i].fov);Line(o.str()); }
      if(info){ std::lock_guard<std::mutex> lock(g_mutex); ViewSnap s; s.t=info->displayTime;s.space=info->space;s.views.assign(views,views+n);g_recent.push_back(std::move(s)); if(g_recent.size()>64)g_recent.erase(g_recent.begin(),g_recent.begin()+32); }
    }
    return r;
}
static XRAPI_ATTR XrResult XRAPI_CALL Layer_xrEndFrame(XrSession session,const XrFrameEndInfo* info){
    uint64_t seq=++g_endSeq; std::ostringstream h;h<<"END #"<<seq; if(info)h<<" displayTime="<<info->displayTime<<" layers="<<info->layerCount<<" blend="<<info->environmentBlendMode;Line(h.str());
    if(info){ bool exact=false; {std::lock_guard<std::mutex> lock(g_mutex);for(auto it=g_recent.rbegin();it!=g_recent.rend();++it)if(it->t==info->displayTime){exact=true;break;}} Line(std::string("  LocateViews exact displayTime match: ")+(exact?"YES":"NO"));
      for(uint32_t li=0;li<info->layerCount;li++){ auto base=info->layers[li]; if(!base)continue; std::ostringstream l;l<<"  LAYER["<<li<<"] type="<<base->type;Line(l.str()); if(base->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION){ auto p=reinterpret_cast<const XrCompositionLayerProjection*>(base); std::ostringstream q;q<<"    PROJECTION space=0x"<<std::hex<<(uintptr_t)p->space<<std::dec<<" viewCount="<<p->viewCount;Line(q.str()); for(uint32_t i=0;i<p->viewCount;i++){auto&v=p->views[i];std::ostringstream o;o<<"    PVIEW["<<i<<"] swapchain=0x"<<std::hex<<(uintptr_t)v.subImage.swapchain<<std::dec<<" arrayIndex="<<v.subImage.imageArrayIndex<<" rect=("<<v.subImage.imageRect.offset.x<<","<<v.subImage.imageRect.offset.y<<" "<<v.subImage.imageRect.extent.width<<"x"<<v.subImage.imageRect.extent.height<<") "<<Pose(v.pose)<<" "<<Fov(v.fov);Line(o.str());}}
      }
    }
    return g_nextEndFrame(session,info);
}
static XRAPI_ATTR XrResult XRAPI_CALL Layer_xrDestroyInstance(XrInstance instance){ XrResult r=g_nextDestroyInstance?g_nextDestroyInstance(instance):XR_SUCCESS; Line("=== instance destroyed ==="); return r; }

extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance instance,const char* name,PFN_xrVoidFunction* fn){
    if(!name||!fn)return XR_ERROR_VALIDATION_FAILURE;
    if(strcmp(name,"xrGetInstanceProcAddr")==0){*fn=(PFN_xrVoidFunction)xrGetInstanceProcAddr;return XR_SUCCESS;}
    if(strcmp(name,"xrLocateViews")==0){*fn=(PFN_xrVoidFunction)Layer_xrLocateViews;return XR_SUCCESS;}
    if(strcmp(name,"xrEndFrame")==0){*fn=(PFN_xrVoidFunction)Layer_xrEndFrame;return XR_SUCCESS;}
    if(strcmp(name,"xrDestroyInstance")==0){*fn=(PFN_xrVoidFunction)Layer_xrDestroyInstance;return XR_SUCCESS;}
    return g_nextGIPA?g_nextGIPA(instance,name,fn):XR_ERROR_FUNCTION_UNSUPPORTED;
}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrCreateApiLayerInstance(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layerInfo,XrInstance* instance){
    if(!info||!layerInfo||!layerInfo->nextInfo||!instance)return XR_ERROR_INITIALIZATION_FAILED;
    const XrApiLayerNextInfo* next=layerInfo->nextInfo; g_nextGIPA=next->nextGetInstanceProcAddr;
    XrApiLayerCreateInfo nextLayer=*layerInfo; nextLayer.nextInfo=next->next;
    XrResult r=next->nextCreateApiLayerInstance(info,&nextLayer,instance); if(XR_FAILED(r))return r;
    g_nextGIPA(*instance,"xrLocateViews",(PFN_xrVoidFunction*)&g_nextLocateViews); g_nextGIPA(*instance,"xrEndFrame",(PFN_xrVoidFunction*)&g_nextEndFrame); g_nextGIPA(*instance,"xrDestroyInstance",(PFN_xrVoidFunction*)&g_nextDestroyInstance);
    OpenLog(); Line("xrCreateApiLayerInstance OK"); return r;
}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loaderInfo,const char* layerName,XrNegotiateApiLayerRequest* request){
    if(!loaderInfo||!request)return XR_ERROR_INITIALIZATION_FAILED;
    if(loaderInfo->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||request->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST)return XR_ERROR_INITIALIZATION_FAILED;
    if(loaderInfo->minInterfaceVersion>XR_CURRENT_LOADER_API_LAYER_VERSION||loaderInfo->maxInterfaceVersion<1)return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion=XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion=XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr=xrGetInstanceProcAddr;
    request->createApiLayerInstance=xrCreateApiLayerInstance;
    return XR_SUCCESS;
}
BOOL APIENTRY DllMain(HMODULE,DWORD,LPVOID){ return TRUE; }
