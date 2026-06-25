<?php

use App\Http\Middleware\ApiAuthMiddleware;
use Illuminate\Foundation\Application;
use Illuminate\Foundation\Configuration\Exceptions;
use Illuminate\Foundation\Configuration\Middleware;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\URL;

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

        $middleware->trustProxies(at: '*', headers: Request::HEADER_X_FORWARDED_FOR |
                                                Request::HEADER_X_FORWARDED_HOST |
                                                Request::HEADER_X_FORWARDED_PORT |
                                                Request::HEADER_X_FORWARDED_PROTO);
        
        // ADD THIS: Force API routes prefix
        // $middleware->api(prepend: 'api');
    })
    ->withExceptions(function (Exceptions $exceptions) {
        //
    })->create();

// ADD THIS: Force HTTPS in production
if (app()->environment('production')) {
    URL::forceScheme('https');
}