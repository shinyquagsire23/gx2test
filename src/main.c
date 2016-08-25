#include <coreinit/core.h>
#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <coreinit/foreground.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>

#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/swap.h>
#include <gx2/clear.h>
#include <gx2/state.h>
#include <gx2/texture.h>
#include <gx2/display.h>
#include <gx2/context.h>
#include <gx2/shaders.h>
#include <gx2/registers.h>

#include <vpad/input.h>

#include "memory.h"
#include "model.h"
#include "shader.h"
#include "glmWrapper.h"

bool isAppRunning = true;

#define GX2_INVALIDATE_ATTRIBUTE_BUFFER                 0x00000001
#define GX2_INVALIDATE_TEXTURE                          0x00000002
#define GX2_INVALIDATE_UNIFORM_BLOCK                    0x00000004
#define GX2_INVALIDATE_SHADER                           0x00000008
#define GX2_INVALIDATE_COLOR_BUFFER                     0x00000010
#define GX2_INVALIDATE_DEPTH_BUFFER                     0x00000020
#define GX2_INVALIDATE_CPU                              0x00000040
#define GX2_INVALIDATE_STREAM_OUT_BUFFER                0x00000080
#define GX2_INVALIDATE_EXPORT_BUFFER                    0x00000100

#define ATTRIBUTE_COUNT                 2
#define DegToRad(a)   ( (a) *  0.01745329252f )
#define RadToDeg(a)   ( (a) * 57.29577951f )

static GX2VertexShader *vertexShader = NULL;
static GX2PixelShader *pixelShader = NULL;
static GX2FetchShader *fetchShader = NULL;
static GX2AttribStream *attributes = NULL;
void *fetchShaderProgramm;

unsigned char *gx2CommandBuffer;
unsigned char *tvScanBuffer;
unsigned char *drcScanBuffer;
GX2ContextState *tvContextState;
GX2ContextState *drcContextState;
GX2ColorBuffer tvColorBuffer;
GX2DepthBuffer tvDepthBuffer;
GX2ColorBuffer drcColorBuffer;
GX2DepthBuffer drcDepthBuffer;

static f32 * cubePosVtx = NULL;
static f32 * cubeColorBuf = NULL;

static Mtx44 projectionMtx;
static Mtx44 viewMtx;

static f32 degreeX = 0.0f;
static f32 degreeY = 0.0f;
static f32 degreeZ = 0.0f;
static bool manualControl = false;

bool initialized = false;

//TODO: Add these to wut
void (*GX2Flush)(void);
void (*GX2Invalidate)(int buf_type, void *addr, int length);
void (*GX2CalcColorBufferAuxInfo)(GX2ColorBuffer *colorBuffer, u32 *size, u32 *align);

static inline void GX2InitAttribStream(GX2AttribStream* attr, u32 location, u32 buffer, u32 offset, s32 format)
{
    attr->location = location;
    attr->buffer = buffer;
    attr->offset = offset;
    attr->format = format;
    attr->type = 0;
    attr->aluDivisor = 0;
    attr->mask = 0x00010205;//attribute_dest_comp_selector[format & 0xff]; //TODO
    attr->endianSwap  = GX2_ENDIAN_SWAP_DEFAULT;
}

