<?php

use Illuminate\Support\Facades\Route;

Route::get('/debug-url', function() {
    return [
        'app_url' => config('app.url'),
        'current_url' => request()->url(),
        'is_secure' => request()->isSecure(),
        'scheme' => request()->getScheme(),
        'headers' => [
            'host' => request()->getHost(),
            'x-forwarded-proto' => request()->header('x-forwarded-proto'),
            'x-forwarded-for' => request()->header('x-forwarded-for'),
        ]
    ];
});

Route::get('/', function () {
    return view('welcome');
});
