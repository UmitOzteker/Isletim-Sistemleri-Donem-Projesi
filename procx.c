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
volatile sig_atomic_t running = 1; // Ana döngü kontrolü

void init_resources()
{
    FILE *fp = fopen("procx_mq_key", "a");
    if (fp)
        fclose(fp);
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

    sem_wait(sem);
    if (shared_data->process_count == 0 || shared_data->process_count > 50)
    {
        shared_data->process_count = 0; // İlk kez başlatıyoruz
        printf("[Init] Shared memory initialized.\n");
    }
    else
    {
        printf("[Init] Shared memory already exists with %d processes.\n", shared_data->process_count);
    }
    sem_post(sem);
}

void cleanup_resources() // Kaynakları temizle
{
    if (sem != NULL && sem != SEM_FAILED)
    {
        sem_wait(sem);
        for (int i = 0; i < shared_data->process_count; i++)
        {
            if (shared_data->processes[i].is_active &&
                shared_data->processes[i].mode == ATTACHED &&
                shared_data->processes[i].owner_pid == getpid())
            {
                kill(shared_data->processes[i].pid, SIGTERM); // Aktif processleri sonlandır

                shared_data->processes[i].is_active = 0;
                shared_data->processes[i].status = TERMINATED;
            }
        }
        sem_post(sem);
    }
    // Semaphore kilitle

    shmctl(shmid, IPC_RMID, NULL); // Paylaşılan belleği kaldır
    msgctl(msqid, IPC_RMID, NULL); // Mesaj kuyruğunu kaldır
    sem_close(sem);                // Semaphore'u kapat,
    sem_unlink(SEM_NAME);          // Semaphore'u kaldır

    printf("[Cleanup] Resources cleaned up successfully.\n");
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
    int status;
    pid_t result;
    printf("[Monitor] Monitor thread started (PID: %d)\n", getpid());
    while (running)
    {
        while ((result = waitpid(-1, &status, WNOHANG)) > 0) // Bitmiş child processları beklemeden kontrol et
        {
            sem_wait(sem);
            int found = 0;
            for (int i = 0; i < shared_data->process_count; i++)
            {
                if (shared_data->processes[i].pid == result) // Eşleşen process bulundu
                {
                    shared_data->processes[i].status = TERMINATED;
                    shared_data->processes[i].is_active = 0;
                    found = 1;
                    printf("[Monitor] Process %d has terminated. Updated shared memory.\n", result);
                    break;
                }
            }
            sem_post(sem);

            Message msg;
            msg.command = CMD_TERMINATE;
            msg.msg_type = 1;
            msg.sender_pid = getpid();
            msg.target_pid = result;

            msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), IPC_NOWAIT); // Terminate mesajı gönder
        }
        sleep(2); // 2 saniye bekle
    }
    return NULL;
}

void *ipc_listener_thread(void *arg)
{ // IPC dinleyici iş parçacığı
    Message msg;
    printf("[IPC Listener] IPC listener thread started (PID: %d)\n", getpid());

    while (running)
    {
        // Mesaj gelene kadar burada bekler, işlemci harcamaz.
        if (msgrcv(msqid, &msg, sizeof(Message) - sizeof(long), 0, 0) != -1)
        {
            if (msg.sender_pid == getpid())
            {
                continue; // Kendi mesajlarımızı yoksay
            }

            if (msg.command == CMD_START) // START komutu
            {
                printf("\n[IPC Listener] Received START command from PID %d\n", msg.sender_pid);
            }
            else if (msg.command == CMD_TERMINATE) // TERMINATE komutu
            {
                printf("\n[IPC Listener] Received TERMINATE command for PID %d from PID %d\n", 
                       msg.target_pid, msg.sender_pid);

                // Fiziksel Öldürme
                if (kill(msg.target_pid, SIGTERM) == 0)
                {
                    printf("[IPC Listener] Successfully sent SIGTERM to PID %d\n", msg.target_pid);

                    sem_wait(sem); // Kilitle

                    int found = 0;
                    for (int i = 0; i < shared_data->process_count; i++)
                    {
                        if (shared_data->processes[i].pid == msg.target_pid)
                        {
                            shared_data->processes[i].status = TERMINATED;
                            shared_data->processes[i].is_active = 0;
                            found = 1;
                            break;
                        }
                    }

                    sem_post(sem); // Kilidi aç

                    if (found)
                        printf("[IPC Listener] Process %d terminated and updated via IPC.\n", msg.target_pid);
                    else
                        printf("[IPC Listener] Process %d not found in shared memory.\n", msg.target_pid);
                }
                else
                {
                    perror("[IPC Listener] Failed to send termination signal");
                }
            }
            else
            {
                printf("\n[IPC Listener] Unknown command received: %d\n", msg.command);
            }
        }
        else
        {
            // Mesaj kuyruğu silinmişse veya ciddi hata varsa döngüden çık
            if (errno == EIDRM || errno == EINVAL)
            {
                printf("[IPC Listener] Message queue removed or invalid. Exiting thread.\n");
                break;
            }
            else if (errno != EINTR && errno != ENOMSG)
            {
                perror("[IPC Listener] msgrcv failed");
                break;
            }
        }
    }
    
    printf("[IPC Listener] Thread terminating.\n");
    return NULL;
}

