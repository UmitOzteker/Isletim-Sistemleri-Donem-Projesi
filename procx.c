#define _POSIX_C_SOURCE 200809L // POSIX.1-2008 standardını etkinleştir
#define _DEFAULT_SOURCE         // usleep için gerekli
#include <stdio.h>     // printf, perror
#include <stdlib.h>    // exit, atoi
#include <string.h>    // memset, strncpy, strtok
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
#include <ctype.h>     // isspace fonksiyonu için gerekli

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
int shm_fd;                        // Shared Memory dosya tanıtıcısı
SharedData *shared_data;           // Paylaşılan bellek işaretçisi
sem_t *sem;                        // Semaphore işaretçisi
int msqid;                         // Message Queue ID
key_t key;                         // Message Queue anahtarı
volatile sig_atomic_t running = 1; // Ana döngü kontrolü

void init_resources() // Kaynakları başlat
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

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666); // Paylaşılan bellek oluştur
    if (shm_fd == -1)
    {
        perror("shm_open failed");
        exit(1);
    }

    int res = ftruncate(shm_fd, sizeof(SharedData)); // Paylaşılan bellek boyutunu ayarla
    if (res == -1)
    {
        perror("ftruncate failed");
        exit(1);
    }

    shared_data = mmap(0, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); // Paylaşılan belleği eşleştir
    if (shared_data == MAP_FAILED)
    {
        perror("mmap failed");
        exit(1);
    }

    close(shm_fd); // Dosya tanıtıcısını kapat

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
        sem_close(sem);       // Semaphore'u kapat
        sem_unlink(SEM_NAME); // Semaphore'u kaldır
    }
    // Semaphore kilitle

    munmap(shared_data, sizeof(SharedData)); // Paylaşılan belleği eşleştirmeyi kaldır
    shm_unlink(SHM_NAME);                    // Paylaşılan belleği kaldır
    msgctl(msqid, IPC_RMID, NULL);           // Mesaj kuyruğunu kaldır

    printf("[Cleanup] Resources cleaned up successfully.\n");
}

void sigint_handler(int signum)
{ // SIGINT işleyici
    (void)signum; // Unused parameter uyarısını bastır
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
{
    (void)arg;
    int status;
    pid_t result;
    // Başlangıç mesajını temizleme yapmadan basabiliriz, çünkü henüz menü çizilmedi.
    printf("[Monitor] Monitor thread started (PID: %d)\n", getpid());
    
    while (running)
    {
        sleep(2); // 2 saniye bekle

        for(int i = 0; i <= 50; i++){ 
            sem_wait(sem);
            if(shared_data->processes[i].is_active == 0){ 
                sem_post(sem);
                continue;
            } else {
                pid_t pid = shared_data->processes[i].pid;
                pid_t owner_pid = shared_data->processes[i].owner_pid;
                int is_dead = 0;
                
                if (owner_pid == getpid()){ 
                    if (waitpid(pid, &status, WNOHANG) == pid) is_dead = 1; 
                } else { 
                    if (kill(pid, 0) == -1 && errno == ESRCH) is_dead = 1; 
                }
                
                if(is_dead){ 
                    shared_data->processes[i].status = TERMINATED; 
                    shared_data->processes[i].is_active = 0;
                    
                    // --- DÜZELTME BURADA ---
                    // Satırı sil -> Mesajı yaz -> Menüyü geri getir
                    printf("\r\033[K[Monitor] Process %d terminated (Detected).\nSeçiminiz: ", pid);
                    fflush(stdout); // Çıktıyı hemen ekrana bas (önemli!)
                    // -----------------------

                    Message msg;
                    msg.command = CMD_TERMINATE;
                    msg.msg_type = 1;
                    msg.sender_pid = getpid();
                    msg.target_pid = pid;
                    msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), IPC_NOWAIT);
                }
                sem_post(sem);
            }
        }
        
        while ((result = waitpid(-1, &status, WNOHANG)) > 0) 
        {
            sem_wait(sem);
            for (int i = 0; i < shared_data->process_count; i++)
            {
                if (shared_data->processes[i].pid == result) 
                {
                    shared_data->processes[i].status = TERMINATED;
                    shared_data->processes[i].is_active = 0;
                    
                    // --- DÜZELTME BURADA ---
                    printf("\r\033[K[Monitor] Process %d has terminated. Updated shared memory.\nSeçiminiz: ", result);
                    fflush(stdout);
                    // -----------------------
                    break;
                }
            }
            sem_post(sem);

            Message msg;
            msg.command = CMD_TERMINATE;
            msg.msg_type = 1;
            msg.sender_pid = getpid();
            msg.target_pid = result;
            msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), IPC_NOWAIT); 
        }
    }
    return NULL;
}

