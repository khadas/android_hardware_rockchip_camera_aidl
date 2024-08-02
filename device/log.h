/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include <utils/Log.h>

#define NDEBUG_V 0

#define LOG_NDEBUG 0 //ALOGV
#define LOG_NIDEBUG 0 //ALOGI
#define LOG_NDDEBUG 0 //ALOGD


#ifndef NDEBUG_V
#undef LOGD
#define LOGD(fmt, args...) ALOGD(fmt, ##args)
#undef LOGL
#define LOGL(fmt, args...) ALOGD(fmt, ##args)
#undef LOGW
#define LOGW(fmt, args...) ALOGW(fmt, ##args)
#undef LOGH
#define LOGH(fmt, args...) ALOGD(fmt, ##args)
#undef LOGE
#define LOGE(fmt, args...) ALOGE(fmt, ##args)
#undef LOGI
#define LOGI(fmt, args...) ALOGV(fmt, ##args)
#undef LOGV
#define LOGV(fmt, args...) ALOGV(fmt, ##args)
#else
#undef LOGD
#define LOGD(fmt, args...)
#undef LOGL
#define LOGL(fmt, args...)
#undef LOGW
#define LOGW(fmt, args...)
#undef LOGH
#define LOGH(fmt, args...)
#undef LOGE
#define LOGE(fmt, args...)
#undef LOGI
#define LOGI(fmt, args...)
#undef LOGV
#define LOGV(fmt, args...)
#endif

#define NS_PER_MS (NS_PER_SEC /MS_PER_SEC)

static inline long get_time_diff_ms(struct timespec *from,
                                    struct timespec *to) {
    return (to->tv_sec - from->tv_sec) * (long)MS_PER_SEC +
           (to->tv_nsec - from->tv_nsec) / (long)NS_PER_MS;
}



namespace {

class ScopedLog {
public:
inline ScopedLog( std::string name,int line) :
  mName(name),
  line(line) {
  pthread_getname_np(pthread_self(), threadName, 256);
  clock_gettime(CLOCK_MONOTONIC_COARSE, &last_tm);
  //LOGD("ENTER-%s:%d",  mName.c_str(),line);
}

inline ~ScopedLog() {
  clock_gettime(CLOCK_MONOTONIC_COARSE, &curr_tm);
  long  duration = get_time_diff_ms(&last_tm,&curr_tm);
  if (duration)
  {
    LOGD("EXIT-%s:%d use %ldms",  mName.c_str(),line, duration);
  }
}

private:

    std::string mName;
    int line;
    char threadName[256];
    struct timespec last_tm;
    struct timespec curr_tm;
};

#define HAL_TRACE_NAME( name) ScopedLog __tracer( name,__LINE__ )
#define HAL_TRACE_CALL() HAL_TRACE_NAME( __FUNCTION__)
#define HAL_TRACE_CALL_PRETTY() HAL_TRACE_NAME( __PRETTY_FUNCTION__)
#define HAL_TRACE_FUNC( name) ScopedLog __tracerF( (std::string(name)+__FUNCTION__), __LINE__ )
#define HAL_TRACE_FUNC_PRETTY( name) ScopedLog __tracerF( (std::string(name)+__PRETTY_FUNCTION__), __LINE__ )

static inline void LOG_FRAME_TIME(std::string  cameraId,int32_t frameNumber,const char* name,int lineNumber,struct timespec *timestamp) {
  struct timespec current_tm;
  clock_gettime(CLOCK_MONOTONIC_COARSE, &current_tm);
  long duration = get_time_diff_ms(timestamp,&current_tm);
  if (duration)
  {
    LOGI("cameraId:(%s)REQ:(%d)[%s][%d] use %ldms",cameraId.c_str(), frameNumber, name, lineNumber, duration);
  }
}
#define LOG_FRAME(cameraId,frameNumber,timestamp) LOG_FRAME_TIME(cameraId,frameNumber,__FUNCTION__,__LINE__,timestamp)
#define LOG_FRAME_PRETTY(cameraId,frameNumber,timestamp) LOG_FRAME_TIME(cameraId,frameNumber,__PRETTY_FUNCTION__,__LINE__,timestamp)
}

