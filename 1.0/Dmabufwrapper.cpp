#define LOG_NDEBUG 0
#define LOG_TAG "DmabufWrapper"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <utils/Log.h>
#include <dlfcn.h>
#include <errno.h>

#include "Demux.h"
#include "dmabufmanage.h"
#include "Dmabufwrapper.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

#define DMABUF_MANAGE_NAME  "libmediahal_dmabufmanage.so"

typedef void * (*fdmabufmanage_init)(void);
typedef int (*fdmabufmanage_uninit)(const void *context);
typedef int (*fdmabufmanage_exportbyinfo)(const void *context, int type, const void *info);
typedef int (*fdmabufmanage_getbufinfo)(const void *context, int type, int fd, void *info);
typedef int (*fdmabufmanage_setfilterinfo)(const void *context, void *info);

typedef struct dmabuf_wrapper {
    void * handle;
    fdmabufmanage_init dmabufmanage_init;
    fdmabufmanage_uninit dmabufmanage_uninit;
    fdmabufmanage_exportbyinfo dmabufmanage_exportbyinfo;
    fdmabufmanage_getbufinfo dmabufmanage_getbufinfo;
    fdmabufmanage_setfilterinfo dmabufmanage_setfilterinfo;
    void * context;
} * dmabuf_wrapper_t;

static dmabuf_wrapper_t gDmabufWrapper = NULL;
static bool gDmabufWrapperInit = false;
static std::mutex mDmaBufWrapperLock;

static dmabuf_wrapper_t dmabuf_wrapper_init(void) {
    if (gDmabufWrapper || gDmabufWrapperInit)
        return gDmabufWrapper;

    std::lock_guard<std::mutex> lock(mDmaBufWrapperLock);
    if (gDmabufWrapper == NULL) {
        gDmabufWrapper = (dmabuf_wrapper_t)malloc(sizeof(*gDmabufWrapper));
        if (!gDmabufWrapper)
            return NULL;
        memset(gDmabufWrapper, 0, sizeof(*gDmabufWrapper));
        gDmabufWrapper->handle = dlopen(DMABUF_MANAGE_NAME, RTLD_NOW | RTLD_NODELETE);
        if (!gDmabufWrapper->handle) {
            ALOGE("Failed to load library: %s (%s)", DMABUF_MANAGE_NAME, dlerror());
            goto ERROR;
        }
        gDmabufWrapper->dmabufmanage_init = (fdmabufmanage_init)dlsym(gDmabufWrapper->handle,
             "dmabufmanage_init");
        if (!gDmabufWrapper->dmabufmanage_init) {
            ALOGE("Failed to load library: %s (%s)", DMABUF_MANAGE_NAME, dlerror());
            goto ERROR;
        }
        gDmabufWrapper->dmabufmanage_uninit = (fdmabufmanage_uninit)dlsym(gDmabufWrapper->handle,
            "dmabufmanage_uninit");
        if (!gDmabufWrapper->dmabufmanage_uninit) {
            ALOGE("Failed to found sym dmabufmanage_uninit");
            goto ERROR;
        }
        gDmabufWrapper->dmabufmanage_exportbyinfo = (fdmabufmanage_exportbyinfo)dlsym(gDmabufWrapper->handle,
            "dmabufmanage_exportByInfo");
        if (!gDmabufWrapper->dmabufmanage_exportbyinfo) {
            ALOGE("Failed to found sym dmabufmanage_exportByInfo");
            goto ERROR;
        }
        gDmabufWrapper->dmabufmanage_getbufinfo = (fdmabufmanage_getbufinfo)dlsym(gDmabufWrapper->handle,
            "dmabufmanage_getBufInfo");
        if (!gDmabufWrapper->dmabufmanage_getbufinfo) {
            ALOGE("Failed to found sym dmabufmanage_getBufInfo");
            goto ERROR;
        }
        gDmabufWrapper->dmabufmanage_setfilterinfo = (fdmabufmanage_setfilterinfo)dlsym(gDmabufWrapper->handle,
            "dmabufmanage_setFilterInfo");
        if (!gDmabufWrapper->dmabufmanage_setfilterinfo) {
            ALOGE("Failed to found sym dmabufmanage_setFilterInfo");
            goto ERROR;
        }
        gDmabufWrapper->context = gDmabufWrapper->dmabufmanage_init();
        if (!gDmabufWrapper->context) {
            ALOGE("Failed to init dmabufmanage");
            goto ERROR;
        }
    }
    gDmabufWrapperInit = true;
    return gDmabufWrapper;
ERROR:
    free(gDmabufWrapper);
    gDmabufWrapper = NULL;
    gDmabufWrapperInit = true;
    return NULL;
}

int dmabuf_manager_support(void) {
    int support = 0;
    dmabuf_wrapper_t wrapper = dmabuf_wrapper_init();

    if (wrapper)
        support = 1;
    ALOGI("Device support dmabuf manager %d", support);
    return support;
}

int dmabuf_wrapper_export(void *data, int64_t av_handle, int32_t token) {
    struct dmx_sec_es_data *esdata = (struct dmx_sec_es_data *)data;
    dmabuf_wrapper_t wrapper = dmabuf_wrapper_init();
    struct dmabuf_dmx_sec_es_data es;

    if (!wrapper)
        return -1;
    memset(&es, 0, sizeof(es));
    memcpy(&es, esdata, sizeof(*esdata));
    es.av_handle = av_handle;
    es.token = token;
    return wrapper->dmabufmanage_exportbyinfo(wrapper->context, DMA_BUF_TYPE_DMX_ES, (void *)&es);
}

int dmabuf_wrapper_getreadpointer(int fd) {
    dmabuf_wrapper_t wrapper = dmabuf_wrapper_init();
    struct dmabuf_dmx_sec_es_data es;

    if (!wrapper)
        return -1;
    memset(&es, 0, sizeof(es));
    if (wrapper->dmabufmanage_getbufinfo(wrapper->context, DMA_BUF_TYPE_DMX_ES, fd, (void *)&es))
        return -1;
    return es.data_end;
}

int dmabuf_wrapper_setfilterinfo(int32_t token, int fd, int32_t release) {
    dmabuf_wrapper_t wrapper = dmabuf_wrapper_init();
    struct filter_info info;

    if (!wrapper || fd < 0)
        return -1;
    info.token = token;
    info.filter_fd = fd;
    info.release = release;
    return wrapper->dmabufmanage_setfilterinfo(wrapper->context, &info);
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android