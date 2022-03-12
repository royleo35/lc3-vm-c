#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<assert.h>
#include<stdint.h>

#ifdef _WIN32
#include<Windows.h>
#include<signal.h>
#include<conio.h>
HANDLE hStdin = INVALID_HANDLE_VALUE;
uint16_t check_key() {
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

DWORD fdwMode, fdwOldMode;
void disable_input_buffering() {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode);
    fdwMode = fdwOldMode ^ ENABLE_ECHO_INPUT ^ ENABLE_LINE_INPUT;
    SetConsoleMode(hStdin, fdwMode);
    FlushConsoleInputBuffer(hStdin);
}

void restore_input_buffering() {
    SetConsoleMode(hStdin, fdwOldMode);
}

void handle_interrupt(int signal) {
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

void set_up() {
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();
}

void shut_down() {
    restore_input_buffering();
}

#elif 

#endif

typedef unsigned short u16;
// const u16 UINT16_MAX = 65535;

#define get_bits(x, low, high) ((x>>low) & ((1<<(high-low+1))-1))

// 10个16位的寄存器 
enum {
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7, // 8个通用寄存器
    R_PC, //pc
    R_COND,
    R_COUNT, // 寄存器数量 10

};
u16 reg[R_COUNT];

// 64k*2B内存
u16 memory[UINT16_MAX];

// run status
int running = 1;

// instruction set 16 op

enum {
    OP_BR=0, // branch
    OP_ADD, //add
    OP_LD, //load
    OP_ST, //store
    OP_JSR, //jump register
    OP_AND, // bitwise and
    OP_LDR, // load register
    OP_STR, // store register
    OP_RTI, // unused
    OP_NOT, // bitwise not
    OP_LDI, //load indirect
    OP_STI, // store indirect
    OP_JMP, //jump
    OP_RES, // reserved
    OP_LEA, // load effective address
    OP_TRAP, // execute trap
};

// lc3 only has 3 condition flags which indicate the sign of the preivous calculation
enum {
    FL_POS = 1<<0, //positive
    FL_ZRO = 1<<1, // zero
    FL_NEG = 1<<2, //negtive
};

u16 sign_extending(u16 x, int bit_count) {
    // test MSB if 1 neg, else pos
    if( x >> (bit_count-1)) {
        // neg
        x |= (0xffff<<bit_count);
    }
    return x;
}

void update_flags(u16 r) {
    if (reg[r] == 0) {
        reg[R_COND] = FL_ZRO;
    }else if (reg[r] >> 15) {
        reg[R_COND] = FL_NEG;
    }else {
        reg[R_COND] = FL_POS;
    }
}
// memory mapped registers
enum{
    MR_KBSR = 0xFE00, // keyboard status
    MR_KBSD = 0xFE02  // keyboard data
};


u16 mem_read(u16 addr){
    //  write keyboard reg
    if ( addr == MR_KBSR) {
        if (check_key()) {
            memory[MR_KBSR] = (1<<15);
            memory[MR_KBSD] = getchar();
        } else {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[addr];
}

void mem_write(u16 addr, u16 val){
    memory[addr] = val;
}


 // eg add
    /*
    bit15-bit12:0001
    bit11-9: DR   means desternation register idx
    bit8-6:SR1 means source register one
    bit5: if 0 ->bit4-3:00, and bit2-0: SR2
          if 1 ->bit4-0: means immediate number
    the imm number need to sign extending
    ADD R2 R0 R1 ; add the contents of R0 to R1 and store in R2.
    */
void add(u16 instr) {
    // add r
    u16 dr = (instr >> 9) & 0x7;
    u16 sr1 = (instr>>6) & 0x7;
    u16 imm = (instr>>5) & 0x1;
    // add dr, sr1, imm  --> dr=sr1+imm
    if (imm) {
        u16 imm_num = sign_extending((instr&0x1f), 5);
        reg[dr] = reg[sr1] + imm_num;
    } else {
        u16 sr2 = instr & 0x7;
        reg[dr] = reg[sr1] + reg[sr2];
    }
    update_flags(dr);
}

/*
bit11-9: dr
bit8-0: pcoffset9
*/
void load_indirect(u16 instr) {
    u16 dr = (instr >> 9) & 0x7;
    u16 pc_off = sign_extending(instr & 0x1ff, 9);
    // addr of pc + pc_off store a addr of value ,like u16**
    reg[dr] = mem_read(mem_read(reg[R_PC] + pc_off));
    update_flags(dr);

}
// bitmap like add
// bit11-9: dr
//
void and(u16 instr) {
    // add r
    u16 dr = (instr >> 9) & 0x7;
    u16 sr1 = (instr>>6) & 0x7;
    u16 imm = (instr>>5) & 0x1;
    // add dr, sr1, imm  --> dr=sr1+imm
    if (imm) {
        u16 imm_num = sign_extending((instr&0x1f), 5);
        reg[dr] = reg[sr1] & imm_num;
    } else {
        u16 sr2 = instr & 0x7;
        reg[dr] = reg[sr1] & reg[sr2];
    }
    update_flags(dr);
}

// bitwise not
// bit11-9 dr
// bit9-6 sr
void not(u16 instr){
    u16 dr = (instr>>9) & 0x7;
    u16 sr = (instr>>6) & 0x7;
    reg[dr] = ~reg[sr];
    update_flags(dr);
}

//branch 
// bit11 n
// bit10 z
// bit9 p
// bit8-0 pc_off
void branch(u16 instr) {
    u16 cond = (instr>>0x9) & 0x7;
    u16 pc_off = sign_extending(instr&0x1ff, 9);
    // printf("cond:%d, pc_off: %d\n", cond, pc_off);
    if (cond & reg[R_COND]) {
        reg[R_PC] = pc_off;
    }
}

// jump
// bit8-6 baseReg
// means set pc=baseReg
void jump(u16 instr){
    u16 baseReg = (instr >> 0x6) & 0x7;
    reg[R_PC] = reg[baseReg]; 
}

// jump register means
// bit11 mode
// if mode=1 bit10-0 pc_off
// if mode=0 bit8-6 baseR
void jump_reg(u16 instr) {
    u16 flag = (instr >> 11) & 0x1;
    // store pc in r7 for jump back
    reg[R_R7] = reg[R_PC];
    if (flag) {
        u16 pc_off = sign_extending(instr & 0x7ff, 11);
        reg[R_PC] += pc_off; // JSR long offset jump
    }else {
        u16 base_reg = (instr >> 0x6) & 0x7;
        reg[R_PC] = reg[base_reg]; // jump reg
    }
}

// load means load a value of mem into reg
// bit11-9 dr
// bit8-0 pc_off
void load(u16 instr) {
    u16 dr = (instr >> 9) & 0x7;
    u16 pc_off = sign_extending(instr&0x1ff, 9);
    reg[dr] = mem_read(reg[R_PC] + pc_off);
    update_flags(dr);
}

// load register mean load a value in addr of reg[sr] + off into dr reg
// bit11-9 dr
// bit8-6 baseR
// bit5-0 off
void load_reg(u16 instr) {
    u16 dr = (instr >> 0x9) & 0x7;
    u16 baseReg = (instr >> 0x6) & 0x7;
    u16 off6 = sign_extending(instr&0x3f, 6);
    reg[dr] = mem_read(reg[baseReg] + off6);
    update_flags(dr);

}

// load effective address
// means the value:[pc + off] into a reg
// bit11-9 dr
// bit8-0 off9
void lea(u16 instr) {
    u16 dr = (instr>>9) & 0x7;
    u16 pc_off = sign_extending(instr&0x1ff, 9);
    reg[dr] = reg[R_PC] + pc_off;
    update_flags(dr);
}

// store means write a value in reg sr into addr: pc+off
// bit11-9 sr
// bit8-0 off9
void store(u16 instr) {
    u16 sr = (instr>> 9) & 0x7;
    u16 pc_off = sign_extending(instr&0x1ff, 9);
    mem_write(reg[R_PC] + pc_off, reg[sr]);
}

// store indirect means wirte a value in reg into: addr of addr pc+off
// bit11-9 sr
// bit8-0 off9
void store_indirect(u16 instr) {
    u16 sr = (instr>> 9) & 0x7;
    u16 pc_off = sign_extending(instr&0x1ff, 9);
    mem_write(mem_read(reg[R_PC] + pc_off), reg[sr]);
}

// store register means write a value in reg into addr of reg dr+off6
// bit11-9 dr
// bit8-6 sr
// bit5-0 off6
void store_register(u16 instr) {
    u16 dr = (instr >> 9) & 0x7;
    u16 sr = (instr >> 6) & 0x7;
    u16 off6 = sign_extending(instr&0x3f, 6);
    mem_write(reg[dr]+off6, reg[sr]);
}

void trap_puts() {
    // each char use two bytes, be careful of pointer
    u16* c = memory + reg[R_R0];
    while(*c) {
        putc((char)*c, stdout);
        ++c;
    }
    fflush(stdout);
}

// read a char and store in reg0
void trap_getc() {
    reg[R_R0] = (u16)getchar();
    update_flags(R_R0);
}

// output a char
void trap_out() {
    putc((char)reg[R_R0], stdout);
    fflush(stdout);
}

// in 
void trap_in () {
    printf("Enter a character: ");
    char c = getchar();
    // echo
    putc(c, stdout);
    fflush(stdout);
    // store char in reg 0
    reg[R_R0] = (u16)c;
    update_flags(R_R0);
}

// put string, echo 16bit store 2 bytes
void trap_putsp( ){
    u16 *c = memory + reg[R_R0];
    while(*c) {
        char c1 = (*c) & 0xff;
        putc(c1, stdout);
        char c2 = (*c) >> 8;
        if (c2) putc(c2, stdout);
        ++c;
    }
    fflush(stdout);
}

void trap_halt( ){
    puts("HALT");
    fflush(stdout);
    running = 0;

}
// trap
enum {
    TRAP_GETC = 0x20, // get char from keyboard, not echoed on the terminal
    TRAP_OUT = 0x21, // output a char
    TRAP_PUTS = 0x22, // output a word string
    TRAP_IN = 0x23, // get char from keyboard, echoed on the terminal
    TRAP_PUTSP = 0x24, // output a byte string
    TRAP_HALT = 0x25 // halt the program
};

void trap(u16 instr) {
    u16 trap_vect = instr & 0xff;
    // printf("trap_vect: 0x%02x\n", trap_vect);
    switch (trap_vect)
    {
    case TRAP_GETC:
        trap_getc();
        break;
    case TRAP_OUT:
        trap_out();
        break;
    case TRAP_PUTS:
        trap_puts();
        break;
    case TRAP_IN:
        trap_in();
        break;
    case TRAP_PUTSP:
        trap_putsp();
        break;
    case TRAP_HALT:
        trap_halt();
        break;
    default:
        break;
    }
}



void test_get_bits() {
    u16 tmp = 0x1f;
    u16 val = get_bits(tmp, 4,5);
    assert(val==1);
    val = get_bits(tmp, 4,4);
    assert(val==1);
    val = get_bits(tmp, 5,6);
    assert(val==0);
    val = get_bits(tmp, 1,3);
    assert(val=7);
}

u16 swap16(u16 x) {
    return (x<<8) | (x>>8);
}
void print_bytes(u16 *addr, int cnt) {
    int st = 0x3000;
    for(int i =0; i < cnt; i++){
        printf("\n 0x%04x:\t ins[%d], %2d|%4d", st+i, addr[i], get_bits(addr[i], 12,15), addr[i]&0x7ff);
    }
    printf("\n\n");
}
void read_image_file(FILE *file) {
    // the first 2 byte of program indicate the start addr of program
    u16 origin;
    fread(&origin, sizeof(origin), 1, file);
    // lc3 is big-endian swich to small-endian
    origin = swap16(origin);
    printf("image origin: 0x%04x\n", origin);
    u16 max_read = UINT16_MAX - origin;
    u16 *p = memory + origin;
    size_t cnt = fread(p, sizeof(origin), max_read, file);
    printf("image size: %d bytes\n", cnt);
    int tmp = cnt;
    // big-endian to small endian
    while(cnt--) {
        *p = swap16(*p);
        p++;
    }
    print_bytes(&memory[0x3000], tmp);
}

int read_image(const char* filePath) {
    FILE *fp = fopen(filePath, "r");
    if (!fp) {
        return 0;
    }
    read_image_file(fp);
    fclose(fp);
    return 1;
}

void print_regs() {
    for(int i=0; i < 8; i++) {
        printf("R_R%d=0x%04x\n", i, reg[i]);
    }
    printf("R_COND=0x%04x\n", reg[R_COND]);
    printf("R_PC=0x%04x\n\n", reg[R_PC]);
}



int main(int argc, const char* argv[]) {

    // load images
    if (argc < 2) {
        printf("lc2 [image-file]...\n");
        exit(2);
    }
    for(int i = 1; i < argc; i++) {
        if (!read_image(argv[i])) {
            printf("read image: %s, error", argv[i]);
            exit(1);
        }
    }
    printf("read image successfully.\n");

    // setup
    set_up();

    // set cond register to FL_ZRO
    reg[R_COND] = FL_ZRO;
    // PC_START Addr is 0x3000
    enum {PC_START = 0x3000};
    reg[R_PC] = PC_START;

    // instrcution execute has 5 step
    /*
    1 fetch a instruction
    2 add pc
    3 get op code in instruction to determain which op to be done
    4 execute the instruction
    5 go to step 1
    
    */
    // each instruction is 2-bytes
    // eg add
    /*
    bit15-bit12:0001
    bit11-9: DR   means desternation register idx
    bit8-6:SR1 means source register one
    bit5: if 0 ->bit4-3:00, and bit2-0: SR2
          if 1 ->bit4-0: means immediate number
    the imm number need to sign extending
    ADD R2 R0 R1 ; add the contents of R0 to R1 and store in R2.
    */
   /*
   enum {
    OP_BR=0, // branch
    OP_ADD, //add
    OP_LD, //load
    OP_ST, //store
    OP_JSR, //jump register
    OP_AND, // bitwise and
    OP_LDR, // load register
    OP_STR, // store register
    OP_RTI, // unused
    OP_NOT, // bitwise not
    OP_LDI, //load indirect
    OP_STI, // store indirect
    OP_JMP, //jump
    OP_RES, // reserved
    OP_LEA, // load effective address
    OP_TRAP, // execute trap
};
   */
    printf("start addr of program: 0x%04x\n", reg[R_PC]);

    while(running) {
        // fetch instruction
        // printf("pc 0x%04x\n", reg[R_PC]);
        u16 instr = mem_read(reg[R_PC]++);
        u16 op = instr >> 12;
        // printf("current instruction op code: %d\n", op);
        print_regs();
        // getchar();
        switch (op)
        {
        case OP_BR:
            branch(instr);
            break;
        case OP_ADD:
            add(instr);
            break;
        case OP_LD:
            load(instr);
            break;
        case OP_ST:
            store(instr);
            break;
        case OP_JSR:
            jump_reg(instr);
            break;
        case OP_AND:
            and(instr);
            break;
        case OP_LDR:
            load_reg(instr);
            break;
        case OP_STR:
            store_register(instr);
            break;
        case OP_RTI:
            abort();
            break;
        case OP_NOT:
            not(instr);
            break;
        case OP_LDI:
            load_indirect(instr);
            break;
        case OP_STI:
            store_indirect(instr);
            break;
        case OP_JMP:
            jump(instr);
            break;
        case OP_RES:
            abort();
            break;
        case OP_LEA:
            lea(instr);
            break;
        case OP_TRAP:
            trap(instr);
            break;
        default:
            printf("unsupport instruction.");
            shut_down();
            exit(1);
        }
    }
    shut_down();
    return 0;
}




