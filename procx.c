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
#include <sys/ipc.h>    // ftok, shmget, shmat, shmctl
#include <sys/shm.h>   // shmget, shmat, shmctl

    // --- ENUM VE SABITLER ---

// Proje için gerekli dosya isimleri ve anahtarlar
#define SHM_NAME "/procx_shm"      // Shared Memory Adı (POSIX standardı) [cite: 1925]
#define SEM_NAME "/procx_sem"      // Semaphore Adı [cite: 1925]
#define MQ_KEY_FILE "procx_mq_key" // Message Queue ftok dosyası (Lab 8 mantığı) [cite: 1116]
#define PROJ_ID 65                 // ftok proje ID

// ProcessMode Tanımı (Attached/Detached)
typedef enum { 
    ATTACHED = 0, 
    DETACHED = 1 
} ProcessMode;

// ProcessStatus Tanımı (Running/Terminated)
typedef enum { 
    RUNNING = 0, 
    TERMINATED = 1 
} ProcessStatus;

// Mesaj Komutları
#define CMD_START 1
#define CMD_TERMINATE 2
// Process bilgisi
typedef struct {
 pid_t pid; // Process ID
 pid_t owner_pid; // Başlatan instance'ın PID'si
 char command[256]; // Çalıştırılan komut
 ProcessMode mode; // Attached (0) veya Detached (1)
 ProcessStatus status; // Running (0) veya Terminated (1)
 time_t start_time; // Başlangıç zamanı
 int is_active; // Aktif mi? (1: Evet, 0: Hayır)
} ProcessInfo;
// Paylaşılan bellek yapısı
typedef struct {
 ProcessInfo processes[50]; // Maksimum 50 process
 int process_count; // Aktif process sayısı
} SharedData;
// Mesaj yapısı
typedef struct {
 long msg_type; // Mesaj tipi
 int command; // Komut (START/TERMINATE)
 pid_t sender_pid; // Gönderen PID
 pid_t target_pid; // Hedef process PID
} Message;

volatile sig_atomic_t interrupt_count = 0; // SIGINT kesme sayacı

// Global değişkenler
int shm_fd; // Shared Memory dosya tanıtıcısı
SharedData *shared_data; // Paylaşılan bellek işaretçisi
sem_t *sem; // Semaphore işaretçisi
int msqid; // Message Queue ID
int shmid; // Shared Memory ID
key_t key; // Message Queue anahtarı

void init_resources() {
    key = ftok("procx_mq_key", 65); // Mesaj kuyruğu anahtarı oluştur

    if (key == -1) {
         perror("ftok failed");
         exit(1);
    }

    shmid = shmget(key, sizeof(SharedData), 0666 | IPC_CREAT); // Paylaşılan bellek oluştur

    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }

    shared_data = (SharedData *) shmat(shmid, (void*)0, 0); // Paylaşılan belleği ata

    if (shared_data == (SharedData *)(-1)) {
        perror("shmat failed");
        exit(1);
    }

    msqid = msgget(key, 0666 | IPC_CREAT); // Mesaj kuyruğu oluştur
    if (msqid == -1) {
        perror("msgget failed");
        exit(1);
    }
    
    sem = sem_open(SEM_NAME, O_CREAT, 0644, 1); // Semaphore oluştur
    if (sem == SEM_FAILED) {
        perror("sem_open failed");
        exit(1);
    }
}

void cleanup_resources() { // Kaynakları temizle
    shmctl(shmid, IPC_RMID, NULL); // Paylaşılan belleği kaldır
    msgctl(msqid, IPC_RMID, NULL); // Mesaj kuyruğunu kaldır
    sem_close(sem); // Semaphore'u kapat,
    sem_unlink(SEM_NAME); // Semaphore'u kaldır
}

void sigint_handler(int signum) { // SIGINT işleyici
    interrupt_count++;
    
    if (interrupt_count == 1) {
        printf("\n[Handler] Caught SIGINT (1/3). Press Ctrl+C 2 more times to exit.\n");
    } else if (interrupt_count == 2) {
        printf("\n[Handler] Caught SIGINT (2/3). Press Ctrl+C 1 more time to exit.\n");
    } else {
        printf("\n[Handler] Caught SIGINT (3/3). Exiting now.\n");
        cleanup_resources(); // Kaynakları temizle
        exit(0);
    }
}

int main() { // Ana fonksiyon

    struct sigaction sa;
    
    // TR: İşleyici fonksiyonumuzu ata
    sa.sa_handler = sigint_handler;
    
    // TR: Güvenli olması için tüm sinyal bayraklarını sıfırla
    sigemptyset(&sa.sa_mask);
    
    // TR: Ekstra bayrak yok
    sa.sa_flags = 0;

    // TR: SIGINT (Ctrl+C) sinyali için 'sa' ayarlarını kaydet.
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error setting up sigaction");
        return 1;
    }

    init_resources();

    printf("[Main] Process started (PID: %d). Waiting for signals...\n", getpid());
    printf("[Main] Press Ctrl+C to trigger the handler.\n");
    while (1) {
        // Sonsuz döngü
        sleep(5);
    }
    return 0;
}