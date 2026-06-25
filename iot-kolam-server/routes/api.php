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


    // Di dalam grup middleware auth.api
    Route::get('/kolam/{id}/perintah', [KolamController::class, 'perintahTerbaru']);
    Route::get('/kolam/{id}/perintah-pending', [KolamController::class, 'perintahPending']);


    // Di dalam grup middleware auth.api
    Route::put('/user', [AuthController::class, 'updateProfile']);
    Route::put('/global-cooldown', [KolamController::class, 'updateGlobalCooldown']);
    
    Route::get('/notifikasi', [NotifikasiController::class, 'index']);
    Route::put('/notifikasi/{id}/baca', [NotifikasiController::class, 'markAsRead']);
    Route::delete('/notifikasi/{id}', [NotifikasiController::class, 'destroy']);
});