bool scene_setup = false;
static void setupScene(void)
{
    if(scene_setup) return;
    
    //! all GX2 variables have to reside inside valid memory range and can not be inside our own setup range (0x00800000 - 0x01000000)
    //! therefore we move all static data from the data section related to GX2 to heap memory

    //!----------------------------------------------------------------------------------------------------------
    //! setup vertex shader
    //!----------------------------------------------------------------------------------------------------------
    vertexShader = (GX2VertexShader*) memalign(0x40, sizeof(GX2VertexShader));
    memset(vertexShader, 0, sizeof(GX2VertexShader));
    vertexShader->mode = GX2_SHADER_MODE_UNIFORM_REGISTER;
    vertexShader->size = sizeof(ucVertexProgram);
    vertexShader->program = memalign(0x100, vertexShader->size);
    memcpy(vertexShader->program, ucVertexProgram, vertexShader->size);
    GX2Invalidate(GX2_INVALIDATE_CPU | GX2_INVALIDATE_SHADER, vertexShader->program, vertexShader->size);
    memcpy(&vertexShader->regs, cuVertexProgramRegs, sizeof(cuVertexProgramRegs));

    vertexShader->uniformVarCount = 1;
    vertexShader->uniformVars = (GX2UniformVar*) malloc(vertexShader->uniformVarCount * sizeof(GX2UniformVar));
    vertexShader->uniformVars[0].name = "MVP";
    vertexShader->uniformVars[0].type = GX2_SHADER_VAR_TYPE_MATRIX4X4 ;
    vertexShader->uniformVars[0].count = 1;
    vertexShader->uniformVars[0].offset = 0;
    vertexShader->uniformVars[0].block = -1;

    vertexShader->attribVarCount = ATTRIBUTE_COUNT;
    vertexShader->attribVars = (GX2AttribVar*) malloc(vertexShader->attribVarCount * sizeof(GX2AttribVar));
    vertexShader->attribVars[0].name = "vertexColor";
    vertexShader->attribVars[0].type = GX2_SHADER_VAR_TYPE_FLOAT3;
    vertexShader->attribVars[0].count = 0;
    vertexShader->attribVars[0].location = 1;
    vertexShader->attribVars[1].name = "vertexPosition_modelspace";
    vertexShader->attribVars[1].type = GX2_SHADER_VAR_TYPE_FLOAT3;
    vertexShader->attribVars[1].count = 0;
    vertexShader->attribVars[1].location = 0;

    //!----------------------------------------------------------------------------------------------------------
    //! setup pixel shader
    //!----------------------------------------------------------------------------------------------------------
    pixelShader = (GX2PixelShader*) memalign(0x40, sizeof(GX2PixelShader));
    memset(pixelShader, 0, sizeof(GX2PixelShader));
    pixelShader->mode = GX2_SHADER_MODE_UNIFORM_REGISTER;
    pixelShader->size = sizeof(ucPixelProgram);
    pixelShader->program = memalign(0x100, pixelShader->size);
    memcpy(pixelShader->program, ucPixelProgram, pixelShader->size);
    GX2Invalidate(GX2_INVALIDATE_CPU | GX2_INVALIDATE_SHADER, pixelShader->program, pixelShader->size);
    memcpy(&pixelShader->regs, ucPixelProgrammRegs, sizeof(ucPixelProgrammRegs));

    //!----------------------------------------------------------------------------------------------------------
    //! setup attributes
    //!----------------------------------------------------------------------------------------------------------
    attributes = (GX2AttribStream*) malloc(sizeof(GX2AttribStream) * ATTRIBUTE_COUNT);
    GX2InitAttribStream(&attributes[0], vertexShader->attribVars[1].location, 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
    GX2InitAttribStream(&attributes[1], vertexShader->attribVars[0].location, 1, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32);

    //!----------------------------------------------------------------------------------------------------------
    //! setup fetch shader for the attributes
    //!----------------------------------------------------------------------------------------------------------
    u32 shaderSize = GX2CalcFetchShaderSizeEx(ATTRIBUTE_COUNT, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    fetchShaderProgramm = memalign(0x100, shaderSize);
    fetchShader = (GX2FetchShader *) malloc(sizeof(GX2FetchShader));
    GX2InitFetchShaderEx(fetchShader, fetchShaderProgramm, ATTRIBUTE_COUNT, attributes, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_CPU | GX2_INVALIDATE_SHADER, fetchShaderProgramm, shaderSize);

    //!----------------------------------------------------------------------------------------------------------
    //! move cube vertex and color buffer to heap
    //!----------------------------------------------------------------------------------------------------------
    cubePosVtx = (f32 *)memalign(0x40, sizeof(g_vertex_buffer_data));
    memcpy(cubePosVtx, g_vertex_buffer_data, sizeof(g_vertex_buffer_data));
    GX2Invalidate(GX2_INVALIDATE_CPU | GX2_INVALIDATE_ATTRIBUTE_BUFFER, cubePosVtx, sizeof(g_vertex_buffer_data));

    cubeColorBuf = (f32 *)memalign(0x40, sizeof(g_color_buffer_data));
    memcpy(cubeColorBuf, g_color_buffer_data, sizeof(g_color_buffer_data));
    GX2Invalidate(GX2_INVALIDATE_CPU | GX2_INVALIDATE_ATTRIBUTE_BUFFER, cubeColorBuf, sizeof(g_color_buffer_data));
    
    scene_setup = true;
}

void takedownScene()
{
    if(!scene_setup) return;
    
    free(vertexShader->attribVars);
    free(vertexShader->uniformVars);
    free(vertexShader->program);
    free(vertexShader);
    
    free(pixelShader->program);
    free(pixelShader);
    
    free(attributes);
    
    free(fetchShaderProgramm);
    free(fetchShader);
    
    free(cubePosVtx);
    free(cubeColorBuf);
    
    scene_setup = false;
}

static void prepareRendering(GX2ColorBuffer * currColorBuffer, GX2DepthBuffer * currDepthBuffer, GX2ContextState * currContextState)
{
    GX2ClearColor(currColorBuffer, 0.0f, 0.0f, 0.4f, 1.0f);
    GX2ClearDepthStencilEx(currDepthBuffer, currDepthBuffer->depthClear, currDepthBuffer->stencilClear, GX2_CLEAR_FLAGS_DEPTH | GX2_CLEAR_FLAGS_STENCIL);

    GX2SetContextState(currContextState);
    GX2SetViewport(0.0f, 0.0f, currColorBuffer->surface.width, currColorBuffer->surface.height, 0.0f, 1.0f);
    GX2SetScissor(0, 0, currColorBuffer->surface.width, currColorBuffer->surface.height);

    GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LESS);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD, TRUE, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD);

    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
}

