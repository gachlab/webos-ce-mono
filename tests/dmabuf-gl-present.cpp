// Contract: CPU-fill a GBM dma-buf, import via EGLImage + TEXTURE_EXTERNAL_OES,
// blit to an FBO, and sample a pixel (#79 GL compose).
//
// Mutation: change kExpectedArgb (not kFillArgb) → must FAIL.

#include "dmabuf_window.h"

#include <cstdio>
#include <unistd.h>

static const uint32_t kFillArgb = 0xFFCC3311u;
static const uint32_t kExpectedArgb = 0xFFCC3311u;

static bool fillSolid(dmabuf_window::Frame* frame, uint32_t argb)
{
    uint32_t stride = 0;
    void* ptr = frame->mapWrite(&stride);
    if (!ptr)
        return false;
    auto* rows = static_cast<uint8_t*>(ptr);
    for (uint32_t y = 0; y < frame->height(); ++y) {
        auto* row = reinterpret_cast<uint32_t*>(rows + y * stride);
        for (uint32_t x = 0; x < frame->width(); ++x)
            row[x] = argb;
    }
    frame->unmap();
    return true;
}

int main()
{
    if (!dmabuf_window::available()) {
        std::fprintf(stderr, "dmabuf-gl-present: no render node\n");
        return 1;
    }

    auto device = dmabuf_window::Device::openDefault();
    auto frame = dmabuf_window::Frame::create(device, 64, 48);
    if (!frame || !fillSolid(frame.get(), kFillArgb)) {
        std::fprintf(stderr, "dmabuf-gl-present: create/fill failed\n");
        return 1;
    }

    dmabuf_window::Export desc;
    if (!frame->exportDesc(&desc)) {
        std::fprintf(stderr, "dmabuf-gl-present: export failed\n");
        return 1;
    }

    auto gl = dmabuf_window::GlImporter::create();
    if (!gl) {
        std::fprintf(stderr, "dmabuf-gl-present: GlImporter::create failed\n");
        ::close(desc.fd);
        return 1;
    }

    uint32_t got = 0;
    if (!gl->samplePixel(desc, 32, 24, &got)) {
        std::fprintf(stderr, "dmabuf-gl-present: samplePixel failed\n");
        ::close(desc.fd);
        return 1;
    }
    ::close(desc.fd);

    if (got != kExpectedArgb) {
        std::fprintf(stderr,
                     "dmabuf-gl-present: pixel mismatch got=0x%08x expected=0x%08x\n",
                     got, kExpectedArgb);
        return 1;
    }

    std::printf("dmabuf-gl-present: ok pixel=0x%08x\n", got);
    return 0;
}
