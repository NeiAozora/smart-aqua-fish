<?php

namespace App\Http\Controllers;

use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;

class NotifikasiController extends Controller
{
    public function index(Request $request)
    {
        $query = DB::table('pemberitahuan')
            ->join('kolam', 'pemberitahuan.id_kolam', '=', 'kolam.id_kolam')
            ->where('kolam.id_user', 1)
            ->select(['pemberitahuan.*', 'kolam.nama_kolam']);
        
        if ($request->has('jenis')) {
            $query->where('pemberitahuan.jenis', $request->jenis);
        }
        
        if ($request->has('dibaca')) {
            $query->where('pemberitahuan.dibaca', filter_var($request->dibaca, FILTER_VALIDATE_BOOLEAN));
        }
        
        if ($request->has('kolam_id')) {
            $query->where('pemberitahuan.id_kolam', $request->kolam_id);
        }
        
        $notifikasi = $query->orderBy('pemberitahuan.waktu_dibuat', 'desc')
            ->paginate($request->get('limit', 20));
        
        return response()->json($notifikasi);
    }
    
    public function markAsRead(Request $request, $id)
    {
        $notif = DB::table('pemberitahuan')
            ->join('kolam', 'pemberitahuan.id_kolam', '=', 'kolam.id_kolam')
            ->where('pemberitahuan.id_pemberitahuan', $id)
            ->where('kolam.id_user', 1)
            ->first();
        
        if (!$notif) {
            return response()->json(['message' => 'Notifikasi not found'], 404);
        }
        
        DB::table('pemberitahuan')
            ->where('id_pemberitahuan', $id)
            ->update(['dibaca' => true, 'updated_at' => now()]);
        
        return response()->json(['message' => 'Notifikasi ditandai dibaca']);
    }
    
    public function destroy(Request $request, $id)
    {
        $notif = DB::table('pemberitahuan')
            ->join('kolam', 'pemberitahuan.id_kolam', '=', 'kolam.id_kolam')
            ->where('pemberitahuan.id_pemberitahuan', $id)
            ->where('kolam.id_user', 1)
            ->first();
        
        if (!$notif) {
            return response()->json(['message' => 'Notifikasi not found'], 404);
        }
        
        DB::table('pemberitahuan')->where('id_pemberitahuan', $id)->delete();
        
        return response()->json(['message' => 'Notifikasi dihapus']);
    }
}
