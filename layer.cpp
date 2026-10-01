#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <windows.h>
#include <mutex>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <vector>
#include <string>

static PFN_xrGetInstanceProcAddr g_nextGIPA = nullptr;
static PFN_xrLocateViews g_nextLocateViews = nullptr;
static PFN_xrEndFrame g_nextEndFrame = nullptr;
static PFN_xrDestroyInstance g_nextDestroyInstance = nullptr;

static std::mutex g_mutex;
static std::ofstream g_log;

static uint64_t g_locateSeq = 0;
static uint64_t g_endSeq = 0;

struct ViewSnap {
    XrTime t{};
    XrSpace space{};
    std::vector<XrView> views;
};

static std::vector<ViewSnap> g_recent;


// ------------------------------------------------------------
// Logging
// ------------------------------------------------------------

static std::string LogPath()
{
    char tmp[MAX_PATH]{};

    DWORD n = GetTempPathA(MAX_PATH, tmp);

    if (n == 0 || n >= MAX_PATH)
        return "PicoOpenXRViewDiag.log";

    return std::string(tmp) + "PicoOpenXRViewDiag.log";
}


static void OpenLog()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_log.is_open())
        return;

    g_log.open(
        LogPath(),
        std::ios::out | std::ios::app
    );
}


static void Line(const std::string& s)
{
    OpenLog();

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_log) {
        g_log << s << "\n";
        g_log.flush();
    }
}


// ------------------------------------------------------------
// Formatting
// ------------------------------------------------------------

static std::string Pose(const XrPosef& p)
{
    std::ostringstream o;

    o << std::fixed << std::setprecision(6)
      << "pos("
      << p.position.x << ","
      << p.position.y << ","
      << p.position.z << ") "
      << "quat("
      << p.orientation.x << ","
      << p.orientation.y << ","
      << p.orientation.z << ","
      << p.orientation.w << ")";

    return o.str();
}


static std::string Fov(const XrFovf& f)
{
    std::ostringstream o;

    o << std::fixed << std::setprecision(6)
      << "fov("
      << "L=" << f.angleLeft
      << " R=" << f.angleRight
      << " U=" << f.angleUp
      << " D=" << f.angleDown
      << ")";

    return o.str();
}


// ------------------------------------------------------------
// xrLocateViews
// ------------------------------------------------------------

