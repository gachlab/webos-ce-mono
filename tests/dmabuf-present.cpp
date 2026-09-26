// Contract for #79 phase 1: CPU-blit a solid frame into a GBM dma-buf, export
// the fd, import it as an EGLImage, and read back the pixel. This is the
// present path the factories will select (WEBOS_DMABUF=1), without a live
// two-process session.
//
// Mutation: change kExpectedArgb (not kFillArgb) → must FAIL.

#include "dmabuf_window.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>

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
        std::fprintf(stderr, "dmabuf-present: no render node / GBM\n");
        return 1;
    }

    auto device = dmabuf_window::Device::openDefault();
    if (!device) {
        std::fprintf(stderr, "dmabuf-present: Device::openDefault failed\n");
        return 1;
    }

    const uint32_t width = 64;
    const uint32_t height = 48;
    auto frame = dmabuf_window::Frame::create(device, width, height);
    if (!frame) {
        std::fprintf(stderr, "dmabuf-present: Frame::create failed\n");
        return 1;
    }

    if (!fillSolid(frame.get(), kFillArgb)) {
        std::fprintf(stderr, "dmabuf-present: CPU fill failed\n");
        return 1;
    }

    dmabuf_window::Export desc;
    if (!frame->exportDesc(&desc)) {
        std::fprintf(stderr, "dmabuf-present: exportDesc failed\n");
        return 1;
    }

    // Registry round-trip mirrors what RemoteWindowDataDmaBuf publishes for
    // HostWindowDataFactory in-process.
    const int fakeKey = 424242;
    dmabuf_window::registryPut(fakeKey, desc);
    dmabuf_window::Export fromRegistry;
    if (!dmabuf_window::registryTake(fakeKey, &fromRegistry)) {
        std::fprintf(stderr, "dmabuf-present: registryTake failed\n");
        ::close(desc.fd);
        return 1;
    }

    auto importer = dmabuf_window::Importer::create();
    if (!importer) {
        std::fprintf(stderr, "dmabuf-present: Importer::create failed\n");
        ::close(desc.fd);
        ::close(fromRegistry.fd);
        dmabuf_window::registryClear(fakeKey);
        return 1;
    }

    uint32_t got = 0;
    if (!importer->samplePixel(fromRegistry, width / 2, height / 2, &got)) {
        std::fprintf(stderr, "dmabuf-present: samplePixel failed\n");
        ::close(desc.fd);
        ::close(fromRegistry.fd);
        dmabuf_window::registryClear(fakeKey);
        return 1;
    }

    ::close(desc.fd);
    ::close(fromRegistry.fd);
    dmabuf_window::registryClear(fakeKey);

    if (got != kExpectedArgb) {
        std::fprintf(stderr,
                     "dmabuf-present: pixel mismatch got=0x%08x expected=0x%08x\n",
                     got, kExpectedArgb);
        return 1;
    }

    if (!dmabuf_window::wantFactoryBackend()) {
        // Default is on when a render node exists; ctest sets WEBOS_DMABUF=1.
        std::fprintf(stderr,
                     "dmabuf-present: wantFactoryBackend false "
                     "(need render node / WEBOS_DMABUF!=0)\n");
        return 1;
    }

    std::printf("dmabuf-present: ok pixel=0x%08x\n", got);
    return 0;
}
