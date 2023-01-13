#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include "structs.h"

#define MAX 100
#define MAXMEM 1<<24
#define NUMBLOCK 1<<18
#define LD 0
#define ST 1
#define ADD 2

// Functions definitions, so that the program will work properly
void print_real_time_info();
void update_tlb(int *physical_address, int *virtual_address, int *data_start, int i, int j, int k, int l);
void scheduler();
void *scheduler_timer();
void loader();
void load_tlb(struct TLBe *tlb, int pid, int vc_address, int freeBlock, int pageCounter);
void *loader_timer();
void pushQueueProcess(PCB *pcb, int priority);
PCB *pullQueueProcess(int priority);
int contarLineas(char *filename);

// <--- Program generator --->
struct program_config conf;
unsigned int user_highest;
unsigned int user_space;

// <--- Machine structure --->
machine *Machine;
int cpus, cores, threads;

// <--- Physycal Memory --->
struct PM PhyM;

// <--- RTC Structure --->
struct RTC realTimeClass;
int PID=1;

// <--- Real-time executing threads --->
int threads_executing=0;

// <--- Timers for Loader and Scheduler (number of clocks) --->
int loader_c;
int scheduler_c;

// <--- Real-time program that's being read --->
int reading_program;

// <--- Timers manager --->
int end_timers=0;

// <--- Real-time number of clocks running --->
int actual_clock = 0;

// <--- Atomic access control structures (mutex) --->
pthread_cond_t next_thread;
pthread_cond_t broadcast;
pthread_mutex_t machine_clock;
pthread_mutex_t locker;
int flag=0;

