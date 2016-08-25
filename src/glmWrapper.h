#ifndef __GLM_WRAPPER_H_
#define __GLM_WRAPPER_H_

#include <wut.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef f32 Mtx44[4][4];

void glmIdentity(Mtx44 mtx);
void glmPerspective(Mtx44 mtx, f32 fovy, f32 aspect, f32 near, f32 far);
void glmLookAt(Mtx44 mtx, f32 eyeX, f32 eyeY, f32 eyeZ, f32 centerX, f32 centerY, f32 centerZ, f32 upX, f32 upY, f32 upZ);
void glmTranslate(Mtx44 mtx, f32 x, f32 y, f32 z);
void glmRotate(Mtx44 mtx, f32 rad, f32 x, f32 y, f32 z);
void glmScale(Mtx44 mtx, f32 x, f32 y, f32 z);
void glmMultiply(Mtx44 mtxOut, Mtx44 mtx1, Mtx44 mtx2);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __GLM_WRAPPER_H_
