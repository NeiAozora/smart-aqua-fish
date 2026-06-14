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
        
        // Simpan user ke request (properti dinamis)
        $request->setUserResolver(fn () => $user);
        $request->user = $user;  // ← Tambahkan ini, hapus $request->merge()
        
        return $next($request);
    }
}