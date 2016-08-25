#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glmWrapper.h"

//! this is all not performant at all and really not how it should be used
//! but to keep the example plain C the C++ math library is just wrapped to keep out C++ out of the main code

static inline void Mtx44Copy(f32 * mtxOut, f32 * mtxIn)
{
    for(int n = 0; n < 16; n++)
        mtxOut[n] = mtxIn[n];
}

extern "C" void glmIdentity(Mtx44 mtx)
{
    glm::mat4 identity(1.0f);
    Mtx44Copy(&mtx[0][0], &identity[0][0]);
}

extern "C" void glmPerspective(Mtx44 mtx, f32 fovy, f32 aspect, f32 near, f32 far)
{
    glm::mat4 projMtx = glm::perspective(fovy, aspect, near, far);
    Mtx44Copy(&mtx[0][0], &projMtx[0][0]);
}

extern "C" void glmLookAt(Mtx44 mtx, f32 eyeX, f32 eyeY, f32 eyeZ, f32 centerX, f32 centerY, f32 centerZ, f32 upX, f32 upY, f32 upZ)
{
    glm::mat4 viewMtx = glm::lookAt(glm::vec3(eyeX,eyeY,eyeZ), glm::vec3(centerX,centerY,centerZ), glm::vec3(upX,upY,upZ));
    Mtx44Copy(&mtx[0][0], &viewMtx[0][0]);
}

extern "C" void glmTranslate(Mtx44 mtx, f32 x, f32 y, f32 z)
{
    glm::mat4 glmMat;
    Mtx44Copy(&glmMat[0][0], &mtx[0][0]);
	glmMat = glm::translate(glmMat, glm::vec3(x, y, z));
    Mtx44Copy(&mtx[0][0], &glmMat[0][0]);

}

extern "C" void glmRotate(Mtx44 mtx, f32 rad, f32 x, f32 y, f32 z)
{
    glm::mat4 glmMat;
    Mtx44Copy(&glmMat[0][0], &mtx[0][0]);
	glmMat = glm::rotate(glmMat, rad, glm::vec3(x, y, z));
    Mtx44Copy(&mtx[0][0], &glmMat[0][0]);
}

extern "C" void glmScale(Mtx44 mtx, f32 x, f32 y, f32 z)
{
    glm::mat4 glmMat;
    Mtx44Copy(&glmMat[0][0], &mtx[0][0]);
	glmMat = glm::scale(glmMat, glm::vec3(x, y, z));
    Mtx44Copy(&mtx[0][0], &glmMat[0][0]);
}

extern "C" void glmMultiply(Mtx44 mtxOut, Mtx44 mtx1, Mtx44 mtx2)
{
    glm::mat4 glmMat1;
    glm::mat4 glmMat2;
    Mtx44Copy(&glmMat1[0][0], &mtx1[0][0]);
    Mtx44Copy(&glmMat2[0][0], &mtx2[0][0]);

    glmMat1 = glmMat1 * glmMat2;

    Mtx44Copy(&mtxOut[0][0], &glmMat1[0][0]);
}
