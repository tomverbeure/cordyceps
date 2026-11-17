#include "ring_buf.h"


void ring_buf_init(struct ring_buf_t * r, uint8_t * buf, int size)
{
    r->buffer   = buf;
    r->size     = size;
    r->wr_ptr   = 0;
    r->rd_ptr   = 0;

    critical_section_init(&r->locked);
}

bool ring_buf_wr(struct ring_buf_t * r, uint8_t d)
{
    critical_section_enter_blocking(&r->locked);

    // Check for buf full
    bool buf_full = ((r->wr_ptr+1) % r->size) == r->rd_ptr;

    if (!buf_full){
        r->buffer[r->wr_ptr++]  = d;

        if (r->wr_ptr == r->size)
            r->wr_ptr   = 0;
    }

    critical_section_exit(&r->locked);

    return !buf_full;
}

bool ring_buf_wr_n(struct ring_buf_t * r, uint8_t * d, int count)
{
    critical_section_enter_blocking(&r->locked);

    // Check for buf full
    bool buf_full = ((r->wr_ptr+count) % r->size) == r->rd_ptr;

    if (!buf_full){
        for(int i=0;i<count;++i){
            r->buffer[r->wr_ptr++]  = d[i];

            if (r->wr_ptr == r->size)
                r->wr_ptr   = 0;
        }
    }

    critical_section_exit(&r->locked);

    return !buf_full;
}



bool ring_buf_rd(struct ring_buf_t * r, uint8_t * d)
{
    critical_section_enter_blocking(&r->locked);

    bool buf_empty = r->wr_ptr == r->rd_ptr;

    if (!buf_empty){
        *d = r->buffer[r->rd_ptr++];

        if (r->rd_ptr == r->size)
            r->rd_ptr   = 0;
    }

    critical_section_exit(&r->locked);

    return !buf_empty;
}

bool ring_buf_rd_n(struct ring_buf_t * r, uint8_t * d, int count)
{
    critical_section_enter_blocking(&r->locked);

    int available = r->wr_ptr - r->rd_ptr;
    if (available < 0){
        available   += r->size;
    }

    bool buf_empty = available < count;

    if (!buf_empty){
        for(int i=0;i<count;++i){
            d[i] = r->buffer[r->rd_ptr++];

            if (r->rd_ptr == r->size)
                r->rd_ptr   = 0;
        }
    }

    critical_section_exit(&r->locked);

    return !buf_empty;
}

int ring_buf_available(struct ring_buf_t * r)
{
    critical_section_enter_blocking(&r->locked);

    int available = r->wr_ptr - r->rd_ptr;
    if (available < 0){
        available   += r->size;
    }

    critical_section_exit(&r->locked);

    return available;
}

int ring_buf_free_space(struct ring_buf_t *r)
{
    return r->size - ring_buf_available(r) - 1;
}