static void renderScene(void)
{
    GX2SetFetchShader(fetchShader);
    GX2SetVertexShader(vertexShader);
    GX2SetPixelShader(pixelShader);

    GX2SetAttribBuffer(0, sizeof(g_vertex_buffer_data), sizeof(f32) * 3, cubePosVtx);
    GX2SetAttribBuffer(1, sizeof(g_color_buffer_data), sizeof(f32) * 3, cubeColorBuf);

    Mtx44 modelMtx;
	glmIdentity(modelMtx);
	glmRotate(modelMtx, DegToRad(degreeX), 1.0f, 0.0f, 0.0f);
    glmRotate(modelMtx, DegToRad(degreeY), 0.0f, 1.0f, 0.0f);
	glmRotate(modelMtx, DegToRad(degreeZ), 0.0f, 0.0f, 1.0f);

    if(!manualControl)
    {
        degreeX += 0.5f;
        degreeY += 0.8f;
        degreeZ += 0.4f;
    }

    Mtx44 mtxMVP;
	glmMultiply(mtxMVP, projectionMtx, viewMtx);
	glmMultiply(mtxMVP, mtxMVP, modelMtx);

    GX2SetVertexUniformReg(vertexShader->uniformVars[0].offset, 16, &mtxMVP[0][0]);

    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, vtxCount, 0, 1);
}

static inline void GX2InitDepthBuffer(GX2DepthBuffer *depthBuffer, s32 dim, u32 width, u32 height, u32 depth, s32 format, s32 aa)
{
    depthBuffer->surface.dim = dim;
    depthBuffer->surface.width = width;
    depthBuffer->surface.height = height;
    depthBuffer->surface.depth = depth;
    depthBuffer->surface.mipLevels = 1;
    depthBuffer->surface.format = format;
    depthBuffer->surface.aa = aa;
    depthBuffer->surface.use = ((format==GX2_SURFACE_FORMAT_UNORM_R24_X8) || (format==GX2_SURFACE_FORMAT_FLOAT_D24_S8)) ? GX2_SURFACE_USE_DEPTH_BUFFER : (GX2_SURFACE_USE_DEPTH_BUFFER | GX2_SURFACE_USE_TEXTURE);
    depthBuffer->surface.tileMode = GX2_TILE_MODE_DEFAULT;
    depthBuffer->surface.swizzle  = 0;
    depthBuffer->viewMip = 0;
    depthBuffer->viewFirstSlice = 0;
    depthBuffer->viewNumSlices = depth;
    depthBuffer->depthClear = 1.0f;
    depthBuffer->stencilClear = 0;
    depthBuffer->hiZPtr = NULL;
    depthBuffer->hiZSize = 0;
    GX2CalcSurfaceSizeAndAlignment(&depthBuffer->surface);
    GX2InitDepthBufferRegs(depthBuffer);
}

