#include "fake65c02.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <readline/readline.h>

uint8_t memory[65536];
int rws[65536];
int writes[65536];

int getc_addr = 0xf004,
    putc_addr = 0xf001,
    mfio_addr = 0xf100,
    ticks = 0;

// fake6502_context ctx;

typedef struct MFIO {
    uint8_t  action;   // I: request an action (write after setting other params)
    uint8_t  status;   // O: action status
    uint16_t bufptr;   // I: low-endian pointer to data buffer
    uint16_t bufsiz;   // I: max size of data buffer
    uint16_t result;   // O: actual read/write size or open'd fileno
    uint     offset;   // IO: 32-bit offset for seek/tell (read/write)
} MFIO;

MFIO* mfio;


// uint8_t fake6502_mem_read(fake6502_context *c, uint16_t addr) {
uint8_t read6502(uint16_t addr) {
    char buf[1];
    int ch;
    if (addr == getc_addr) {
//        int n = read(0, buf, 1);
//        memory[addr] = n == 1 ? (uint8_t)buf[0]: 0;
        ch = (uint8_t)getchar();
        ch = ch == 10 ? 13 : ch;
//        printf("[%d]", ch);
        memory[addr] = ch;
    }
    rws[addr] += 1;
    return memory[addr];
}


// void fake6502_mem_write(fake6502_context *c, uint16_t addr, uint8_t val) {
void write6502(uint16_t addr, uint8_t val) {
    char *line;
    int n;
    if (addr == putc_addr) {
        putc((int)val, stdout);
    }
    else if (addr == mfio_addr) {
        /*
        printf("mfio: action=%02x, bytes=", mfio->action);
        for (int i=0; i<12; i++) printf("%02x ", memory[i+0xf100]);
        printf("\n");
        */
        if (val == 0x10) {
            // TODO implement interface in mfio.c
            line = readline(">");
            n = strlen(line);  /* todo deal with n > 200-1 */
            memcpy(memory + mfio->bufptr, line, n);
            memory[mfio->bufptr+n] = '\n';
            free(line);
            mfio->status = 0;
            mfio->result = n+1;
        }
    }
    rws[addr] += 1;
    writes[addr] += 1;
    memory[addr] = val;
}

// void show_cpu(fake6502_context *c) {
void show_cpu() {
    /*
    printf(
        "PC=%04x A=%02x X=%02x Y=%02x S=%02x FLAGS=<N%d V%d B%d D%d I%d Z%d C%d>\n",
        c->cpu.pc, c.cpu.a, c.cpu.x, c.cpu.y, c.cpu.s,
        c->cpu.flags & FAKE6502_SIGN_FLAG ? 1: 0,
        c->cpu.flags & FAKE6502_OVERFLOW_FLAG ? 1: 0,
        c->cpu.flags & FAKE6502_BREAK_FLAG ? 1: 0,
        c->cpu.flags & FAKE6502_DECIMAL_FLAG ? 1: 0,
        c->cpu.flags & FAKE6502_INTERRUPT_FLAG ? 1: 0,
        c->cpu.flags & FAKE6502_ZERO_FLAG ? 1: 0,
        c->cpu.flags & FAKE6502_CARRY_FLAG ? 1: 0
    );
    */
    printf(
        "\nPC=%04x A=%02x X=%02x Y=%02x S=%02x FLAGS=<N%d V%d B%d D%d I%d Z%d C%d> ticks=%u\n",
        pc, a, x, y, sp,
        status & FLAG_SIGN ? 1: 0,
        status & FLAG_OVERFLOW ? 1: 0,
        status & FLAG_BREAK ? 1: 0,
        status & FLAG_DECIMAL ? 1: 0,
        status & FLAG_INTERRUPT ? 1: 0,
        status & FLAG_ZERO ? 1: 0,
        status & FLAG_CARRY ? 1: 0,
        ticks
    );
}

int main(int argc, char* argv[]) {

    FILE *fin, *fout;
    const char* romfile = NULL;
    int addr = -1,
        reset = -1,
        max_ticks = -1,
        errflg = 0,
        sz = 0,
        c;

    while ((c = getopt(argc, argv, ":r:a:g:t:i:o:x:")) != -1) {
        switch(c) {
        case 'r':
            romfile = optarg;
            break;
        case 'a':
            addr = strtol(optarg, NULL, 0);
            break;
        case 'g':
            reset = strtol(optarg, NULL, 0);
            break;
        case 't':
            max_ticks = strtol(optarg, NULL, 0);
            break;
        case 'i':
            getc_addr = strtol(optarg, NULL, 0);
            break;
        case 'o':
            putc_addr = strtol(optarg, NULL, 0);
            break;
        case 'x':
            mfio_addr = strtol(optarg, NULL, 0);
            break;
        case ':':       /* -f or -o without operand */
            fprintf(stderr,
                "Option -%c requires an argument\n", optopt);
            errflg++;
            break;
        case '?':
            fprintf(stderr,
                "Unrecognized option: '-%c'\n", optopt);
            errflg++;
        }
    }
    if (romfile == NULL) errflg++;
    if (errflg) {
        fprintf(stderr,
            "Usage: pro65 -r file.rom [...]\n"
            "Options:\n"
            "-?              : Show this message\n"
            "-r <file>       : Load file to memory and reset into it\n"
            "-a <address>    : Address to load (default top of address space)\n"
            "-g <address>    : Set reset vector @ $fffc to <address>\n"
            "-t <ticks>      : Run for max ticks (default forever)\n"
            "-i <address>    : magic read for getc (default $f004)\n"
            "-o <address>    : magic write for putc (default $f001)\n"
            "-x <address>    : magic ioctl, write 0xfe to exit\n"
        );
        exit(2);
    }

    /* non-blocking stdin */
//    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    mfio = (MFIO*)(memory + mfio_addr);

    fin = fopen(romfile, "rb");
    if (!fin) {
        fprintf(stderr, "File not found: %s\n", romfile);
        exit(3);
    }
    fseek(fin, 0L, SEEK_END);
    sz = ftell(fin);
    rewind(fin);
    if (addr < 0) addr = 65536 - sz;
    printf("prof65: reading %s @ 0x%04x\n", romfile, addr);
    fread(memory + addr, 1, sz, fin);
    fclose(fin);

    if (reset >= 0) {
        memory[0xfffc] = reset & 0xff;
        memory[0xfffd] = (reset >> 8) & 0xff;
    }

/*
    show_cpu(&ctx);
    fake6502_reset(&ctx);
    show_cpu(&ctx);
*/
    printf("prof65: starting %d\n");
    show_cpu();
    reset6502();
    show_cpu();
//    while (ctx.emu.clockticks != cycles && mfio->action != 0xfe) fake6502_step(&ctx);
    while (ticks != max_ticks && mfio->action != 0xfe) ticks += step6502();
    //    show_cpu(&ctx);
    show_cpu();

    fout = fopen("forth-coverage.dat", "wb");
    fwrite(rws, sizeof(int), 65536, fout);
    fclose(fout);

    fout = fopen("forth-writes.dat", "wb");
    fwrite(writes, sizeof(int), 65536, fout);
    fclose(fout);
}

