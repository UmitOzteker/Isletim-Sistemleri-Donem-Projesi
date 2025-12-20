# 🚀 ProcX v1.0 - Gelişmiş Süreç Yönetim Sistemi

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen?style=flat-square)](#) 
[![License](https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square)](#)
[![Platform](https://img.shields.io/badge/platform-UNIX-blue?style=flat-square)](#)
[![Language](https://img.shields.io/badge/language-C-orange?style=flat-square)](#)

**ProcX**, UNIX tabanlı işletim sistemleri için geliştirilmiş, birden fazla terminalden yönetilebilen asenkron süreç yönetim aracıdır. POSIX standartlarıyla IPC ve paylaşılan bellek mimarisini birleştirir.

---

## ✨ Öne Çıkan Özellikler

- 🎯 **Hedefli IPC**: PID tabanlı mesajlaşma ile güvenilir, kayıpsız iletişim.
- 👀 **Otomatik İzleme**: Arka planda çalışan thread ile süreç durumları sürekli güncellenir.
- 🔁 **Çoklu Terminal Desteği**: Birden fazla terminalden sürece erişim.
- 🔒 **Senkronizasyon**: Veri yarışlarını önleyecek şekilde POSIX Semaforları.

![test2](https://github.com/user-attachments/assets/e8604565-a3e6-40f9-9e1d-b83749c8ba89)

---

## 🛠 Mimari ve Bileşenler

| Bileşen          | Teknoloji              | Açıklama                                |
|------------------|-----------------------|-----------------------------------------|
| **Paylaşılan Bellek** | POSIX `shm_open`      | Süreç listesinin tüm terminallerle ortak yönetimi. |
| **Senkronizasyon**    | POSIX Semaphores      | Kaynak yarışlarının önlenmesi.          |
| **Mesajlaşma**        | System V Message Queues | Terminaller arası hızlı ve güvenli bildirimler. |

<img width="600" alt="Mimari Şema" src="https://github.com/user-attachments/assets/fcde4939-784a-4018-b50f-d4049b75a8bd" />

---

## 🚀 Kurulum ve Çalıştırma

Projeyi derlemek ve başlatmak için:

```bash
make
```

> İlgili `gcc`, POSIX ve System V kütüphanelerinin sisteminizde mevcut olması gerekmektedir.

![test1](https://github.com/user-attachments/assets/da1d5435-065a-445c-add2-f1b3903dcae8)

---

## 📋 Kullanım ve İzleme

Başlatılan tüm süreçlerin yaşam döngüsü `monitor_thread` ile izlenir ve arayüzde anlık olarak güncellenir.

![test2](https://github.com/user-attachments/assets/36abaeec-4177-4bc8-8751-b940c7472d6f)

- **Yeni Süreç Başlatma**: Terminalde gerekli komut ile yeni süreç yaratın.
- **Durum Takibi**: Tüm süreçler arka planda otomatik izlenir.
- **Mesajlaşma**: PID seçilerek hedefli bildirim gönderilebilir.

---

## 🧠 Teknik Detaylar

- 🧹 **Zombi Süreç Koruması:** `waitpid` kullanılarak sonlanan çocuk süreçlerin sistem kaynaklarını tüketmesi engellenir.
- 🛡 **Sinyal Güvenliği:** `Ctrl+C` sinyali özelleştirilmiş ve güvenli temiz çıkış protokolüyle süreçler ve kaynaklar güvence altına alınmıştır.

![test4](https://github.com/user-attachments/assets/ccbad6e2-c10c-45c1-ade7-f1cc88ea1641)

---

## 💡 Katkı ve Lisans

Projeye katkıda bulunmak için PR gönderebilir veya Issue açabilirsiniz.

Bu proje [MIT](LICENSE) lisansı ile korunmaktadır.

---