static inline void GX2InitColorBuffer(GX2ColorBuffer *colorBuffer, s32 dim, u32 width, u32 height, u32 depth, s32 format, s32 aa)
{
    colorBuffer->surface.dim = dim;
    colorBuffer->surface.width = width;
    colorBuffer->surface.height = height;
    colorBuffer->surface.depth = depth;
    colorBuffer->surface.mipLevels = 1;
    colorBuffer->surface.format = format;
    colorBuffer->surface.aa = aa;
    colorBuffer->surface.use = GX2_SURFACE_USE_COLOR_BUFFER | 0x80000000;
    colorBuffer->surface.imageSize = 0;
    colorBuffer->surface.image = NULL;
    colorBuffer->surface.mipmapSize = 0;
    colorBuffer->surface.mipmaps = NULL;
    colorBuffer->surface.tileMode = GX2_TILE_MODE_DEFAULT;
    colorBuffer->surface.swizzle = 0;
    colorBuffer->surface.alignment = 0;
    colorBuffer->surface.pitch = 0;
    u32 i;
    for(i = 0; i < 13; i++)
        colorBuffer->surface.mipLevelOffset[i] = 0;
    colorBuffer->viewMip = 0;
    colorBuffer->viewFirstSlice = 0;
    colorBuffer->viewNumSlices = depth;
    colorBuffer->aaBuffer = NULL;
    colorBuffer->aaSize = 0;
    for(i = 0; i < 5; i++)
        colorBuffer->regs[i] = 0;

    GX2CalcSurfaceSizeAndAlignment(&colorBuffer->surface);
    GX2InitColorBufferRegs(colorBuffer);
}

static inline void GX2InitTexture(GX2Texture *tex, u32 width, u32 height, u32 depth, u32 mipLevels, s32 format, s32 dim, s32 tile)
{
    tex->surface.dim = dim;
    tex->surface.width = width;
    tex->surface.height = height;
    tex->surface.depth = depth;
    tex->surface.mipLevels = mipLevels;
    tex->surface.format = format;
    tex->surface.aa = GX2_AA_MODE1X;
    tex->surface.use = GX2_SURFACE_USE_TEXTURE;
    tex->surface.imageSize = 0;
    tex->surface.image = NULL;
    tex->surface.mipmapSize = 0;
    tex->surface.mipmaps = NULL;
    tex->surface.tileMode = tile;
    tex->surface.swizzle = 0;
    tex->surface.alignment = 0;
    tex->surface.pitch = 0;
    u32 i;
    for(i = 0; i < 13; i++)
        tex->surface.mipLevelOffset[i] = 0;
    tex->viewFirstMip = 0;
    tex->viewNumMips = mipLevels;
    tex->viewFirstSlice = 0;
    tex->viewNumSlices = depth;
    tex->compMap = 0x00010203;//texture_comp_selector[format & 0x3f]; //TODO
    for(i = 0; i < 5; i++)
        tex->regs[i] = 0;

    GX2CalcSurfaceSizeAndAlignment(&tex->surface);
    GX2InitTextureRegs(tex);
}

bool mem1_freed = false;
void free_MEM1_buffers()
{
    if(mem1_freed) return;
    
    MEM2_free(gx2CommandBuffer);
    MEM2_free(tvContextState);
    MEM2_free(drcContextState);
    
    if(tvColorBuffer.surface.aa)
        MEM2_free(tvColorBuffer.aaBuffer);

    if(drcColorBuffer.surface.aa)
        MEM2_free(drcColorBuffer.aaBuffer);

    MEMBucket_free(tvScanBuffer);
    MEMBucket_free(drcScanBuffer);
    
    MEM1_free(tvColorBuffer.surface.image);
    MEM1_free(tvDepthBuffer.surface.image);
    MEM1_free(tvDepthBuffer.hiZPtr);
    MEM1_free(drcColorBuffer.surface.image);
    MEM1_free(drcDepthBuffer.surface.image);
    MEM1_free(drcDepthBuffer.hiZPtr);
    mem1_freed = true;
    
    memoryRelease();
}

bool gx2_killed = false;
void kill_GX2()
{
    if(gx2_killed) return;
    
    GX2SetTVEnable(FALSE);
    GX2SetDRCEnable(FALSE);
    GX2WaitForVsync();
    GX2DrawDone();
    GX2Shutdown();
    gx2_killed = true;
}

void
SaveCallback()
{
   OSSavesDone_ReadyToRelease(); // Required
}

