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