void *clck() {
    int i, j, k, l;
    int binary, block, hpid, op, reg1, reg2, reg3, data, data_start, physical_address, virtual_address;
    while (1) {
        pthread_mutex_lock( &machine_clock );
        actual_clock++;
        printf("Clock %d\n", actual_clock);
        while(end_timers<2){
            pthread_cond_wait( &next_thread, &machine_clock );
        }
        pthread_mutex_lock( &locker );
        for (i = 0; i < cpus; i++) {
            for (j = 0; j < cores; j++) {
                for (k = 0; k < threads; k++) {
                    if (Machine->cpu[i].core[j].threads[k].running_process->pid != 0) {
                        if(Machine->cpu[i].core[j].threads[k].running_process->remQuantum == 0){
                            Machine->cpu[i].core[j].threads[k].running_process->remQuantum = 10;
                            Machine->cpu[i].core[j].threads[k].running_process->PC = Machine->cpu[i].core[j].threads[k].PC;
                            Machine->cpu[i].core[j].threads[k].running_process->REG16 = Machine->cpu[i].core[j].threads[k].REG16;
                            pushQueueProcess(Machine->cpu[i].core[j].threads[k].running_process, Machine->cpu[i].core[j].threads[k].running_process->priority);
                            PCB *pcb = (PCB*)malloc(sizeof(struct PCB));
                            pcb->pid = 0;
                            pcb->PC = 0;
                            pcb->priority = 0;
                            Machine->cpu[i].core[j].threads[k].running_process = pcb;
                            Machine->cpu[i].core[j].threads[k].PC = 0;
                            threads_executing--;
                        }else{
                            physical_address=-1;
                            // Look at TLB
                            block = (Machine->cpu[i].core[j].threads[k].PC / 64) * 64;
                            hpid = Machine->cpu[i].core[j].threads[k].running_process->pid;
                            for (l = 0; l < 6 ; ++l) {
                                if(Machine->cpu[i].core[j].threads[k].TLBA[l].virt == block && Machine->cpu[i].core[j].threads[k].TLBA[l].PID == hpid){
                                    physical_address = Machine->cpu[i].core[j].threads[k].TLBA[l].phys + Machine->cpu[i].core[j].threads[k].PC;
                                    break;
                                }
                            }
                            // If not in TLB, look at PTBR
                            if(physical_address == -1){
                                for(l = 0; l<Machine->cpu[i].core[j].threads[k].running_process->TLBentries; ++l){
                                    // Virtual address and block coincide
                                    if(Machine->cpu[i].core[j].threads[k].running_process->mm->pgb[l].virt == block){
                                        int TLBAindex = Machine->cpu[i].core[j].threads[k].indexTLB;
                                        Machine->cpu[i].core[j].threads[k].TLBA[TLBAindex] = Machine->cpu[i].core[j].threads[k].running_process->mm->pgb[l];
                                        Machine->cpu[i].core[j].threads[k].indexTLB++;
                                        if(Machine->cpu[i].core[j].threads[k].indexTLB == 6){
                                            Machine->cpu[i].core[j].threads[k].indexTLB = 0;
                                        }
                                        physical_address=Machine->cpu[i].core[j].threads[k].TLBA[l].phys + Machine->cpu[i].core[j].threads[k].PC;
                                        break;
                                    }
                                }
                            }
                            // Getting the instruction from the physical address, then process it
                            binary = PhyM.M[physical_address];
                            op = (binary >> 28) & 0x0F;
                            reg1 = (binary >> 24) & 0x0F;
                            reg2 = (binary >> 20) & 0x0F;
                            reg3 = (binary >> 16) & 0x0F;
                            virtual_address = binary & 0x00FFFFFF;
                            Machine->cpu[i].core[j].threads[k].RI=op;
                            if(op == LD){
                                // Look for the virtual_address inside the TLB
                                physical_address    = -1;
                                data_start          = Machine->cpu[i].core[j].threads[k].running_process->mm->data;
                                block               = (((virtual_address - data_start)/4)/64)*64;
                                block               += data_start;
                                hpid                = Machine->cpu[i].core[j].threads[k].running_process->pid;
                                for (l = 0; l < 6 ; ++l) {
                                    // Virtual address and block coincide and the process is the same
                                    if(Machine->cpu[i].core[j].threads[k].TLBA[l].virt == block && Machine->cpu[i].core[j].threads[k].TLBA[l].PID == hpid){
                                        physical_address = Machine->cpu[i].core[j].threads[k].TLBA[l].phys + ((virtual_address/4)-(data_start/4));
                                        break;
                                    }
                                }
                                // If not in TLB, look at PTBR
                                if(physical_address == -1){
                                    for(l = 0; l < Machine->cpu[i].core[j].threads[k].running_process->TLBentries; ++l){
                                        // Virtual address and block coincide
                                        if(Machine->cpu[i].core[j].threads[k].running_process->mm->pgb[l].virt == block){
                                            update_tlb(&physical_address, &virtual_address, &data_start, i, j, k, l);
                                            break;
                                        }
                                    }
                                }
                                data = PhyM.M[physical_address];
                                Machine->cpu[i].core[j].threads[k].REG16[reg1] = data;
                            }else if(op == ST){
                                physical_address    = -1;
                                data_start          = Machine->cpu[i].core[j].threads[k].running_process->mm->data;
                                block               = (((virtual_address - data_start)/4)/64)*64;
                                block               += data_start;
                                hpid                = Machine->cpu[i].core[j].threads[k].running_process->pid;
                                for (l = 0; l <6 ; ++l) {
                                    // Virtual address and block coincide and the process is the same
                                    if(Machine->cpu[i].core[j].threads[k].TLBA[l].virt == block && Machine->cpu[i].core[j].threads[k].TLBA[l].PID == hpid){
                                        physical_address = Machine->cpu[i].core[j].threads[k].TLBA[l].phys + ((virtual_address/4)-(data_start/4));
                                        break;
                                    }
                                }
                                // If not in TLB, look at PTBR
                                if(physical_address==-1){
                                    for(l = 0; l < Machine->cpu[i].core[j].threads[k].running_process->TLBentries; ++l){
                                        // Virtual address and block coincide
                                        if(Machine->cpu[i].core[j].threads[k].running_process->mm->pgb[l].virt == block){
                                            update_tlb(&physical_address, &virtual_address, &data_start, i, j, k, l);
                                            break;
                                        }
                                    }
                                }
                                PhyM.M[physical_address] = Machine->cpu[i].core[j].threads[k].REG16[reg1];
                            }else if(op == ADD){
                                Machine->cpu[i].core[j].threads[k].REG16[reg1] = Machine->cpu[i].core[j].threads[k].REG16[reg2] + Machine->cpu[i].core[j].threads[k].REG16[reg3];
                            }else{
                                // EXIT
                                for(l=0; l<Machine->cpu[i].core[j].threads[k].running_process->TLBentries; l++){
                                    PhyM.FF[Machine->cpu[i].core[j].threads[k].running_process->mm->pgb[l].phys]=0;
                                    PhyM.FB++;
                                    PCB *pcb=(PCB*)malloc(sizeof(struct PCB));
                                    pcb->pid = 0;
                                    pcb->PC = 0;
                                    pcb->priority = 0;
                                    Machine->cpu[i].core[j].threads[k].PC = 0;
                                    Machine->cpu[i].core[j].threads[k].running_process = pcb;
                                }
                            }
                        }
                        if(op!=15){
                            Machine->cpu[i].core[j].threads[k].PC++;
                            Machine->cpu[i].core[j].threads[k].running_process->PC++;
                            Machine->cpu[i].core[j].threads[k].running_process->remQuantum--;
                        }
                    }
                }
            }
        }
        print_real_time_info();
        pthread_mutex_unlock( &locker );
        end_timers=0;
        sleep(1);
        pthread_cond_broadcast( &broadcast );
        pthread_mutex_unlock( &machine_clock );
    }
}

