#ifndef _DMABUF_WRAPPER_H_
#define _DMABUF_WRAPPER_H_

#ifdef  __cplusplus
extern "C" {
#endif
using namespace std;

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

int dmabuf_manager_support(void);
int dmabuf_wrapper_export(void *data, int64_t av_handle, int32_t token);
int dmabuf_wrapper_getreadpointer(int fd);
int dmabuf_wrapper_setfilterinfo(int32_t token, int fd, int32_t release);

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android

#ifdef  __cplusplus
}
#endif

#endif
