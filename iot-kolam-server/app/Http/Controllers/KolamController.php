<?php

namespace App\Http\Controllers;

use App\Helpers\IotHelper;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;

class KolamController extends Controller
{
    public function dashboard(Request $request)
    {

        $kolams = DB::table('kolam')
            ->where('id_user', 1)
            ->leftJoin('cache_kolam', 'kolam.id_kolam', '=', 'cache_kolam.id_kolam')
            ->select(
                [
                'kolam.id_kolam',
                'kolam.nama_kolam',
                'kolam.lokasi',
                'kolam.batasan_amonia',
                'kolam.pengurasan_otomatis',
                'kolam.cooldown_menit',
                'cache_kolam.kadar_amonia_terakhir',
                'cache_kolam.waktu_amonia_terakhir'
                ]
            )->orderBy("kolam.id_kolam")
            ->get();
        
        foreach ($kolams as $kolam) {
            $kolam->status = 'normal';
            if ($kolam->batasan_amonia && $kolam->kadar_amonia_terakhir && 
                $kolam->kadar_amonia_terakhir > $kolam->batasan_amonia) {
                $kolam->status = 'bahaya';
            }
            
            $kolam->notifikasi_belum_dibaca = DB::table('pemberitahuan')
                ->where('id_kolam', $kolam->id_kolam)
                ->where('dibaca', false)
                ->count();
        }
        
        return response()->json($kolams);
    }
    
    public function detail(Request $request, $id)
    {
        IotHelper::processHeartbeat();

        $kolam = DB::table('kolam')
            ->where('id_kolam', $id)
            ->where('id_user', 1)
            ->first();
        
        if (!$kolam) {
            return response()->json(['message' => 'Kolam not found'], 404);
        }
        
        $cache = DB::table('cache_kolam')->where('id_kolam', $id)->first();
        
        $riwayat = DB::table('pemberitahuan')
            ->where('id_kolam', $id)
            ->orderBy('waktu_dibuat', 'desc')
            ->limit(50)
            ->get();
        
        return response()->json([
            'kolam' => $kolam,
            'cache' => $cache,
            'riwayat' => $riwayat
        ]);
    }
    
    public function update(Request $request, $id)
    {
        $request->validate([
            'nama_kolam' => 'sometimes|string|max:100',
            'lokasi' => 'nullable|string|max:255',
            'deskripsi' => 'nullable|string',
            'batasan_amonia' => 'nullable|numeric',
            'pengurasan_otomatis' => 'sometimes|boolean',
            'cooldown_menit' => 'sometimes|integer|min:1'
        ]);
        
        $kolam = DB::table('kolam')
            ->where('id_kolam', $id)
            ->where('id_user', 1)
            ->first();
        
        if (!$kolam) {
            return response()->json(['message' => 'Kolam not found'], 404);
        }
        
        $updateData = [];
        if ($request->has('nama_kolam')) $updateData['nama_kolam'] = $request->nama_kolam;
        if ($request->has('lokasi')) $updateData['lokasi'] = $request->lokasi;
        if ($request->has('deskripsi')) $updateData['deskripsi'] = $request->deskripsi;
        if ($request->has('batasan_amonia')) $updateData['batasan_amonia'] = $request->batasan_amonia;
        if ($request->has('pengurasan_otomatis')) $updateData['pengurasan_otomatis'] = $request->pengurasan_otomatis;
        if ($request->has('cooldown_menit')) $updateData['cooldown_menit'] = $request->cooldown_menit;
        $updateData['updated_at'] = now();
        
        DB::table('kolam')->where('id_kolam', $id)->update($updateData);
        
        return response()->json(['message' => 'Kolam updated']);
    }
    
    public function kurasManual(Request $request, $id)
    {
        $kolam = DB::table('kolam')
            ->where('id_kolam', $id)
            ->where('id_user', 1)
            ->first();
        
        if (!$kolam) {
            return response()->json(['message' => 'Kolam not found'], 404);
        }
        
        if (!$kolam->device_id) {
            return response()->json(['message' => 'Device not registered'], 400);
        }
        
        IotHelper::buatPerintahKuras($kolam->device_id, $kolam->id_kolam, 'manual', now());
        
        return response()->json(['message' => 'Perintah kuras manual dikirim']);
    }


    // app/Http/Controllers/KolamController.php

    public function perintahTerbaru($id, Request $request)
    {
        $kolam = DB::table('kolam')->where('id_kolam', $id)->where('id_user', 1)->first();
        if (!$kolam) return response()->json(['message' => 'Not found'], 404);
        
        $limit = $request->get('limit', 2);
        $perintah = DB::table('perintah_device')
            ->where('device_id', $kolam->device_id)
            ->whereIn('status', ['belum_dikirim', 'pending'])
            ->orderBy('id', 'desc')
            ->limit($limit)
            ->get(['perintah', 'status', 'created_at']);
        
        return response()->json($perintah);
    }

    public function perintahPending($id)
    {
        $kolam = DB::table('kolam')->where('id_kolam', $id)->where('id_user', 1)->first();
        if (!$kolam) return response()->json([]);
        
        $pending = DB::table('perintah_device')
            ->where('device_id', $kolam->device_id)
            ->whereIn('status', ['belum_dikirim', 'pending'])
            ->exists();
        
        return response()->json(['has_pending' => $pending]);
    }

    public function updateGlobalCooldown(Request $request)
    {
        $request->validate([
            'cooldown_menit' => 'required|integer|min:1|max:1440'
        ]);
        
        $cooldown = $request->cooldown_menit;
        
        // Update semua kolam milik user id=1
        $updated = DB::table('kolam')
            ->where('id_user', 1)
            ->update(['cooldown_menit' => $cooldown, 'updated_at' => now()]);
        
        return response()->json([
            'message' => "Cooldown global berhasil diupdate ke {$cooldown} menit",
            'affected' => $updated
        ]);
    }

}