void update_tlb(int *physical_address, int *virtual_address, int *data_start, int i, int j, int k, int l) {
    int TLBAindex = Machine->cpu[i].core[j].threads[k].indexTLB;
    Machine->cpu[i].core[j].threads[k].TLBA[TLBAindex] = Machine->cpu[i].core[j].threads[k].running_process->mm->pgb[l];
    Machine->cpu[i].core[j].threads[k].indexTLB++;
    if(Machine->cpu[i].core[j].threads[k].indexTLB == 6){
        Machine->cpu[i].core[j].threads[k].indexTLB = 0;
    }
    *physical_address = Machine->cpu[i].core[j].threads[k].TLBA[l].phys + ((*virtual_address/4)-(*data_start/4));
}

void loader(){
    int i, j, code_start, contBlock, codeBlocks, sumContBlock, dataBlocks, blocks, binary, data_start, dataLines, number_of_lines, vc_address, pageCounter, freeBlock, priority;
    double div;
    char line[80];
    char *fileName;
    struct TLBe tlb;
    struct PCB *nullPCB;
    FILE *fd;

    struct TLBe *tlbs = malloc(sizeof(struct TLBe) * NUMBLOCK );
    nullPCB = (PCB*)malloc(sizeof(struct PCB));
    nullPCB->mm = malloc(sizeof(struct mm));
    nullPCB->mm->pgb = tlbs;
    nullPCB->pid = PID;
    nullPCB->quantum = 10;
    nullPCB->REG16 = (int*)malloc(sizeof(int)*16);
    nullPCB->PC=0;
    priority = rand() % 99;
    if (priority == 0){
        priority=1;
    }
    nullPCB->priority = priority;
    PID++;

    // Check file name format

    int rp = reading_program;
    int length = snprintf( NULL, 0, "%d", rp );
    if(rp < 10){
        fileName = malloc( length + 11 );
        snprintf( fileName, length + 11, "prog00%d.elf", rp );
    }else if(rp > 9){
        fileName = malloc( length + 10 );
        snprintf( fileName, length + 10, "prog0%d.elf", rp );
    }else{
        fileName = malloc( length + 9 );
        snprintf( fileName, length + 9, "prog%d.elf", rp );
    }

    // Ger number of instructions of the program

    char c;
    int noInstr = 1;
    FILE *fp = fopen(fileName, "r");
    for(c = getc(fp); c != EOF; c = getc(fp)){
        if(c == '\n'){
            noInstr++;
        }
    }
    fclose(fp);
    noInstr -= 2;

    fd = fopen(fileName, "r");
    fscanf(fd,"%s %X", line, &code_start);
    fscanf(fd,"%s %X", line, &data_start);
    nullPCB->mm->code = code_start;
    nullPCB->mm->data = data_start;

    // Number of code blocks
    number_of_lines = (data_start - code_start) >> 2;
    div = (double)number_of_lines / (double)64;
    codeBlocks = (int)ceil(div);
    if (codeBlocks<0){
        codeBlocks=1;
    }

    // Number of data blocks
    dataLines = noInstr - number_of_lines;
    div = (double)dataLines / (double)64;
    dataBlocks = (int)ceil(div);
    if (dataBlocks<0){
        dataBlocks = 1;
    }

    blocks = dataBlocks + codeBlocks;
    if(PhyM.FB > blocks && realTimeClass.pQ[nullPCB->priority]->count < MAX){
        vc_address = 0;
        freeBlock = 0;
        pageCounter = 0;
        contBlock = 0;
        for(i = 0; i < number_of_lines; i++) {
            fscanf(fd, "%08X", &binary);
            if (pageCounter == 0) {
                for (j = freeBlock; j < NUMBLOCK; j++) {
                    if (PhyM.FF[j] == 0) { // First-Fit
                        freeBlock = j;
                        load_tlb(&tlb, nullPCB->pid, vc_address, freeBlock, pageCounter);
                        PhyM.M[tlb.phys] = binary;
                        nullPCB->mm->pgb[contBlock] = tlb;
                        break;
                    }
                }

            } else {
                for (j = freeBlock; j < NUMBLOCK; j++) {
                    if (PhyM.FF[j] == 0) { // First fit
                        freeBlock = j;
                        PhyM.M[freeBlock * 64 + pageCounter] = binary;
                        break;
                    }
                }
            }
            pageCounter++;
            vc_address += 4;
            if (pageCounter == 64) { // End of block
                pageCounter = 0;
                sumContBlock++;
                contBlock++;
                PhyM.FF[j] = 1;
            }
        }

        // Marked as complete if not at the end
        if(pageCounter != 0){ 
            pageCounter = 0;
            contBlock++;
            sumContBlock++;
            PhyM.FF[j] = 1;
            freeBlock = 0;
        }

        // Load data in memory
        while (fscanf(fd, "%8X", &binary) != EOF) {
            if(pageCounter == 0){
                for(j = freeBlock; j < NUMBLOCK; j++) {
                    if(PhyM.FF[j] == 0){
                        freeBlock = j;
                        load_tlb(&tlb, nullPCB->pid, vc_address, freeBlock, pageCounter);
                        PhyM.M[tlb.phys] = binary;
                        nullPCB->mm->pgb[contBlock] = tlb;
                        break;
                    }
                }

                

            }else{
                for(j = freeBlock; j < NUMBLOCK; j++) {
                    if(PhyM.FF[j] == 0){ // First fit
                        freeBlock = j;
                        int index = freeBlock * 64 + pageCounter;
                        PhyM.M[index] = binary;
                        break;
                    }
                }
            }
            vc_address += 4;
            pageCounter++;
            if(pageCounter == 64){
                pageCounter = 0;
                contBlock++;
                sumContBlock++;
                PhyM.FF[j] = 1;
            }
        }

        if(pageCounter != 0){
            sumContBlock++;
            PhyM.FF[j] = 1;
        }
        PhyM.FB -= sumContBlock;
        nullPCB->TLBentries = sumContBlock;
        pushQueueProcess(nullPCB, nullPCB->priority);
        reading_program++;
    }else{
        PID--;
    }

    fclose(fd);
}

