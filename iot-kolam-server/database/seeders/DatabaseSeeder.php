<?php

namespace Database\Seeders;

use Illuminate\Database\Seeder;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Hash;

class DatabaseSeeder extends Seeder
{
    public function run(): void
    {
        DB::table('users')->insert([
            'id_user' => 1,
            'name' => 'Admin Utama',
            'username' => 'admin',
            'password_hash' => Hash::make('password'),
            'session_token' => null,
            'session_expired_at' => null,
            'created_at' => now(),
            'updated_at' => now()
        ]);
        
        DB::table('kolam')->insert([
            'id_kolam' => 1,
            'id_user' => 1,
            'nama_kolam' => 'Kolam Lele 1',
            'lokasi' => 'Blok A',
            'deskripsi' => 'Kolam utama untuk budidaya lele',
            'device_id' => 'DEVICE_001',
            'batasan_amonia' => 0.5,
            'pengurasan_otomatis' => true,
            'cooldown_menit' => 30,
            'created_at' => now(),
            'updated_at' => now()
        ]);
        
        DB::table('kolam')->insert([
            'id_kolam' => 2,
            'id_user' => 1,
            'nama_kolam' => 'Kolam Nila 2',
            'lokasi' => 'Blok B',
            'deskripsi' => 'Kolam nila eksperimen',
            'device_id' => 'DEVICE_002',
            'batasan_amonia' => 0.4,
            'pengurasan_otomatis' => false,
            'cooldown_menit' => 45,
            'created_at' => now(),
            'updated_at' => now()
        ]);
        
        DB::table('cache_kolam')->insert([
            'id_kolam' => 1,
            'kadar_amonia_terakhir' => 0.2,
            'waktu_amonia_terakhir' => now(),
            'created_at' => now(),
            'updated_at' => now()
        ]);
        
        DB::table('cache_kolam')->insert([
            'id_kolam' => 2,
            'kadar_amonia_terakhir' => 0.3,
            'waktu_amonia_terakhir' => now(),
            'created_at' => now(),
            'updated_at' => now()
        ]);
    }
}
