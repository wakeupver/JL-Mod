#include "utils.h"
#include <android/bitmap.h>
#include <vector>
#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif

static thread_local std::vector<uint8_t> t_readBuf;

static void blit(JNIEnv *env, jint x, jint y, jint width, jint height,
                 jobject bitmap_buffer,
                 unsigned int (*readPixels)(int, int, int, int, void *)) {

    if (width <= 0 || height <= 0) return;

    int ret;
    AndroidBitmapInfo info;
    ret = AndroidBitmap_getInfo(env, bitmap_buffer, &info);
    if (ret < 0) {
        LOGE("AndroidBitmap_getInfo() failed! error=%d", ret)
        GLES_UTILS_RAISE_EXCEPTION(env, "java/lang/IllegalStateException",
                                   "AndroidBitmap_getInfo() failed!")
        return;
    }

    void *pixels;
    ret = AndroidBitmap_lockPixels(env, bitmap_buffer, &pixels);
    if (ret < 0) {
        LOGE("AndroidBitmap_lockPixels() failed! error=%d", ret)
        GLES_UTILS_RAISE_EXCEPTION(env, "java/lang/IllegalStateException",
                                   "AndroidBitmap_lockPixels() failed!")
        return;
    }

    const size_t needed = (size_t)width * (size_t)height * 4u;
    if (t_readBuf.size() < needed) {
        t_readBuf.resize(needed);
    }

    uint8_t *data = t_readBuf.data();
    ret = readPixels(x, y, width, height, data);

    if (ret == 0) {
        const uint32_t bs = info.stride;
        auto *dst_row = (uint8_t *)pixels + (uint32_t)x * 4u + bs * (uint32_t)y;

        // Flip Y: OpenGL origin is bottom-left, Android bitmap is top-left.
        for (int i = height - 1; i >= 0; --i) {
            const uint8_t *src = data + (size_t)i * (size_t)width * 4u;
            uint8_t       *dst = dst_row;

            for (int j = 0; j < width; ++j) {
                const uint32_t r  = *src++;
                const uint32_t g  = *src++;
                const uint32_t b  = *src++;
                const uint32_t a  = *src++;
                const uint32_t ia = 255u - a;

                // Fast /255 via (x * 257 + 128) >> 16 — avoids integer division.
                *dst++ = (uint8_t)(((a * r  + ia * (uint32_t)dst[0]) * 257u + 32768u) >> 16u);
                *dst++ = (uint8_t)(((a * g  + ia * (uint32_t)dst[0]) * 257u + 32768u) >> 16u);
                *dst++ = (uint8_t)(((a * b  + ia * (uint32_t)dst[0]) * 257u + 32768u) >> 16u);
                *dst++ = 0xFFu;
            }

            dst_row += bs;
        }
    } else {
        LOGE("glReadPixels() failed! error=%d", ret)
    }

    // Unconditional unlock — even on readPixels failure — prevents bitmap lock leak.
    ret = AndroidBitmap_unlockPixels(env, bitmap_buffer);
    if (ret < 0) {
        LOGE("AndroidBitmap_unlockPixels() failed! error=%d", ret)
        GLES_UTILS_RAISE_EXCEPTION(env, "java/lang/IllegalStateException",
                                   "AndroidBitmap_unlockPixels() failed!")
    }
}

JNIEXPORT void JNICALL Java_ru_woesss_gles_GLESUtils_blit
        (JNIEnv *env, jclass /*clazz*/,
         jint x, jint y, jint width, jint height, jobject bitmap_buffer) {
    blit(env, x, y, width, height, bitmap_buffer, GLES1_glReadPixels);
}

JNIEXPORT void JNICALL Java_ru_woesss_gles_GLESUtils_blit2
        (JNIEnv *env, jclass /*clazz*/,
         jint x, jint y, jint width, jint height, jobject bitmap_buffer) {
    blit(env, x, y, width, height, bitmap_buffer, GLES2_glReadPixels);
}

#ifdef __cplusplus
}
#endif
