#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>    // fork, execvp, sleep
#include <fcntl.h>     // O_CREAT, O_EXCL, O_RDWR
#include <sys/mman.h>  // shm_open, mmap, shm_unlink, munmap
#include <sys/stat.h>  // 0666
#include <semaphore.h> // sem_open, sem_wait, sem_post, sem_close, sem_unlink
#include <sys/msg.h>   // msgget, msgsnd, msgrcv
#include <sys/types.h> // pid_t, key_t
#include <errno.h>     // error handling
#include <time.h>      // time
#include <signal.h>    // kill, SIGTERM
#include <pthread.h>   // pthread_create, pthread_join
#include <sys/wait.h>  // waitpid
#include <sys/ipc.h>   // ftok, shmget, shmat, shmctl
#include <sys/shm.h>   // shmget, shmat, shmctl

// --- ENUM VE SABITLER ---

// Proje için gerekli dosya isimleri ve anahtarlar
#define SHM_NAME "/procx_shm"      // Shared Memory Adı (POSIX standardı) [cite: 1925]
#define SEM_NAME "/procx_sem"      // Semaphore Adı [cite: 1925]
#define MQ_KEY_FILE "procx_mq_key" // Message Queue ftok dosyası (Lab 8 mantığı) [cite: 1116]
#define PROJ_ID 65                 // ftok proje ID

// ProcessMode Tanımı (Attached/Detached)
typedef enum
{
    ATTACHED = 0,
    DETACHED = 1
} ProcessMode;

// ProcessStatus Tanımı (Running/Terminated)
typedef enum
{
    RUNNING = 0,
    TERMINATED = 1
} ProcessStatus;

// Mesaj Komutları
#define CMD_START 1
#define CMD_TERMINATE 2
// Process bilgisi
typedef struct
{
    pid_t pid;            // Process ID
    pid_t owner_pid;      // Başlatan instance'ın PID'si
    char command[256];    // Çalıştırılan komut
    ProcessMode mode;     // Attached (0) veya Detached (1)
    ProcessStatus status; // Running (0) veya Terminated (1)
    time_t start_time;    // Başlangıç zamanı
    int is_active;        // Aktif mi? (1: Evet, 0: Hayır)
} ProcessInfo;
// Paylaşılan bellek yapısı
typedef struct
{
    ProcessInfo processes[50]; // Maksimum 50 process
    int process_count;         // Aktif process sayısı
} SharedData;
// Mesaj yapısı
typedef struct
{
    long msg_type;    // Mesaj tipi
    int command;      // Komut (START/TERMINATE)
    pid_t sender_pid; // Gönderen PID
    pid_t target_pid; // Hedef process PID
} Message;

volatile sig_atomic_t interrupt_count = 0; // SIGINT kesme sayacı

// Global değişkenler
int shm_fd;              // Shared Memory dosya tanıtıcısı
SharedData *shared_data; // Paylaşılan bellek işaretçisi
sem_t *sem;              // Semaphore işaretçisi
int msqid;               // Message Queue ID
int shmid;               // Shared Memory ID
key_t key;               // Message Queue anahtarı

void init_resources()
{
    key = ftok("procx_mq_key", 65); // Mesaj kuyruğu anahtarı oluştur

    if (key == -1)
    {
        perror("ftok failed");
        exit(1);
    }

    shmid = shmget(key, sizeof(SharedData), 0666 | IPC_CREAT); // Paylaşılan bellek oluştur

    if (shmid == -1)
    {
        perror("shmget failed");
        exit(1);
    }

    shared_data = (SharedData *)shmat(shmid, (void *)0, 0); // Paylaşılan belleği ata

    if (shared_data == (SharedData *)(-1))
    {
        perror("shmat failed");
        exit(1);
    }

    msqid = msgget(key, 0666 | IPC_CREAT); // Mesaj kuyruğu oluştur
    if (msqid == -1)
    {
        perror("msgget failed");
        exit(1);
    }

    sem = sem_open(SEM_NAME, O_CREAT, 0644, 1); // Semaphore oluştur
    if (sem == SEM_FAILED)
    {
        perror("sem_open failed");
        exit(1);
    }
}

void cleanup_resources()
{                                  // Kaynakları temizle
    shmctl(shmid, IPC_RMID, NULL); // Paylaşılan belleği kaldır
    msgctl(msqid, IPC_RMID, NULL); // Mesaj kuyruğunu kaldır
    sem_close(sem);                // Semaphore'u kapat,
    sem_unlink(SEM_NAME);          // Semaphore'u kaldır
}

void sigint_handler(int signum)
{ // SIGINT işleyici
    interrupt_count++;

    if (interrupt_count == 1)
    {
        printf("\n[Handler] Caught SIGINT (1/3). Press Ctrl+C 2 more times to exit.\n");
    }
    else if (interrupt_count == 2)
    {
        printf("\n[Handler] Caught SIGINT (2/3). Press Ctrl+C 1 more time to exit.\n");
    }
    else
    {
        printf("\n[Handler] Caught SIGINT (3/3). Exiting now.\n");
        cleanup_resources(); // Kaynakları temizle
        exit(0);
    }
}

