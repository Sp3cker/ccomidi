// pugl_mac_metal.h - Pugl Metal backend for macOS
// SPDX-License-Identifier: ISC

#pragma once

#include <pugl/pugl.h>

#ifdef __OBJC__
#import <Metal/Metal.h>
@class CAMetalLayer;
@protocol CAMetalDrawable;

typedef struct {
    id<MTLDevice>               device;
    id<MTLCommandQueue>         commandQueue;
    CAMetalLayer               *metalLayer;
    id<CAMetalDrawable>         currentDrawable;
    id<MTLCommandBuffer>        commandBuffer;
    MTLRenderPassDescriptor    *renderPassDescriptor;
    id<MTLRenderCommandEncoder> renderEncoder;
} PuglMetalContext;

#endif

#ifdef __cplusplus
extern "C" {
#endif

const PuglBackend *puglMetalBackend(void);
PuglStatus puglEnterContext(PuglView *view);
PuglStatus puglLeaveContext(PuglView *view);

#ifdef __cplusplus
}
#endif
