#pragma once

#include "esp_rom_sys.h"
#include "hal/dma2d_ll.h"

// The SDK holds its platform mutex and has yet to publish the freshly reset pool.
static bool a3_dma2d_reset_ready(dma2d_dev_t* dev) {
    enum { ReadyPolls = 1000 };
    for (unsigned i = 0; i < DMA2D_LL_GET(TX_CHANS_PER_INST); ++i)
        dma2d_ll_tx_abort(dev, i, true);
    for (unsigned i = 0; i < DMA2D_LL_GET(RX_CHANS_PER_INST); ++i)
        dma2d_ll_rx_abort(dev, i, true);

    bool ready;
    for (unsigned attempt = 0; ; ++attempt) {
        ready = true;
        for (unsigned i = 0; i < DMA2D_LL_GET(TX_CHANS_PER_INST); ++i)
            ready &= dma2d_ll_tx_is_reset_avail(dev, i);
        for (unsigned i = 0; i < DMA2D_LL_GET(RX_CHANS_PER_INST); ++i)
            ready &= dma2d_ll_rx_is_reset_avail(dev, i);
        if (ready || attempt == ReadyPolls) break;
        esp_rom_delay_us(1);
    }

    for (unsigned i = 0; i < DMA2D_LL_GET(TX_CHANS_PER_INST); ++i)
        dma2d_ll_tx_abort(dev, i, false);
    for (unsigned i = 0; i < DMA2D_LL_GET(RX_CHANS_PER_INST); ++i)
        dma2d_ll_rx_abort(dev, i, false);
    return ready;
}