void *monitor_thread(void *arg)
{ // İzleme iş parçacığı
    printf("[Monitor] Monitor thread started (PID: %d)\n", getpid());
    while (1)
    {
        sleep(5);
    }
    // TODO: Zombi processları temizle (waitpid)
    // TODO: Bitmiş processların status'ünü güncelle
}

void *ipc_listener_thread(void *arg)
{ // IPC dinleyici iş parçacığı
    printf("[IPC Listener] IPC listener thread started (PID: %d)\n", getpid());
    while (1)
    {
        sleep(5);
        // TODO: msgrcv() ile mesaj dinle
        // TODO: CMD_START/CMD_TERMINATE komutlarını işle
    }
}

void start_process(char *command, int mode)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("Fork failed");
        return;
    }
    else if (pid == 0)
    { // Child process
        char *args[10];
        int i = 0;
        char *token = strtok(command, " ");
        while (token != NULL && i < 9)
        {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;
        execvp(args[0], args);
        perror("Exec failed");
        exit(1);
    }
    else
    {                  // Parent process
        sem_wait(sem); // Semaphore kilitle
        int idx = shared_data->process_count;
        shared_data->processes[idx].pid = pid;
        shared_data->processes[idx].owner_pid = getpid();
        strncpy(shared_data->processes[idx].command, command, 255);
        shared_data->processes[idx].mode = mode;
        shared_data->processes[idx].status = RUNNING;
        shared_data->processes[idx].start_time = time(NULL);
        shared_data->processes[idx].is_active = 1;

        shared_data->process_count++;
        sem_post(sem); // Semaphore aç
        printf("[Main] Started process (PID: %d) in %s mode\n",
               pid, mode == ATTACHED ? "ATTACHED" : "DETACHED");

        if (mode == ATTACHED)
        {
            // Attached modda child process bitene kadar bekle
            int status;
            waitpid(pid, &status, 0);
            printf("[Main] Process %d finished\n", pid);
        }
    }
}

int main() // Ana fonksiyon
{ 

    struct sigaction sa;
    pthread_t monitor_tid, ipc_listener_tid;
    int choice;

    // TR: İşleyici fonksiyonumuzu ata
    sa.sa_handler = sigint_handler;

    // TR: Güvenli olması için tüm sinyal bayraklarını sıfırla
    sigemptyset(&sa.sa_mask);

    // TR: Ekstra bayrak yok
    sa.sa_flags = 0;

    // TR: SIGINT (Ctrl+C) sinyali için 'sa' ayarlarını kaydet.
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("Error setting up sigaction");
        return 1;
    }

    init_resources(); // Kaynakları başlat

    pthread_create(&monitor_tid, NULL, monitor_thread, NULL);           // İzleme iş parçacığını başlat
    pthread_create(&ipc_listener_tid, NULL, ipc_listener_thread, NULL); // IPC dinleyici iş parçacığını başlat

    printf("[Main] Process started (PID: %d). Waiting for signals...\n", getpid());
    printf("[Main] Press Ctrl+C to trigger the handler.\n");

    while (1)
    { // Ana menü döngüsü
        printf("\n╔════════════════════════════════════╗\n");
        printf("║                                    ║\n");
        printf("║           ProcX v1.0               ║\n");
        printf("║                                    ║\n");
        printf("╠════════════════════════════════════╣\n");
        printf("║ 1. Yeni Program Çalıştır           ║\n");
        printf("║ 2. Çalışan Programları Listele     ║\n");
        printf("║ 3. Program Sonlandır               ║\n");
        printf("║ 0. Çıkış                           ║\n");
        printf("╚════════════════════════════════════╝\n");

        printf("Seçiminiz: ");

        scanf("%d", &choice); // Kullanıcıdan seçim al
        while (getchar() != '\n' ); // Giriş tamponunu temizle
        
        switch (choice) // Seçime göre işlem yap
        { 
        case 1: {
            char command[256];
            int mode;

            printf("Enter command to execute: ");
            fgets(command, sizeof(command), stdin);
            command[strcspn(command, "\n")] = 0; // Newline kaldır

            printf("Mode (0=ATTACHED, 1=DETACHED): ");
            scanf("%d", &mode);

            start_process(command, mode);
            break;
        }
        case 2:
            printf("Listing running programs...\n"); // Çalışan programları listele
            // TODO: shared_data->processes[] döngüsü ekle
            // TODO: Aktif processları yazdır
            break;
        case 3:
            printf("Enter PID of program to terminate: "); // Program sonlandır
            // TODO: PID al, kill(pid, SIGTERM) gönder
            // TODO: ProcessInfo.status = TERMINATED yap
            break;
        case 0:
            cleanup_resources(); // Kaynakları temizle
            printf("Exiting ProcX. Goodbye!\n");
            exit(0);
        default:
            printf("Invalid choice. Please try again.\n");
        }
    }
    return 0;
}