void start_process(char *command, int mode) // Yeni process başlat
{
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("Fork failed");
        return;
    }
    else if (pid == 0)
    { // Child process

        if (mode == DETACHED)
        {
            if (setsid() < 0)
            { // Terminalden kopar
                perror("setsid failed");
                exit(1);
            }
        }

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
        shared_data->processes[idx].pid = pid;                      // Process bilgilerini kaydet
        shared_data->processes[idx].owner_pid = getpid();           // Başlatan PID
        strncpy(shared_data->processes[idx].command, command, 255); // Komut
        shared_data->processes[idx].mode = mode;                    // Mod
        shared_data->processes[idx].status = RUNNING;               // Durum
        shared_data->processes[idx].start_time = time(NULL);        // Başlangıç zamanı
        shared_data->processes[idx].is_active = 1;                  // Aktif

        shared_data->process_count++;
        sem_post(sem); // Semaphore aç
        printf("[Main] Started process (PID: %d) in %s mode\n",
               pid, mode == ATTACHED ? "ATTACHED" : "DETACHED");

        Message msg;
        msg.msg_type = 1;
        msg.command = CMD_START;
        msg.sender_pid = getpid();
        msg.target_pid = pid;

        msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), 0); // Başlatma mesajı gönder

        if(mode == ATTACHED){
            int status;
            waitpid(pid, &status, 0); // Attached modda bekle

            printf("[Main] Attached process (PID: %d) has terminated.\n", pid);

            sem_wait(sem); // Semaphore kilitle
            for (int i = 0; i < shared_data->process_count; i++){
                if(shared_data[i].processes[i].pid == pid){
                    shared_data->processes[i].status = TERMINATED;
                    shared_data->processes[i].is_active = 0;
                    break;
                }
            }
            sem_post(sem); // Semaphore aç

            msg.command = CMD_TERMINATE; // TERMINATE komutu
            msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), 0); // Terminate mesajı gönder
        }
    }
}

void display_menu() // Menü göster
{
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
}

void handle_start_process() // Yeni program başlat
{
    char command[256];
    int mode;

    printf("Enter command to execute: ");
    fgets(command, sizeof(command), stdin);
    command[strcspn(command, "\n")] = 0; // Newline kaldır

    printf("Mode (0=ATTACHED, 1=DETACHED): ");
    scanf("%d", &mode);

    start_process(command, mode);
}

