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

volatile sig_atomic_t interrupt_count = 0;

void sigint_handler(int signum) {
    interrupt_count++;
    
    if (interrupt_count == 1) {
        printf("\n[Handler] Caught SIGINT (1/3). Press Ctrl+C 2 more times to exit.\n");
    } else if (interrupt_count == 2) {
        printf("\n[Handler] Caught SIGINT (2/3). Press Ctrl+C 1 more time to exit.\n");
    } else {
        printf("\n[Handler] Caught SIGINT (3/3). Exiting now.\n");
        exit(0); // TR: Güvenli çıkış / EN: Clean exit
    }
}

int main() {

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

    printf("[Main] Process started (PID: %d). Waiting for signals...\n", getpid());
    printf("[Main] Press Ctrl+C to trigger the handler.\n");
    while (1) {
        // Sonsuz döngü
        sleep(5);
    }
    return 0;
}