bool
AppRunning()
{
   if(!OSIsMainCore())
   {
      ProcUISubProcessMessages(true);
   }
   else
   {
      ProcUIStatus status = ProcUIProcessMessages(true);
    
      if(status == PROCUI_STATUS_EXITING)
      {
          // Being closed, deinit, free, and prepare to exit
          kill_GX2();
          takedownScene();
          free_MEM1_buffers();
          
          initialized = false;
          isAppRunning = false;
          
          ProcUIShutdown();
      }
      else if(status == PROCUI_STATUS_RELEASE_FOREGROUND)
      {
          // Free up MEM1 to next foreground app, deinit screen, etc.
          kill_GX2();
          takedownScene();
          free_MEM1_buffers();
          initialized = false;
          
          ProcUIDrawDoneRelease();
      }
      else if(status == PROCUI_STATUS_IN_FOREGROUND)
      {
         // Executed while app is in foreground
         if(!initialized)
         {
            memoryInitialize();
         
            //! allocate MEM2 command buffer memory
            gx2CommandBuffer = MEM2_alloc(0x400000, 0x40);

            //! initialize GX2 command buffer
            //TODO: wut needs defines for these transferred from Decaf
            u32 gx2_init_attributes[9];
            gx2_init_attributes[0] = 1; //CommandBufferPoolBase
            gx2_init_attributes[1] = (u32)gx2CommandBuffer;
            gx2_init_attributes[2] = 2; //CommandBufferPoolSize
            gx2_init_attributes[3] = 0x400000;
            gx2_init_attributes[4] = 7; //ArgC
            gx2_init_attributes[5] = 0;
            gx2_init_attributes[6] = 8; //ArgV
            gx2_init_attributes[7] = 0;
            gx2_init_attributes[8] = 0; //end
            GX2Init(gx2_init_attributes);
            
            //! allocate memory and setup context state TV
            tvContextState = (GX2ContextState*)MEM2_alloc(sizeof(GX2ContextState), 0x100);
            GX2SetupContextStateEx(tvContextState, TRUE);

            //! allocate memory and setup context state DRC
            drcContextState = (GX2ContextState*)MEM2_alloc(sizeof(GX2ContextState), 0x100);
            GX2SetupContextStateEx(drcContextState, TRUE);
            
            GX2SetDepthStencilControl(TRUE, TRUE, 3, FALSE, FALSE, 0,0,0,0,0,0,0,0);

            u32 scanBufferSize = 0;
            s32 scaleNeeded = 0;

            s32 tvScanMode = GX2_TV_RENDER_MODE_WIDE_720P; //GX2GetSystemTVScanMode(); //TODO: wut needs these functions
            s32 drcScanMode = GX2_TV_RENDER_MODE_WIDE_480P; //GX2GetSystemDRCScanMode();

            s32 tvRenderMode;
            u32 tvWidth = 0;
            u32 tvHeight = 0;

            switch(tvScanMode)
            {
            case GX2_TV_RENDER_MODE_WIDE_480P:
                tvWidth = 854;
                tvHeight = 480;
                tvRenderMode = GX2_TV_RENDER_MODE_WIDE_480P;
                break;
            case GX2_TV_RENDER_MODE_WIDE_1080P:
                tvWidth = 1920;
                tvHeight = 1080;
                tvRenderMode = GX2_TV_RENDER_MODE_WIDE_1080P;
                break;
            case GX2_TV_RENDER_MODE_WIDE_720P:
            default:
                tvWidth = 1280;
                tvHeight = 720;
                tvRenderMode = GX2_TV_RENDER_MODE_WIDE_720P;
                break;
            }

            s32 tvAAMode = GX2_AA_MODE1X;
            s32 drcAAMode = GX2_AA_MODE1X;
            
            u32 surface_format = GX2_ATTRIB_FORMAT_UNORM_8_8_8_8 | 0x10;

            //! calculate the size needed for the TV scan buffer and allocate the buffer from bucket memory
            GX2CalcTVSize(tvRenderMode, surface_format, GX2_BUFFERING_MODE_DOUBLE, &scanBufferSize, &scaleNeeded);
            tvScanBuffer = MEMBucket_alloc(scanBufferSize, 0x1000);
            GX2Invalidate(GX2_INVALIDATE_CPU, tvScanBuffer, scanBufferSize);
            GX2SetTVBuffer(tvScanBuffer, scanBufferSize, tvRenderMode, surface_format, GX2_BUFFERING_MODE_DOUBLE);

            //! calculate the size needed for the DRC scan buffer and allocate the buffer from bucket memory
            GX2CalcDRCSize(drcScanMode, surface_format, GX2_BUFFERING_MODE_DOUBLE, &scanBufferSize, &scaleNeeded);
            drcScanBuffer = MEMBucket_alloc(scanBufferSize, 0x1000);
            GX2Invalidate(GX2_INVALIDATE_CPU, drcScanBuffer, scanBufferSize);
            GX2SetDRCBuffer(drcScanBuffer, scanBufferSize, drcScanMode, surface_format, GX2_BUFFERING_MODE_DOUBLE);

            //! Setup color buffer for TV rendering
            GX2InitColorBuffer(&tvColorBuffer, GX2_SURFACE_DIM_TEXTURE_2D, tvWidth, tvHeight, 1, surface_format, tvAAMode);

            //! Setup TV depth buffer (can be the same for both if rendered one after another)
            GX2InitDepthBuffer(&tvDepthBuffer, GX2_SURFACE_DIM_TEXTURE_2D, tvColorBuffer.surface.width, tvColorBuffer.surface.height, 1, GX2_SURFACE_FORMAT_FLOAT_R32, tvAAMode);

            //! Setup TV HiZ buffer
            GX2InitDepthBufferHiZEnable(&tvDepthBuffer, TRUE);

            //! Setup color buffer for DRC rendering
            GX2InitColorBuffer(&drcColorBuffer, GX2_SURFACE_DIM_TEXTURE_2D, 854, 480, 1, surface_format, drcAAMode);

            //! Setup DRC depth buffer (can be the same for both if rendered one after another)
            GX2InitDepthBuffer(&drcDepthBuffer, GX2_SURFACE_DIM_TEXTURE_2D, drcColorBuffer.surface.width, drcColorBuffer.surface.height, 1, GX2_SURFACE_FORMAT_FLOAT_R32, drcAAMode);

            //! Setup DRC HiZ buffer
            GX2InitDepthBufferHiZEnable(&drcDepthBuffer, TRUE);
            
            //! allocate auxilary buffer last as there might not be enough MEM1 left for other stuff after that
            if (tvColorBuffer.surface.aa)
            {
                u32 auxSize, auxAlign;
                GX2CalcColorBufferAuxInfo(&tvColorBuffer, &auxSize, &auxAlign);
                tvColorBuffer.aaBuffer = MEM2_alloc(auxSize, auxAlign);

                tvColorBuffer.aaSize = auxSize;
                memset(tvColorBuffer.aaBuffer, 0xCC, auxSize);
                GX2Invalidate(GX2_INVALIDATE_CPU, tvColorBuffer.aaBuffer, auxSize);
            }

            if (drcColorBuffer.surface.aa)
            {
                u32 auxSize, auxAlign;
                GX2CalcColorBufferAuxInfo(&drcColorBuffer, &auxSize, &auxAlign);
                drcColorBuffer.aaBuffer = MEM2_alloc(auxSize, auxAlign);
                drcColorBuffer.aaSize = auxSize;
                memset(drcColorBuffer.aaBuffer, 0xCC, auxSize);
                GX2Invalidate(GX2_INVALIDATE_CPU, drcColorBuffer.aaBuffer, auxSize );
            }
         
         
             //Allocate all MEM1 buffers
             u32 size, align;
             
             tvColorBuffer.surface.image = MEM1_alloc(tvColorBuffer.surface.imageSize, tvColorBuffer.surface.alignment);
             GX2Invalidate(GX2_INVALIDATE_CPU, tvColorBuffer.surface.image, tvColorBuffer.surface.imageSize);
             
             tvDepthBuffer.surface.image = MEM1_alloc(tvDepthBuffer.surface.imageSize, tvDepthBuffer.surface.alignment);
             GX2Invalidate(GX2_INVALIDATE_CPU, tvDepthBuffer.surface.image, tvDepthBuffer.surface.imageSize);
    
             GX2CalcDepthBufferHiZInfo(&tvDepthBuffer, &size, &align);
             tvDepthBuffer.hiZPtr = MEM1_alloc(size, align);
             GX2Invalidate(GX2_INVALIDATE_CPU, tvDepthBuffer.hiZPtr, size);
             
             drcColorBuffer.surface.image = MEM1_alloc(drcColorBuffer.surface.imageSize, drcColorBuffer.surface.alignment);
             GX2Invalidate(GX2_INVALIDATE_CPU, drcColorBuffer.surface.image, drcColorBuffer.surface.imageSize);
             
             drcDepthBuffer.surface.image = MEM1_alloc(drcDepthBuffer.surface.imageSize, drcDepthBuffer.surface.alignment);
             GX2Invalidate(GX2_INVALIDATE_CPU, drcDepthBuffer.surface.image, drcDepthBuffer.surface.imageSize);
             
             GX2CalcDepthBufferHiZInfo(&drcDepthBuffer, &size, &align);
             drcDepthBuffer.hiZPtr = MEM1_alloc(size, align);
             GX2Invalidate(GX2_INVALIDATE_CPU, drcDepthBuffer.hiZPtr, size);
             
             //! set initial context state and render buffers
             GX2SetContextState(tvContextState);
             GX2SetColorBuffer(&tvColorBuffer, GX2_RENDER_TARGET_0);
             GX2SetDepthBuffer(&tvDepthBuffer);

             GX2SetContextState(drcContextState);
             GX2SetColorBuffer(&drcColorBuffer, GX2_RENDER_TARGET_0);
             GX2SetDepthBuffer(&drcDepthBuffer);
             
             setupScene();
             
             initialized = true;
             gx2_killed = false;
             mem1_freed = false;
         }
      }
   }

   return isAppRunning;
}

