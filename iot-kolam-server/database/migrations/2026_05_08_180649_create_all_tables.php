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
