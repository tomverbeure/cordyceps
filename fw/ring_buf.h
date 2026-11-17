#ifndef RING_BUF_H
#define RING_BUF_H

#include "pico/stdlib.h"
#include "pico/critical_section.h"

struct ring_buf_t
{
    uint8_t *           buffer;
    int                 size;
    int                 wr_ptr;
    int                 rd_ptr;
    critical_section_t  locked;
};

void ring_buf_init(struct ring_buf_t * r, uint8_t * buffer, int size);
bool ring_buf_wr(struct ring_buf_t * r, uint8_t d);
bool ring_buf_wr_n(struct ring_buf_t * r, uint8_t * d, int count);
bool ring_buf_rd(struct ring_buf_t * r, uint8_t * d);
bool ring_buf_rd_n(struct ring_buf_t * r, uint8_t * d, int count);
int  ring_buf_available(struct ring_buf_t * r);
int  ring_buf_free_space(struct ring_buf_t *r);

#endif
