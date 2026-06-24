<?php

namespace App\Helpers;

use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Log;

class IotHelper
{
    // ================================================================
    // PROPERTIES UNTUK TESTING / DEBUG
    // ================================================================
    public static $testerMode = false;      // aktifkan mode tester (abaikan cooldown jika forceCooldown false)
    public static $forceCooldown = false;   // jika true, tetap pakai cooldown normal (meski testerMode aktif)
    public static $coolDownTime = null;     // override cooldown (dalam menit), jika diisi maka nilai ini yang dipakai
    public static $noCooldown = false;      // jika true, NOTIFIKASI akan selalu dikirim (abaikan cooldown sama sekali)

    public static function processHeartbeat()
    {
        Log::info('IoT Heartbeat started');
        
        $kolams = DB::table('kolam')
            ->leftJoin('cache_kolam', 'kolam.id_kolam', '=', 'cache_kolam.id_kolam')
            ->select(['kolam.*', 'cache_kolam.kadar_amonia_terakhir', 'cache_kolam.waktu_amonia_terakhir'])
            ->get();

        $now = now();

        foreach ($kolams as $kolam) {
            if (is_null($kolam->kadar_amonia_terakhir)) continue;

            $kadar = (float) $kolam->kadar_amonia_terakhir;
            $batas = $kolam->batasan_amonia ? (float) $kolam->batasan_amonia : null;

            if ($batas && $kadar > $batas) {
                self::handleAmoniaExceed($kolam, $kadar, $now);
            }
        }

        self::resetStuckCommands();
        self::cleanExpiredSessions();
        
        Log::info('IoT Heartbeat finished');
    }

    public static function handleAmoniaExceed($kolam, $kadar, $now)
    {
        Log::info("Amonia exceed terdeteksi! Kolam: {$kolam->id_kolam}, Kadar: {$kadar}, Batas: {$kolam->batasan_amonia}");
        
        // ================================================================
        // COOLDOWN NOTIFIKASI - DENGAN DUKUNGAN TESTER MODE & NOCOOLDOWN
        // ================================================================
        
        // Cek apakah notifikasi tetap dikirim meskipun dalam cooldown
        $forceSendNotification = self::$noCooldown;
        
        $lastNotif = DB::table('pemberitahuan')
            ->where('id_kolam', $kolam->id_kolam)
            ->where('jenis', 'amonia')
            ->orderBy('waktu_dibuat', 'desc')
            ->first();

        // Tentukan nilai cooldown yang akan dipakai
        if (self::$testerMode && !self::$forceCooldown) {
            // Mode testing: abaikan cooldown (anggap cooldown = 0)
            $effectiveCooldown = 0;
            Log::info("TESTER MODE: cooldown diabaikan untuk kolam {$kolam->id_kolam}");
        } else {
            // Gunakan cooldown dari kolam, atau override jika coolDownTime diisi
            $effectiveCooldown = self::$coolDownTime ?? ($kolam->cooldown_menit ?? 30);
        }

        $canSendNotification = true;
        
        // Jika noCooldown aktif, maka abaikan pengecekan cooldown
        if (!$forceSendNotification && $lastNotif && $now->diffInMinutes($lastNotif->waktu_dibuat) < $effectiveCooldown) {
            $canSendNotification = false;
            Log::info("Amonia exceed tapi masih cooldown ({$effectiveCooldown} menit) - tidak kirim notifikasi");
        } else if ($forceSendNotification) {
            Log::info("NOCOOLDOWN MODE: notifikasi tetap dikirim meskipun masih dalam cooldown");
        }

        if ($canSendNotification) {
            DB::table('pemberitahuan')->insert([
                'id_kolam' => $kolam->id_kolam,
                'jenis' => 'amonia',
                'pesan' => "Kadar amonia {$kadar} mg/L melebihi batas ({$kolam->batasan_amonia} mg/L)",
                'dibaca' => false,
                'waktu_dibuat' => $now,
                'created_at' => $now,
                'updated_at' => $now
            ]);
            Log::info("Notifikasi amonia terkirim untuk kolam {$kolam->id_kolam}");
        }

        // Perintah kuras tetap dibuat (tidak terpengaruh cooldown notifikasi)
        if ($kolam->pengurasan_otomatis && $kolam->device_id) {
            self::buatPerintahKuras($kolam->device_id, $kolam->id_kolam, 'otomatis', $now);
        }
    }

    public static function buatPerintahKuras($deviceId, $idKolam, $sumber, $now)
    {
        // Cek apakah sudah ada perintah kuras yang belum selesai (belum_dikirim atau pending)
        $existing = DB::table('perintah_device')
            ->where('device_id', $deviceId)
            ->where('perintah', 'kuras')
            ->whereIn('status', ['belum_dikirim', 'pending'])
            ->first();

        if ($existing) {
            Log::info("Perintah kuras untuk device {$deviceId} sudah ada (status {$existing->status}), skip buat baru");
            return;
        }

        DB::table('perintah_device')->insert([
            'device_id' => $deviceId,
            'perintah' => 'kuras',
            'status' => 'belum_dikirim',
            'created_at' => $now,
            'updated_at' => $now
        ]);

        DB::table('pemberitahuan')->insert([
            'id_kolam' => $idKolam,
            'jenis' => 'pengurasan',
            'pesan' => "Perintah kuras ({$sumber}) telah dikirim ke device",
            'dibaca' => false,
            'waktu_dibuat' => $now,
            'created_at' => $now,
            'updated_at' => $now
        ]);
        
        Log::info("Perintah kuras baru dibuat untuk device {$deviceId} via {$sumber}");
    }

    private static function resetStuckCommands()
    {
        $timeout = now()->subMinutes(2);
        $updated = DB::table('perintah_device')
            ->where('status', 'pending')
            ->where('updated_at', '<', $timeout)
            ->update(['status' => 'belum_dikirim', 'updated_at' => now()]);

        if ($updated) {
            Log::info("Reset {$updated} stuck commands");
        }
    }

    private static function cleanExpiredSessions()
    {
        DB::table('users')
            ->where('session_expired_at', '<', now())
            ->update(['session_token' => null, 'session_expired_at' => null]);
    }
}