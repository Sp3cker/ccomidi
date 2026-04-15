// pugl_mac_metal.h - Pugl Metal backend for macOS (outside the Pugl submodule)
// SPDX-License-Identifier: ISC

#pragma once

#include <pugl/pugl.h>

#ifdef __OBJC__
#import <Metal/Metal.h>
@class CAMetalLayer;
@protocol CAMetalDrawable;

typedef struct {
  id<MTLDevice> device;
  id<MTLCommandQueue> commandQueue;
  CAMetalLayer *metalLayer;
  /* Per-frame state (valid between enter/leave during PUGL_EXPOSE) */
  id<CAMetalDrawable> currentDrawable;
  id<MTLCommandBuffer> commandBuffer;
  MTLRenderPassDescriptor *renderPassDescriptor;
  id<MTLRenderCommandEncoder> renderEncoder;
} PuglMetalContext;

#endif /* __OBJC__ */

#ifdef __cplusplus
extern "C" {
#endif

const PuglBackend *puglMetalBackend(void);

/* These are normally declared in pugl/gl.h which we no longer include on macOS
 */
PuglStatus puglEnterContext(PuglView *view);
PuglStatus puglLeaveContext(PuglView *view);

#ifdef __cplusplus
}
#endif