int
CoreEntryPoint(int argc, const char **argv)
{
   OSReport("Hello world from %s", argv[0]);
   return argc;
}

int
main(int argc, char **argv)
{
   ProcUIInit(&SaveCallback);
   OSReport("Main thread running on core %d", OSGetCoreId());
   
   uint32_t gx2_handle;
   OSDynLoad_Acquire("gx2.rpl", &gx2_handle);
   OSDynLoad_FindExport(gx2_handle, 0, "GX2Flush", &GX2Flush);
   OSDynLoad_FindExport(gx2_handle, 0, "GX2Invalidate", &GX2Invalidate);
   OSDynLoad_FindExport(gx2_handle, 0, "GX2CalcColorBufferAuxInfo", &GX2CalcColorBufferAuxInfo);

	glmPerspective(projectionMtx, 45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
	glmLookAt(viewMtx, 4.0f, 3.0f, -3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    int vpadError = -1;
    VPADStatus vpad;

    while(AppRunning())
    {
        if(!initialized) continue;
        
        VPADRead(0, &vpad, 1, &vpadError);

        if(vpadError == 0 && ((vpad.trigger | vpad.hold) & VPAD_BUTTON_HOME))
            break;

        if(vpad.hold & VPAD_BUTTON_LEFT) {
            degreeX -= 1.0f;
            manualControl = true;
        }
        if(vpad.hold & VPAD_BUTTON_RIGHT) {
            degreeX += 1.0f;
            manualControl = true;
        }
        if(vpad.hold & VPAD_BUTTON_UP) {
            degreeY -= 1.0f;
            manualControl = true;
        }
        if(vpad.hold & VPAD_BUTTON_DOWN) {
            degreeY += 1.0f;
            manualControl = true;
        }
        if(vpad.hold & VPAD_BUTTON_L) {
            degreeZ -= 1.0f;
            manualControl = true;
        }
        if(vpad.hold & VPAD_BUTTON_R) {
            degreeZ += 1.0f;
            manualControl = true;
        }
        if(vpad.hold & VPAD_BUTTON_A) {
            manualControl = false;
        }


        //!*******************************************************************
        //!                          DRC Rending                             *
        //!*******************************************************************
        prepareRendering(&drcColorBuffer, &drcDepthBuffer, drcContextState);
        renderScene();
        GX2CopyColorBufferToScanBuffer(&drcColorBuffer, GX2_SCAN_TARGET_DRC);

        //!*******************************************************************
        //!                          TV Rending                              *
        //!*******************************************************************
        prepareRendering(&tvColorBuffer, &tvDepthBuffer, tvContextState);
        renderScene();
        GX2CopyColorBufferToScanBuffer(&tvColorBuffer, GX2_SCAN_TARGET_TV);
        GX2SwapScanBuffers();
        GX2Flush();
        GX2DrawDone();
        
        GX2SetTVEnable(TRUE);
        GX2SetDRCEnable(TRUE);

        GX2WaitForVsync();
    }
   
   return 0;
}
