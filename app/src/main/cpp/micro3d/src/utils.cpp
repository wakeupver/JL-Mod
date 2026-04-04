#include "utils.h"
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT void JNICALL Java_ru_woesss_j2me_micro3d_Utils_fillBuffer
        (JNIEnv *env, jclass,
         jobject buffer, jobject vertices, jintArray indices) {
    auto       *dst      = static_cast<Vec3f *>(env->GetDirectBufferAddress(buffer));
    jsize        len      = env->GetArrayLength(indices);
    jint        *indexPtr = env->GetIntArrayElements(indices, nullptr);
    const auto  *src      = static_cast<Vec3f *>(env->GetDirectBufferAddress(vertices));

    for (int i = 0; i < len; ++i) {
        dst[i] = src[indexPtr[i]];
    }

    env->ReleaseIntArrayElements(indices, indexPtr, JNI_ABORT);
}

JNIEXPORT void JNICALL Java_ru_woesss_j2me_micro3d_Utils_glReadPixels
        (JNIEnv *env, jclass,
         jint x, jint y, jint width, jint height, jobject bitmap_buffer) {

    if (width <= 0 || height <= 0) return;

    int ret;
    AndroidBitmapInfo info;
    ret = AndroidBitmap_getInfo(env, bitmap_buffer, &info);
    if (ret < 0) {
        LOGE("AndroidBitmap_getInfo() failed! error=%d", ret)
        MICRO3D_RAISE_EXCEPTION(env, "java/lang/IllegalStateException",
                                "AndroidBitmap_getInfo() failed!")
        return;
    }

    void *pixels;
    ret = AndroidBitmap_lockPixels(env, bitmap_buffer, &pixels);
    if (ret < 0) {
        LOGE("AndroidBitmap_lockPixels() failed! error=%d", ret)
        MICRO3D_RAISE_EXCEPTION(env, "java/lang/IllegalStateException",
                                "AndroidBitmap_lockPixels() failed!")
        return;
    }

    const uint32_t bw = info.width;
    const uint32_t bs = info.stride;

    if (x == 0 && (uint32_t)width == bw) {
        void *dst = (uint8_t *)pixels + bs * (uint32_t)y;
        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, dst);
    } else {
        static thread_local std::vector<uint8_t> t_buf;
        const size_t rowBytes = (size_t)width * 4u;
        const size_t needed   = rowBytes * (size_t)height;
        if (t_buf.size() < needed) t_buf.resize(needed);

        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, t_buf.data());

        uint8_t *dst = (uint8_t *)pixels + (uint32_t)x * 4u + bs * (uint32_t)y;
        for (int i = 0; i < height; ++i) {
            memcpy(dst, t_buf.data() + (size_t)i * rowBytes, rowBytes);
            dst += bs;
        }
    }

    ret = AndroidBitmap_unlockPixels(env, bitmap_buffer);
    if (ret < 0) {
        LOGE("AndroidBitmap_unlockPixels() failed! error=%d", ret)
        MICRO3D_RAISE_EXCEPTION(env, "java/lang/IllegalStateException",
                                "AndroidBitmap_unlockPixels() failed!")
    }
}

