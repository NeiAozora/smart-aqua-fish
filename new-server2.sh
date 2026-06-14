#!/bin/bash

# ============================================
# Auto Setup Script for Laravel IoT Kolam Server
# ============================================

set -e  # Hentikan script jika ada error

# Warna untuk output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}Setup Laravel IoT Kolam Server${NC}"
echo -e "${GREEN}============================================${NC}"

# 1. Masukkan nama project
read -p "Masukkan nama project [iot-kolam-server]: " PROJECT_NAME
PROJECT_NAME=${PROJECT_NAME:-iot-kolam-server}

echo -e "${YELLOW}Membuat project Laravel: $PROJECT_NAME ...${NC}"
composer create-project laravel/laravel "$PROJECT_NAME"

cd "$PROJECT_NAME"

# 2. Konfigurasi database PostgreSQL
echo -e "${YELLOW}Konfigurasi Database PostgreSQL${NC}"
read -p "Database host [127.0.0.1]: " DB_HOST
DB_HOST=${DB_HOST:-127.0.0.1}
read -p "Database port [5432]: " DB_PORT
DB_PORT=${DB_PORT:-5432}
read -p "Database name [iot_kolam]: " DB_DATABASE
DB_DATABASE=${DB_DATABASE:-iot_kolam}
read -p "Database username [postgres]: " DB_USERNAME
DB_USERNAME=${DB_USERNAME:-postgres}
read -sp "Database password: " DB_PASSWORD
echo ""

# Update .env
sed -i "s/DB_CONNECTION=.*/DB_CONNECTION=pgsql/" .env
sed -i "s/DB_HOST=.*/DB_HOST=$DB_HOST/" .env
sed -i "s/DB_PORT=.*/DB_PORT=$DB_PORT/" .env
sed -i "s/DB_DATABASE=.*/DB_DATABASE=$DB_DATABASE/" .env
sed -i "s/DB_USERNAME=.*/DB_USERNAME=$DB_USERNAME/" .env
sed -i "s/DB_PASSWORD=.*/DB_PASSWORD=$DB_PASSWORD/" .env

# 3. Buat file migration (sesuai yang diberikan user)
echo -e "${YELLOW}Membuat migration...${NC}"
php artisan make:migration create_all_tables

