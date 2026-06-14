<?php

namespace App\Http\Controllers;

use App\Helpers\IotHelper;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Log;

class IotController extends Controller
{
    /**
     * IOT mengirim data sensor
     */
    public function terimaSensor(Request $request)
    {
        $request->validate([
            'device_id'   => 'required|string|exists:kolam,device_id',
            'kadar_amonia'=> 'required|numeric|min:0',
            'recorded_at' => 'nullable|date'
        ]);

        $deviceId = $request->device_id;
        $kadar    = (float) $request->kadar_amonia;
        $waktu    = $request->recorded_at ?: now();

        $kolam = DB::table('kolam')->where('device_id', $deviceId)->first();
        if (!$kolam) {
            Log::warning("Device {$deviceId} tidak terdaftar");
            return response()->json(['message' => 'Device not registered'], 404);
        }

        // Update cache
        DB::table('cache_kolam')->updateOrInsert(
            ['id_kolam' => $kolam->id_kolam],
            [
                'kadar_amonia_terakhir' => $kadar,
                'waktu_amonia_terakhir' => $waktu,
                'updated_at' => now()
            ]
        );

        Log::info("Sensor dari {$deviceId}: kadar={$kadar}, batas={$kolam->batasan_amonia}");

        // Jika melebihi batas, proses via helper (mencakup cooldown dan pembuatan perintah)
        if ($kolam->batasan_amonia && $kadar > $kolam->batasan_amonia) {
            IotHelper::handleAmoniaExceed($kolam, $kadar, now());
        }

        return response()->json(['message' => 'Sensor data received']);
    }

    /**
     * IOT meminta perintah (polling setiap 5 detik)
     */
public function ambilPerintah($deviceId)
{
    Log::info("Ambil perintah untuk device: {$deviceId}");
    $perintah = DB::table('perintah_device')
        ->where('device_id', $deviceId)
        ->where('status', 'belum_dikirim')
        ->orderBy('id', 'asc')
        ->first();

    if (!$perintah) {
        Log::info("Tidak ada perintah untuk device {$deviceId}");
        return response()->json(null, 204);
    }

    $updated = DB::table('perintah_device')
        ->where('id', $perintah->id)
        ->where('status', 'belum_dikirim')
        ->update(['status' => 'pending', 'updated_at' => now()]);

    if (!$updated) {
        Log::warning("Gagal update status perintah id {$perintah->id}, mungkin sudah berubah");
        return response()->json(null, 204);
    }

    Log::info("Perintah id {$perintah->id} diberikan ke device, status sekarang pending");
    return response()->json([
        'command_id' => $perintah->id,
        'perintah' => $perintah->perintah
    ]);
}


    /**
     * IOT melaporkan hasil eksekusi perintah
     */
    public function terimaResponse(Request $request)
    {
        $request->validate([
            'command_id'       => 'required|integer|exists:perintah_device,id',
            'status'           => 'required|in:OK,ERROR',
            'response_message' => 'nullable|string'
        ]);

        $perintah = DB::table('perintah_device')->where('id', $request->command_id)->first();
        if (!$perintah) {
            return response()->json(['message' => 'Command not found'], 404);
        }

        // Update status perintah
        DB::table('perintah_device')
            ->where('id', $request->command_id)
            ->update([
                'status'        => $request->status,
                'response'      => $request->response_message,
                'responded_at'  => now(),
                'updated_at'    => now()
            ]);

        // Cari kolam terkait
        $kolam = DB::table('kolam')->where('device_id', $perintah->device_id)->first();
        if ($kolam) {
            $now = now();
            if ($request->status == 'OK') {
                DB::table('pemberitahuan')->insert([
                    'id_kolam'      => $kolam->id_kolam,
                    'jenis'         => 'pengurasan',
                    'pesan'         => "Prosedur pengurasan sudah berhasil dilakukan",
                    'dibaca'        => false,
                    'waktu_dibuat'  => $now,
                    'created_at'    => $now,
                    'updated_at'    => $now
                ]);
                Log::info("Perintah {$perintah->id} ({$perintah->perintah}) sukses dieksekusi device {$perintah->device_id}");
            } else {
                DB::table('pemberitahuan')->insert([
                    'id_kolam'      => $kolam->id_kolam,
                    'jenis'         => 'device',
                    'pesan'         => "Gagal eksekusi perintah: {$request->response_message}",
                    'dibaca'        => false,
                    'waktu_dibuat'  => $now,
                    'created_at'    => $now,
                    'updated_at'    => $now
                ]);
                Log::warning("Perintah {$perintah->id} gagal: {$request->response_message}");
            }
        } else {
            Log::error("Device {$perintah->device_id} tidak ditemukan saat menerima response");
        }

        return response()->json(['message' => 'Response recorded']);
    }
}