#define VIRTUAL_BITS_DEFAULT  24            // Virtual address size
#define PAGE_SIZE_BITS        8             
#define USER_LOWEST_ADDRESS   0x000000

#define PROG_NAME_DEFAULT	  "prog"        // Programs' name start with 'prog'
#define FIRST_NUMBER_DEFAULT  0
#define HOW_MANY_DEFAULT      50
#define MAX_LINE_LENGTH       20            // Max length of a code line
#define MAX_LINES_DEFAULT	  20
#define VALUE                 400

typedef struct PCB{
    int pid;
    int *REG16;
    int PC;
    int TLBentries;
    int priority;
    int quantum;
    int remQuantum;
    struct mm *mm;
    struct PCB *next;
} PCB;

typedef struct PM{
    int *FF; // Free spaces (Byte array)
    int *M;
    int FB;  // Free blocks
} PM;

typedef struct PQ{
    int max;
    int count;
    PCB *head;
    PCB *tail;
} PQ;

typedef struct TLBe{
    int PID;
    int virt;
    int phys;
} TLBe;

typedef struct thread{
    PCB *running_process;
    int RI;
    int PC;
    int *REG16;
    TLBe *PTBR;
    int indexTLB;
    TLBe *TLBA;
} thread;

typedef struct core{
    thread *threads;
} core;

typedef struct CPU{
    core *core;
} CPU;

typedef struct machine{
    CPU *cpu;
} machine;

typedef struct RTC{
    PQ *pQ[100];
    int *bitmap;
    int count;
} RTC;

typedef struct mm{
    int code;
    int data;
    TLBe *pgb;
} mm;

typedef struct program_config {
    unsigned int  virtual_bits;
    unsigned int  user_lowest;
    unsigned int  num_page_bits;
    unsigned int  pages;
    unsigned int  max_lines;
    char		  *prog_name;
    unsigned int  first_number;
    unsigned int  how_many;
    unsigned int  offset_bits;
    unsigned int  offset_mask;
} program_config;