# Hapus file migration default yang dibuat Laravel
rm database/migrations/*_create_users_table.php 2>/dev/null || true
rm database/migrations/*_create_cache_table.php 2>/dev/null || true
rm database/migrations/*_create_jobs_table.php 2>/dev/null || true
rm database/migrations/*_create_failed_jobs_table.php 2>/dev/null || true
rm database/migrations/*_create_password_resets_table.php 2>/dev/null || true

# Buat file migration baru dengan isi dari user
MIGRATION_FILE=$(ls database/migrations/*_create_all_tables.php | head -1)
cat > "$MIGRATION_FILE" << 'EOF'
<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        // Tabel users
        Schema::create('users', function (Blueprint $table) {
            $table->id('id_user');
            $table->string('name', 65);
            $table->string('username', 50)->unique();
            $table->string('password_hash', 255);
            $table->string('session_token', 100)->unique()->nullable();
            $table->timestamp('session_expired_at')->nullable();
            $table->timestamps();
        });

        // Tabel kolam
        Schema::create('kolam', function (Blueprint $table) {
            $table->id('id_kolam');
            $table->foreignId('id_user')->constrained('users', 'id_user')->onDelete('cascade');
            $table->string('nama_kolam', 100);
            $table->string('lokasi', 255)->nullable();
            $table->text('deskripsi')->nullable();
            $table->string('device_id', 50)->unique()->nullable();
            $table->double('batasan_amonia')->nullable();
            $table->boolean('pengurasan_otomatis')->default(true);
            $table->integer('cooldown_menit')->default(30);
            $table->timestamps();
        });

        // Tabel cache_kolam
        Schema::create('cache_kolam', function (Blueprint $table) {
            $table->id('id_cache');
            $table->foreignId('id_kolam')->constrained('kolam', 'id_kolam')->onDelete('cascade')->unique();
            $table->float('kadar_amonia_terakhir')->nullable();
            $table->datetime('waktu_amonia_terakhir')->nullable();
            $table->timestamps();
        });

        // Tabel pemberitahuan
        Schema::create('pemberitahuan', function (Blueprint $table) {
            $table->id('id_pemberitahuan');
            $table->foreignId('id_kolam')->constrained('kolam', 'id_kolam')->onDelete('cascade');
            $table->enum('jenis', ['amonia', 'device', 'pengurasan']);
            $table->text('pesan');
            $table->boolean('dibaca')->default(false);
            $table->datetime('waktu_dibuat');
            $table->timestamps();
        });

        // Tabel perintah_device
        Schema::create('perintah_device', function (Blueprint $table) {
            $table->id();
            $table->string('device_id', 50);
            $table->string('perintah', 50);
            $table->enum('status', ['belum_dikirim', 'pending', 'OK', 'ERROR'])->default('belum_dikirim');
            $table->text('response')->nullable();
            $table->timestamp('responded_at')->nullable();
            $table->timestamps();

            $table->foreign('device_id')->references('device_id')->on('kolam')->onDelete('cascade');
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('perintah_device');
        Schema::dropIfExists('pemberitahuan');
        Schema::dropIfExists('cache_kolam');
        Schema::dropIfExists('kolam');
        Schema::dropIfExists('users');
    }
};
EOF

# 4. Membuat semua file yang diperlukan
echo -e "${YELLOW}Membuat Helper, Middleware, Controllers, Command, dll...${NC}"

# Buat directory Helper jika belum ada
mkdir -p app/Helpers

# Helper IotHelper.php
cat > app/Helpers/IotHelper.php << 'EOF'
<?php

namespace App\Helpers;

use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Log;

class IotHelper
{
    public static function processHeartbeat()
    {
        Log::info('IoT Heartbeat started');
        
        $kolams = DB::table('kolam')
            ->leftJoin('cache_kolam', 'kolam.id_kolam', '=', 'cache_kolam.id_kolam')
            ->select('kolam.*', 'cache_kolam.kadar_amonia_terakhir', 'cache_kolam.waktu_amonia_terakhir')
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
        $lastNotif = DB::table('pemberitahuan')
            ->where('id_kolam', $kolam->id_kolam)
            ->where('jenis', 'amonia')
            ->orderBy('waktu_dibuat', 'desc')
            ->first();

        $cooldown = $kolam->cooldown_menit ?? 30;
        if ($lastNotif && $now->diffInMinutes($lastNotif->waktu_dibuat) < $cooldown) {
            return;
        }

        DB::table('pemberitahuan')->insert([
            'id_kolam' => $kolam->id_kolam,
            'jenis' => 'amonia',
            'pesan' => "Kadar amonia {$kadar} mg/L melebihi batas ({$kolam->batasan_amonia} mg/L)",
            'dibaca' => false,
            'waktu_dibuat' => $now,
            'created_at' => $now,
            'updated_at' => $now
        ]);

        if ($kolam->pengurasan_otomatis && $kolam->device_id) {
            self::buatPerintahKuras($kolam->device_id, $kolam->id_kolam, 'otomatis', $now);
        }
    }

    public static function buatPerintahKuras($deviceId, $idKolam, $sumber, $now)
    {
        $existing = DB::table('perintah_device')
            ->where('device_id', $deviceId)
            ->where('perintah', 'kuras')
            ->whereIn('status', ['belum_dikirim', 'pending'])
            ->first();

        if ($existing) return;

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
    }

    private static function resetStuckCommands()
    {
        $timeout = now()->subMinutes(2);
        $updated = DB::table('perintah_device')
            ->where('status', 'pending')
            ->where('updated_at', '<', $timeout)
            ->update(['status' => 'belum_dikirim', 'updated_at' => now()]);
        if ($updated) Log::info("Reset $updated stuck commands");
    }

    private static function cleanExpiredSessions()
    {
        DB::table('users')
            ->where('session_expired_at', '<', now())
            ->update(['session_token' => null, 'session_expired_at' => null]);
    }
}
EOF

# Middleware ApiAuthMiddleware.php
mkdir -p app/Http/Middleware
cat > app/Http/Middleware/ApiAuthMiddleware.php << 'EOF'
<?php

namespace App\Http\Middleware;

use Closure;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;
use Symfony\Component\HttpFoundation\Response;

class ApiAuthMiddleware
{
    public function handle(Request $request, Closure $next): Response
    {
        $token = $request->bearerToken();
        
        if (!$token) {
            return response()->json(['message' => 'Unauthorized'], 401);
        }
        
        $user = DB::table('users')
            ->where('session_token', $token)
            ->where('session_expired_at', '>', now())
            ->first();
        
        if (!$user) {
            return response()->json(['message' => 'Invalid or expired token'], 401);
        }
        
        $request->merge(['user' => $user]);
        $request->setUserResolver(fn () => $user);
        
        return $next($request);
    }
}
EOF

# Update bootstrap/app.php untuk registrasi middleware
cat > bootstrap/app.php << 'EOF'
<?php

use App\Http\Middleware\ApiAuthMiddleware;
use Illuminate\Foundation\Application;
use Illuminate\Foundation\Configuration\Exceptions;
use Illuminate\Foundation\Configuration\Middleware;

return Application::configure(basePath: dirname(__DIR__))
    ->withRouting(
        web: __DIR__.'/../routes/web.php',
        api: __DIR__.'/../routes/api.php',
        commands: __DIR__.'/../routes/console.php',
        health: '/up',
    )
    ->withMiddleware(function (Middleware $middleware) {
        $middleware->alias([
            'auth.api' => ApiAuthMiddleware::class,
        ]);
    })
    ->withExceptions(function (Exceptions $exceptions) {
        //
    })->create();
EOF

# Buat Controllers
mkdir -p app/Http/Controllers

# AuthController
cat > app/Http/Controllers/AuthController.php << 'EOF'
<?php

namespace App\Http\Controllers;

use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Hash;
use Illuminate\Support\Str;

class AuthController extends Controller
{
    public function login(Request $request)
    {
        $request->validate([
            'username' => 'required|string',
            'password' => 'required|string'
        ]);
        
        $user = DB::table('users')->where('username', $request->username)->first();
        
        if (!$user || !Hash::check($request->password, $user->password_hash)) {
            return response()->json(['message' => 'Invalid credentials'], 401);
        }
        
        $token = Str::random(80);
        $expiredAt = now()->addDays(7);
        
        DB::table('users')
            ->where('id_user', $user->id_user)
            ->update([
                'session_token' => $token,
                'session_expired_at' => $expiredAt
            ]);
        
        return response()->json([
            'token' => $token,
            'expired_at' => $expiredAt,
            'user' => [
                'id' => $user->id_user,
                'name' => $user->name,
                'username' => $user->username
            ]
        ]);
    }
    
    public function logout(Request $request)
    {
        $user = $request->user;
        DB::table('users')
            ->where('id_user', $user->id_user)
            ->update([
                'session_token' => null,
                'session_expired_at' => null
            ]);
        
        return response()->json(['message' => 'Logged out']);
    }
    
    public function me(Request $request)
    {
        $user = $request->user;
        return response()->json([
            'id' => $user->id_user,
            'name' => $user->name,
            'username' => $user->username
        ]);
    }
}
EOF

# IotController
cat > app/Http/Controllers/IotController.php << 'EOF'
<?php

namespace App\Http\Controllers;

use App\Helpers\IotHelper;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Log;

class IotController extends Controller
{
    public function terimaSensor(Request $request)
    {
        $request->validate([
            'device_id' => 'required|string|exists:kolam,device_id',
            'kadar_amonia' => 'required|numeric',
            'recorded_at' => 'nullable|date'
        ]);
        
        $deviceId = $request->device_id;
        $kadar = (float) $request->kadar_amonia;
        $waktu = $request->recorded_at ?: now();
        
        $kolam = DB::table('kolam')->where('device_id', $deviceId)->first();
        if (!$kolam) {
            return response()->json(['message' => 'Device not registered'], 404);
        }
        
        DB::table('cache_kolam')->updateOrInsert(
            ['id_kolam' => $kolam->id_kolam],
            [
                'kadar_amonia_terakhir' => $kadar,
                'waktu_amonia_terakhir' => $waktu,
                'updated_at' => now()
            ]
        );
        
        if ($kolam->batasan_amonia && $kadar > $kolam->batasan_amonia) {
            IotHelper::handleAmoniaExceed($kolam, $kadar, now());
        }
        
        return response()->json(['message' => 'Sensor data received']);
    }
    
    public function ambilPerintah($deviceId)
    {
        $perintah = DB::table('perintah_device')
            ->where('device_id', $deviceId)
            ->where('status', 'belum_dikirim')
            ->orderBy('id', 'asc')
            ->first();
        
        if (!$perintah) {
            return response()->json(null, 204);
        }
        
        $updated = DB::table('perintah_device')
            ->where('id', $perintah->id)
            ->where('status', 'belum_dikirim')
            ->update(['status' => 'pending', 'updated_at' => now()]);
        
        if (!$updated) {
            return response()->json(null, 204);
        }
        
        return response()->json([
            'command_id' => $perintah->id,
            'perintah' => $perintah->perintah
        ]);
    }
    
    public function terimaResponse(Request $request)
    {
        $request->validate([
            'command_id' => 'required|integer|exists:perintah_device,id',
            'status' => 'required|in:OK,ERROR',
            'response_message' => 'nullable|string'
        ]);
        
        $perintah = DB::table('perintah_device')->where('id', $request->command_id)->first();
        if (!$perintah) {
            return response()->json(['message' => 'Command not found'], 404);
        }
        
        DB::table('perintah_device')
            ->where('id', $request->command_id)
            ->update([
                'status' => $request->status,
                'response' => $request->response_message,
                'responded_at' => now(),
                'updated_at' => now()
            ]);
        
        $kolam = DB::table('kolam')->where('device_id', $perintah->device_id)->first();
        if ($kolam) {
            $now = now();
            if ($request->status == 'OK') {
                DB::table('pemberitahuan')->insert([
                    'id_kolam' => $kolam->id_kolam,
                    'jenis' => 'pengurasan',
                    'pesan' => "Pengurasan berhasil dilakukan oleh device",
                    'dibaca' => false,
                    'waktu_dibuat' => $now,
                    'created_at' => $now,
                    'updated_at' => $now
                ]);
            } else {
                DB::table('pemberitahuan')->insert([
                    'id_kolam' => $kolam->id_kolam,
                    'jenis' => 'device',
                    'pesan' => "Gagal eksekusi perintah: {$request->response_message}",
                    'dibaca' => false,
                    'waktu_dibuat' => $now,
                    'created_at' => $now,
                    'updated_at' => $now
                ]);
            }
        }
        
        return response()->json(['message' => 'Response recorded']);
    }
}
EOF

# KolamController
cat > app/Http/Controllers/KolamController.php << 'EOF'
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
                'kolam.id_kolam',
                'kolam.nama_kolam',
                'kolam.lokasi',
                'kolam.batasan_amonia',
                'kolam.pengurasan_otomatis',
                'kolam.cooldown_menit',
                'cache_kolam.kadar_amonia_terakhir',
                'cache_kolam.waktu_amonia_terakhir'
            )
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
}
EOF

# NotifikasiController
cat > app/Http/Controllers/NotifikasiController.php << 'EOF'
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
            ->select('pemberitahuan.*', 'kolam.nama_kolam');
        
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
EOF

# Buat routes/api.php
cat > routes/api.php << 'EOF'
<?php

use App\Http\Controllers\AuthController;
use App\Http\Controllers\IotController;
use App\Http\Controllers\KolamController;
use App\Http\Controllers\NotifikasiController;
use Illuminate\Support\Facades\Route;

Route::post('/login', [AuthController::class, 'login']);

Route::post('/iot/sensor', [IotController::class, 'terimaSensor']);
Route::get('/iot/command/{device_id}', [IotController::class, 'ambilPerintah']);
Route::post('/iot/response', [IotController::class, 'terimaResponse']);

Route::middleware('auth.api')->group(function () {
    Route::post('/logout', [AuthController::class, 'logout']);
    Route::get('/me', [AuthController::class, 'me']);
    
    Route::get('/dashboard', [KolamController::class, 'dashboard']);
    Route::get('/kolam/{id}/detail', [KolamController::class, 'detail']);
    Route::put('/kolam/{id}', [KolamController::class, 'update']);
    Route::post('/kolam/{id}/kuras', [KolamController::class, 'kurasManual']);
    
    Route::get('/notifikasi', [NotifikasiController::class, 'index']);
    Route::put('/notifikasi/{id}/baca', [NotifikasiController::class, 'markAsRead']);
    Route::delete('/notifikasi/{id}', [NotifikasiController::class, 'destroy']);
});
EOF

# Buat Command Heartbeat
mkdir -p app/Console/Commands
cat > app/Console/Commands/IotHeartbeatCommand.php << 'EOF'
<?php

namespace App\Console\Commands;

use App\Helpers\IotHelper;
use Illuminate\Console\Command;

class IotHeartbeatCommand extends Command
{
    protected $signature = 'iot:heartbeat';
    protected $description = 'Run IOT heartbeat processing for auto detection and commands';

    public function handle()
    {
        $this->info('Starting IoT Heartbeat...');
        IotHelper::processHeartbeat();
        $this->info('IoT Heartbeat completed at ' . now());
        return Command::SUCCESS;
    }
}
EOF

# Buat scheduler di routes/console.php
cat > routes/console.php << 'EOF'
<?php

use Illuminate\Support\Facades\Schedule;

Schedule::command('iot:heartbeat')->everyMinute();
EOF

# Buat Seeder
cat > database/seeders/DatabaseSeeder.php << 'EOF'
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
EOF

# 5. Jalankan migration dan seeder
echo -e "${YELLOW}Menjalankan migration...${NC}"
php artisan migrate:fresh --force

echo -e "${YELLOW}Menjalankan seeder...${NC}"
php artisan db:seed --force

# 6. Install tambahan jika perlu (tidak ada, karena hanya menggunakan dasar Laravel)
echo -e "${GREEN}Setup selesai!${NC}"
echo -e "${GREEN}Project berhasil dibuat di folder: $PROJECT_NAME${NC}"
echo -e "${YELLOW}Silakan jalankan:${NC}"
echo -e "  cd $PROJECT_NAME"
echo -e "  php artisan serve"
echo -e "  php artisan schedule:work  # di terminal terpisah untuk heartbeat"
echo -e ""
echo -e "${GREEN}Default login:${NC}"
echo -e "  username: admin"
echo -e "  password: password"