static XRAPI_ATTR XrResult XRAPI_CALL Layer_xrLocateViews(
    XrSession session,
    const XrViewLocateInfo* info,
    XrViewState* state,
    uint32_t cap,
    uint32_t* count,
    XrView* views)
{
    if (!g_nextLocateViews) {
        Line("ERROR: Layer_xrLocateViews called but next xrLocateViews is NULL");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    XrResult r =
        g_nextLocateViews(
            session,
            info,
            state,
            cap,
            count,
            views
        );

    uint64_t seq = ++g_locateSeq;

    std::ostringstream h;

    h << "LOCATE #" << seq
      << " result=" << r;

    if (info) {
        h << " displayTime=" << info->displayTime
          << " space=0x"
          << std::hex
          << reinterpret_cast<uintptr_t>(info->space)
          << std::dec
          << " viewConfig="
          << info->viewConfigurationType;
    }

    if (state) {
        h << " flags=0x"
          << std::hex
          << state->viewStateFlags
          << std::dec;
    }

    h << " count="
      << (count ? *count : 0);

    Line(h.str());


    if (XR_SUCCEEDED(r) && views && count) {

        uint32_t n =
            (*count < cap)
            ? *count
            : cap;

        for (uint32_t i = 0; i < n; ++i) {

            std::ostringstream o;

            o << "  VIEW[" << i << "] "
              << Pose(views[i].pose)
              << " "
              << Fov(views[i].fov);

            Line(o.str());
        }


        if (info) {

            std::lock_guard<std::mutex> lock(g_mutex);

            ViewSnap snap;

            snap.t = info->displayTime;
            snap.space = info->space;
            snap.views.assign(
                views,
                views + n
            );

            g_recent.push_back(
                std::move(snap)
            );

            if (g_recent.size() > 64) {
                g_recent.erase(
                    g_recent.begin(),
                    g_recent.begin() + 32
                );
            }
        }
    }

    return r;
}


// ------------------------------------------------------------
// xrEndFrame
// ------------------------------------------------------------

static XRAPI_ATTR XrResult XRAPI_CALL Layer_xrEndFrame(
    XrSession session,
    const XrFrameEndInfo* info)
{
    uint64_t seq = ++g_endSeq;

    std::ostringstream h;

    h << "END #" << seq;

    if (info) {
        h << " displayTime="
          << info->displayTime
          << " layers="
          << info->layerCount
          << " blend="
          << info->environmentBlendMode;
    }

    Line(h.str());


    if (info) {

        bool exact = false;

        {
            std::lock_guard<std::mutex> lock(g_mutex);

            for (
                auto it = g_recent.rbegin();
                it != g_recent.rend();
                ++it)
            {
                if (it->t == info->displayTime) {
                    exact = true;
                    break;
                }
            }
        }

        Line(
            std::string(
                "  LocateViews exact displayTime match: "
            ) +
            (exact ? "YES" : "NO")
        );


        for (
            uint32_t li = 0;
            li < info->layerCount;
            ++li)
        {
            const XrCompositionLayerBaseHeader* base =
                info->layers[li];

            if (!base)
                continue;


            std::ostringstream l;

            l << "  LAYER[" << li << "] type="
              << base->type;

            Line(l.str());


            if (
                base->type ==
                XR_TYPE_COMPOSITION_LAYER_PROJECTION)
            {
                const auto* projection =
                    reinterpret_cast<
                        const XrCompositionLayerProjection*
                    >(base);


                std::ostringstream q;

                q << "    PROJECTION space=0x"
                  << std::hex
                  << reinterpret_cast<uintptr_t>(
                         projection->space
                     )
                  << std::dec
                  << " viewCount="
                  << projection->viewCount;

                Line(q.str());


                for (
                    uint32_t i = 0;
                    i < projection->viewCount;
                    ++i)
                {
                    const auto& v =
                        projection->views[i];

                    std::ostringstream o;

                    o << "    PVIEW[" << i << "]"
                      << " swapchain=0x"
                      << std::hex
                      << reinterpret_cast<uintptr_t>(
                             v.subImage.swapchain
                         )
                      << std::dec
                      << " arrayIndex="
                      << v.subImage.imageArrayIndex
                      << " rect=("
                      << v.subImage.imageRect.offset.x
                      << ","
                      << v.subImage.imageRect.offset.y
                      << " "
                      << v.subImage.imageRect.extent.width
                      << "x"
                      << v.subImage.imageRect.extent.height
                      << ") "
                      << Pose(v.pose)
                      << " "
                      << Fov(v.fov);

                    Line(o.str());
                }
            }
        }
    }


    if (!g_nextEndFrame) {
        Line("ERROR: next xrEndFrame is NULL");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    return g_nextEndFrame(
        session,
        info
    );
}


// ------------------------------------------------------------
// xrDestroyInstance
// ------------------------------------------------------------

static XRAPI_ATTR XrResult XRAPI_CALL Layer_xrDestroyInstance(
    XrInstance instance)
{
    Line("xrDestroyInstance ENTER");

    if (!g_nextDestroyInstance) {
        Line("ERROR: next xrDestroyInstance is NULL");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    XrResult r =
        g_nextDestroyInstance(instance);

    {
        std::ostringstream o;
        o << "xrDestroyInstance result=" << r;
        Line(o.str());
    }

    return r;
}


// ------------------------------------------------------------
// xrGetInstanceProcAddr
// ------------------------------------------------------------

extern "C"
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* fn)
{
    if (!name || !fn)
        return XR_ERROR_VALIDATION_FAILURE;


    if (
        std::strcmp(
            name,
            "xrGetInstanceProcAddr"
        ) == 0)
    {
        *fn =
            reinterpret_cast<PFN_xrVoidFunction>(
                xrGetInstanceProcAddr
            );

        return XR_SUCCESS;
    }


    if (
        std::strcmp(
            name,
            "xrLocateViews"
        ) == 0)
    {
        *fn =
            reinterpret_cast<PFN_xrVoidFunction>(
                Layer_xrLocateViews
            );

        return XR_SUCCESS;
    }


    if (
        std::strcmp(
            name,
            "xrEndFrame"
        ) == 0)
    {
        *fn =
            reinterpret_cast<PFN_xrVoidFunction>(
                Layer_xrEndFrame
            );

        return XR_SUCCESS;
    }


    if (
        std::strcmp(
            name,
            "xrDestroyInstance"
        ) == 0)
    {
        *fn =
            reinterpret_cast<PFN_xrVoidFunction>(
                Layer_xrDestroyInstance
            );

        return XR_SUCCESS;
    }


    if (!g_nextGIPA)
        return XR_ERROR_FUNCTION_UNSUPPORTED;


    return g_nextGIPA(
        instance,
        name,
        fn
    );
}


// ------------------------------------------------------------
// xrCreateApiLayerInstance
// ------------------------------------------------------------

extern "C"
XRAPI_ATTR XrResult XRAPI_CALL xrCreateApiLayerInstance(
    const XrInstanceCreateInfo* info,
    const XrApiLayerCreateInfo* layerInfo,
    XrInstance* instance)
{
    Line(
        "xrCreateApiLayerInstance ENTER"
    );


    if (
        !info ||
        !layerInfo ||
        !layerInfo->nextInfo ||
        !instance)
    {
        Line(
            "xrCreateApiLayerInstance FAILED: "
            "invalid parameters"
        );

        return XR_ERROR_INITIALIZATION_FAILED;
    }


    const XrApiLayerNextInfo* next =
        layerInfo->nextInfo;


    if (
        !next->nextGetInstanceProcAddr ||
        !next->nextCreateApiLayerInstance)
    {
        Line(
            "xrCreateApiLayerInstance FAILED: "
            "invalid next layer pointers"
        );

        return XR_ERROR_INITIALIZATION_FAILED;
    }


    g_nextGIPA =
        next->nextGetInstanceProcAddr;


    XrApiLayerCreateInfo nextLayer =
        *layerInfo;

    nextLayer.nextInfo =
        next->next;


    XrResult r =
        next->nextCreateApiLayerInstance(
            info,
            &nextLayer,
            instance
        );


    {
        std::ostringstream o;

        o << "nextCreateApiLayerInstance result="
          << r;

        Line(o.str());
    }


    if (XR_FAILED(r)) {
        Line(
            "xrCreateApiLayerInstance FAILED downstream"
        );

        return r;
    }


    Line(
        "xrCreateApiLayerInstance downstream SUCCESS"
    );


    PFN_xrVoidFunction fn = nullptr;


    if (
        XR_SUCCEEDED(
            g_nextGIPA(
                *instance,
                "xrLocateViews",
                &fn
            )
        ))
    {
        g_nextLocateViews =
            reinterpret_cast<PFN_xrLocateViews>(fn);

        Line(
            "Resolved next xrLocateViews"
        );
    }
    else {
        Line(
            "WARNING: could not resolve xrLocateViews"
        );
    }


    fn = nullptr;

    if (
        XR_SUCCEEDED(
            g_nextGIPA(
                *instance,
                "xrEndFrame",
                &fn
            )
        ))
    {
        g_nextEndFrame =
            reinterpret_cast<PFN_xrEndFrame>(fn);

        Line(
            "Resolved next xrEndFrame"
        );
    }
    else {
        Line(
            "WARNING: could not resolve xrEndFrame"
        );
    }


    fn = nullptr;

    if (
        XR_SUCCEEDED(
            g_nextGIPA(
                *instance,
                "xrDestroyInstance",
                &fn
            )
        ))
    {
        g_nextDestroyInstance =
            reinterpret_cast<
                PFN_xrDestroyInstance
            >(fn);

        Line(
            "Resolved next xrDestroyInstance"
        );
    }
    else {
        Line(
            "WARNING: could not resolve xrDestroyInstance"
        );
    }


    Line(
        "xrCreateApiLayerInstance SUCCESS"
    );

    return r;
}


// ------------------------------------------------------------
// Loader negotiation
// ------------------------------------------------------------

extern "C"
__declspec(dllexport)
XRAPI_ATTR XrResult XRAPI_CALL
xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* request)
{
    Line(
        "xrNegotiateLoaderApiLayerInterface ENTER"
    );


    if (layerName) {
        Line(
            std::string("layerName=") +
            layerName
        );
    }
    else {
        Line(
            "layerName=NULL"
        );
    }


    if (!loaderInfo || !request) {
        Line(
            "NEGOTIATION FAILED: NULL parameters"
        );

        return XR_ERROR_INITIALIZATION_FAILED;
    }


    {
        std::ostringstream o;

        o << "loaderInfo:"
          << " structType="
          << loaderInfo->structType
          << " structVersion="
          << loaderInfo->structVersion
          << " structSize="
          << loaderInfo->structSize
          << " minInterfaceVersion="
          << loaderInfo->minInterfaceVersion
          << " maxInterfaceVersion="
          << loaderInfo->maxInterfaceVersion
          << " minApiVersion="
          << loaderInfo->minApiVersion
          << " maxApiVersion="
          << loaderInfo->maxApiVersion;

        Line(o.str());
    }


    if (
        loaderInfo->structType !=
        XR_LOADER_INTERFACE_STRUCT_LOADER_INFO)
    {
        Line(
            "NEGOTIATION FAILED: "
            "unexpected loader structType"
        );

        return XR_ERROR_INITIALIZATION_FAILED;
    }


    if (
        request->structType !=
        XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST)
    {
        Line(
            "NEGOTIATION FAILED: "
            "unexpected request structType"
        );

        return XR_ERROR_INITIALIZATION_FAILED;
    }


    if (
        loaderInfo->minInterfaceVersion >
        XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion < 1)
    {
        Line(
            "NEGOTIATION FAILED: "
            "unsupported loader interface version"
        );

        return XR_ERROR_INITIALIZATION_FAILED;
    }


    request->layerInterfaceVersion =
        XR_CURRENT_LOADER_API_LAYER_VERSION;

    request->layerApiVersion =
        XR_CURRENT_API_VERSION;

    request->getInstanceProcAddr =
        xrGetInstanceProcAddr;

    request->createApiLayerInstance =
        xrCreateApiLayerInstance;


    {
        std::ostringstream o;

        o << "NEGOTIATION SUCCESS"
          << " layerInterfaceVersion="
          << request->layerInterfaceVersion
          << " layerApiVersion="
          << request->layerApiVersion;

        Line(o.str());
    }


    return XR_SUCCESS;
}


// ------------------------------------------------------------
// DLL entry point
// ------------------------------------------------------------

BOOL APIENTRY DllMain(
    HMODULE module,
    DWORD reason,
    LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {

        DisableThreadLibraryCalls(module);

        // Do not use std::ofstream/mutex here:
        // DllMain executes under the Windows loader lock.
        OutputDebugStringA(
            "[PicoOpenXRViewDiag] DLL_PROCESS_ATTACH\n"
        );
    }

    return TRUE;
}