JNIEXPORT void JNICALL
Java_ru_woesss_j2me_micro3d_Utils_transform(JNIEnv *env, jclass,
                                             jobject src_vertices,
                                             jobject dst_vertices,
                                             jobject src_normals,
                                             jobject dst_normals,
                                             jobject aBones,
                                             jfloatArray action_matrices) {
    auto *srcVert = static_cast<Vec3f *>(env->GetDirectBufferAddress(src_vertices));
    auto *dstVert = static_cast<Vec3f *>(env->GetDirectBufferAddress(dst_vertices));

    Vec3f *srcNorm = nullptr;
    Vec3f *dstNorm = nullptr;
    if (src_normals != nullptr) {
        srcNorm = static_cast<Vec3f *>(env->GetDirectBufferAddress(src_normals));
        dstNorm = static_cast<Vec3f *>(env->GetDirectBufferAddress(dst_normals));
    }

    auto  *bones    = static_cast<Bone *>(env->GetDirectBufferAddress(aBones));
    jsize  bonesLen = (jsize)(env->GetDirectBufferCapacity(aBones) / sizeof(Bone));

    jsize   actionsLen = 0;
    float  *actionsPtr = nullptr;
    Matrix *actions    = nullptr;
    if (action_matrices != nullptr) {
        actionsPtr = env->GetFloatArrayElements(action_matrices, nullptr);
        actionsLen = env->GetArrayLength(action_matrices) / 12;
        actions    = reinterpret_cast<Matrix *>(actionsPtr);
    }

    std::vector<Matrix> tmp(bonesLen);

    int vertIdx = 0;
    int normIdx = 0;

    for (int i = 0; i < bonesLen; ++i) {
        const Bone &bone   = bones[i];
        const int   parent = bone.parent;
        Matrix     &matrix = tmp[i];

        if (parent == -1) {
            matrix = bone.matrix;
        } else {
            matrix.multiply(&tmp[parent], const_cast<Matrix *>(&bone.matrix));
        }

        if (i < actionsLen) matrix.multiply(actions++);

        const int boneLen = bone.length;
        for (int j = 0; j < boneLen; ++j) {
            matrix.transformPoint(&dstVert[vertIdx], &srcVert[vertIdx]);
            if (srcNorm != nullptr)
                matrix.transformVector(&dstNorm[normIdx], &srcNorm[normIdx]);
            ++vertIdx;
            ++normIdx;
        }
    }

    if (action_matrices != nullptr) {
        env->ReleaseFloatArrayElements(action_matrices, actionsPtr, JNI_ABORT);
    }
}

void Matrix::multiply(Matrix *lm, Matrix *rm) {
    const float l00=lm->m00, l01=lm->m01, l02=lm->m02, l03=lm->m03;
    const float l10=lm->m10, l11=lm->m11, l12=lm->m12, l13=lm->m13;
    const float l20=lm->m20, l21=lm->m21, l22=lm->m22, l23=lm->m23;

    const float r00=rm->m00, r01=rm->m01, r02=rm->m02, r03=rm->m03;
    const float r10=rm->m10, r11=rm->m11, r12=rm->m12, r13=rm->m13;
    const float r20=rm->m20, r21=rm->m21, r22=rm->m22, r23=rm->m23;

    m00 = l00*r00 + l01*r10 + l02*r20;
    m01 = l00*r01 + l01*r11 + l02*r21;
    m02 = l00*r02 + l01*r12 + l02*r22;
    m03 = l00*r03 + l01*r13 + l02*r23 + l03;

    m10 = l10*r00 + l11*r10 + l12*r20;
    m11 = l10*r01 + l11*r11 + l12*r21;
    m12 = l10*r02 + l11*r12 + l12*r22;
    m13 = l10*r03 + l11*r13 + l12*r23 + l13;

    m20 = l20*r00 + l21*r10 + l22*r20;
    m21 = l20*r01 + l21*r11 + l22*r21;
    m22 = l20*r02 + l21*r12 + l22*r22;
    m23 = l20*r03 + l21*r13 + l22*r23 + l23;
}

void Matrix::multiply(Matrix *rm) {
    multiply(this, rm);
}

void Matrix::transformPoint(Vec3f *dst, Vec3f *src) const {
    transformVector(dst, src);
    dst->x += m03;
    dst->y += m13;
    dst->z += m23;
}

void Matrix::transformVector(Vec3f *dst, Vec3f *src) const {
    const float x = src->x, y = src->y, z = src->z;
    dst->x = x*m00 + y*m01 + z*m02;
    dst->y = x*m10 + y*m11 + z*m12;
    dst->z = x*m20 + y*m21 + z*m22;
}

#ifdef __cplusplus
}
#endif