void load_tlb(struct TLBe *tlb, int pid, int vc_address, int freeBlock, int pageCounter) {
    tlb->PID = pid;
    tlb->virt = vc_address;
    tlb->phys = freeBlock * 64 + pageCounter;
}

void *loader_timer(){
    int time = loader_c;
    int cycles = 0; 
    pthread_mutex_lock(&machine_clock);
    while(1){
        end_timers++;
        cycles++;
        if(cycles == time){
            pthread_mutex_lock( &locker );
            loader();
            pthread_mutex_unlock( &locker );
            cycles = 0;
        }
        pthread_cond_signal(&next_thread);
        pthread_cond_wait(&broadcast, &machine_clock);
    }
}

void scheduler(){
    int i, j, k, l;
    int index = 0;
    if(realTimeClass.count > 0){
        for(i = 0; i < cpus; i++){
            for(j = 0; j < cores; j++){
                for(k = 0; k < threads; k++){
                    if(Machine->cpu[i].core[j].threads[k].running_process->pid == 0){
                        for (l = index; l < 100; l++) {
                            if (realTimeClass.bitmap[l] == 1) {
                                PCB *process = pullQueueProcess(l);
                                process->remQuantum = process->quantum;
                                Machine->cpu[i].core[j].threads[k].running_process = process;
                                // Recover machine's state
                                Machine->cpu[i].core[j].threads[k].PC = process->PC;
                                Machine->cpu[i].core[j].threads[k].REG16 = process->REG16;
                                Machine->cpu[i].core[j].threads[k].PTBR = process->mm->pgb;
                                threads_executing++;
                                index = l;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

void *scheduler_timer(){
    int time = scheduler_c;
    int cycles = 0; 
    pthread_mutex_lock(&machine_clock);
    while(1){
        end_timers++;
        cycles++;
        if(cycles == time){
            pthread_mutex_lock( &locker );
            scheduler();
            pthread_mutex_unlock( &locker );
            cycles = 0;
        }
        pthread_cond_signal(&next_thread);
        pthread_cond_wait(&broadcast, &machine_clock);
    }
}

void print_real_time_info(){
    int i,j,k;
    char lines[1024];

    strcpy(lines,"-------");
    printf("        ");
    for (i = 0; i < cpus; i++)
    {
        printf("|     CPU %02d                 ",i);
        strcat(lines,"-----------------------------");
    }
    printf("|\n%s-\n",lines);
    printf("        ");

    for (i = 0; i < cpus; i++)
    {
        printf("| PID    PC       Priority   ",i);
    }
    printf("|\n%s-\n",lines);

    strcpy(lines, "---------------------------------------------------------------");
    for(j = 0; j < cores; j++){
        for(k = 0; k < threads; k++){
            printf("Core %02d |",j);
            for(i = 0; i < cpus; i++){
                PCB *running = Machine->cpu[i].core[j].threads[k].running_process;
                printf(" %02d ", running->pid);
                printf(" %d ", running->PC);
                printf(" %d ", running->priority);
                printf("|");
            }
            printf("\n");
        }
        printf("%s-\n",lines);
    }
    printf("\n");
}

int contarLineas(char *filename){
    char c;
    int count = 1;
    FILE *fp = fopen(filename, "r");
    for(c = getc(fp); c != EOF; c = getc(fp)){
        if(c == '\n'){
            count++;
        }
    }
    fclose(fp);
    count -= 2;
    return count;
}

PCB *pullQueueProcess(int priority){
    PCB *pcb = realTimeClass.pQ[priority]->head;
    if(realTimeClass.pQ[priority]->count==1){
        realTimeClass.pQ[priority]->head=NULL;
        realTimeClass.pQ[priority]->tail=NULL;
        realTimeClass.bitmap[priority]=0;
    }else{
        realTimeClass.pQ[priority]->head= realTimeClass.pQ[priority]->head->next;
    }
    realTimeClass.pQ[priority]->count--;
    realTimeClass.count--;
    return(pcb);
}

void pushQueueProcess(PCB *pcb, int priority){

    if(realTimeClass.pQ[priority]->count==0){
        realTimeClass.pQ[priority]->head = pcb;
        realTimeClass.bitmap[priority]=1;
    }else{
        realTimeClass.pQ[priority]->tail->next = pcb;
    }
    realTimeClass.pQ[priority]->tail = pcb;
    realTimeClass.pQ[priority]->count++;
    realTimeClass.count++;
}

int main(int argc, char *argv[]) {

    int i, j, k,max;
    if(argc!=6){
        printf("Usage of the program: ./machine <n_of_CPUs> <n_of_cores> <n_of_threads> <loader_clocks> <scheduler_clocks>\n");
        exit(1);
    }
    char *p;
    long converter = strtol(argv[1], &p, 10);
    cpus= (int) converter;
    converter = strtol(argv[2], &p, 10);
    cores = (int) converter;
    converter = strtol(argv[3], &p, 10);
    threads = (int) converter;
    converter = strtol(argv[5], &p, 10);
    loader_c = (int) converter;
    converter = strtol(argv[6], &p, 10);
    scheduler_c = (int) converter;

    // Physical memory initialization

    PhyM.FF = malloc(sizeof(int) * NUMBLOCK);
    PhyM.FB = NUMBLOCK;
    PhyM.M=(int*) malloc(sizeof(int)*MAXMEM);

    // Machine structure initialization

    reading_program = 0; // Actual reading program initialized to 0
    Machine = (machine *) malloc(sizeof(machine));
    Machine->cpu = (CPU *) malloc(cpus * sizeof(CPU));

    for (i = 0; i < cpus; i++) {
        struct CPU cpu;
        Machine->cpu[i] = cpu;
        Machine->cpu[i].core = (core *) malloc(cores * sizeof(core));
        for (j = 0; j < cores; j++) {
            struct core core;
            Machine->cpu[i].core[j] = core;
            Machine->cpu[i].core[j].threads = (thread *) malloc(threads * sizeof(thread));
            for (k = 0; k < threads; k++) {
                PCB *pcb=(PCB*)malloc(sizeof(struct PCB));
                pcb->pid=0;
                struct thread thread;
                thread.running_process = pcb;
                thread.indexTLB = 0;
                thread.REG16= (int*)malloc(sizeof(int)*16);
                thread.TLBA= (TLBe *) malloc(sizeof(struct TLBe) * 6); //6 TLB entries
                Machine->cpu[i].core[j].threads[k] = thread;
            }
        }
    }

    // Real Time Class Queue
    realTimeClass.bitmap = (int*)malloc(100*sizeof(int));
    for (int l = 0; l < MAX; ++l) {
        struct PQ *procQ = malloc(sizeof(struct PQ));
        procQ->count = 0;
        procQ->max = 100;
        realTimeClass.bitmap[l] = 0;
        realTimeClass.pQ[l] = procQ;
    }
    realTimeClass.count = 0;

    // Control structures initialization
    pthread_cond_init(&next_thread, NULL);
    pthread_cond_init(&broadcast, NULL);
    pthread_mutex_init(&machine_clock,NULL);
    pthread_mutex_init(&locker, NULL);

    // Threads initialization
    pthread_t tid[3];
    pthread_create(&tid[0], NULL, clck, NULL);
    pthread_create(&tid[1], NULL, loader_timer,NULL);
    pthread_create(&tid[2], NULL, scheduler_timer,NULL);
    pthread_join(tid[0],NULL);
    pthread_join(tid[1],NULL);
    pthread_join(tid[2],NULL);
}