void handle_list_process() // Çalışan programları listele
{
    printf("Listing running programs...\n"); // Çalışan programları listele
    sem_wait(sem);                           // Semaphore kilitle
    printf("\n");
    printf("╔═════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                            ÇALIŞAN PROGRAMLAR                           ║\n");
    printf("╠═════════╤══════════════════════╤══════════╤═════════╤═══════════════╣\n");
    printf("║ PID     │ Command              │ Mode     │ Owner   │ Süre          ║\n");
    printf("╠═════════╪══════════════════════╪══════════╪═════════╪═══════════════╣\n");
    for (int i = 0; i < shared_data->process_count; i++)
    {
        if (shared_data->processes[i].is_active)
        {
            time_t now = time(NULL);                                               // Şu anki zaman
            double duration = difftime(now, shared_data->processes[i].start_time); // Süre hesapla
            printf("║ %-7d │ %-20.20s │ %-8s │ %-7d │ %-9.0f sn ║\n",
                   shared_data->processes[i].pid,
                   shared_data->processes[i].command,
                   (shared_data->processes[i].mode == ATTACHED) ? "Attached" : "Detached",
                   shared_data->processes[i].owner_pid,
                   duration);
        }
    }
    printf("╚═════════╧══════════════════════╧══════════╧═════════╧═══════════════╝\n");
    sem_post(sem); // Semaphore aç
}

void handle_terminate_process() // Program sonlandır
{
    pid_t target_pid;
    printf("Enter PID of program to terminate: "); // Program sonlandır
    scanf("%d", &target_pid);
    if (kill(target_pid, SIGTERM) == 0)
    {
        printf("Sent termination signal to PID %d\n", target_pid);
        sem_wait(sem); // Semaphore kilitle
        int found = 0;
        for (int i = 0; i < shared_data->process_count; i++) // Paylaşılan bellekte ara
        {
            if (shared_data->processes[i].pid == target_pid)
            {
                shared_data->processes[i].status = TERMINATED; // Durumu güncelle
                shared_data->processes[i].is_active = 0;       // Aktif değil olarak işaretle

                found = 1;
                break;
            }
        }
        sem_post(sem); // Semaphore aç
        if (found)
        {
            printf("Process %d marked as terminated in shared memory.\n", target_pid);
        }
        else
        {
            printf("Process %d not found in shared memory.\n", target_pid);
        }
    }
    else
    {
        perror("Failed to send termination signal");
    }
}

void setup_signal_handlers() // Sinyal işleyicilerini ayarla
{
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("Error setting up sigaction");
        exit(1);
    }
}

void start_threads(pthread_t *monitor_tid, pthread_t *ipc_listener_tid) // İş parçacıklarını başlat
{
    pthread_create(monitor_tid, NULL, monitor_thread, NULL);           // İzleme iş parçacığını başlat
    pthread_create(ipc_listener_tid, NULL, ipc_listener_thread, NULL); // IPC dinleyici iş parçacığını başlat
}

int main() // Ana fonksiyon
{

    pthread_t monitor_tid, ipc_listener_tid;
    int choice;

    setup_signal_handlers(); // Sinyal işleyicilerini ayarla

    init_resources(); // Kaynakları başlat

    start_threads(&monitor_tid, &ipc_listener_tid);

    printf("[Main] Process started (PID: %d). Waiting for signals...\n", getpid());
    printf("[Main] Press Ctrl+C to trigger the handler.\n");

    while (1)
    {                   // Ana menü döngüsü
        display_menu(); // Menü göster

        scanf("%d", &choice); // Kullanıcıdan seçim al
        while (getchar() != '\n')
            ; // Giriş tamponunu temizle

        switch (choice) // Seçime göre işlem yap
        {
        case 1:
        {
            handle_start_process(); // Yeni process başlat
            break;
        }
        case 2:
            handle_list_process(); // Çalışan processleri listele
            break;
        case 3:
        {
            handle_terminate_process(); // Process sonlandır
            break;
        }
        case 0:
            running = 0; // Döngüyü durdur       
            cleanup_resources(); // Kaynakları temizle
            pthread_cancel(monitor_tid); // İzleme iş parçacığını iptal et
            pthread_cancel(ipc_listener_tid); // IPC dinleyici iş parçacığını iptal
            pthread_join(monitor_tid, NULL); // İzleme iş parçacığını bekle
            pthread_join(ipc_listener_tid, NULL); // IPC dinleyici iş parçacığını bekle
            printf("Exiting ProcX. Goodbye!\n");
            exit(0);
        default:
            printf("Invalid choice. Please try again.\n");
        }
    }
    return 0;
}