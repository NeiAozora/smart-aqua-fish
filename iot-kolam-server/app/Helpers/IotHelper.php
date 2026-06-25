<?php

namespace App\Helpers;

use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Log;
use Carbon\Carbon;

class IotHelper
{
    // ================================================================
    // PROPERTIES UNTUK TESTING / DEBUG
    // ================================================================
    public static $testerMode = false;
    public static $forceCooldown = false;
    public static $coolDownTime = null;     // override cooldown notifikasi (menit)
    public static $noCooldown = false;

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

        // ──────────── NOTIFIKASI ────────────
        $forceSendNotification = self::$noCooldown;
        $lastNotif = DB::table('pemberitahuan')
            ->where('id_kolam', $kolam->id_kolam)
            ->where('jenis', 'amonia')
            ->orderBy('waktu_dibuat', 'desc')
            ->first();

        if (self::$testerMode && !self::$forceCooldown) {
            $effectiveCooldown = 0;
            Log::info("TESTER MODE: cooldown diabaikan untuk kolam {$kolam->id_kolam}");
        } else {
            $effectiveCooldown = self::$coolDownTime ?? ($kolam->cooldown_menit ?? 30);
        }

        $canSendNotification = true;
        if (!$forceSendNotification && $lastNotif && $now->diffInMinutes($lastNotif->waktu_dibuat) < $effectiveCooldown) {
            $canSendNotification = false;
            Log::info("Amonia exceed tapi masih cooldown notifikasi ({$effectiveCooldown} menit)");
        }

        if ($canSendNotification) {
            DB::table('pemberitahuan')->insert([
                'id_kolam'      => $kolam->id_kolam,
                'jenis'         => 'amonia',
                'pesan'         => "Kadar amonia {$kadar} mg/L melebihi batas ({$kolam->batasan_amonia} mg/L)",
                'dibaca'        => false,
                'waktu_dibuat'  => $now,
                'created_at'    => $now,
                'updated_at'    => $now
            ]);
            Log::info("Notifikasi amonia terkirim untuk kolam {$kolam->id_kolam}");
        }

        // ──────────── PERINTAH KURAS ────────────
        if ($kolam->pengurasan_otomatis && $kolam->device_id) {
            self::buatPerintahKuras($kolam->device_id, $kolam->id_kolam, 'otomatis', $now);
        }
    }

    /**
     * Buat perintah kuras baru jika:
     * - Tidak ada perintah kuras yang sedang berjalan (belum_dikirim / pending / proses)
     * - Cooldown dari perintah terakhir sudah lewat
     */
    public static function buatPerintahKuras($deviceId, $idKolam, $sumber, $now)
    {
        // 1. Cegah duplikasi perintah yang belum final
        $existing = DB::table('perintah_device')
            ->where('device_id', $deviceId)
            ->where('perintah', 'kuras')
            ->whereIn('status', ['belum_dikirim', 'pending', 'proses'])
            ->first();

        if ($existing) {
            Log::info("Masih ada perintah kuras aktif (status {$existing->status}) untuk {$deviceId}, tidak buat baru");
            return;
        }

        // 2. Periksa cooldown dari perintah terakhir yang sudah final (OK/ERROR)
        $lastFinal = DB::table('perintah_device')
            ->where('device_id', $deviceId)
            ->where('perintah', 'kuras')
            ->whereIn('status', ['OK', 'ERROR'])
            ->orderBy('responded_at', 'desc')
            ->first();

        if ($lastFinal && $lastFinal->responded_at) {
            $cooldownSeconds = 0;

            // Coba ambil angka cooldown dari response_message (format "Cooldown:XX")
            if ($lastFinal->response && preg_match('/Cooldown:(\d+)/', $lastFinal->response, $matches)) {
                $cooldownSeconds = (int) $matches[1];
            } else {
                // Fallback: jika tidak ada teks cooldown, pakai 60 detik (atau ambil dari kolam)
                $cooldownSeconds = 60; // bisa diganti dengan $kolam->cooldown_kuras_detik ?? 60
            }

            $cooldownUntil = Carbon::parse($lastFinal->responded_at)->addSeconds($cooldownSeconds);

            if ($now->lessThan($cooldownUntil)) {
                Log::info("Cooldown kuras untuk {$deviceId} sampai {$cooldownUntil}, skip");
                return;
            }
        }

        // 3. Aman, buat perintah baru
        DB::table('perintah_device')->insert([
            'device_id'  => $deviceId,
            'perintah'   => 'kuras',
            'status'     => 'belum_dikirim',
            'created_at' => $now,
            'updated_at' => $now
        ]);

        DB::table('pemberitahuan')->insert([
            'id_kolam'      => $idKolam,
            'jenis'         => 'pengurasan',
            'pesan'         => "Perintah kuras ({$sumber}) telah dikirim ke device",
            'dibaca'        => false,
            'waktu_dibuat'  => $now,
            'created_at'    => $now,
            'updated_at'    => $now
        ]);

        Log::info("Perintah kuras baru dibuat untuk device {$deviceId} via {$sumber}");
    }

    private static function resetStuckCommands()
    {
        // Reset perintah yang 'pending' atau 'proses' tapi tidak ada heartbeat selama 2 menit
        $timeout = now()->subMinutes(2);
        $updated = DB::table('perintah_device')
            ->whereIn('status', ['pending', 'proses'])
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