void *ipc_listener_thread(void *arg)
{ // IPC dinleyici iş parçacığı
    (void)arg; // Unused parameter uyarısını bastır
    Message msg;
    printf("[IPC Listener] IPC listener thread started (PID: %d)\n", getpid());

    while (running)
    {
        // Mesaj gelene kadar burada bekler, işlemci harcamaz.
        if (msgrcv(msqid, &msg, sizeof(Message) - sizeof(long), 0, 0) != -1)
        {
            if (msg.sender_pid == getpid())
            {
                msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), 0);

                usleep(50000); 
                continue; 
            }

            if (msg.command == CMD_START) // START komutu
            {
                printf("\n[IPC] Process %d started by PID %d\n", msg.target_pid, msg.sender_pid);
            }
            else if (msg.command == CMD_TERMINATE) // TERMINATE komutu
            {
               printf("\n[IPC] Terminate request for PID %d from PID %d\n", msg.target_pid, msg.sender_pid);

                int kill_result = kill(msg.target_pid, SIGTERM);

                // Fiziksel Öldürme
                if (kill_result == 0 || errno == ESRCH)
                {
                    if (kill_result == 0)
                    {
                        printf("[IPC] SIGTERM sent to PID %d\n", msg.target_pid);
                    }

                    sem_wait(sem); // Kilitle

                    int found = 0;
                    for (int i = 0; i < shared_data->process_count; i++) // Paylaşılan bellekte ara
                    {
                        if (shared_data->processes[i].pid == msg.target_pid) // Eşleşen process bulundu
                        {
                            shared_data->processes[i].status = TERMINATED; // Durumu güncelle
                            shared_data->processes[i].is_active = 0; // Aktiflik durumunu güncelle
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

        char command_copy[256]; // Komutun kopyası
        strncpy(command_copy, command, 255); // Komutun bir kopyasını al
        command_copy[255] = '\0'; // Null terminator ekle

        char *args[10]; // Argüman dizisi
        int i = 0; // Argüman sayacı
        char *token = strtok(command_copy, " "); // Komutu boşluklara göre parçala

        while (token != NULL && i < 9) // Maksimum 9 argüman
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
        if (shared_data->process_count >= 50)
        {
            printf("[Main] Maximum process limit reached. Cannot start new process.\n");
            sem_post(sem);
            kill(pid, SIGTERM); // Başarısızsa child'ı öldür
            return;
        }

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

        if (mode == ATTACHED)
        {
            int status;
            waitpid(pid, &status, 0); // Attached modda bekle

            printf("[Main] Attached process (PID: %d) has terminated.\n", pid);

            sem_wait(sem); // Semaphore kilitle
            for (int i = 0; i < shared_data->process_count; i++)
            {
                if (shared_data->processes[i].pid == pid)
                {
                    shared_data->processes[i].status = TERMINATED;
                    shared_data->processes[i].is_active = 0;
                    break;
                }
            }
            sem_post(sem); // Semaphore aç

            msg.command = CMD_TERMINATE;                            // TERMINATE komutu
            msgsnd(msqid, &msg, sizeof(Message) - sizeof(long), 0); // Terminate mesajı gönder
        }
    }
}

void trim(char *str) { // Baş ve sondaki boşlukları silen yardımcı fonksiyon
    if (str == NULL) return;

    int len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) { // Sondaki boşlukları atla
        str[--len] = '\0';
    }

    char *start = str;
    while (*start && isspace((unsigned char)*start)) { // Baştaki boşlukları atla
        start++;
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1); // +1 null karakteri de taşımak için
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
    
    trim(command); // Baş ve sondaki boşlukları kaldır

    command[strcspn(command, "\n")] = 0; // Newline kaldır

    printf("Mode (0=ATTACHED, 1=DETACHED): ");
    scanf("%d", &mode);
    while (getchar() != '\n'); // Tamponu temizle

    if (mode != 0 && mode != 1)
    {
        printf("[ERROR] Invalid mode. Use 0 or 1.\n");
        return;
    }

    start_process(command, mode);
}

void handle_list_process() // Çalışan programları listele
{
    printf("Listing running programs...\n");
    sem_wait(sem);
    printf("\n");
    printf("╔═══════╦═══════════════════╦══════════╦═══════════╦═════════╦══════════╗\n");
    printf("║  PID  ║     Command       ║   Mode   ║  Status   ║  Owner  ║   Time   ║\n");
    printf("╠═══════╬═══════════════════╬══════════╬═══════════╬═════════╬══════════╣\n");
    
    for (int i = 0; i < shared_data->process_count; i++)
    {
        if (shared_data->processes[i].is_active)
        {
            time_t now = time(NULL);
            double duration = difftime(now, shared_data->processes[i].start_time);

            char *status_str = (shared_data->processes[i].status == RUNNING) ? "Running" : "Terminate";
            char *mode_str = (shared_data->processes[i].mode == ATTACHED) ? "Attached" : "Detached";
            
            printf("║ %-5d ║ %-17.17s ║ %-8s ║ %-9s ║ %-7d ║ %6.0f s ║\n",
                   shared_data->processes[i].pid,
                   shared_data->processes[i].command,
                   mode_str,
                   status_str,
                   shared_data->processes[i].owner_pid,
                   duration);
        }
    }
    sem_post(sem);
    printf("╚═══════╩═══════════════════╩══════════╩═══════════╩═════════╩══════════╝\n");
}

void handle_terminate_process() // Program sonlandır
{
    pid_t target_pid;
    printf("Enter PID of program to terminate: ");
    if (scanf("%d", &target_pid) != 1)
    {
        printf("[ERROR] Invalid PID format.\n");
        while (getchar() != '\n');
        return;
    }
    while (getchar() != '\n'); // Tamponu temizle

    // PID'nin yönetilen processler arasında olup olmadığını kontrol et
    sem_wait(sem);
    int found = 0;
    for (int i = 0; i < shared_data->process_count; i++)
    {
        if (shared_data->processes[i].pid == target_pid && 
            shared_data->processes[i].is_active)
        {
            found = 1;
            break;
        }
    }
    sem_post(sem);

    if (!found)
    {
        printf("[ERROR] PID %d not found in managed processes.\n", target_pid);
        return;
    }

    // PID doğrulandı, şimdi sonlandır
    if (kill(target_pid, SIGTERM) == 0)
    {
        printf("Sent termination signal to PID %d\n", target_pid);
        sem_wait(sem); // Semaphore kilitle
        for (int i = 0; i < shared_data->process_count; i++) // Paylaşılan bellekte ara
        {
            if (shared_data->processes[i].pid == target_pid && 
                shared_data->processes[i].is_active)
            {
                shared_data->processes[i].status = TERMINATED; // Durumu güncelle
                shared_data->processes[i].is_active = 0;       // Aktif değil olarak işaretle
                printf("Process %d marked as terminated in shared memory.\n", target_pid);
                break;
            }
        }
        sem_post(sem); // Semaphore aç
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

    usleep(100000); // İş parçacıklarının başlaması için kısa bir süre bekle
}

int main() // Ana fonksiyon
{

    pthread_t monitor_tid, ipc_listener_tid;
    int choice;

    setup_signal_handlers(); // Sinyal işleyicilerini ayarla

    init_resources(); // Kaynakları başlat

    printf("[Main] Process started (PID: %d). Waiting for signals...\n", getpid());
    printf("[Main] Press Ctrl+C to trigger the handler.\n");

    start_threads(&monitor_tid, &ipc_listener_tid); // İş parçacıklarını başlat

    printf("\nWelcome to ProcX - Process Management System\n");

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
            printf("[Main] Exiting ProcX...\n");
            running = 0; // Döngüyü durdur

            Message wake_msg = {.msg_type = 1, .command = 0, .sender_pid = getpid(), .target_pid = 0};
            msgsnd(msqid, &wake_msg, sizeof(Message) - sizeof(long), IPC_NOWAIT); // IPC dinleyiciyi uyandır

            pthread_join(monitor_tid, NULL);      // İzleme iş parçacığını bekle
            pthread_join(ipc_listener_tid, NULL); // IPC dinleyici iş parçacığını bekle

            cleanup_resources(); // Kaynakları temizle
            printf("Exiting ProcX. Goodbye!\n");
            exit(0);
        default:
            printf("Invalid choice. Please try again.\n");
        }
    }
    return 0;
}